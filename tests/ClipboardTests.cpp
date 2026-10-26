#include "UnitTests.h"

void UnitTests::clipboardHistoryDatabaseUpgradesVersionFour()
{
    QTemporaryDir temporary(g_testData->filePath(QStringLiteral("history-v4-upgrade-XXXXXX")));
    QVERIFY(temporary.isValid());
    const QString databasePath = QDir(temporary.path()).filePath(QStringLiteral("screenshots.db"));
    const QString connectionName = QStringLiteral("history-v4-fixture");
    { ApplicationDatabase seed(databasePath); QVERIFY(seed.initialize()); }
    {
        QSqlDatabase fixture = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        fixture.setDatabaseName(databasePath);
        QVERIFY(fixture.open());
        QSqlQuery query(fixture);
        QVERIFY(query.exec(QStringLiteral("DROP TABLE clipboard_history")));
        QVERIFY(query.exec(QStringLiteral("DROP TABLE file_shelf_collection_items")));
        QVERIFY(query.exec(QStringLiteral("DROP TABLE file_shelf_collections")));
        QVERIFY(query.exec(QStringLiteral("DROP TABLE file_shelf")));
        QVERIFY(query.exec(QStringLiteral("DROP INDEX screenshots_path_key_idx")));
        QVERIFY(query.exec(QStringLiteral("ALTER TABLE screenshots DROP COLUMN path_key")));
        QVERIFY(query.exec(QStringLiteral("PRAGMA user_version=4")));
        fixture.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
    ApplicationDatabase database(databasePath);
    QVERIFY2(database.initialize(), qPrintable(database.lastError()));
    QVERIFY(database.isReady());
    ClipboardHistoryRecord record;
    record.id = QStringLiteral("31234567-89ab-cdef-0123-456789abcdef");
    record.type = QStringLiteral("text");
    record.capturedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    record.fingerprint = QStringLiteral("migrated-schema-five");
    record.text = QStringLiteral("v4 to v5");
    QVERIFY2(database.saveClipboardHistory(record), qPrintable(database.lastError()));
    QCOMPARE(database.clipboardHistory().size(), 1);
}

void UnitTests::clipboardServiceSuppressesAppOwnedImageChanges()
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    auto previous = std::make_unique<QMimeData>();
    if (const auto *mime = clipboard->mimeData())
        for (const auto &format : mime->formats()) previous->setData(format, mime->data(format));
    const auto restore = qScopeGuard([&] { clipboard->setMimeData(previous.release()); });
    ClipboardService service(clipboard);
    QSignalSpy external(&service, &ClipboardService::externalImageAvailable);
    QImage image(18, 12, QImage::Format_ARGB32);
    image.fill(Qt::magenta);
    service.writeImage(image);
    QCOMPARE(service.hasImage(), true);
    QCOMPARE(external.count(), 0);
    service.processImageChange(image, true);
    QCOMPARE(external.count(), 0);
    service.processImageChange(image, false);
    QCOMPARE(external.count(), 1);
    QCOMPARE(external.first().first().value<QImage>(), image);
}

