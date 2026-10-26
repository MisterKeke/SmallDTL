#include "UnitTests.h"

void UnitTests::screenshotFilenameCollision()
{
    QTemporaryDir directory(g_testData->filePath(QStringLiteral("screenshots-XXXXXX")));
    QVERIFY(directory.isValid());
    const QDateTime timestamp(QDate(2026, 10, 14), QTime(13, 45, 2), QTimeZone::UTC);
    const QString first = QDir(directory.path()).filePath(QStringLiteral("Screenshot_2026-10-14_13-45-02.png"));
    QFile existing(first);
    QVERIFY(existing.open(QIODevice::WriteOnly));
    existing.write("existing");
    existing.close();
    QCOMPARE(QFileInfo(ScreenshotNaming::uniqueFilePath(directory.path(), timestamp)).fileName(),
             QStringLiteral("Screenshot_2026-10-14_13-45-02_2.png"));
    const QString second = QDir(directory.path()).filePath(QStringLiteral("Screenshot_2026-10-14_13-45-02_2.png"));
    QCOMPARE(QFileInfo(ScreenshotNaming::uniqueFilePath(directory.path(), timestamp, {second})).fileName(),
             QStringLiteral("Screenshot_2026-10-14_13-45-02_3.png"));
}

void UnitTests::screenshotIndexMigrationIsPreservedAndRestartSafe()
{
    const QString imagePath = QDir(AppDataPaths::root()).filePath(QStringLiteral("legacy-image.png"));
    const QString missingPath = QDir(AppDataPaths::root()).filePath(QStringLiteral("legacy-missing.png"));
    QImage image(31, 17, QImage::Format_ARGB32);
    image.fill(Qt::cyan);
    QVERIFY(image.save(imagePath));
    QFile imageBeforeFile(imagePath);
    QVERIFY(imageBeforeFile.open(QIODevice::ReadOnly));
    const QByteArray imageBytes = imageBeforeFile.readAll();
    const QJsonObject indexObject{{QStringLiteral("schemaVersion"), 1},
                                  {QStringLiteral("paths"), QJsonArray{imagePath, missingPath}}};
    QCOMPARE(indexObject.value(QStringLiteral("paths")).toArray().size(), 2);
    const QByteArray indexBytes = QJsonDocument(indexObject).toJson(QJsonDocument::Indented);
    const QString indexPath = QDir(AppDataPaths::root()).filePath(QStringLiteral("screenshot-index.json"));
    QFile indexFile(indexPath);
    QVERIFY(indexFile.open(QIODevice::WriteOnly));
    QCOMPARE(indexFile.write(indexBytes), indexBytes.size());
    indexFile.close();
    const QString databasePath = QDir(AppDataPaths::root()).filePath(QStringLiteral("screenshots.db"));
    QStringList ids;
    {
        ApplicationDatabase database(databasePath);
        QVERIFY2(database.initialize(indexPath), qPrintable(database.lastError()));
        const auto records = database.screenshots();
        QStringList importedPaths;
        for (const auto &record : records)
            importedPaths.append(record.path);
        QVERIFY2(records.size() == 2, qPrintable(QStringLiteral("Imported %1; expected %2 and %3")
                                                      .arg(importedPaths.join(QStringLiteral(" | ")), imagePath, missingPath)));
        for (const auto &record : records) {
            ids.append(record.id);
            QCOMPARE(record.source, QStringLiteral("legacy-index"));
            if (record.path == imagePath) {
                QCOMPARE(record.width, 31);
                QCOMPARE(record.height, 17);
                QCOMPARE(record.sizeBytes, QFileInfo(imagePath).size());
            } else {
                QCOMPARE(record.path, missingPath);
                QVERIFY(!QFileInfo::exists(record.path));
            }
        }
    }
    QFile preserved(indexPath);
    QVERIFY(preserved.open(QIODevice::ReadOnly));
    QCOMPARE(preserved.readAll(), indexBytes);
    {
        ApplicationDatabase database(databasePath);
        QVERIFY2(database.initialize(indexPath), qPrintable(database.lastError()));
        const auto records = database.screenshots();
        QCOMPARE(records.size(), 2);
        QStringList restartedIds;
        for (const auto &record : records)
            restartedIds.append(record.id);
        std::sort(ids.begin(), ids.end());
        std::sort(restartedIds.begin(), restartedIds.end());
        QCOMPARE(restartedIds, ids);
    }
    QVERIFY(QFileInfo::exists(imagePath));
    QFile imageAfterFile(imagePath);
    QVERIFY(imageAfterFile.open(QIODevice::ReadOnly));
    QCOMPARE(imageAfterFile.readAll(), imageBytes);
}

