#include "UnitTests.h"
#include "../src/storage/ThumbnailCache.h"

void UnitTests::historyClearDialogsRequireConfirmation()
{
    ApplicationController controller;
    ClipboardSnapshot text;
    text.type = QStringLiteral("text"); text.text = QStringLiteral("pinned");
    controller.screenshots()->clipboardService()->processContentChange(text, false);
    QTRY_COMPARE(controller.clipboardHistory()->count(), 1);
    QVERIFY(controller.clipboardHistory()->setPinned(controller.clipboardHistory()->model()->recordAt(0).id, true));
    text.text = QStringLiteral("ordinary");
    controller.screenshots()->clipboardService()->processContentChange(text, false);
    QTRY_COMPARE(controller.clipboardHistory()->count(), 2);
    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("thumbnail"), new ThumbnailProvider);
    engine.addImageProvider(QStringLiteral("qrpreview"), new QrImageProvider(controller.qrCode()));
    for (const auto &entry : QList<QPair<QString, QObject *>>{
        {QStringLiteral("app"), &controller}, {QStringLiteral("settings"), controller.settings()},
        {QStringLiteral("profiles"), controller.profiles()}, {QStringLiteral("screenshots"), controller.screenshots()},
        {QStringLiteral("clipboardHistory"), controller.clipboardHistory()}, {QStringLiteral("fileShelf"), controller.fileShelf()},
        {QStringLiteral("qrCode"), controller.qrCode()}, {QStringLiteral("hotkeys"), controller.hotkeys()},
        {QStringLiteral("actions"), controller.actions()}, {QStringLiteral("actionRunner"), controller.actionRunner()},
        {QStringLiteral("windowArrangements"), controller.windowArrangements()},
        {QStringLiteral("commandPalette"), controller.commandPalette()}})
        engine.rootContext()->setContextProperty(entry.first, entry.second);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    engine.loadFromModule(QStringLiteral("ScreenshotHotkeyManagerTest"), QStringLiteral("Main"));
    QVERIFY(!engine.rootObjects().isEmpty());
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
    controller.initializeWindow(window);
    window->show();
    controller.setCurrentPage(QStringLiteral("Clipboard History"));
    controller.clipboardHistory()->setSearch(QStringLiteral("no match"));
    QTRY_COMPARE(controller.clipboardHistory()->count(), 0);
    QQuickItem *button = findItem(window->contentItem(), QStringLiteral("clearHistoryButton")); QVERIFY(button);
    QVERIFY(button->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
    QObject *dialog = window->findChild<QObject *>(QStringLiteral("pageHistoryClearDialog")); QVERIFY(dialog);
    QTRY_VERIFY(dialog->property("visible").toBool());
    QTest::qWait(100);
    QVERIFY(window->grabWindow().save(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("history-clear-confirmation.png"))));
    QCOMPARE(controller.clipboardHistory()->totalCount(), 2);
    QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
    QCOMPARE(controller.clipboardHistory()->totalCount(), 2);
    QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
    QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
    QCOMPARE(controller.clipboardHistory()->totalCount(), 1); // Pin survives by default.
    controller.toggleClipboardHistoryPopup();
    QQuickWindow *popup = nullptr;
    for (QWindow *candidate : QGuiApplication::topLevelWindows())
        if (candidate->objectName() == QStringLiteral("clipboardHistoryPopup")) popup = qobject_cast<QQuickWindow *>(candidate);
    QVERIFY(popup);
    QQuickItem *popupButton = findItem(popup->contentItem(), QStringLiteral("popupClearHistoryButton")); QVERIFY(popupButton);
    QVERIFY(QMetaObject::invokeMethod(popupButton, "clicked"));
    QObject *popupDialog = popup->findChild<QObject *>(QStringLiteral("popupHistoryClearDialog")); QVERIFY(popupDialog);
    QTRY_VERIFY(popupDialog->property("visible").toBool());
    auto *includePins = popupDialog->findChild<QObject *>(QStringLiteral("historyClearIncludePins")); QVERIFY(includePins);
    includePins->setProperty("checked", true);
    QVERIFY(QMetaObject::invokeMethod(popupDialog, "accept"));
    QCOMPARE(controller.clipboardHistory()->totalCount(), 0);
    QCOMPARE(warnings.count(), 0);
    controller.setClipboardHistoryPopupVisible(false);
    controller.setCurrentPage(QStringLiteral("Settings"));
    QTest::qWait(150);
    QVERIFY(window->grabWindow().save(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("maintenance-settings.png"))));
    auto *settingsScroll = findItem(window->contentItem(), QStringLiteral("settingsScrollView")); QVERIFY(settingsScroll);
    auto *flickable = settingsScroll->property("contentItem").value<QQuickItem *>(); QVERIFY(flickable);
    auto *storage = findItem(window->contentItem(), QStringLiteral("storageSettingsPanel")); QVERIFY(storage);
    const qreal offset = flickable->property("contentY").toReal() + storage->mapToItem(flickable, QPointF()).y() - 16;
    flickable->setProperty("contentY", offset);
    QTest::qWait(100);
    QVERIFY(window->grabWindow().save(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("maintenance-storage.png"))));
    window->hide();
}