void UnitTests::clipboardHistoryPersistsAndDeduplicatesTextAndFiles()
{
    QTemporaryDir referenceDirectory(g_testData->filePath(QStringLiteral("history-file-refs-XXXXXX")));
    QVERIFY(referenceDirectory.isValid());
    const QString referencedFile = QDir(referenceDirectory.path()).filePath(QStringLiteral("kept.txt"));
    QFile source(referencedFile);
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write(QByteArrayLiteral("keep this user file"));
    source.close();

    QString textId;
    QString filesId;
    QString imageId;
    {
        SettingsManager settings;
        ProfileManager profiles(&settings);
        ScreenshotManager screenshots(&profiles, &settings);
        ClipboardHistoryManager history(screenshots.database(), screenshots.clipboardService(), &settings, &screenshots);
        // Captures can share the same millisecond. Row zero is not a reliable
        // identity: SQL breaks equal timestamps by stable ID, not event order.
        const auto entryOfType = [&](const QString &type) {
            for (int row = 0; row < history.model()->rowCount(); ++row) {
                const auto record = history.model()->recordAt(row);
                if (record.type == type) return record;
            }
            return ClipboardHistoryRecord{};
        };
        ClipboardSnapshot text;
        text.type = QStringLiteral("text");
        text.text = QStringLiteral("persisted note");
        screenshots.clipboardService()->processContentChange(text, false);
        QTRY_VERIFY2(history.count() == 1, qPrintable(screenshots.database()->lastError()));
        textId = entryOfType(QStringLiteral("text")).id;
        QVERIFY(!textId.isEmpty());
        screenshots.clipboardService()->processContentChange(text, false);
        QTRY_COMPARE(history.count(), 1);
        QCOMPARE(entryOfType(QStringLiteral("text")).id, textId);

        ClipboardSnapshot files;
        files.type = QStringLiteral("files");
        files.urls.append(QUrl::fromLocalFile(referencedFile));
        screenshots.clipboardService()->processContentChange(files, false);
        QTRY_COMPARE(history.count(), 2);
        filesId = entryOfType(QStringLiteral("files")).id;
        QVERIFY(!filesId.isEmpty());
        screenshots.clipboardService()->processContentChange(files, false);
        QTRY_COMPARE(history.count(), 2);
        QCOMPARE(entryOfType(QStringLiteral("files")).id, filesId);
        QVERIFY(history.restoreEntry(filesId));
        QVERIFY(QGuiApplication::clipboard()->mimeData()->hasUrls());
        QCOMPARE(QGuiApplication::clipboard()->mimeData()->urls().first().toLocalFile(), referencedFile);

        ClipboardSnapshot image;
        image.type = QStringLiteral("image");
        image.image = QImage(13, 9, QImage::Format_ARGB32);
        image.image.fill(QColor(QStringLiteral("#6090d0")));
        screenshots.clipboardService()->processContentChange(image, false);
        screenshots.clipboardService()->processContentChange(image, false);
        QTRY_COMPARE_WITH_TIMEOUT(history.count(), 3, 5000);
        history.finishPendingSaves();
        imageId = entryOfType(QStringLiteral("image")).id;
        QVERIFY(!imageId.isEmpty());
        QVERIFY(QFileInfo::exists(history.model()->recordById(imageId).payloadPath));

        ClipboardSnapshot pausedText;
        pausedText.type = QStringLiteral("text");
        pausedText.text = QStringLiteral("paused content");
        history.setPaused(true);
        screenshots.clipboardService()->processContentChange(pausedText, false);
        QTRY_COMPARE(history.count(), 3);
        history.setPaused(false);
        screenshots.clipboardService()->processContentChange(pausedText, false);
        QTRY_COMPARE(history.count(), 4);
    }

    {
        SettingsManager settings;
        ProfileManager profiles(&settings);
        ScreenshotManager screenshots(&profiles, &settings);
        ClipboardHistoryManager history(screenshots.database(), screenshots.clipboardService(), &settings, &screenshots);
        QTRY_COMPARE(history.count(), 4);
        QCOMPARE(history.model()->recordById(textId).text, QStringLiteral("persisted note"));
        QCOMPARE(history.model()->recordById(filesId).fileUrls.size(), 1);
        QVERIFY(QFileInfo::exists(history.model()->recordById(imageId).payloadPath));
        QVERIFY(QFileInfo::exists(referencedFile));
        QSignalSpy messages(&history, &ClipboardHistoryManager::messageChanged);
        QVERIFY(QFile::remove(referencedFile));
        history.refresh();
        QTRY_VERIFY(history.model()->recordById(filesId).missing);
        QVERIFY(!history.restoreEntry(filesId));
        QVERIFY(messages.last().first().toString().contains(QStringLiteral("missing"), Qt::CaseInsensitive));
    }
}