void UnitTests::screenshotDatabaseTagsAndBulkTransactions()
{
    const QString firstPath = QDir(AppDataPaths::root()).filePath(QStringLiteral("first.png"));
    const QString secondPath = QDir(AppDataPaths::root()).filePath(QStringLiteral("second.png"));
    QImage image(20, 12, QImage::Format_ARGB32);
    image.fill(Qt::yellow);
    QVERIFY(image.save(firstPath));
    QVERIFY(image.save(secondPath));
    ApplicationDatabase database(QDir(AppDataPaths::root()).filePath(QStringLiteral("screenshots.db")));
    QVERIFY2(database.initialize(), qPrintable(database.lastError()));
    QString firstId, secondId;
    QVERIFY2(database.addScreenshot(firstPath, image.size(), QFileInfo(firstPath).size(), QStringLiteral("clipboard"), &firstId), qPrintable(database.lastError()));
    QVERIFY2(database.addScreenshot(secondPath, image.size(), QFileInfo(secondPath).size(), QStringLiteral("clipboard"), &secondId), qPrintable(database.lastError()));
    QVERIFY(!firstId.isEmpty());
    QVERIFY(!secondId.isEmpty());
    const QString topicId = database.createTag(QStringLiteral("Research"));
    QVERIFY2(!topicId.isEmpty(), qPrintable(database.lastError()));
    QVERIFY(database.createTag(QStringLiteral("research")).isEmpty());
    QVERIFY(database.setTagForScreenshots({firstId}, topicId, true));
    QCOMPARE(database.tagAssignmentState(topicId, {firstId, secondId}), 2);
    QVERIFY2(database.setTagForScreenshots({firstId, secondId, firstId}, topicId, true), qPrintable(database.lastError()));
    QCOMPARE(database.tagAssignmentState(topicId, {firstId, secondId}), 1);

    const QString rollbackTag = database.createTag(QStringLiteral("Rollback"));
    QVERIFY(!rollbackTag.isEmpty());
    QVERIFY(!database.setTagForScreenshots({firstId, QStringLiteral("missing-screenshot-id")}, rollbackTag, true));
    QCOMPARE(database.tagAssignmentState(rollbackTag, {firstId}), 0);
    QVERIFY(database.renameTag(topicId, QStringLiteral("Project")));
    auto records = database.screenshots();
    QCOMPARE(records.size(), 2);
    for (const auto &record : records)
        QCOMPARE(record.tags, QStringList{QStringLiteral("Project")});
    QVERIFY(database.setTagForScreenshots({firstId, secondId}, topicId, false));
    QCOMPARE(database.tagAssignmentState(topicId, {firstId, secondId}), 0);
    QVERIFY(database.setTagForScreenshots({firstId, secondId}, topicId, true));
    QCOMPARE(database.tagAssignmentState(topicId, {firstId, secondId}), 1);

    QString originalTimestamp;
    for (const auto &record : database.screenshots())
        if (record.id == firstId)
            originalTimestamp = record.timestamp;
    const QString renamedPath = QDir(AppDataPaths::root()).filePath(QStringLiteral("renamed.png"));
    QVERIFY(QFile::rename(firstPath, renamedPath));
    QVERIFY2(database.updateScreenshotPath(firstId, renamedPath), qPrintable(database.lastError()));
    QCOMPARE(database.screenshotIdForPath(renamedPath), firstId);
    bool preservedRename = false;
    for (const auto &record : database.screenshots()) {
        if (record.id == firstId) {
            preservedRename = true;
            QCOMPARE(record.timestamp, originalTimestamp);
            QCOMPARE(record.tags, QStringList{QStringLiteral("Project")});
        }
    }
    QVERIFY(preservedRename);
    QVERIFY(database.deleteTag(topicId));
    QVERIFY(QFileInfo::exists(renamedPath));
    QVERIFY(QFileInfo::exists(secondPath));
    records = database.screenshots();
    QCOMPARE(records.size(), 2);
    for (const auto &record : records)
        QVERIFY(record.tags.isEmpty());
    QVERIFY2(database.removeScreenshot(secondId), qPrintable(database.lastError()));
    QVERIFY(QFileInfo::exists(secondPath));
}