void UnitTests::storageCompactionPreservesOtherData()
{
    ApplicationDatabase database;
    QVERIFY(database.initialize());
    const QString tag = database.createTag(QStringLiteral("Keep this tag"));
    QVERIFY(!tag.isEmpty());
    QStringList ids;
    for (int index = 0; index < 30; ++index) {
        ClipboardHistoryRecord record;
        record.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        record.type = QStringLiteral("text"); record.text = QString(64 * 1024, QLatin1Char('x'));
        record.capturedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        record.fingerprint = QString::number(index);
        QVERIFY(database.saveClipboardHistory(record)); ids.append(record.id);
    }
    const qint64 before = QFileInfo(database.databasePath()).size();
    QVERIFY(database.deleteClipboardHistory(ids));
    QVERIFY(database.compact());
    QVERIFY(QFileInfo(database.databasePath()).size() < before);
    QCOMPARE(database.tags().size(), 1);
    QCOMPARE(database.tags().first().id, tag);
}

void UnitTests::historyPrivacyPreferencesAndSensitiveMarkers()
{
    SettingsManager settings;
    QVERIFY(settings.setClipboardHistoryEnabled(false));
    QVERIFY(settings.setClipboardHistoryExcludedApplications({QStringLiteral(" PasswordManager.EXE ")}));
    SettingsManager restarted;
    QVERIFY(!restarted.clipboardHistoryEnabled());
    QCOMPARE(restarted.clipboardHistoryExcludedApplications(), QStringList{QStringLiteral("passwordmanager.exe")});
    ProfileManager profiles(&settings);
    ScreenshotManager screenshots(&profiles, &settings);
    ClipboardHistoryManager history(screenshots.database(), screenshots.clipboardService(), &settings, &screenshots);
    ClipboardSnapshot text;
    text.type = QStringLiteral("text"); text.text = QStringLiteral("private");
    screenshots.clipboardService()->processContentChange(text, false);
    QCOMPARE(history.totalCount(), 0);
    history.setPaused(false);
    text.sourceApplication = QStringLiteral("C:/Apps/PasswordManager.exe");
    screenshots.clipboardService()->processContentChange(text, false);
    QCOMPARE(history.totalCount(), 0);
    text.sourceApplication = QStringLiteral("C:/Apps/Editor.exe");
    text.sensitive = true;
    screenshots.clipboardService()->processContentChange(text, false);
    QCOMPARE(history.totalCount(), 0);
    text.sensitive = false;
    screenshots.clipboardService()->processContentChange(text, false);
    QTRY_COMPARE(history.count(), 1);
    ClipboardSnapshot image;
    image.type = QStringLiteral("image"); image.image = QImage(32, 32, QImage::Format_ARGB32); image.image.fill(Qt::green);
    screenshots.clipboardService()->processContentChange(image, false);
    history.setPaused(true);
    history.finishPendingSaves();
    QCOMPARE(history.totalCount(), 1); // Pausing invalidates in-flight captures.
    SettingsManager afterPause;
    QVERIFY(!afterPause.clipboardHistoryEnabled());

    QMimeData mime;
    mime.setText(QStringLiteral("secret"));
    mime.setData(QStringLiteral("application/x-qt-windows-mime;value=\"ExcludeClipboardContentFromMonitorProcessing\""), QByteArrayLiteral("1"));
    QVERIFY(ClipboardService::isSensitiveMimeData(&mime));
    mime.removeFormat(mime.formats().last());
    QMimeData zero;
    zero.setData(QStringLiteral("application/x-qt-windows-mime;value=\"CanIncludeInClipboardHistory\""), QByteArray(4, '\0'));
    QVERIFY(ClipboardService::isSensitiveMimeData(&zero));
    zero.setData(zero.formats().first(), QByteArray::fromHex("01000000"));
    QVERIFY(!ClipboardService::isSensitiveMimeData(&zero));
}