void UnitTests::clipboardHistoryImagesAreOwnedAndRestoringDoesNotTriggerAutomaticSave()
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    auto previous = std::make_unique<QMimeData>();
    if (const auto *mime = clipboard->mimeData())
        for (const auto &format : mime->formats()) previous->setData(format, mime->data(format));
    const auto restore = qScopeGuard([&] { clipboard->setMimeData(previous.release()); });

    SettingsManager settings;
    QVERIFY(settings.setAutomaticClipboardSavingEnabled(true));
    ProfileManager profiles(&settings);
    ScreenshotManager screenshots(&profiles, &settings);
    ClipboardHistoryManager history(screenshots.database(), screenshots.clipboardService(), &settings, &screenshots);
    QImage image(48, 32, QImage::Format_ARGB32);
    image.fill(QColor(QStringLiteral("#d65078")));
    ClipboardSnapshot snapshot;
    snapshot.type = QStringLiteral("image");
    snapshot.image = image;
    screenshots.clipboardService()->processContentChange(snapshot, false);
    screenshots.clipboardService()->processContentChange(snapshot, false);
    QTRY_COMPARE_WITH_TIMEOUT(history.count(), 1, 5000);
    history.finishPendingSaves();
    screenshots.finishPendingSaves();
    QTRY_VERIFY2(history.count() == 1, qPrintable(screenshots.database()->lastError()));
    QCOMPARE(screenshots.database()->screenshots().size(), 1);
    const ClipboardHistoryRecord record = history.model()->recordAt(0);
    QVERIFY(QFileInfo::exists(record.payloadPath));
    QVERIFY(QFileInfo(record.payloadPath).absolutePath().endsWith(QStringLiteral("clipboard-history")));
    QCOMPARE(QImage(record.payloadPath).convertToFormat(QImage::Format_RGBA8888), image.convertToFormat(QImage::Format_RGBA8888));

    const QSignalSpy external(screenshots.clipboardService(), &ClipboardService::externalContentAvailable);
    QVERIFY(history.restoreEntry(record.id));
    QTRY_VERIFY_WITH_TIMEOUT(clipboard->mimeData()->hasImage(), 5000);
    QTest::qWait(150);
    screenshots.finishPendingSaves();
    QCOMPARE(external.count(), 0);
    QTRY_COMPARE(history.count(), 1);
    QCOMPARE(screenshots.database()->screenshots().size(), 1);

    const QString tagId = screenshots.createTag(QStringLiteral("History export"));
    QVERIFY(!tagId.isEmpty());
    QVERIFY(history.saveToGallery(record.id, tagId));
    screenshots.finishPendingSaves();
    const auto galleryRecords = screenshots.database()->screenshots();
    QCOMPARE(galleryRecords.size(), 2);
    bool foundHistoryCopy = false;
    QString galleryCopyPath;
    for (const ScreenshotRecord &galleryRecord : galleryRecords) {
        if (galleryRecord.source == QStringLiteral("clipboard-history")) {
            foundHistoryCopy = true;
            galleryCopyPath = galleryRecord.path;
            QCOMPARE(galleryRecord.tags, QStringList{QStringLiteral("History export")});
        }
    }
    QVERIFY(foundHistoryCopy);
    QVERIFY(history.clearHistory());
    QVERIFY(!QFileInfo::exists(record.payloadPath));
    QVERIFY(QFileInfo::exists(galleryCopyPath));
}

void UnitTests::clipboardHistoryClearInvalidatesActiveAndQueuedImageCaptures()
{
    SettingsManager settings;
    ProfileManager profiles(&settings);
    ScreenshotManager screenshots(&profiles, &settings);
    ClipboardHistoryManager history(screenshots.database(), screenshots.clipboardService(), &settings, &screenshots);
    auto imageSnapshot = [](const QColor &color) {
        ClipboardSnapshot snapshot;
        snapshot.type = QStringLiteral("image");
        snapshot.image = QImage(64, 48, QImage::Format_ARGB32);
        snapshot.image.fill(color);
        return snapshot;
    };
    screenshots.clipboardService()->processContentChange(imageSnapshot(QColor(QStringLiteral("#d24b4b"))), false);
    screenshots.clipboardService()->processContentChange(imageSnapshot(QColor(QStringLiteral("#4bd26f"))), false);
    screenshots.clipboardService()->processContentChange(imageSnapshot(QColor(QStringLiteral("#4b65d2"))), false);
    QVERIFY(history.clearHistory());
    screenshots.clipboardService()->processContentChange(imageSnapshot(QColor(QStringLiteral("#e6c44b"))), false);
    history.finishPendingSaves();

    const QVector<ClipboardHistoryRecord> records = screenshots.database()->clipboardHistory();
    QCOMPARE(records.size(), 1);
    QTRY_COMPARE(history.count(), 1);
    QVERIFY(QFileInfo::exists(records.first().payloadPath));
    QCOMPARE(QImage(records.first().payloadPath).pixelColor(0, 0), QColor(QStringLiteral("#e6c44b")));
    const QString payloadDirectory = QDir(AppDataPaths::root()).filePath(QStringLiteral("clipboard-history"));
    QCOMPARE(QDir(payloadDirectory).entryList({QStringLiteral("*.png")}, QDir::Files).size(), 1);
    QCOMPARE(screenshots.database()->screenshots().size(), 0);
}