void UnitTests::screenshotEditorCoordinatesCropAndDrawingUseSourcePixels()
{
    const QSize sourceSize(100, 50);
    const QPointF atOneScale = ScreenshotEditorImage::displayToImagePoint(QPointF(99, 48), QSizeF(200, 100), sourceSize);
    const QPointF atTwoScale = ScreenshotEditorImage::displayToImagePoint(QPointF(198, 96), QSizeF(400, 200), sourceSize);
    QCOMPARE(atOneScale, QPointF(49.5, 24));
    QCOMPARE(atTwoScale, atOneScale);
    const QPointF letterboxed = ScreenshotEditorImage::displayToImagePoint(QPointF(200, 150), QSizeF(400, 300), sourceSize);
    QCOMPARE(letterboxed, QPointF(50, 25));

    const QRectF locked = ScreenshotEditorImage::cropFromDrag(QPointF(90, 40), QPointF(20, 20), QSize(100, 50), 1.0);
    QCOMPARE(locked, QRectF(70, 20, 20, 20));
    const QRectF free = ScreenshotEditorImage::cropFromDrag(QPointF(10, 5), QPointF(30, 15), QSize(100, 50));
    QCOMPARE(free, QRectF(10, 5, 20, 10));

    const QString sourcePath = QDir(AppDataPaths::root()).filePath(QStringLiteral("editor-source.png"));
    QImage source(32, 24, QImage::Format_ARGB32);
    source.fill(Qt::black);
    source.setDotsPerMeterX(3780);
    source.setDotsPerMeterY(3780);
    QVERIFY(source.save(sourcePath));
    QVariantMap stroke;
    stroke.insert(QStringLiteral("color"), QStringLiteral("#ffff0000"));
    stroke.insert(QStringLiteral("width"), 4.0);
    QVariantList strokePoints;
    strokePoints.append(QVariantMap{{QStringLiteral("x"), 8.0}, {QStringLiteral("y"), 10.0}});
    strokePoints.append(QVariantMap{{QStringLiteral("x"), 14.0}, {QStringLiteral("y"), 10.0}});
    stroke.insert(QStringLiteral("points"), strokePoints);
    const QVariantList strokes{stroke};
    QImage output;
    QString error;
    QVERIFY2(ScreenshotEditorImage::render(sourcePath, QRect(4, 5, 16, 10), strokes, &output, &error), qPrintable(error));
    QCOMPARE(output.size(), QSize(16, 10));
    QCOMPARE(output.dotsPerMeterX(), 3780);
    QCOMPARE(output.dotsPerMeterY(), 3780);
    QCOMPARE(output.pixelColor(7, 5), QColor(Qt::red));
    QCOMPARE(output.pixelColor(1, 1), QColor(Qt::black));
}

void UnitTests::screenshotEditedCopyPersistsSourceAndInheritedTags()
{
    const QString originalPath = QDir(AppDataPaths::root()).filePath(QStringLiteral("editor-original.png"));
    const QString editedPath = QDir(AppDataPaths::root()).filePath(QStringLiteral("editor-copy.png"));
    QImage image(18, 12, QImage::Format_ARGB32);
    image.fill(Qt::green);
    QVERIFY(image.save(originalPath));
    QVERIFY(image.save(editedPath));
    const QString databasePath = QDir(AppDataPaths::root()).filePath(QStringLiteral("screenshots.db"));
    QString originalId;
    QString editedId;
    const QString label = QStringLiteral("Keep");
    {
        ApplicationDatabase database(databasePath);
        QVERIFY2(database.initialize(), qPrintable(database.lastError()));
        QVERIFY2(database.addScreenshot(originalPath, image.size(), QFileInfo(originalPath).size(), QStringLiteral("clipboard"), &originalId), qPrintable(database.lastError()));
        const QString tagId = database.createTag(label);
        QVERIFY(!tagId.isEmpty());
        QVERIFY(database.setTagForScreenshots({originalId}, tagId, true));
        QVERIFY2(database.addScreenshot(editedPath, image.size(), QFileInfo(editedPath).size(),
                                       QStringLiteral("edited-copy"), &editedId, originalId, true),
                 qPrintable(database.lastError()));
        const ScreenshotRecord edited = database.screenshotById(editedId);
        QCOMPARE(edited.sourceScreenshotId, originalId);
        QCOMPARE(edited.tags, QStringList{label});
    }
    {
        ApplicationDatabase database(databasePath);
        QVERIFY2(database.initialize(), qPrintable(database.lastError()));
        const ScreenshotRecord edited = database.screenshotById(editedId);
        QCOMPARE(edited.sourceScreenshotId, originalId);
        QCOMPARE(edited.tags, QStringList{label});
        const QString renamedPath = QDir(AppDataPaths::root()).filePath(QStringLiteral("editor-copy-renamed.png"));
        QVERIFY(QFile::rename(editedPath, renamedPath));
        QVERIFY2(database.updateScreenshotPath(editedId, renamedPath), qPrintable(database.lastError()));
        const ScreenshotRecord renamed = database.screenshotById(editedId);
        QCOMPARE(renamed.id, editedId);
        QCOMPARE(renamed.sourceScreenshotId, originalId);
        QCOMPARE(renamed.tags, QStringList{label});
    }
}