void UnitTests::historySessionCleanupAndPinnedClear()
{
    SettingsManager settings;
    QVERIFY(settings.setClipboardHistorySessionOnly(true));
    ProfileManager profiles(&settings);
    ScreenshotManager screenshots(&profiles, &settings);
    {
        ClipboardHistoryManager history(screenshots.database(), screenshots.clipboardService(), &settings, &screenshots);
        ClipboardSnapshot text;
        text.type = QStringLiteral("text"); text.text = QStringLiteral("pinned");
        screenshots.clipboardService()->processContentChange(text, false);
        QTRY_COMPARE(history.count(), 1);
        QVERIFY(history.setPinned(history.model()->recordAt(0).id, true));
        text.text = QStringLiteral("unpinned");
        screenshots.clipboardService()->processContentChange(text, false);
        QTRY_COMPARE(history.count(), 2);
        history.setSearch(QStringLiteral("does not match"));
        QTRY_COMPARE(history.count(), 0);
        QCOMPARE(history.clearCount(false), 1);
        QCOMPARE(history.clearCount(true), 2);
        QVERIFY(history.clearHistory(false));
        QCOMPARE(history.totalCount(), 1);
    }
    QCOMPARE(screenshots.database()->clipboardHistoryCount(), 0);
    // Simulate the data left by a process that did not get a normal shutdown.
    ClipboardHistoryRecord interrupted;
    interrupted.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    interrupted.type = QStringLiteral("text"); interrupted.fingerprint = QStringLiteral("interrupted");
    interrupted.text = QStringLiteral("previous session");
    interrupted.capturedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    QVERIFY(screenshots.database()->saveClipboardHistory(interrupted));
    ClipboardHistoryManager next(screenshots.database(), screenshots.clipboardService(), &settings, &screenshots);
    QCOMPARE(next.totalCount(), 0);
}

void UnitTests::historyMaintenanceReconcilesAndRetriesLockedPayload()
{
    SettingsManager settings;
    ProfileManager profiles(&settings);
    ScreenshotManager screenshots(&profiles, &settings);
    ClipboardHistoryManager history(screenshots.database(), screenshots.clipboardService(), &settings, &screenshots);
    QTRY_VERIFY(!history.maintaining());
    ClipboardSnapshot image;
    image.type = QStringLiteral("image"); image.image = QImage(16, 16, QImage::Format_ARGB32); image.image.fill(Qt::red);
    screenshots.clipboardService()->processContentChange(image, false);
    QTRY_COMPARE(history.count(), 1);
    const auto record = history.model()->recordAt(0);
    QSignalSpy messages(&history, &ClipboardHistoryManager::messageChanged);
#ifdef Q_OS_WIN
    HANDLE locked = CreateFileW(reinterpret_cast<LPCWSTR>(record.payloadPath.utf16()), GENERIC_READ, FILE_SHARE_READ,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    QVERIFY(locked != INVALID_HANDLE_VALUE);
    const auto close = qScopeGuard([&] { if (locked != INVALID_HANDLE_VALUE) CloseHandle(locked); });
    QVERIFY(history.clearHistory());
    QVERIFY(QFileInfo::exists(record.payloadPath));
    QVERIFY(!messages.isEmpty());
    QVERIFY(messages.last().at(1).toBool());
    CloseHandle(locked); locked = INVALID_HANDLE_VALUE;
#else
    QVERIFY(history.clearHistory());
#endif
    const QString directory = QDir(AppDataPaths::root()).filePath(QStringLiteral("clipboard-history"));
    const QString orphan = QDir(directory).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".png"));
    QFile file(orphan); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("orphan");
    QVERIFY(file.setFileTime(QDateTime::currentDateTimeUtc().addSecs(-120), QFileDevice::FileModificationTime)); file.close();
    const QString foreign = QDir(directory).filePath(QStringLiteral("user.png"));
    QFile user(foreign); QVERIFY(user.open(QIODevice::WriteOnly)); user.write("keep"); user.close();
    history.maintainStorage(true);
    QTRY_VERIFY(!history.maintaining());
    QVERIFY(!QFileInfo::exists(orphan));
    QVERIFY(!QFileInfo::exists(record.payloadPath));
    QVERIFY(QFileInfo::exists(foreign));
    QCOMPARE(history.storageUsage().value(QStringLiteral("failedFiles")).toInt(), 0);
}

