#include "UnitTests.h"

#ifdef Q_OS_WIN
void UnitTests::fileShelfMoveRejectsDirectoryAliasReplace()
{
    QTemporaryDir temporary(g_testData->filePath(QStringLiteral("junction-alias-XXXXXX")));
    QVERIFY(temporary.isValid());
    const QString source = QDir(temporary.path()).filePath(QStringLiteral("source"));
    const QString alias = QDir(temporary.path()).filePath(QStringLiteral("parent-alias"));
    QVERIFY(QDir().mkpath(source));
    QFile sentinel(QDir(source).filePath(QStringLiteral("keep.txt")));
    QVERIFY(sentinel.open(QIODevice::WriteOnly));
    QCOMPARE(sentinel.write(QByteArrayLiteral("source survives")), qint64(15));
    sentinel.close();
    DWORD junctionError = ERROR_SUCCESS;
    bool linked = CreateDirectoryW(reinterpret_cast<LPCWSTR>(alias.utf16()), nullptr) != FALSE;
    if (!linked)
        junctionError = GetLastError();
    HANDLE junction = linked
        ? CreateFileW(reinterpret_cast<LPCWSTR>(alias.utf16()), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                      FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr)
        : INVALID_HANDLE_VALUE;
    if (linked && junction == INVALID_HANDLE_VALUE) {
        linked = false;
        junctionError = GetLastError();
    }
    if (linked) {
        const QString substitute = QStringLiteral("\\??\\") + QDir::toNativeSeparators(temporary.path());
        const QString printName = QDir::toNativeSeparators(temporary.path());
        const qsizetype substituteBytes = substitute.size() * qsizetype(sizeof(wchar_t));
        const qsizetype printBytes = printName.size() * qsizetype(sizeof(wchar_t));
        const qsizetype dataBytes = 8 + substituteBytes + qsizetype(sizeof(wchar_t)) +
                                    printBytes + qsizetype(sizeof(wchar_t));
        QByteArray storage(8 + dataBytes, Qt::Uninitialized);
        struct MountPointReparseData {
            ULONG tag;
            USHORT dataLength;
            USHORT reserved;
            USHORT substituteOffset;
            USHORT substituteLength;
            USHORT printOffset;
            USHORT printLength;
            WCHAR pathBuffer[1];
        };
        auto *reparse = reinterpret_cast<MountPointReparseData *>(storage.data());
        reparse->tag = 0xA0000003UL;
        reparse->dataLength = static_cast<USHORT>(dataBytes);
        reparse->reserved = 0;
        reparse->substituteOffset = 0;
        reparse->substituteLength = static_cast<USHORT>(substituteBytes);
        reparse->printOffset = static_cast<USHORT>(substituteBytes + sizeof(wchar_t));
        reparse->printLength = static_cast<USHORT>(printBytes);
        std::memcpy(reparse->pathBuffer, substitute.utf16(), size_t(substituteBytes));
        reparse->pathBuffer[substitute.size()] = L'\0';
        std::memcpy(reinterpret_cast<char *>(reparse->pathBuffer) + substituteBytes + sizeof(wchar_t),
                    printName.utf16(), size_t(printBytes));
        reparse->pathBuffer[substitute.size() + 1 + printName.size()] = L'\0';
        DWORD bytesReturned = 0;
        linked = DeviceIoControl(junction, FSCTL_SET_REPARSE_POINT, reparse,
                                 static_cast<DWORD>(8 + dataBytes), nullptr, 0, &bytesReturned, nullptr) != FALSE;
        if (!linked)
            junctionError = GetLastError();
        CloseHandle(junction);
    }
    if (!linked) {
        RemoveDirectoryW(reinterpret_cast<LPCWSTR>(alias.utf16()));
        const QString reason = QStringLiteral("Could not create an isolated temporary directory junction (Win32 error %1).")
                                   .arg(junctionError);
        QSKIP(qPrintable(reason));
    }
    const auto removeAlias = qScopeGuard([&] { RemoveDirectoryW(reinterpret_cast<LPCWSTR>(alias.utf16())); });
    QVERIFY(QFileInfo(QDir(alias).filePath(QStringLiteral("source"))).isDir());

    SettingsManager settings;
    ProfileManager profiles(&settings);
    ScreenshotManager screenshots(&profiles, &settings);
    FileShelfManager shelf(screenshots.database(), screenshots.clipboardService());
    QVERIFY(shelf.addPaths({source}));
    QCOMPARE(shelf.rowCount(), 1);
    shelf.toggleSelection(shelf.data(shelf.index(0), FileShelfManager::IdRole).toString());
    QVariantList plan = shelf.previewTransfer(alias, true);
    QCOMPARE(plan.size(), 1);
    QVERIFY(plan.first().toMap().value(QStringLiteral("conflict")).toBool());
    QVariantMap item = plan.first().toMap();
    item.insert(QStringLiteral("resolution"), QStringLiteral("replace"));
    plan[0] = item;
    QSignalSpy finished(&shelf, &FileShelfManager::operationFinished);
    QVERIFY(shelf.startTransfer(plan, alias, true));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
    const QList<QVariant> outcome = finished.takeFirst();
    QCOMPARE(outcome.at(0).toInt(), 0);
    QCOMPARE(outcome.at(2).toInt(), 1);
    QFile stillThere(QDir(source).filePath(QStringLiteral("keep.txt")));
    QVERIFY(stillThere.open(QIODevice::ReadOnly));
    QCOMPARE(stillThere.readAll(), QByteArrayLiteral("source survives"));
    QVERIFY(QFileInfo(QDir(alias).filePath(QStringLiteral("source/keep.txt"))).exists());
}
#endif