void UnitTests::screenshotEditorSourceLinkMigrationUpgradesVersionTwo()
{
    const QString databasePath = QDir(AppDataPaths::root()).filePath(QStringLiteral("version-two.db"));
    const QString connectionName = QStringLiteral("ScreenshotEditorMigrationFixture");
    {
        QSqlDatabase legacy = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        legacy.setDatabaseName(databasePath);
        QVERIFY(legacy.open());
        {
            QSqlQuery query(legacy);
            QVERIFY(query.exec(QStringLiteral("CREATE TABLE screenshots (id TEXT PRIMARY KEY NOT NULL, path TEXT NOT NULL COLLATE NOCASE UNIQUE, captured_at TEXT NOT NULL DEFAULT '', width INTEGER NOT NULL DEFAULT 0, height INTEGER NOT NULL DEFAULT 0, size_bytes INTEGER NOT NULL DEFAULT 0, source TEXT NOT NULL DEFAULT '')")));
            QVERIFY(query.exec(QStringLiteral("CREATE TABLE tags (id TEXT PRIMARY KEY NOT NULL, name TEXT NOT NULL COLLATE NOCASE UNIQUE)")));
            QVERIFY(query.exec(QStringLiteral("CREATE TABLE screenshot_tags (screenshot_id TEXT NOT NULL REFERENCES screenshots(id) ON DELETE CASCADE, tag_id TEXT NOT NULL REFERENCES tags(id) ON DELETE CASCADE, PRIMARY KEY(screenshot_id, tag_id))")));
            QVERIFY(query.exec(QStringLiteral("CREATE TABLE migration_state (key TEXT PRIMARY KEY NOT NULL, value TEXT NOT NULL)")));
            QVERIFY(query.exec(QStringLiteral("INSERT INTO screenshots(id,path,captured_at,width,height,size_bytes,source) VALUES('existing-v2','old.png','2026-01-01T00:00:00Z',40,30,120,'legacy-index')")));
            QVERIFY(query.exec(QStringLiteral("PRAGMA user_version=2")));
        }
        legacy.close();
    }
    QSqlDatabase::removeDatabase(connectionName);

    ApplicationDatabase database(databasePath);
    QVERIFY2(database.initialize(), qPrintable(database.lastError()));
    const ScreenshotRecord migrated = database.screenshotById(QStringLiteral("existing-v2"));
    QCOMPARE(migrated.id, QStringLiteral("existing-v2"));
    QCOMPARE(migrated.width, 40);
    QCOMPARE(migrated.height, 30);
    QCOMPARE(migrated.sourceScreenshotId, QString());
    QString childId;
    QVERIFY2(database.addScreenshot(QDir(AppDataPaths::root()).filePath(QStringLiteral("new-copy.png")), QSize(8, 6), 20,
                                    QStringLiteral("edited-copy"), &childId, migrated.id, false,
                                    QStringLiteral("captured-profile-id")),
             qPrintable(database.lastError()));
    QCOMPARE(database.screenshotById(childId).captureProfileId, QStringLiteral("captured-profile-id"));
    QVERIFY2(database.removeScreenshot(migrated.id), qPrintable(database.lastError()));
    QVERIFY(database.screenshotById(childId).sourceScreenshotId.isEmpty());
}

