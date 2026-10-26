#include "UnitTests.h"
#include <atomic>

void UnitTests::oversizedProfileEditPreservesSavedState()
{
    SettingsManager settings;
    ProfileManager profiles(&settings);
    ActionManager actions(&profiles);
    const QString id = profiles.activeProfileId();
    const QString sequence = actions.createSequence(QStringLiteral("Size boundary"));
    const QString step = actions.addStep(sequence, QStringLiteral("launchApplication"));
    QVERIFY(actions.updateStep(sequence, step, {{QStringLiteral("arguments"), QStringLiteral("--original")}}));
    const QString path = QDir(AppDataPaths::root()).filePath(QStringLiteral("profiles/") + id + QStringLiteral(".json"));
    auto read = [&] { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); };
    const QByteArray before = read();
    QVERIFY(!before.isEmpty());
    QSignalSpy errors(&profiles, &ProfileManager::errorOccurred);
    QVERIFY(!actions.updateStep(sequence, step, {{QStringLiteral("arguments"), QString(5 * 1024 * 1024, QLatin1Char('x'))}}));
    QCOMPARE(read(), before);
    QVERIFY(!errors.isEmpty());
    QVERIFY(errors.last().first().toString().contains(QStringLiteral("4 MiB")));
    QCOMPARE(actions.steps(sequence).first().toMap().value(QStringLiteral("arguments")).toString(), QStringLiteral("--original"));
    ProfileManager restarted(&settings);
    QVERIFY(restarted.profileById(id));
    QCOMPARE(restarted.profileById(id)->sequences.first().steps.first().arguments, QStringLiteral("--original"));
}

void UnitTests::unicodeScreenshotMigrationMergesTagsAndSourceLinks()
{
    const QString databasePath = QDir(AppDataPaths::root()).filePath(QStringLiteral("screenshots.db"));
    const QString upper = QDir(AppDataPaths::root()).filePath(QStringLiteral("Ä.png"));
    const QString lower = QDir(AppDataPaths::root()).filePath(QStringLiteral("ä.png"));
    const QString child = QDir(AppDataPaths::root()).filePath(QStringLiteral("edited.png"));
    QImage image(8, 8, QImage::Format_ARGB32); image.fill(Qt::cyan);
    QVERIFY(image.save(upper));
    { ApplicationDatabase database(databasePath); QVERIFY(database.initialize()); }
    const QString connection = QStringLiteral("unicode-schema-six-fixture");
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        database.setDatabaseName(databasePath); QVERIFY(database.open());
        QSqlQuery query(database);
        QVERIFY(query.exec(QStringLiteral("DROP INDEX screenshots_path_key_idx")));
        QVERIFY(query.exec(QStringLiteral("ALTER TABLE screenshots DROP COLUMN path_key")));
        QVERIFY(query.exec(QStringLiteral("PRAGMA user_version=6")));
        for (const auto &entry : QVector<QPair<QString, QString>>{{QStringLiteral("first"), upper}, {QStringLiteral("duplicate"), lower}, {QStringLiteral("child"), child}}) {
            query.prepare(QStringLiteral("INSERT INTO screenshots(id,path) VALUES(?,?)"));
            query.addBindValue(entry.first); query.addBindValue(entry.second); QVERIFY(query.exec());
        }
        QVERIFY(query.exec(QStringLiteral("UPDATE screenshots SET source_screenshot_id='duplicate' WHERE id='child'")));
        QVERIFY(query.exec(QStringLiteral("INSERT INTO tags(id,name) VALUES('a','First tag'),('b','Second tag')")));
        QVERIFY(query.exec(QStringLiteral("INSERT INTO screenshot_tags(screenshot_id,tag_id) VALUES('first','a'),('duplicate','b')")));
        database.close();
    }
    QSqlDatabase::removeDatabase(connection);
    {
        ApplicationDatabase database(databasePath);
        QVERIFY2(database.initialize(), qPrintable(database.lastError()));
        QCOMPARE(database.screenshots().size(), 2);
        QCOMPARE(database.screenshotIdForPath(upper), QStringLiteral("first"));
        QCOMPARE(database.screenshotIdForPath(lower), QStringLiteral("first"));
        QCOMPARE(database.screenshotById(QStringLiteral("first")).tags, QStringList({QStringLiteral("First tag"), QStringLiteral("Second tag")}));
        QCOMPARE(database.screenshotById(QStringLiteral("child")).sourceScreenshotId, QStringLiteral("first"));
        QString duplicateId;
        QVERIFY(database.addScreenshot(lower, image.size(), QFileInfo(upper).size(), QStringLiteral("test"), &duplicateId));
        QCOMPARE(duplicateId, QStringLiteral("first"));
        ScreenshotRecord removed;
        QVERIFY(database.removeScreenshot(duplicateId, &removed));
        QVERIFY(database.restoreScreenshot(removed));
        QCOMPARE(database.screenshotIdForPath(lower), QStringLiteral("first"));
    }
    ApplicationDatabase restarted(databasePath); QVERIFY(restarted.initialize());
    QCOMPARE(restarted.screenshots().size(), 2);
    QCOMPARE(QImage(upper).pixelColor(0, 0), QColor(Qt::cyan));
}