void UnitTests::fileDropUrlConversionPreservesUnicodeAndRejectsRemoteUrls()
{
    QTemporaryDir temporary(g_testData->filePath(QStringLiteral("file-url-unicode-XXXXXX")));
    QVERIFY(temporary.isValid());
    const QString unicodePath = QDir(temporary.path()).filePath(QStringLiteral("café space % file.txt"));
    QFile file(unicodePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();
    QString converted, error;
    QVERIFY2(LocalFileUrl::toPath(QVariant::fromValue(QUrl::fromLocalFile(unicodePath)), &converted, &error),
             qPrintable(error));
    QCOMPARE(converted, unicodePath);
    QVERIFY(!LocalFileUrl::toPath(QVariant::fromValue(QUrl(QStringLiteral("https://example.test/file.png"))),
                                  &converted, &error));
    QVERIFY(!error.isEmpty());
#ifdef Q_OS_WIN
    QVERIFY(LocalFileUrl::toPath(QVariant::fromValue(QUrl(QStringLiteral("file://server/share/folder%20name/a.png"))),
                                 &converted, &error));
    QVERIFY(converted.startsWith(QStringLiteral("\\\\server\\share"), Qt::CaseInsensitive) ||
            converted.startsWith(QStringLiteral("//server/share"), Qt::CaseInsensitive));
#endif
}

void UnitTests::fileShelfDatabaseUpgradesVersionFive()
{
    QTemporaryDir temporary(g_testData->filePath(QStringLiteral("shelf-v5-upgrade-XXXXXX")));
    QVERIFY(temporary.isValid());
    const QString path = QDir(temporary.path()).filePath(QStringLiteral("shelf.db"));
    const QString connection = QStringLiteral("shelf-v5-fixture");
    { ApplicationDatabase seed(path); QVERIFY(seed.initialize()); }
    {
        QSqlDatabase fixture = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        fixture.setDatabaseName(path);
        QVERIFY(fixture.open());
        QSqlQuery query(fixture);
        QVERIFY(query.exec(QStringLiteral("DROP TABLE file_shelf")));
        QVERIFY(query.exec(QStringLiteral("DROP INDEX screenshots_path_key_idx")));
        QVERIFY(query.exec(QStringLiteral("ALTER TABLE screenshots DROP COLUMN path_key")));
        QVERIFY(query.exec(QStringLiteral("PRAGMA user_version=5")));
        fixture.close();
    }
    QSqlDatabase::removeDatabase(connection);
    ApplicationDatabase database(path);
    QVERIFY2(database.initialize(), qPrintable(database.lastError()));
    FileShelfRecord record;
    record.path = QStringLiteral("C:/资料/研究 文档.txt");
    record.displayName = QStringLiteral("研究 文档.txt");
    record.sourceFolder = QStringLiteral("C:/资料");
    record.itemType = QStringLiteral("file");
    int inserted = 0;
    QVERIFY2(database.addFileShelfEntries({record}, &inserted), qPrintable(database.lastError()));
    QCOMPARE(inserted, 1);
    QCOMPARE(database.fileShelfEntries().first().displayName, QStringLiteral("研究 文档.txt"));
    FileShelfRecord caseVariant = record;
    caseVariant.id = QStringLiteral("case-variant-id");
    caseVariant.path = QStringLiteral("c:/资料/研究 文档.TXT");
    int duplicateCount = 0;
    QVERIFY2(database.addFileShelfEntries({caseVariant}, &inserted, &duplicateCount), qPrintable(database.lastError()));
    QCOMPARE(inserted, 0);
    QCOMPARE(duplicateCount, 1);
    const QString verifyConnection = QStringLiteral("shelf-v6-version-check");
    {
        QSqlDatabase verify = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), verifyConnection);
        verify.setDatabaseName(path);
        QVERIFY(verify.open());
        QSqlQuery query(verify);
        QVERIFY(query.exec(QStringLiteral("PRAGMA user_version")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 7);
        verify.close();
    }
    QSqlDatabase::removeDatabase(verifyConnection);
}