void UnitTests::clipboardHistoryRetentionPreservesPinsAndNeverDeletesReferencedOrGalleryFiles()
{
    SettingsManager settings;
    QVERIFY(settings.setClipboardHistoryRetentionDays(1));
    QVERIFY(settings.setClipboardHistoryRetentionCount(2));
    ProfileManager profiles(&settings);
    ScreenshotManager screenshots(&profiles, &settings);

    ClipboardHistoryRecord stale;
    stale.id = QStringLiteral("01234567-89ab-cdef-0123-456789abcdef");
    stale.type = QStringLiteral("text");
    stale.capturedAt = QDateTime::currentDateTimeUtc().addDays(-4).toString(Qt::ISODateWithMs);
    stale.fingerprint = QStringLiteral("stale-unpinned");
    stale.text = QStringLiteral("expired");
    QVERIFY2(screenshots.database()->saveClipboardHistory(stale), qPrintable(screenshots.database()->lastError()));
    ClipboardHistoryRecord pinnedStale = stale;
    pinnedStale.id = QStringLiteral("11234567-89ab-cdef-0123-456789abcdef");
    pinnedStale.fingerprint = QStringLiteral("stale-pinned");
    pinnedStale.pinned = true;
    QVERIFY(screenshots.database()->saveClipboardHistory(pinnedStale));
    ClipboardHistoryManager history(screenshots.database(), screenshots.clipboardService(), &settings, &screenshots);
    QTRY_VERIFY(history.model()->recordById(stale.id).id.isEmpty());
    QTRY_VERIFY(!history.model()->recordById(pinnedStale.id).id.isEmpty());

    QTemporaryDir references(g_testData->filePath(QStringLiteral("history-ownership-XXXXXX")));
    QVERIFY(references.isValid());
    const QString userFile = QDir(references.path()).filePath(QStringLiteral("user-owned.txt"));
    QFile file(userFile);
    QVERIFY(file.open(QIODevice::WriteOnly)); file.write(QByteArrayLiteral("user")); file.close();
    ClipboardSnapshot fileSnapshot;
    fileSnapshot.type = QStringLiteral("files");
    fileSnapshot.urls.append(QUrl::fromLocalFile(userFile));
    screenshots.clipboardService()->processContentChange(fileSnapshot, false);
    QTRY_COMPARE(history.count(), 2); // The pinned stale entry plus this reference.

    const QString externalPayload = QDir(references.path()).filePath(QStringLiteral("external.png"));
    QImage sourceImage(9, 7, QImage::Format_ARGB32); sourceImage.fill(Qt::blue); QVERIFY(sourceImage.save(externalPayload));
    ClipboardHistoryRecord externalRecord;
    externalRecord.id = QStringLiteral("21234567-89ab-cdef-0123-456789abcdef");
    externalRecord.type = QStringLiteral("image");
    externalRecord.capturedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    externalRecord.fingerprint = QStringLiteral("external-path-record");
    externalRecord.payloadPath = externalPayload;
    externalRecord.width = 9; externalRecord.height = 7; externalRecord.sizeBytes = QFileInfo(externalPayload).size();
    QVERIFY(screenshots.database()->saveClipboardHistory(externalRecord));
    const QString galleryImage = QDir(references.path()).filePath(QStringLiteral("gallery.png"));
    QVERIFY(sourceImage.save(galleryImage));
    QVERIFY(screenshots.database()->addScreenshot(galleryImage, sourceImage.size(), QFileInfo(galleryImage).size(), QStringLiteral("clipboard")));
    history.refresh();
    QVERIFY(history.deleteEntry(externalRecord.id));
    QVERIFY(QFileInfo::exists(externalPayload));
    QVERIFY(history.clearHistory());
    QVERIFY(QFileInfo::exists(userFile));
    QVERIFY(QFileInfo::exists(galleryImage));
    QCOMPARE(screenshots.database()->screenshots().size(), 1);
}