void UnitTests::fileShelfMoveRejectsEditingSources()
{
    for (bool directory : {false, true}) {
        QTemporaryDir workspace(QDir(AppDataPaths::root()).filePath(QStringLiteral("editing-source-XXXXXX")));
        QVERIFY(workspace.isValid());
        const QString sourceFolder = QDir(workspace.path()).filePath(QStringLiteral("source"));
        const QString destination = QDir(workspace.path()).filePath(QStringLiteral("destination"));
        QVERIFY(QDir().mkpath(sourceFolder)); QVERIFY(QDir().mkpath(destination));
        const QString path = QDir(sourceFolder).filePath(QStringLiteral("editing.txt"));
        QFile writer(path); QVERIFY(writer.open(QIODevice::ReadWrite));
        writer.write("current editing data"); QVERIFY(writer.flush());
        ApplicationDatabase database(QDir(workspace.path()).filePath(QStringLiteral("test.db")));
        QVERIFY(database.initialize());
        FileShelfManager shelf(&database, nullptr);
        QVERIFY(shelf.addPaths({directory ? sourceFolder : path})); shelf.selectAll();
        QSignalSpy finished(&shelf, &FileShelfManager::operationFinished);
        QVERIFY(shelf.startTransfer(shelf.previewTransfer(destination, true), destination, true));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QCOMPARE(finished.first().at(0).toInt(), 0);
        QCOMPARE(finished.first().at(2).toInt(), 1);
        QVERIFY(QFileInfo::exists(path));
        writer.seek(0); QCOMPARE(writer.readAll(), QByteArrayLiteral("current editing data"));
        QCOMPARE(QDir(destination).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size(), 0);
    }
}