void UnitTests::fileShelfPersistsUnicodeReferencesDeduplicatesAndMarksMissing()
{
    QTemporaryDir sources(g_testData->filePath(QStringLiteral("shelf-unicode-XXXXXX")));
    QVERIFY(sources.isValid());
    const QString source = QDir(sources.path()).filePath(QStringLiteral("資料 résumé_日本語.txt"));
    const QString retained = QDir(sources.path()).filePath(QStringLiteral("keep.bin"));
    for (const QString &path : {source, retained}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(QByteArrayLiteral("shelf source")), qint64(12));
    }
    QString sourceId;
    {
        SettingsManager settings;
        ProfileManager profiles(&settings);
        ScreenshotManager screenshots(&profiles, &settings);
        FileShelfManager shelf(screenshots.database(), screenshots.clipboardService());
        QVERIFY(shelf.addPaths({source, retained}));
        QCOMPARE(shelf.rowCount(), 2);
        for (int row = 0; row < shelf.rowCount(); ++row)
            if (shelf.data(shelf.index(row, 0), FileShelfManager::PathRole).toString() == source)
                sourceId = shelf.data(shelf.index(row, 0), FileShelfManager::IdRole).toString();
        QVERIFY(!sourceId.isEmpty());
        const QList<QUrl> urls = shelf.localFileUrlsForIds({sourceId});
        QCOMPARE(urls.size(), 1);
        QCOMPARE(urls.first().toLocalFile(), source);
        QMimeData fileTransfer;
        fileTransfer.setUrls(urls);
        QVERIFY(fileTransfer.hasUrls());
        QVERIFY(fileTransfer.formats().contains(QStringLiteral("text/uri-list")));
        QVERIFY(shelf.addPaths({source, source}));
        QCOMPARE(shelf.rowCount(), 2);

        QString retainedId;
        for (int row = 0; row < shelf.rowCount(); ++row)
            if (shelf.data(shelf.index(row, 0), FileShelfManager::PathRole).toString() == retained)
                retainedId = shelf.data(shelf.index(row, 0), FileShelfManager::IdRole).toString();
        QVERIFY(!retainedId.isEmpty());
        shelf.clickItem(sourceId, false, false);
        shelf.clickItem(retainedId, false, true);
        QCOMPARE(shelf.selectedCount(), 2);
        shelf.clearSelection();
        QVERIFY(shelf.removeEntry(retainedId));
        QVERIFY(QFileInfo::exists(retained));

        QVERIFY(QFile::remove(source));
        shelf.refresh();
        bool foundMissing = false;
        for (int index = 0; index < shelf.rowCount(); ++index) {
            if (shelf.data(shelf.index(index, 0), FileShelfManager::IdRole).toString() == sourceId) {
                foundMissing = true;
                QVERIFY(!shelf.data(shelf.index(index, 0), FileShelfManager::AvailableRole).toBool());
                QCOMPARE(shelf.data(shelf.index(index, 0), FileShelfManager::DisplayNameRole).toString(), QStringLiteral("資料 résumé_日本語.txt"));
            }
        }
        QVERIFY(foundMissing);
    }
    ApplicationDatabase restarted(QDir(AppDataPaths::root()).filePath(QStringLiteral("screenshots.db")));
    QVERIFY2(restarted.initialize(), qPrintable(restarted.lastError()));
    const auto entries = restarted.fileShelfEntries();
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().id, sourceId);
    QVERIFY(!QFileInfo::exists(entries.first().path));
    QVERIFY(QFileInfo::exists(retained));
    int cleared = 0;
    QVERIFY(restarted.clearFileShelf(&cleared));
    QCOMPARE(cleared, 1);
    QVERIFY(QFileInfo::exists(retained));
}