void UnitTests::clipboardHistoryRetentionExcludesAgeExpiredImageBytes()
{
    constexpr qint64 MiB = 1024 * 1024;
    SettingsManager settings;
    QVERIFY(settings.setClipboardHistoryRetentionDays(1));
    QVERIFY(settings.setClipboardHistoryRetentionCount(20));
    QVERIFY(settings.setClipboardHistoryStorageLimitMB(16));
    ProfileManager profiles(&settings);
    ScreenshotManager screenshots(&profiles, &settings);
    const QString payloadDirectory = QDir(AppDataPaths::root()).filePath(QStringLiteral("clipboard-history"));
    QVERIFY(QDir().mkpath(payloadDirectory));
    auto addImage = [&](const QString &id, const QString &name, qint64 bytes, const QDateTime &captured) {
        const QString payload = QDir(payloadDirectory).filePath(name);
        QFile file(payload);
        if (!file.open(QIODevice::WriteOnly) || !file.resize(bytes)) return false;
        file.close();
        ClipboardHistoryRecord record;
        record.id = id;
        record.type = QStringLiteral("image");
        record.capturedAt = captured.toUTC().toString(Qt::ISODateWithMs);
        record.fingerprint = QStringLiteral("retention-") + id;
        record.payloadPath = QFileInfo(payload).absoluteFilePath();
        record.sizeBytes = bytes;
        record.width = 1;
        record.height = 1;
        return screenshots.database()->saveClipboardHistory(record);
    };
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QString expiredId = QStringLiteral("31234567-89ab-cdef-0123-456789abcdef");
    const QString freshId = QStringLiteral("41234567-89ab-cdef-0123-456789abcdef");
    QVERIFY(addImage(expiredId, expiredId + QStringLiteral(".png"), 17 * MiB, now.addDays(-3)));
    QVERIFY(addImage(freshId, freshId + QStringLiteral(".png"), MiB, now));

    ClipboardHistoryManager history(screenshots.database(), screenshots.clipboardService(), &settings, &screenshots);
    QTRY_VERIFY(history.model()->recordById(expiredId).id.isEmpty());
    QTRY_VERIFY(!history.model()->recordById(freshId).id.isEmpty());
    QVERIFY(!QFileInfo::exists(QDir(payloadDirectory).filePath(expiredId + QStringLiteral(".png"))));
    QVERIFY(QFileInfo::exists(QDir(payloadDirectory).filePath(freshId + QStringLiteral(".png"))));

    const QString largeFreshId = QStringLiteral("51234567-89ab-cdef-0123-456789abcdef");
    QVERIFY(addImage(largeFreshId, largeFreshId + QStringLiteral(".png"), 20 * MiB, now.addSecs(-7200)));
    ClipboardHistoryRecord olderText;
    olderText.id = QStringLiteral("61234567-89ab-cdef-0123-456789abcdef");
    olderText.type = QStringLiteral("text");
    olderText.capturedAt = now.addSecs(-3600).toString(Qt::ISODateWithMs);
    olderText.fingerprint = QStringLiteral("retention-middle-text");
    olderText.text = QStringLiteral("middle");
    QVERIFY(screenshots.database()->saveClipboardHistory(olderText));
    ClipboardHistoryRecord newestText = olderText;
    newestText.id = QStringLiteral("71234567-89ab-cdef-0123-456789abcdef");
    newestText.capturedAt = now.addSecs(-60).toString(Qt::ISODateWithMs);
    newestText.fingerprint = QStringLiteral("retention-newest-text");
    newestText.text = QStringLiteral("newest");
    QVERIFY(screenshots.database()->saveClipboardHistory(newestText));
    QVERIFY(settings.setClipboardHistoryRetentionCount(2));

    const auto retained = screenshots.database()->clipboardHistory();
    QCOMPARE(retained.size(), 2);
    const auto containsId = [&retained](const QString &id) {
        return std::any_of(retained.cbegin(), retained.cend(), [&id](const ClipboardHistoryRecord &record) {
            return record.id == id;
        });
    };
    QVERIFY(containsId(freshId));
    QVERIFY(containsId(newestText.id));
    QVERIFY(!containsId(largeFreshId));
    QVERIFY(!containsId(olderText.id));
}

void UnitTests::clipboardHistoryShortcutIsConfigurableAndPersisted()
{
    const int modifiers = int(Qt::ControlModifier | Qt::ShiftModifier);
    {
        ApplicationController controller;
        QVERIFY(!controller.setFileShelfShortcut(Qt::Key_V, Qt::ControlModifier));
        QVERIFY(!controller.setClipboardHistoryShortcut(Qt::Key_V, Qt::ControlModifier));
        QVERIFY(!HotkeyManager::isSupportedKey(Qt::Key_V, Qt::ControlModifier));
        QVERIFY(controller.addHotkey(Qt::Key_F18, modifiers, QStringLiteral("saveScreenshot"), QString(), QString()));
        QVERIFY(!controller.setFileShelfShortcut(Qt::Key_F18, modifiers));
        QVERIFY(controller.setFileShelfShortcut(Qt::Key_F20, modifiers));
        QVERIFY(!controller.setClipboardHistoryShortcut(Qt::Key_F20, modifiers));
        QVERIFY(!controller.setClipboardHistoryShortcut(Qt::Key_F18, modifiers));
        QVERIFY(controller.setClipboardHistoryShortcut(Qt::Key_F19, modifiers));
        QVERIFY(controller.settings()->setClipboardHistoryRetentionDays(14));
        QVERIFY(controller.settings()->setClipboardHistoryRetentionCount(750));
        QVERIFY(controller.settings()->setClipboardHistoryStorageLimitMB(768));
        QCOMPARE(controller.settings()->clipboardHistoryShortcutKey(), int(Qt::Key_F19));
        QCOMPARE(controller.settings()->clipboardHistoryShortcutModifiers(), modifiers);
        QVERIFY(controller.hotkeys()->clipboardHistoryStatus().contains(QStringLiteral("window"), Qt::CaseInsensitive));
    }
    SettingsManager restored;
    QCOMPARE(restored.fileShelfShortcutKey(), int(Qt::Key_F20));
    QCOMPARE(restored.fileShelfShortcutModifiers(), modifiers);
    QCOMPARE(restored.clipboardHistoryShortcutKey(), int(Qt::Key_F19));
    QCOMPARE(restored.clipboardHistoryShortcutModifiers(), modifiers);
    QCOMPARE(restored.clipboardHistoryRetentionDays(), 14);
    QCOMPARE(restored.clipboardHistoryRetentionCount(), 750);
    QCOMPARE(restored.clipboardHistoryStorageLimitMB(), 768);
}