void UnitTests::fileShelfMovePreservesNewFolderEntries()
{
    const QString source = QDir(AppDataPaths::root()).filePath(QStringLiteral("changing-folder"));
    const QString destination = QDir(AppDataPaths::root()).filePath(QStringLiteral("destination"));
    QVERIFY(QDir().mkpath(source)); QVERIFY(QDir().mkpath(destination));
    QFile original(QDir(source).filePath(QStringLiteral("large.bin")));
    QVERIFY(original.open(QIODevice::WriteOnly));
    const QByteArray block(1024 * 1024, 'A');
    for (int i = 0; i < 2; ++i) QCOMPARE(original.write(block), qint64(block.size()));
    original.close();
    ApplicationDatabase database; QVERIFY(database.initialize());
    std::atomic_bool added = false;
    QString injectionError;
    FileShelfManager shelf(&database, nullptr); QVERIFY(shelf.addPaths({source})); shelf.selectAll();
    QSignalSpy finished(&shelf, &FileShelfManager::operationFinished);
    // busyChanged runs before the worker starts. Inject after its first chunk,
    // with another chunk still to copy, rather than racing a polling thread.
    connect(&shelf, &FileShelfManager::busyChanged, &shelf, [&] {
        if (shelf.busy()) {
            shelf.m_transfer->afterFirstCopyChunkForTesting = [&] {
                QFile late(QDir(source).filePath(QStringLiteral("new-during-move.txt")));
                if (late.open(QIODevice::WriteOnly)) {
                    added = late.write("new user data") == 13;
                    late.close();
                }
                if (!added.load()) injectionError = late.errorString();
            };
        }
    });
    QVERIFY(shelf.startTransfer(shelf.previewTransfer(destination, true), destination, true));
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 10000);
    QVERIFY2(added.load(), qPrintable(injectionError));
    QCOMPARE(shelf.bytesCopied(), double(2 * block.size()));
    QFile preserved(QDir(source).filePath(QStringLiteral("new-during-move.txt")));
    QVERIFY(preserved.open(QIODevice::ReadOnly)); QCOMPARE(preserved.readAll(), QByteArrayLiteral("new user data"));
    QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(), block.repeated(2));
    QCOMPARE(finished.first().at(0).toInt(), 0);
    QCOMPARE(finished.first().at(2).toInt(), 1);
    QVERIFY(finished.first().at(3).toStringList().join(QLatin1Char('\n')).contains(QStringLiteral("source folder changed")));
    QCOMPARE(shelf.data(shelf.index(0, 0), FileShelfManager::PathRole).toString(), source);
    QVERIFY(QDir(destination).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System).isEmpty());
}

void UnitTests::screenshotAnnotationsRenderShapesTextAndOpaqueRedaction()
{
    const QString source = QDir(AppDataPaths::root()).filePath(QStringLiteral("annotations.png"));
    QImage image(400, 240, QImage::Format_ARGB32); image.fill(Qt::white); QVERIFY(image.save(source));
    auto annotation = [](const QString &type, const QString &color, QPointF first, QPointF last) {
        const QVariantList points{QVariantMap{{QStringLiteral("x"), first.x()}, {QStringLiteral("y"), first.y()}},
                                  QVariantMap{{QStringLiteral("x"), last.x()}, {QStringLiteral("y"), last.y()}}};
        return QVariantMap{{QStringLiteral("type"), type}, {QStringLiteral("color"), color}, {QStringLiteral("width"), 4},
                           {QStringLiteral("points"), points}};
    };
    QVariantMap text = annotation(QStringLiteral("text"), QStringLiteral("#000000"), {20, 150}, {20, 150});
    text.insert(QStringLiteral("text"), QStringLiteral("Hello 123")); text.insert(QStringLiteral("fontSize"), 28);
    const QVariantList annotations{
        annotation(QStringLiteral("arrow"), QStringLiteral("#ff0000"), {20, 25}, {100, 25}),
        annotation(QStringLiteral("rectangle"), QStringLiteral("#0000ff"), {130, 20}, {190, 65}),
        annotation(QStringLiteral("ellipse"), QStringLiteral("#008000"), {220, 20}, {290, 65}),
        annotation(QStringLiteral("highlight"), QStringLiteral("#ffff00"), {20, 90}, {95, 125}),
        annotation(QStringLiteral("redact"), QStringLiteral("#0100ff00"), {130, 90}, {195, 125}), text};
    QImage output; QString error;
    QVERIFY2(ScreenshotEditorImage::render(source, image.rect(), annotations, &output, &error), qPrintable(error));
    QCOMPARE(output.pixelColor(50, 25), QColor(Qt::red));
    QCOMPARE(output.pixelColor(130, 40), QColor(Qt::blue));
    QCOMPARE(output.pixelColor(255, 20), QColor(QStringLiteral("#008000")));
    const QColor highlighted = output.pixelColor(50, 100);
    QCOMPARE(highlighted.red(), 255); QCOMPARE(highlighted.green(), 255); QVERIFY(highlighted.blue() > 100 && highlighted.blue() < 230);
    QCOMPARE(output.pixelColor(150, 100), QColor(Qt::black));
    QCOMPARE(output.pixelColor(150, 100).alpha(), 255);
    int inkPixels = 0;
    for (int y = 150; y < 190; ++y) for (int x = 20; x < 180; ++x) if (output.pixelColor(x, y) != QColor(Qt::white)) ++inkPixels;
    QVERIFY(inkPixels > 50);
    QImage cropped;
    QVERIFY(ScreenshotEditorImage::render(source, QRect(120, 80, 90, 60), annotations, &cropped, &error));
    QCOMPARE(cropped.pixelColor(30, 20), QColor(Qt::black));
    QCOMPARE(QImage(source).pixelColor(150, 100), QColor(Qt::white));
}