void UnitTests::fileShelfSelectionConflictsAndPartialOperations()
{
    QTemporaryDir workspace(g_testData->filePath(QStringLiteral("shelf-operations-XXXXXX")));
    QVERIFY(workspace.isValid());
    const QString sourceDir = QDir(workspace.path()).filePath(QStringLiteral("source"));
    const QString destDir = QDir(workspace.path()).filePath(QStringLiteral("dest"));
    const QString movedDir = QDir(workspace.path()).filePath(QStringLiteral("moved"));
    QVERIFY(QDir().mkpath(sourceDir));
    QVERIFY(QDir().mkpath(destDir));
    QVERIFY(QDir().mkpath(movedDir));
    const QString source = QDir(sourceDir).filePath(QStringLiteral("report.txt"));
    const QString conflict = QDir(destDir).filePath(QStringLiteral("report.txt"));
    auto writeFile = [](const QString &path, const QByteArray &bytes) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return false;
        return file.write(bytes) == bytes.size();
    };
    QVERIFY(writeFile(source, QByteArrayLiteral("source contents")));
    QVERIFY(writeFile(conflict, QByteArrayLiteral("old destination")));

    SettingsManager settings;
    ProfileManager profiles(&settings);
    ScreenshotManager screenshots(&profiles, &settings);
    FileShelfManager shelf(screenshots.database(), screenshots.clipboardService());
    QVERIFY(shelf.addPaths({source}));
    const QString id = shelf.data(shelf.index(0, 0), FileShelfManager::IdRole).toString();
    shelf.clickItem(id, false, false);
    QCOMPARE(shelf.selectedCount(), 1);
    auto plan = shelf.previewTransfer(destDir, false);
    QCOMPARE(plan.size(), 1);
    QVariantMap entry = plan.first().toMap();
    QVERIFY(entry.value(QStringLiteral("conflict")).toBool());
    QSignalSpy finished(&shelf, &FileShelfManager::operationFinished);

    entry.insert(QStringLiteral("resolution"), QStringLiteral("skip"));
    plan[0] = entry;
    QVERIFY(shelf.startTransfer(plan, destDir, false));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QCOMPARE(finished.first().at(0).toInt(), 0);
    QCOMPARE(finished.first().at(1).toInt(), 1);
    QFile oldDest(conflict);
    QVERIFY(oldDest.open(QIODevice::ReadOnly));
    QCOMPARE(oldDest.readAll(), QByteArrayLiteral("old destination"));
    finished.clear();

    entry.insert(QStringLiteral("resolution"), QStringLiteral("rename"));
    plan[0] = entry;
    QVERIFY(shelf.startTransfer(plan, destDir, false));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QVERIFY2(finished.first().at(0).toInt() == 1, qPrintable(finished.first().at(3).toStringList().join(QLatin1Char('\n'))));
    QVERIFY(QFileInfo::exists(QDir(destDir).filePath(QStringLiteral("report (2).txt"))));
    QVERIFY(QFileInfo::exists(source));
    finished.clear();

    const QString replacement = QDir(workspace.path()).filePath(QStringLiteral("replace"));
    QVERIFY(QDir().mkpath(replacement));
    const QString replaceTarget = QDir(replacement).filePath(QStringLiteral("report.txt"));
    QVERIFY(writeFile(replaceTarget, QByteArrayLiteral("replace me")));
    auto replacePlan = shelf.previewTransfer(replacement, false);
    QVariantMap replaceItem = replacePlan.first().toMap();
    replaceItem.insert(QStringLiteral("resolution"), QStringLiteral("replace"));
    replacePlan[0] = replaceItem;
    QVERIFY(shelf.startTransfer(replacePlan, replacement, false));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QVERIFY2(finished.first().at(0).toInt() == 1, qPrintable(finished.first().at(3).toStringList().join(QLatin1Char('\n'))));
    QFile replaced(replaceTarget);
    QVERIFY(replaced.open(QIODevice::ReadOnly));
    QCOMPARE(replaced.readAll(), QByteArrayLiteral("source contents"));
    finished.clear();

    const QString movePlanPath = QDir(movedDir).filePath(QStringLiteral("report.txt"));
    auto movePlan = shelf.previewTransfer(movedDir, true);
    QVERIFY(shelf.startTransfer(movePlan, movedDir, true));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QVERIFY2(finished.first().at(0).toInt() == 1, qPrintable(finished.first().at(3).toStringList().join(QLatin1Char('\n'))));
    QVERIFY(!QFileInfo::exists(source));
    QVERIFY(QFileInfo::exists(movePlanPath));
    shelf.refresh();
    QCOMPARE(shelf.data(shelf.index(0, 0), FileShelfManager::IdRole).toString(), id);
    QCOMPARE(shelf.data(shelf.index(0, 0), FileShelfManager::PathRole).toString(), movePlanPath);
    finished.clear();

    const QString goodSource = QDir(sourceDir).filePath(QStringLiteral("good.bin"));
    const QString vanishedSource = QDir(sourceDir).filePath(QStringLiteral("vanished.bin"));
    QVERIFY(writeFile(goodSource, QByteArrayLiteral("good")));
    QVERIFY(writeFile(vanishedSource, QByteArrayLiteral("gone")));
    QVERIFY(shelf.addPaths({goodSource, vanishedSource}));
    QString goodId, vanishedId;
    for (int row = 0; row < shelf.rowCount(); ++row) {
        const QString path = shelf.data(shelf.index(row, 0), FileShelfManager::PathRole).toString();
        if (path == goodSource) goodId = shelf.data(shelf.index(row, 0), FileShelfManager::IdRole).toString();
        if (path == vanishedSource) vanishedId = shelf.data(shelf.index(row, 0), FileShelfManager::IdRole).toString();
    }
    QVERIFY(!goodId.isEmpty());
    QVERIFY(!vanishedId.isEmpty());
    shelf.clearSelection();
    shelf.toggleSelection(goodId);
    shelf.toggleSelection(vanishedId);
    QVERIFY(QFile::remove(vanishedSource));
    const QString partialDest = QDir(workspace.path()).filePath(QStringLiteral("partial"));
    QVERIFY(QDir().mkpath(partialDest));
    const auto partialPlan = shelf.previewTransfer(partialDest, false);
    QVERIFY(shelf.startTransfer(partialPlan, partialDest, false));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QVERIFY2(finished.first().at(0).toInt() == 1, qPrintable(finished.first().at(3).toStringList().join(QLatin1Char('\n'))));
    QCOMPARE(finished.first().at(2).toInt(), 1);
    QVERIFY(QFileInfo::exists(QDir(partialDest).filePath(QStringLiteral("good.bin"))));
    QVERIFY(QFileInfo::exists(goodSource));
}