void UnitTests::automaticClipboardSavesDeduplicateAndKeepManualSaves()
{
    QTemporaryDir destination(g_testData->filePath(QStringLiteral("auto-save-destination-XXXXXX")));
    QVERIFY(destination.isValid());
    SettingsManager settings;
    QVERIFY(settings.setGeneralScreenshotDirectory(destination.path()));
    QVERIFY(settings.setAutomaticClipboardSavingEnabled(true));
    ProfileManager profiles(&settings);
    ScreenshotManager screenshots(&profiles, &settings);
    QSignalSpy finished(&screenshots, &ScreenshotManager::saveFinished);
    const QString tagId = screenshots.createTag(QStringLiteral("Automatic"));
    QVERIFY(!tagId.isEmpty());
    QVERIFY(settings.setAutomaticClipboardSaveTagId(tagId));

    QImage image(29, 17, QImage::Format_ARGB32);
    image.fill(QColor(210, 30, 60));
    screenshots.setAutomaticClipboardSavingPaused(true);
    screenshots.m_clipboardService.processImageChange(image, false);
    QCOMPARE(screenshots.m_fingerprintJobs.size(), 0);
    screenshots.setAutomaticClipboardSavingPaused(false);
    screenshots.m_clipboardService.processImageChange(image, false);
    screenshots.m_clipboardService.processImageChange(image.convertToFormat(QImage::Format_RGB32), false);
    screenshots.finishPendingSaves();
    const auto records = screenshots.m_database.screenshots();
    QCOMPARE(records.size(), 1);
    QCOMPARE(records.first().source, QStringLiteral("clipboard-auto"));
    QCOMPARE(records.first().captureProfileId, profiles.activeProfileId());
    QCOMPARE(records.first().tags, QStringList{QStringLiteral("Automatic")});
    QCOMPARE(QImage(records.first().path).convertToFormat(QImage::Format_RGBA8888),
             image.convertToFormat(QImage::Format_RGBA8888));
    QCOMPARE(finished.count(), 1);
    QVERIFY(finished.first().at(1).toBool());

    // An explicit manual save is intentionally independent of the automatic
    // content fingerprint cache and can still save the same clipboard image.
    QVERIFY(settings.setAutomaticClipboardSavingEnabled(false));
    QClipboard *clipboard = QGuiApplication::clipboard();
    auto previous = std::make_unique<QMimeData>();
    if (const auto *mime = clipboard->mimeData())
        for (const auto &format : mime->formats()) previous->setData(format, mime->data(format));
    const auto restore = qScopeGuard([&] { clipboard->setMimeData(previous.release()); });
    clipboard->setImage(image);
    const QString manual = screenshots.saveClipboardImage();
    QVERIFY(!manual.isEmpty());
    screenshots.finishPendingSaves();
    QVERIFY(QFileInfo::exists(manual));
    QCOMPARE(finished.count(), 2);
    QVERIFY2(finished.last().at(1).toBool(), qPrintable(finished.last().at(2).toString()));
    QCOMPARE(screenshots.m_database.screenshots().size(), 2);
}

void UnitTests::automaticClipboardSnapshotsProfileAndDestinationAcrossRapidChanges()
{
    QTemporaryDir first(g_testData->filePath(QStringLiteral("auto-profile-one-XXXXXX")));
    QTemporaryDir second(g_testData->filePath(QStringLiteral("auto-profile-two-XXXXXX")));
    QVERIFY(first.isValid());
    QVERIFY(second.isValid());
    SettingsManager settings;
    QVERIFY(settings.setGeneralScreenshotDirectory(first.path()));
    QVERIFY(settings.setAutomaticClipboardSavingEnabled(true));
    ProfileManager profiles(&settings);
    const QString profileOne = profiles.activeProfileId();
    QVERIFY(profiles.setScreenshotDirectory(profileOne, first.path()));
    const QString profileTwo = profiles.createProfile(QStringLiteral("Second capture target"));
    QVERIFY(!profileTwo.isEmpty());
    QVERIFY(profiles.setScreenshotDirectory(profileTwo, second.path()));
    ScreenshotManager screenshots(&profiles, &settings);

    QImage one(21, 14, QImage::Format_ARGB32);
    one.fill(Qt::cyan);
    QImage two(22, 15, QImage::Format_ARGB32);
    two.fill(Qt::darkGreen);
    screenshots.m_clipboardService.processImageChange(one, false);
    QVERIFY(profiles.setActiveProfile(profileTwo));
    screenshots.m_clipboardService.processImageChange(two, false);
    screenshots.finishPendingSaves();

    const auto records = screenshots.m_database.screenshots();
    QCOMPARE(records.size(), 2);
    bool foundOne = false;
    bool foundTwo = false;
    for (const ScreenshotRecord &record : records) {
        if (record.captureProfileId == profileOne) {
            foundOne = true;
            QCOMPARE(QFileInfo(record.path).absolutePath(), QFileInfo(first.path()).absoluteFilePath());
            QCOMPARE(QImage(record.path), one);
        } else if (record.captureProfileId == profileTwo) {
            foundTwo = true;
            QCOMPARE(QFileInfo(record.path).absolutePath(), QFileInfo(second.path()).absoluteFilePath());
            QCOMPARE(QImage(record.path), two);
        }
    }
    QVERIFY(foundOne);
    QVERIFY(foundTwo);
}