void UnitTests::screenshotBulkTagPopupAppliesAndRemovesMixedAssignments()
{
    ApplicationController controller; controller.setCurrentPage(QStringLiteral("Screenshots"));
    auto *screenshots = controller.screenshots(); screenshots->setDestinationOverride(AppDataPaths::root());
    QStringList ids;
    for (const QString &name : {QStringLiteral("one.png"), QStringLiteral("two.png")}) {
        const QString path = QDir(AppDataPaths::root()).filePath(name);
        QImage image(60, 40, QImage::Format_ARGB32); image.fill(Qt::cyan); QVERIFY(image.save(path));
        QString id; QVERIFY(screenshots->database()->addScreenshot(path, image.size(), QFileInfo(path).size(), QStringLiteral("test"), &id)); ids.append(id);
    }
    const QString tag = screenshots->createTag(QStringLiteral("UI tag")); QVERIFY(!tag.isEmpty());
    QVERIFY(screenshots->setTagForScreenshots({ids.first()}, tag, true));
    screenshots->refreshGallery();
    QQmlEngine engine;
    engine.addImageProvider(QStringLiteral("thumbnail"), new ThumbnailProvider);
    engine.rootContext()->setContextProperty(QStringLiteral("app"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("settings"), controller.settings());
    engine.rootContext()->setContextProperty(QStringLiteral("screenshots"), screenshots);
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQuickWindow window; window.resize(1200, 850);
    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(TEST_QML_DIR "/pages/ScreenshotsPage.qml")));
    QTRY_VERIFY(component.status() != QQmlComponent::Loading);
    std::unique_ptr<QObject> page(component.create()); QVERIFY2(page, qPrintable(component.errorString()));
    auto *root = qobject_cast<QQuickItem *>(page.get()); QVERIFY(root);
    root->setParentItem(window.contentItem()); root->setSize(QSizeF(1200, 850));
    window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_COMPARE(screenshots->gallery()->rowCount(), 2);
    root->setProperty("selectedIds", ids);
    auto click = [&](QQuickItem *item) { QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
        item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint()); };
    auto *button = findItem(root, QStringLiteral("bulkTagButton")); QVERIFY(button); click(button);
    QObject *popup = page->findChild<QObject *>(QStringLiteral("bulkTagPopup")); QVERIFY(popup);
    QTRY_VERIFY(popup->property("visible").toBool());
    QVERIFY(popup->property("y").toReal() >= button->mapToScene(QPointF(0, button->height())).y() - 1);
    QQuickItem *check = nullptr;
    QTRY_VERIFY((check = findItem(window.contentItem(), QStringLiteral("bulkTagCheckBox_") + tag)) != nullptr);
    QTRY_COMPARE(check->property("checkState").toInt(), int(Qt::PartiallyChecked));
    click(check);
    QTRY_COMPARE(screenshots->tagAssignmentState(tag, ids), 1);
    QTRY_COMPARE(check->property("checkState").toInt(), int(Qt::Checked));
    QTRY_VERIFY(!screenshots->gallery()->loading());
    QVERIFY(window.grabWindow().save(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("bulk-tag-fixed.png"))));
    click(check);
    QTRY_COMPARE(screenshots->tagAssignmentState(tag, ids), 0);
    QTRY_COMPARE(check->property("checkState").toInt(), int(Qt::Unchecked));
    QCOMPARE(warnings.size(), 0);
}