void UnitTests::fileShelfCopiesAndMovesFoldersInBackground()
{
    QTemporaryDir workspace(g_testData->filePath(QStringLiteral("shelf-folder-operations-XXXXXX")));
    QVERIFY(workspace.isValid());
    const QString source = QDir(workspace.path()).filePath(QStringLiteral("素材"));
    const QString nested = QDir(source).filePath(QStringLiteral("subfolder"));
    const QString firstDestination = QDir(workspace.path()).filePath(QStringLiteral("copy-here"));
    const QString secondDestination = QDir(workspace.path()).filePath(QStringLiteral("move-here"));
    QVERIFY(QDir().mkpath(nested));
    QVERIFY(QDir().mkpath(firstDestination));
    QVERIFY(QDir().mkpath(secondDestination));
    const QString content = QDir(nested).filePath(QStringLiteral("notes.txt"));
    QFile file(content);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(QByteArrayLiteral("folder contents")), qint64(15));
    file.close();

    SettingsManager settings;
    ProfileManager profiles(&settings);
    ScreenshotManager screenshots(&profiles, &settings);
    FileShelfManager shelf(screenshots.database(), screenshots.clipboardService());
    QVERIFY(shelf.addPaths({source}));
    QCOMPARE(shelf.data(shelf.index(0, 0), FileShelfManager::ItemTypeRole).toString(), QStringLiteral("folder"));
    const QString id = shelf.data(shelf.index(0, 0), FileShelfManager::IdRole).toString();
    shelf.selectAll();
    QSignalSpy finished(&shelf, &FileShelfManager::operationFinished);
    const QVariantList copyPlan = shelf.previewTransfer(firstDestination, false);
    QVERIFY(shelf.startTransfer(copyPlan, firstDestination, false));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QVERIFY2(finished.first().at(0).toInt() == 1, qPrintable(finished.first().at(3).toStringList().join(QLatin1Char('\n'))));
    const QString copiedContent = QDir(firstDestination).filePath(QStringLiteral("素材/subfolder/notes.txt"));
    QFile copied(copiedContent);
    QVERIFY(copied.open(QIODevice::ReadOnly));
    QCOMPARE(copied.readAll(), QByteArrayLiteral("folder contents"));
    QVERIFY(QFileInfo::exists(source));
    finished.clear();

    const QVariantList movePlan = shelf.previewTransfer(secondDestination, true);
    QVERIFY(shelf.startTransfer(movePlan, secondDestination, true));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QCOMPARE(finished.first().at(0).toInt(), 1);
    QVERIFY(!QFileInfo::exists(source));
    const QString moved = QDir(secondDestination).filePath(QStringLiteral("素材"));
    QVERIFY(QFileInfo::exists(QDir(moved).filePath(QStringLiteral("subfolder/notes.txt"))));
    shelf.refresh();
    QCOMPARE(shelf.data(shelf.index(0, 0), FileShelfManager::IdRole).toString(), id);
    QCOMPARE(shelf.data(shelf.index(0, 0), FileShelfManager::PathRole).toString(), moved);
}