void UnitTests::screenshotEditorFailedDecodeLeavesNoCopy()
{
    SettingsManager settings;
    ProfileManager profiles(&settings);
    ScreenshotManager screenshots(&profiles, &settings);
    QTemporaryDir destination(g_testData->filePath(QStringLiteral("editor-save-destination-XXXXXX")));
    QVERIFY(destination.isValid());
    screenshots.setDestinationOverride(destination.path());

    const QString corruptPath = QDir(AppDataPaths::root()).filePath(QStringLiteral("editor-corrupt.png"));
    QFile corrupt(corruptPath);
    QVERIFY(corrupt.open(QIODevice::WriteOnly));
    QCOMPARE(corrupt.write("not an image"), qint64(12));
    corrupt.close();
    QString sourceId;
    QVERIFY2(screenshots.m_database.addScreenshot(corruptPath, QSize(12, 12), QFileInfo(corruptPath).size(),
                                                   QStringLiteral("clipboard"), &sourceId),
             qPrintable(screenshots.databaseError()));
    QSignalSpy finished(&screenshots, &ScreenshotManager::saveFinished);
    const QString target = screenshots.saveEditedScreenshot(sourceId, {}, {}, true);
    QVERIFY(!target.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QCOMPARE(finished.first().at(1).toBool(), false);
    QVERIFY(!QFileInfo::exists(target));
    QVERIFY(screenshots.m_database.screenshotIdForPath(target).isEmpty());
}

void UnitTests::screenshotEditorSaveCreatesTaggedCopyAndRenameKeepsSource()
{
    SettingsManager settings;
    ProfileManager profiles(&settings);
    ScreenshotManager screenshots(&profiles, &settings);
    QTemporaryDir destination(g_testData->filePath(QStringLiteral("editor-copy-destination-XXXXXX")));
    QVERIFY(destination.isValid());
    screenshots.setDestinationOverride(destination.path());

    const QString sourcePath = QDir(AppDataPaths::root()).filePath(QStringLiteral("editor-save-source.png"));
    QImage original(28, 20, QImage::Format_ARGB32);
    original.fill(QColor(QStringLiteral("#18293a")));
    QVERIFY(original.save(sourcePath));
    QFile originalFile(sourcePath);
    QVERIFY(originalFile.open(QIODevice::ReadOnly));
    const QByteArray originalBytes = originalFile.readAll();
    originalFile.close();
    QString sourceId;
    QVERIFY2(screenshots.m_database.addScreenshot(sourcePath, original.size(), QFileInfo(sourcePath).size(),
                                                   QStringLiteral("clipboard"), &sourceId),
             qPrintable(screenshots.databaseError()));
    const QString tagId = screenshots.createTag(QStringLiteral("Edited source"));
    QVERIFY(!tagId.isEmpty());
    QVERIFY(screenshots.setTagForScreenshots({sourceId}, tagId, true));

    QVariantMap stroke;
    stroke.insert(QStringLiteral("color"), QStringLiteral("#ffffcc33"));
    stroke.insert(QStringLiteral("width"), 3.0);
    stroke.insert(QStringLiteral("points"), QVariantList{
        QVariantMap{{QStringLiteral("x"), 8.0}, {QStringLiteral("y"), 8.0}},
        QVariantMap{{QStringLiteral("x"), 18.0}, {QStringLiteral("y"), 8.0}}});
    const QVariantMap crop{{QStringLiteral("x"), 4}, {QStringLiteral("y"), 3},
                           {QStringLiteral("width"), 12}, {QStringLiteral("height"), 10}};
    QSignalSpy finished(&screenshots, &ScreenshotManager::saveFinished);
    const QString copyPath = screenshots.saveEditedScreenshot(sourceId, crop, {stroke}, true);
    QVERIFY(!copyPath.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QVERIFY2(finished.first().at(1).toBool(), qPrintable(finished.first().at(2).toString()));
    QImage copy(copyPath);
    QCOMPARE(copy.size(), QSize(12, 10));
    QCOMPARE(copy.pixelColor(6, 5), QColor(QStringLiteral("#ffffcc33")));
    QFile verifyOriginal(sourcePath);
    QVERIFY(verifyOriginal.open(QIODevice::ReadOnly));
    QCOMPARE(verifyOriginal.readAll(), originalBytes);

    const QString copyId = screenshots.m_database.screenshotIdForPath(copyPath);
    QVERIFY(!copyId.isEmpty());
    ScreenshotRecord record = screenshots.m_database.screenshotById(copyId);
    QCOMPARE(record.source, QStringLiteral("edited-copy"));
    QCOMPARE(record.sourceScreenshotId, sourceId);
    QCOMPARE(record.tags, QStringList{QStringLiteral("Edited source")});

    const QString renamedPath = screenshots.renameScreenshot(copyPath, QStringLiteral("Editor copy renamed"));
    QVERIFY(!renamedPath.isEmpty());
    record = screenshots.m_database.screenshotById(copyId);
    QCOMPARE(record.path, QFileInfo(renamedPath).absoluteFilePath());
    QCOMPARE(record.id, copyId);
    QCOMPARE(record.sourceScreenshotId, sourceId);
    QCOMPARE(record.tags, QStringList{QStringLiteral("Edited source")});
    QVERIFY(QFileInfo::exists(sourcePath));
}

void UnitTests::screenshotEditorUndoRedoRestoresCropAndStrokes()
{
    SettingsManager settings;
    ProfileManager profiles(&settings);
    ScreenshotManager screenshots(&profiles, &settings);
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("settings"), &settings);
    engine.rootContext()->setContextProperty(QStringLiteral("screenshots"), &screenshots);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(TEST_QML_DIR "/components/ScreenshotEditor.qml")));
    QTRY_VERIFY(component.status() != QQmlComponent::Loading);
    std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));

    const QVariantList initialStrokes{QVariantMap{{QStringLiteral("color"), QStringLiteral("#ffff0000")}}};
    const QVariantMap initialCrop{{QStringLiteral("x"), 2}, {QStringLiteral("y"), 3},
                                  {QStringLiteral("width"), 10}, {QStringLiteral("height"), 8}};
    object->setProperty("strokes", initialStrokes);
    object->setProperty("crop", initialCrop);
    QVERIFY(QMetaObject::invokeMethod(object.get(), "rememberEdits"));
    object->setProperty("strokes", initialStrokes + QVariantList{QVariantMap{{QStringLiteral("color"), QStringLiteral("#ff00ff00")}}});
    object->setProperty("crop", QVariant());

    QVERIFY(QMetaObject::invokeMethod(object.get(), "undo"));
    QCOMPARE(object->property("strokes").toList().size(), 1);
    QCOMPARE(object->property("crop").toMap(), initialCrop);
    QVERIFY(QMetaObject::invokeMethod(object.get(), "redo"));
    QCOMPARE(object->property("strokes").toList().size(), 2);
    QVERIFY(object->property("crop").isNull());
    QCOMPARE(warnings.count(), 0);
}