void UnitTests::historyPeriodicRetentionAndBoundedSearchRestore()
{
    SettingsManager settings;
    ProfileManager profiles(&settings);
    ScreenshotManager screenshots(&profiles, &settings);
    ClipboardHistoryManager history(screenshots.database(), screenshots.clipboardService(), &settings, &screenshots);
    for (int n = 0; n < 220; ++n) {
        ClipboardHistoryRecord record;
        record.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        record.type = QStringLiteral("text"); record.fingerprint = QString::number(n);
        record.text = QString(4000, QLatin1Char('x')) + QStringLiteral("findme-%1").arg(n);
        record.capturedAt = QDateTime::currentDateTimeUtc().addSecs(n).toString(Qt::ISODateWithMs);
        QVERIFY(screenshots.database()->saveClipboardHistory(record));
    }
    history.refresh();
    QTRY_COMPARE(history.count(), 100);
    QVERIFY(history.hasMore());
    QCOMPARE(history.model()->recordAt(0).text.size(), 240);
    history.loadMore();
    QTRY_COMPARE(history.count(), 200);
    history.setSearch(QStringLiteral("findme-219"));
    QTRY_COMPARE(history.count(), 1);
    QVERIFY(history.restoreEntry(history.model()->recordAt(0).id));
    QCOMPARE(QGuiApplication::clipboard()->text(), QString(4000, QLatin1Char('x')) + QStringLiteral("findme-219"));
    ClipboardHistoryRecord old;
    old.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    old.type = QStringLiteral("text"); old.fingerprint = QStringLiteral("expired-idle"); old.text = QStringLiteral("expired-idle");
    old.capturedAt = QDateTime::currentDateTimeUtc().addDays(-100).toString(Qt::ISODateWithMs);
    QVERIFY(screenshots.database()->saveClipboardHistory(old));
    auto *timer = history.findChild<QTimer *>(QStringLiteral("clipboardRetentionTimer"));
    QVERIFY(timer); timer->setInterval(10);
    QTRY_COMPARE(screenshots.database()->clipboardHistory({}, 1, false, old.id).size(), 0);
    timer->stop();
}