void UnitTests::automaticClipboardReportsUnavailableDestinationsAndQueueSaturation()
{
    QTemporaryDir destination(g_testData->filePath(QStringLiteral("auto-error-destination-XXXXXX")));
    QVERIFY(destination.isValid());
    SettingsManager settings;
    QVERIFY(settings.setGeneralScreenshotDirectory(destination.path()));
    QVERIFY(settings.setAutomaticClipboardSavingEnabled(true));
    ProfileManager profiles(&settings);
    ScreenshotManager screenshots(&profiles, &settings);
    QSignalSpy messages(&screenshots, &ScreenshotManager::messageChanged);
    QSignalSpy finished(&screenshots, &ScreenshotManager::saveFinished);
    QImage first(12, 12, QImage::Format_ARGB32); first.fill(Qt::red);
    QImage second(13, 12, QImage::Format_ARGB32); second.fill(Qt::blue);
    QImage third(14, 12, QImage::Format_ARGB32); third.fill(Qt::green);
    screenshots.m_clipboardService.processImageChange(first, false);
    screenshots.m_clipboardService.processImageChange(second, false);
    screenshots.m_clipboardService.processImageChange(third, false);
    QVERIFY(messages.count() >= 1);
    QVERIFY(messages.last().first().toString().contains(QStringLiteral("queue is full")));
    screenshots.finishPendingSaves();
    QCOMPARE(screenshots.m_database.screenshots().size(), 2);

    const QString unavailable = QDir(destination.path()).filePath(QStringLiteral("removed-later"));
    QVERIFY(QDir().mkpath(unavailable));
    screenshots.setDestinationOverride(unavailable);
    QImage fourth(10, 10, QImage::Format_ARGB32); fourth.fill(Qt::yellow);
    screenshots.m_clipboardService.processImageChange(fourth, false);
    QVERIFY(QDir().rmdir(unavailable));
    screenshots.finishPendingSaves();
    QVERIFY(messages.count() >= 2);
    QVERIFY(messages.last().first().toString().contains(QStringLiteral("destination is unavailable")));
    QCOMPARE(screenshots.m_database.screenshots().size(), 2);

    screenshots.clearDestinationOverride();
    const QString transientTag = screenshots.createTag(QStringLiteral("Removed during save"));
    QVERIFY(!transientTag.isEmpty());
    QVERIFY(settings.setAutomaticClipboardSaveTagId(transientTag));
    screenshots.m_clipboardService.processImageChange(fourth, false);
    QVERIFY(screenshots.deleteTag(transientTag));
    screenshots.finishPendingSaves();
    QCOMPARE(finished.count(), 3);
    QVERIFY(!finished.last().at(1).toBool());
    QVERIFY(QFileInfo::exists(finished.last().first().toString()));
    QVERIFY(finished.last().at(2).toString().contains(QStringLiteral("could not be added to the gallery")));
    QCOMPARE(screenshots.m_database.screenshots().size(), 2);
}