void UnitTests::screenshotEditorOpensFromGalleryAndRenders()
{
    ApplicationController controller;
    QVERIFY(controller.settings()->setVisualStyle(QStringLiteral("retro-arcade")));
    applyRetroTestFont();
    controller.setCurrentPage(QStringLiteral("Screenshots"));
    const QString path = QDir(AppDataPaths::root()).filePath(QStringLiteral("editor-visual-source.png"));
    QImage image(640, 400, QImage::Format_ARGB32);
    image.fill(QColor(QStringLiteral("#29415a")));
    QPainter painter(&image);
    painter.setPen(QPen(QColor(QStringLiteral("#f07888")), 12));
    painter.drawLine(QPoint(90, 300), QPoint(540, 80));
    painter.setBrush(QColor(QStringLiteral("#ffc96b")));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QPoint(300, 190), 52, 52);
    painter.end();
    QVERIFY(image.save(path));
    controller.screenshots()->setDestinationOverride(AppDataPaths::root());
    QString screenshotId;
    QVERIFY2(controller.screenshots()->m_database.addScreenshot(path, image.size(), QFileInfo(path).size(),
                                                                 QStringLiteral("clipboard"), &screenshotId),
             qPrintable(controller.screenshots()->databaseError()));
    controller.screenshots()->refreshGallery();

    QQmlEngine engine;
    engine.addImageProvider(QStringLiteral("thumbnail"), new ThumbnailProvider);
    engine.addImageProvider(QStringLiteral("qrpreview"), new QrImageProvider(controller.qrCode()));
    engine.rootContext()->setContextProperty(QStringLiteral("app"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("settings"), controller.settings());
    engine.rootContext()->setContextProperty(QStringLiteral("screenshots"), controller.screenshots());
    engine.rootContext()->setContextProperty(QStringLiteral("qrCode"), controller.qrCode());
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQuickWindow window;
    window.resize(1200, 850);
    window.setColor(controller.settings()->palette().value(QStringLiteral("window")).value<QColor>());
    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(TEST_QML_DIR "/pages/ScreenshotsPage.qml")));
    QTRY_VERIFY(component.status() != QQmlComponent::Loading);
    std::unique_ptr<QObject> page(component.create());
    QVERIFY2(page, qPrintable(component.errorString()));
    auto *root = qobject_cast<QQuickItem *>(page.get());
    QVERIFY(root);
    root->setParentItem(window.contentItem());
    root->setSize(QSizeF(1200, 850));
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_COMPARE(controller.screenshots()->gallery()->rowCount(), 1);
    auto *editButton = findItem(root, QStringLiteral("screenshotCardEdit"));
    QVERIFY(editButton);
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                      editButton->mapToScene(QPointF(editButton->width() / 2, editButton->height() / 2)).toPoint());
    QObject *editor = page->findChild<QObject *>(QStringLiteral("screenshotEditor"));
    QVERIFY(editor);
    QTRY_VERIFY(editor->property("visible").toBool());
    QVERIFY(page->findChild<QObject *>(QStringLiteral("editorSaveCopy")));
    QTest::qWait(200);
    auto *editMouse = findItem(window.contentItem(), QStringLiteral("editorMouseArea"));
    QVERIFY(editMouse);
    int annotationCount = 0;
    for (const QString &tool : {QStringLiteral("arrow"), QStringLiteral("rectangle"), QStringLiteral("ellipse"),
                                QStringLiteral("highlight"), QStringLiteral("redact"), QStringLiteral("text")}) {
        editor->setProperty("tool", tool);
        editor->setProperty("annotationText", QStringLiteral("Annotated screenshot"));
        QTest::qWait(20);
        const QPoint start = editMouse->mapToScene(QPointF(editMouse->width() * 0.15, editMouse->height() * (0.12 + annotationCount * 0.11))).toPoint();
        const QPoint end = editMouse->mapToScene(QPointF(editMouse->width() * 0.55, editMouse->height() * (0.18 + annotationCount * 0.11))).toPoint();
        QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(&window, end, 10);
        QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, end);
        ++annotationCount;
        QTRY_COMPARE(editor->property("strokes").toList().size(), annotationCount);
    }
    QVERIFY(QMetaObject::invokeMethod(editor, "undo"));
    QCOMPARE(editor->property("strokes").toList().size(), 5);
    QVERIFY(QMetaObject::invokeMethod(editor, "redo"));
    QCOMPARE(editor->property("strokes").toList().size(), 6);
    const QString capturePath = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("editor-visual.png"));
    QVERIFY(window.grabWindow().save(capturePath));
    editor->setProperty("crop", QVariantMap{{QStringLiteral("x"), 20}, {QStringLiteral("y"), 15},
                                             {QStringLiteral("width"), 300}, {QStringLiteral("height"), 180}});
    QVERIFY(QMetaObject::invokeMethod(editor, "requestClose"));
    QObject *discardDialog = editor->findChild<QObject *>(QStringLiteral("editorDiscardDialog"));
    QVERIFY(discardDialog);
    QTRY_VERIFY(discardDialog->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(discardDialog, "reject"));
    QTRY_VERIFY(!discardDialog->property("visible").toBool());
    QVERIFY(editor->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(editor, "resetEdits"));
    QVERIFY(QMetaObject::invokeMethod(editor, "requestClose"));
    QTRY_VERIFY(!editor->property("visible").toBool());
    QCOMPARE(warnings.count(), 0);
}

void UnitTests::invalidScreenshotIndexIsPreservedAndReported()
{
    const QString indexPath = QDir(AppDataPaths::root()).filePath(QStringLiteral("screenshot-index.json"));
    const QByteArray invalid = QByteArrayLiteral("{\"schemaVersion\":1,\"paths\":[");
    QFile index(indexPath);
    QVERIFY(index.open(QIODevice::WriteOnly));
    QCOMPARE(index.write(invalid), invalid.size());
    index.close();
    ApplicationDatabase database(QDir(AppDataPaths::root()).filePath(QStringLiteral("screenshots.db")));
    QVERIFY(!database.initialize(indexPath));
    QVERIFY(!database.lastError().isEmpty());
    QFile preserved(indexPath);
    QVERIFY(preserved.open(QIODevice::ReadOnly));
    QCOMPARE(preserved.readAll(), invalid);
}