void UnitTests::thumbnailCacheEvictsOwnedFilesAndPreservesOthers()
{
    const QString directory = QDir(AppDataPaths::root()).filePath(QStringLiteral("cache/thumbnails"));
    QVERIFY(QDir().mkpath(directory));
    ThumbnailCache::setLimitMB(16);
    const auto restore = qScopeGuard([] { ThumbnailCache::setLimitMB(128); });
    for (int index = 0; index < 3; ++index) {
        QFile file(QDir(directory).filePath(QString(64, QChar(QLatin1Char('a').unicode() + index)) + QStringLiteral(".png")));
        QVERIFY(file.open(QIODevice::WriteOnly)); QVERIFY(file.resize(8 * 1024 * 1024));
        QVERIFY(file.setFileTime(QDateTime::currentDateTimeUtc().addSecs(-60 * (3-index)), QFileDevice::FileModificationTime));
    }
    const QString foreign = QDir(directory).filePath(QStringLiteral("keep.txt"));
    QFile user(foreign); QVERIFY(user.open(QIODevice::WriteOnly)); user.write("keep"); user.close();
    const auto result = ThumbnailCache::trim(directory);
    QCOMPARE(result.value(QStringLiteral("bytes")).toLongLong(), 16LL * 1024 * 1024);
    QVERIFY(!QFileInfo::exists(QDir(directory).filePath(QString(64, QLatin1Char('a')) + QStringLiteral(".png"))));
    ThumbnailCache::trim(directory, true);
    QVERIFY(QFileInfo::exists(foreign));
    QCOMPARE(QDir(directory).entryList({QStringLiteral("*.png")}, QDir::Files).size(), 0);
    QFile expired(QDir(directory).filePath(QString(64, QLatin1Char('d')) + QStringLiteral(".png")));
    QVERIFY(expired.open(QIODevice::WriteOnly)); expired.write("old thumbnail");
    QVERIFY(expired.setFileTime(QDateTime::currentDateTimeUtc().addDays(-31), QFileDevice::FileModificationTime)); expired.close();
    ThumbnailCache::trim(directory);
    QVERIFY(!QFileInfo::exists(expired.fileName()));
}

void UnitTests::fileShelfCancellationPreservesMoveSourceAndDestination()
{
    SettingsManager settings;
    ApplicationDatabase database;
    QVERIFY(database.initialize());
    ClipboardService clipboard(QGuiApplication::clipboard());
    FileShelfManager shelf(&database, &clipboard);
    const QString sourceDir = QDir(AppDataPaths::root()).filePath(QStringLiteral("source"));
    const QString destination = QDir(AppDataPaths::root()).filePath(QStringLiteral("destination"));
    QVERIFY(QDir().mkpath(sourceDir)); QVERIFY(QDir().mkpath(destination));
    const QString source = QDir(sourceDir).filePath(QStringLiteral("large.bin"));
    QFile input(source); QVERIFY(input.open(QIODevice::WriteOnly)); QVERIFY(input.resize(256LL * 1024 * 1024)); input.close();
    const QString target = QDir(destination).filePath(QStringLiteral("large.bin"));
    QFile output(target); QVERIFY(output.open(QIODevice::WriteOnly)); output.write("previous destination"); output.close();
    QVERIFY(shelf.addPaths({source})); shelf.selectAll();
    auto plan = shelf.previewTransfer(destination, true);
    auto item = plan.first().toMap(); item.insert(QStringLiteral("resolution"), QStringLiteral("replace")); plan[0] = item;
    QVERIFY(shelf.startTransfer(plan, destination, true));
    QElapsedTimer elapsed; elapsed.start();
    while (shelf.busy() && shelf.bytesCopied() == 0 && elapsed.elapsed() < 5000) QTest::qWait(1);
    QVERIFY(shelf.bytesCopied() > 0);
    shelf.cancelTransfer();
    QTRY_VERIFY_WITH_TIMEOUT(!shelf.busy(), 10000);
    QVERIFY(shelf.cancellationRequested());
    QVERIFY(QFileInfo::exists(source));
    QVERIFY(output.open(QIODevice::ReadOnly)); QCOMPARE(output.readAll(), QByteArrayLiteral("previous destination"));
    QCOMPARE(QDir(destination).entryList(QDir::Files | QDir::Hidden).size(), 1);
    QCOMPARE(database.fileShelfEntries().first().path, source);
    output.close();
    QVERIFY(shelf.startTransfer(plan, destination, true));
    shelf.finishTransfer();
    QVERIFY(!shelf.busy());
    QVERIFY(QFileInfo::exists(source));
    QVERIFY(output.open(QIODevice::ReadOnly)); QCOMPARE(output.readAll(), QByteArrayLiteral("previous destination"));
}