void UnitTests::fileShelfPreviewsBatchFilenameCollisions()
{
    QTemporaryDir workspace(g_testData->filePath(QStringLiteral("shelf-batch-conflicts-XXXXXX")));
    QVERIFY(workspace.isValid());
    const QString firstFolder = QDir(workspace.path()).filePath(QStringLiteral("a"));
    const QString secondFolder = QDir(workspace.path()).filePath(QStringLiteral("b"));
    const QString destination = QDir(workspace.path()).filePath(QStringLiteral("destination"));
    QVERIFY(QDir().mkpath(firstFolder));
    QVERIFY(QDir().mkpath(secondFolder));
    QVERIFY(QDir().mkpath(destination));
    for (const QString &path : {QDir(firstFolder).filePath(QStringLiteral("same.txt")),
                                QDir(secondFolder).filePath(QStringLiteral("same.txt"))}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(QByteArrayLiteral("item")), qint64(4));
    }
    SettingsManager settings;
    ProfileManager profiles(&settings);
    ScreenshotManager screenshots(&profiles, &settings);
    FileShelfManager shelf(screenshots.database(), screenshots.clipboardService());
    QVERIFY(shelf.addPaths({QDir(firstFolder).filePath(QStringLiteral("same.txt")),
                            QDir(secondFolder).filePath(QStringLiteral("same.txt"))}));
    shelf.selectAll();
    const QVariantList plan = shelf.previewTransfer(destination, false);
    QCOMPARE(plan.size(), 2);
    int conflicts = 0;
    for (const QVariant &value : plan)
        conflicts += value.toMap().value(QStringLiteral("conflict")).toBool() ? 1 : 0;
    QCOMPARE(conflicts, 1);
}