void UnitTests::clipboardAndQrTextLayouts()
{
    ApplicationController controller;
    ClipboardSnapshot text;
    text.type=QStringLiteral("text");text.text=QStringLiteral("Readable clipboard preview with enough text to wrap across lines.");
    controller.screenshots()->clipboardService()->processContentChange(text,false);
    QImage image(160,100,QImage::Format_ARGB32);image.fill(Qt::blue);
    controller.screenshots()->clipboardService()->processImageChange(image,false);
    controller.clipboardHistory()->finishPendingSaves();
    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("thumbnail"),new ThumbnailProvider);
    engine.addImageProvider(QStringLiteral("qrpreview"),new QrImageProvider(controller.qrCode()));
    for(const auto &entry:QList<QPair<QString,QObject *>>{{QStringLiteral("app"),&controller},{QStringLiteral("settings"),controller.settings()},{QStringLiteral("profiles"),controller.profiles()},{QStringLiteral("screenshots"),controller.screenshots()},{QStringLiteral("clipboardHistory"),controller.clipboardHistory()},{QStringLiteral("fileShelf"),controller.fileShelf()},{QStringLiteral("qrCode"),controller.qrCode()},{QStringLiteral("hotkeys"),controller.hotkeys()},{QStringLiteral("actions"),controller.actions()},{QStringLiteral("actionRunner"),controller.actionRunner()},{QStringLiteral("windowArrangements"),controller.windowArrangements()}})
        engine.rootContext()->setContextProperty(entry.first,entry.second);
    QSignalSpy warnings(&engine,&QQmlEngine::warnings);
    engine.loadFromModule(QStringLiteral("ScreenshotHotkeyManagerTest"),QStringLiteral("Main"));
    QVERIFY(!engine.rootObjects().isEmpty());
    auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first());QVERIFY(window);
    window->show();QVERIFY(QTest::qWaitForWindowExposed(window));
    for(const QString &theme:{QStringLiteral("dark"),QStringLiteral("light")}){
        QVERIFY(controller.settings()->setAppearance(theme));
        for(const QSize &size:{QSize(1040,680),QSize(1360,850)}){
            window->resize(size);controller.setCurrentPage(QStringLiteral("Clipboard History"));QTest::qWait(100);
            QQuickItem *list=findItem(window->contentItem(),QStringLiteral("clipboardHistoryList"));QVERIFY(list);
            QVector<QQuickItem *> rows;
            std::function<void(QQuickItem *)> collect=[&](QQuickItem *item){
                if(item->objectName()==QStringLiteral("clipboardHistoryEntry"))rows.append(item);
                for(auto *child:item->childItems())collect(child);
            };collect(list);
            QCOMPARE(rows.size(),2);
            for(auto *row:rows){
                auto *actions=findItem(row,QStringLiteral("clipboardHistoryActions"));QVERIFY(actions);
                const QPointF bottom=actions->mapToItem(row,QPointF(actions->width(),actions->height()));
                QVERIFY2(bottom.y()<=row->height()+0.5,"History actions extend outside their card");
                auto *preview=findItem(row,QStringLiteral("clipboardHistoryPreview"));QVERIFY(preview);
                QCOMPARE(preview->property("color").value<QColor>(),controller.settings()->palette().value(QStringLiteral("#dedde3")).value<QColor>());
            }
            QVERIFY(window->grabWindow().save(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("history-text-%1-%2.png").arg(theme).arg(size.width()))));
            controller.setCurrentPage(QStringLiteral("QR Codes"));QTest::qWait(80);
            auto *generatePanel=findItem(window->contentItem(),QStringLiteral("qrGeneratePanel"));
            auto *decodePanel=findItem(window->contentItem(),QStringLiteral("qrDecodePanel"));
            QVERIFY(generatePanel&&decodePanel);
            const QRectF generateRect(generatePanel->position(),generatePanel->size());
            const QRectF decodeRect(decodePanel->position(),decodePanel->size());
            QVERIFY2(!generateRect.intersects(decodeRect),"QR panels overlap");
            QVERIFY(generatePanel->x()>=0&&generatePanel->x()+generatePanel->width()<=generatePanel->parentItem()->width()+0.5);
            QVERIFY(decodePanel->x()>=0&&decodePanel->x()+decodePanel->width()<=decodePanel->parentItem()->width()+0.5);
            for(const QString &name:{QStringLiteral("qrTextInput"),QStringLiteral("qrDecodedContent")}){
                auto *area=findItem(window->contentItem(),name);QVERIFY(area);
                QVERIFY(area->property("topPadding").toReal()>=0);
                auto *placeholder=findItem(area,QStringLiteral("insetPlaceholder"));QVERIFY(placeholder);
                QVERIFY(placeholder->isVisible());QVERIFY(placeholder->y()>=0);
                QVERIFY(placeholder->y()+placeholder->height()<=area->height()+0.5);
                area->forceActiveFocus();QTest::qWait(20);QVERIFY(placeholder->y()>=0);
                area->setProperty("text",QStringLiteral("Example content"));QVERIFY(!placeholder->isVisible());
                area->setProperty("text",QString());QVERIFY(placeholder->isVisible());
            }
            QVERIFY(window->grabWindow().save(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("qr-text-%1-%2.png").arg(theme).arg(size.width()))));
        }
    }
    QCOMPARE(warnings.count(),0);
}