void UnitTests::screenshotDestinationResolution()
{
    SettingsManager settings;
    ProfileManager profiles(&settings);
    QTemporaryDir profileDirectory(g_testData->filePath(QStringLiteral("profile-destination-XXXXXX")));
    QTemporaryDir sessionDirectory(g_testData->filePath(QStringLiteral("session-destination-XXXXXX")));
    QVERIFY(profileDirectory.isValid());
    QVERIFY(sessionDirectory.isValid());
    const QString activeId = profiles.activeProfileId();
    QVERIFY(profiles.setScreenshotDirectory(activeId, profileDirectory.path()));
    ScreenshotManager screenshots(&profiles, &settings);
    QCOMPARE(QDir::cleanPath(screenshots.destination()), QDir::cleanPath(profileDirectory.path()));
    screenshots.setDestinationOverride(sessionDirectory.path());
    QCOMPARE(QDir::cleanPath(screenshots.destination()), QDir::cleanPath(sessionDirectory.path()));
    screenshots.clearDestinationOverride();
    QCOMPARE(QDir::cleanPath(screenshots.destination()), QDir::cleanPath(profileDirectory.path()));
}

void UnitTests::screenshotShutdownCompletesPendingSaves()
{
    SettingsManager settings;ProfileManager profiles(&settings);ScreenshotManager screenshots(&profiles,&settings);
    QClipboard *clipboard=QGuiApplication::clipboard();auto previous=std::make_unique<QMimeData>();if(const auto *mime=clipboard->mimeData())for(const auto &format:mime->formats())previous->setData(format,mime->data(format));const auto restore=qScopeGuard([&]{clipboard->setMimeData(previous.release());});
    QImage image(32,32,QImage::Format_ARGB32);image.fill(Qt::yellow);clipboard->setImage(image);
    const QString first=screenshots.saveClipboardImage();const QString second=screenshots.saveClipboardImage();QVERIFY(!first.isEmpty());QVERIFY(!second.isEmpty());QVERIFY(first!=second);
    QVERIFY(screenshots.saveClipboardImage().isEmpty());
    screenshots.finishPendingSaves();QVERIFY(QFileInfo::exists(first));QVERIFY(QFileInfo::exists(second));
    QTRY_COMPARE(screenshots.gallery()->rowCount(),2);
    const QImage saved(first);QCOMPARE(saved,image);
}

void UnitTests::screenshotCommitDoesNotReplaceExistingFile()
{
    const QString path=QDir(AppDataPaths::root()).filePath(QStringLiteral("competing.png"));QFile existing(path);QVERIFY(existing.open(QIODevice::WriteOnly));existing.write("competitor");existing.close();
    QImage image(16,16,QImage::Format_ARGB32);image.fill(Qt::cyan);
    QVERIFY(!ScreenshotManager::writeImage(image,path).success);
    QVERIFY(existing.open(QIODevice::ReadOnly));QCOMPARE(existing.readAll(),QByteArrayLiteral("competitor"));
    QCOMPARE(QDir(AppDataPaths::root()).entryList({QStringLiteral(".capture-*.tmp")},QDir::Files|QDir::Hidden).size(),0);
}

void UnitTests::thumbnailCacheAndRecentLimit()
{
    const QString path=QDir(AppDataPaths::root()).filePath(QStringLiteral("image.png"));QImage image(800,600,QImage::Format_ARGB32);image.fill(Qt::green);QVERIFY(image.save(path));
    ThumbnailProvider provider;QSize size;
    const QString id=QString::fromLatin1(path.toUtf8().toBase64(QByteArray::Base64UrlEncoding|QByteArray::OmitTrailingEquals));
    const QImage thumbnail=provider.requestImage(id,&size,QSize(200,150));QCOMPARE(size,QSize(200,150));QVERIFY(!thumbnail.isNull());
    const QDir cache(QDir(AppDataPaths::root()).filePath(QStringLiteral("cache/thumbnails")));const auto files=cache.entryInfoList(QDir::Files);QCOMPARE(files.size(),1);
    const auto modified=files.first().lastModified();QVERIFY(!provider.requestImage(id,&size,QSize(200,150)).isNull());QCOMPARE(cache.entryInfoList(QDir::Files).first().lastModified(),modified);
    ScreenshotGalleryModel gallery;QStringList paths;for(int i=0;i<100;++i)paths.append(QDir(AppDataPaths::root()).filePath(QStringLiteral("missing-%1.png").arg(i)));
    gallery.setDirectory(AppDataPaths::root());gallery.setManagedPaths(paths);QTRY_COMPARE(gallery.rowCount(),100);QCOMPARE(gallery.recent()->rowCount(),4);
    QVERIFY(gallery.data(gallery.index(0),ScreenshotGalleryModel::MissingRole).toBool());
}

void UnitTests::screenshotRenameKeepsNormalPaste()
{
    ApplicationController controller;controller.setCurrentPage(QStringLiteral("Screenshots"));
    QQmlEngine engine;engine.addImageProvider(QStringLiteral("thumbnail"),new ThumbnailProvider);engine.addImageProvider(QStringLiteral("qrpreview"),new QrImageProvider(controller.qrCode()));
    for(const auto &entry:QList<QPair<QString,QObject *>>{{QStringLiteral("app"),&controller},{QStringLiteral("settings"),controller.settings()},{QStringLiteral("profiles"),controller.profiles()},{QStringLiteral("screenshots"),controller.screenshots()},{QStringLiteral("qrCode"),controller.qrCode()}})engine.rootContext()->setContextProperty(entry.first,entry.second);
    QQuickWindow window;window.resize(1080,680);
    QQmlComponent component(&engine,QUrl::fromLocalFile(QStringLiteral(TEST_QML_DIR "/pages/ScreenshotsPage.qml")));QTRY_VERIFY(component.status()!=QQmlComponent::Loading);
    std::unique_ptr<QObject> page(component.create());QVERIFY2(page,qPrintable(component.errorString()));
    auto *root=qobject_cast<QQuickItem *>(page.get());root->setParentItem(window.contentItem());root->setSize(QSizeF(1080,680));window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
    QObject *dialog=page->findChild<QObject *>(QStringLiteral("screenshotRenameDialog"));QVERIFY(dialog);QVERIFY(QMetaObject::invokeMethod(dialog,"open"));
    auto *field=qobject_cast<QQuickItem *>(page->findChild<QObject *>(QStringLiteral("screenshotRenameField")));QVERIFY(field);QTRY_VERIFY(field->hasActiveFocus());
    QClipboard *clipboard=QGuiApplication::clipboard();auto previous=std::make_unique<QMimeData>();if(const auto *mime=clipboard->mimeData())for(const auto &format:mime->formats())previous->setData(format,mime->data(format));const auto restore=qScopeGuard([&]{clipboard->setMimeData(previous.release());});
    clipboard->setText(QStringLiteral("Pasted screenshot name"));QSignalSpy saves(controller.screenshots(),&ScreenshotManager::saveFinished);
    QTest::keyClick(&window,Qt::Key_V,Qt::ControlModifier);QTRY_COMPARE(field->property("text").toString(),QStringLiteral("Pasted screenshot name"));QCOMPARE(saves.count(),0);
    QVERIFY(QMetaObject::invokeMethod(dialog,"close"));
}

void UnitTests::galleryAndIdleMeasurements()
{
    ScreenshotGalleryModel gallery;QStringList paths;
    QImage image(8,8,QImage::Format_RGB32);image.fill(Qt::white);
    for(int i=0;i<2000;++i){const QString path=QDir(AppDataPaths::root()).filePath(QStringLiteral("capture-%1.png").arg(i));QVERIFY(image.save(path));paths.append(path);}
    QElapsedTimer elapsed;elapsed.start();gallery.setDirectory(AppDataPaths::root());gallery.setManagedPaths(paths);
    int ticks=0;QTimer heartbeat;connect(&heartbeat,&QTimer::timeout,this,[&]{++ticks;});heartbeat.start(1);
    QTRY_COMPARE_WITH_TIMEOUT(gallery.rowCount(),2000,10000);const qint64 metadataMs=elapsed.elapsed();heartbeat.stop();QVERIFY(ticks>0);QCOMPARE(gallery.recent()->rowCount(),4);
    QJsonObject metrics{{QStringLiteral("galleryFiles"),2000},{QStringLiteral("metadataMilliseconds"),metadataMs},{QStringLiteral("eventLoopTicks"),ticks}};
#ifdef Q_OS_WIN
    QThreadPool::globalInstance()->waitForDone();
    FILETIME created,exitTime,kernelBefore,userBefore,kernelAfter,userAfter;
    QVERIFY(GetProcessTimes(GetCurrentProcess(),&created,&exitTime,&kernelBefore,&userBefore));QElapsedTimer idle;idle.start();QTest::qWait(2000);const qint64 idleMs=idle.elapsed();
    QVERIFY(GetProcessTimes(GetCurrentProcess(),&created,&exitTime,&kernelAfter,&userAfter));
    auto time=[](FILETIME value){return (quint64(value.dwHighDateTime)<<32)|value.dwLowDateTime;};
    const double cpuMs=double(time(kernelAfter)+time(userAfter)-time(kernelBefore)-time(userBefore))/10000.0;
    PROCESS_MEMORY_COUNTERS memory{};memory.cb=sizeof(memory);QVERIFY(GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory)));
    metrics.insert(QStringLiteral("idleWallMilliseconds"),idleMs);metrics.insert(QStringLiteral("idleCpuMilliseconds"),cpuMs);metrics.insert(QStringLiteral("idleOneCoreCpuPercent"),100.0*cpuMs/idleMs);metrics.insert(QStringLiteral("workingSetMiB"),double(memory.WorkingSetSize)/1048576);metrics.insert(QStringLiteral("peakWorkingSetMiB"),double(memory.PeakWorkingSetSize)/1048576);
#endif
    QFile output(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("performance-results.json")));QVERIFY(output.open(QIODevice::WriteOnly));output.write(QJsonDocument(metrics).toJson());
}
