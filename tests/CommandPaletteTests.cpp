#include "UnitTests.h"

namespace {
bool resultExists(CommandPaletteManager *palette, const QString &type, const QString &id = {})
{
    for (int row = 0; row < palette->rowCount(); ++row) {
        const QVariantMap item = palette->resultAt(row);
        if (item.value(QStringLiteral("type")).toString() == type &&
            (id.isEmpty() || item.value(QStringLiteral("id")).toString() == id))
            return true;
    }
    return false;
}
}

void UnitTests::commandPaletteSearchRanksCaseInsensitivelyAndFindsUnicodePaths()
{
    ApplicationController controller;
    QTemporaryDir files(QDir(AppDataPaths::root()).filePath(QStringLiteral("palette-files-XXXXXX")));
    QVERIFY(files.isValid());
    const QString path = QDir(files.path()).filePath(QStringLiteral("café % résumé.txt"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("local reference");
    file.close();
    const QString collectionId = controller.fileShelf()->createCollection(QStringLiteral("Research Collection"));
    QVERIFY(!collectionId.isEmpty());
    QVERIFY(controller.fileShelf()->addPathsToCollection({path}, collectionId));
    const QVector<FileShelfRecord> shelfEntries = controller.screenshots()->database()->fileShelfEntries();
    QCOMPARE(shelfEntries.size(), 1);
    const QString shelfId = shelfEntries.first().id;
    QVERIFY(controller.fileShelf()->setEntryFavorite(shelfId, true));

    const QString screenshotPath = QDir(files.path()).filePath(QStringLiteral("vacation.png"));
    QImage image(24, 16, QImage::Format_ARGB32); image.fill(Qt::cyan);
    QVERIFY(image.save(screenshotPath));
    QString screenshotId;
    QVERIFY(controller.screenshots()->database()->addScreenshot(screenshotPath, image.size(),
        QFileInfo(screenshotPath).size(), QStringLiteral("test"), &screenshotId));
    const QString tagId = controller.screenshots()->createTag(QStringLiteral("Travel notes"));
    QVERIFY(!tagId.isEmpty());
    QVERIFY(controller.screenshots()->setTagForScreenshots({screenshotId}, tagId, true));

    CommandPaletteManager *palette = controller.commandPalette();
    palette->setQuery(QStringLiteral("CAFÉ"));
    QTRY_VERIFY_WITH_TIMEOUT(!palette->busy(), 5000);
    QVERIFY(resultExists(palette, QStringLiteral("shelf"), shelfId));
    const QVariantMap shelf = [&] {
        for (int row = 0; row < palette->rowCount(); ++row) {
            const QVariantMap item = palette->resultAt(row);
            if (item.value(QStringLiteral("id")).toString() == shelfId) return item;
        }
        return QVariantMap{};
    }();
    QVERIFY(shelf.value(QStringLiteral("available")).toBool());
    QVERIFY(shelf.value(QStringLiteral("detail")).toString().contains(QStringLiteral("Research Collection")));
    QVERIFY(shelf.value(QStringLiteral("detail")).toString().contains(QStringLiteral("Favorite")));

    palette->setQuery(QStringLiteral("TRAVEL"));
    QTRY_VERIFY_WITH_TIMEOUT(!palette->busy(), 5000);
    QVERIFY(resultExists(palette, QStringLiteral("screenshot"), screenshotId));
    QCOMPARE(CommandPaletteManager::scoreForTesting(QStringLiteral("readme"), QStringLiteral("README"), QStringLiteral("readme /docs")),
             CommandPaletteManager::scoreForTesting(QStringLiteral("README"), QStringLiteral("readme"), QStringLiteral("README /docs")));
    QVERIFY(CommandPaletteManager::scoreForTesting(QStringLiteral("read"), QStringLiteral("Readme"), QStringLiteral("readme")) >
            CommandPaletteManager::scoreForTesting(QStringLiteral("read"), QStringLiteral("Notes"), QStringLiteral("notes /readme")));
}

void UnitTests::commandPaletteScopesRecentsAndRejectsStaleSearches()
{
    ApplicationController controller;
    const QString originalProfile = controller.profiles()->activeProfileId();
    controller.setHotkeyScope(true);
    const QString globalSequence = controller.actions()->createSequence(QStringLiteral("Scope Sequence Global"));
    QVERIFY(!globalSequence.isEmpty());
    controller.setHotkeyScope(false);
    const QString activeSequence = controller.actions()->createSequence(QStringLiteral("Scope Sequence Active"));
    QVERIFY(!activeSequence.isEmpty());
    const QString inactiveProfile = controller.profiles()->createProfile(QStringLiteral("Inactive Scope"));
    QVERIFY(!inactiveProfile.isEmpty());
    QVERIFY(controller.profiles()->setActiveProfile(inactiveProfile));
    const QString inactiveSequence = controller.actions()->createSequence(QStringLiteral("Scope Sequence Hidden"));
    QVERIFY(!inactiveSequence.isEmpty());
    QVERIFY(controller.profiles()->setActiveProfile(originalProfile));

    CommandPaletteManager *palette = controller.commandPalette();
    palette->setQuery(QStringLiteral("scope sequence"));
    QTRY_VERIFY_WITH_TIMEOUT(!palette->busy(), 5000);
    QVERIFY(resultExists(palette, QStringLiteral("sequence"), globalSequence));
    QVERIFY(resultExists(palette, QStringLiteral("sequence"), activeSequence));
    QVERIFY(!resultExists(palette, QStringLiteral("sequence"), inactiveSequence));
    const QString waitStep = controller.actions()->addStep(activeSequence, QStringLiteral("wait"));
    QVERIFY(!waitStep.isEmpty());
    QVERIFY(controller.actions()->validationError(activeSequence).isEmpty());
    QVERIFY(controller.executeCommandPaletteResult(QStringLiteral("sequence"), activeSequence));
    QCOMPARE(controller.profiles()->activeProfileId(), originalProfile);
    controller.cancelActionSequence();

    const QString recentProfile = controller.profiles()->createProfile(QStringLiteral("Recent Profile"));
    QVERIFY(!recentProfile.isEmpty());
    palette->setQuery(QString());
    palette->remember(QStringLiteral("profile"), recentProfile);
    QTRY_VERIFY_WITH_TIMEOUT(!palette->busy(), 5000);
    QVERIFY(palette->rowCount() > 0);
    QCOMPARE(palette->resultAt(0).value(QStringLiteral("id")).toString(), recentProfile);

    palette->setQuery(QStringLiteral("this will be stale"));
    palette->setQuery(QStringLiteral("Recent Profile"));
    QTRY_VERIFY_WITH_TIMEOUT(!palette->busy(), 5000);
    QVERIFY(palette->rowCount() > 0);
    QCOMPARE(palette->resultAt(0).value(QStringLiteral("id")).toString(), recentProfile);
}

void UnitTests::commandPaletteKeyboardWorksWithMainWindowHidden()
{
    ApplicationController controller;
    QVERIFY(controller.settings()->setVisualStyle(QStringLiteral("retro-arcade")));
    applyRetroTestFont();
    const QString initialProfile = controller.profiles()->activeProfileId();
    const QString alpha = controller.profiles()->createProfile(QStringLiteral("Palette Alpha"));
    const QString beta = controller.profiles()->createProfile(QStringLiteral("Palette Beta"));
    QVERIFY(!alpha.isEmpty());
    QVERIFY(!beta.isEmpty());

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
    auto *mainWindow = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(mainWindow);
    QVERIFY(!mainWindow->isVisible());
    QObject *popupObject = mainWindow->findChild<QObject *>(QStringLiteral("commandPalettePopup"));
    QVERIFY(popupObject);

    // The hotkey routes through this signal even while the main window remains hidden.
    QVERIFY(QMetaObject::invokeMethod(controller.hotkeys(), "commandPaletteTriggered"));
    QTRY_VERIFY(popupObject->property("visible").toBool());
    QVERIFY(!mainWindow->isVisible());
    auto *popup = qobject_cast<QQuickWindow *>(popupObject);
    QVERIFY(popup);
    auto *search = findItem(popup->contentItem(), QStringLiteral("commandPaletteSearch"));
    QVERIFY(search);
    QCOMPARE(search->property("font").value<QFont>().family(), QStringLiteral("Cascadia Code"));
    search->forceActiveFocus();
    QTRY_VERIFY(search->hasActiveFocus());
    for (const QChar character : QStringLiteral("Palette"))
        QTest::keyClick(popup, character.toUpper().unicode(),
                        character.isUpper() ? Qt::ShiftModifier : Qt::NoModifier);
    QTRY_VERIFY_WITH_TIMEOUT(!controller.commandPalette()->busy(), 5000);
    QCOMPARE(controller.profiles()->activeProfileId(), initialProfile);
    QVERIFY(controller.commandPalette()->rowCount() >= 2);
    QTest::keyClick(popup, Qt::Key_Down);
    QCOMPARE(controller.profiles()->activeProfileId(), initialProfile);
    QVERIFY(popup->grabWindow().save(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("command-palette-preview.png"))));
    QTest::keyClick(popup, Qt::Key_Return);
    QTRY_COMPARE(controller.profiles()->activeProfileId(), beta);
    QTRY_VERIFY(!popupObject->property("visible").toBool());
    QCOMPARE(warnings.count(), 0);

    controller.toggleCommandPalettePopup();
    QTRY_VERIFY(popupObject->property("visible").toBool());
    QVERIFY(QTest::qWaitForWindowExposed(popup));
    QTRY_VERIFY(search->hasActiveFocus());
    QTest::keyClick(popup, Qt::Key_Escape);
    QTRY_VERIFY(!popupObject->property("visible").toBool());
    QVERIFY(!mainWindow->isVisible());
    mainWindow->show();
    QVERIFY(QTest::qWaitForWindowExposed(mainWindow));
    QQuickItem *paletteButton = findItem(mainWindow->contentItem(), QStringLiteral("commandPaletteButton"));
    QVERIFY(paletteButton);
    QTest::mouseClick(mainWindow, Qt::LeftButton, Qt::NoModifier,
                      paletteButton->mapToScene(QPointF(paletteButton->width() / 2,
                                                        paletteButton->height() / 2)).toPoint());
    QTRY_VERIFY(popupObject->property("visible").toBool());
    QVERIFY(QTest::qWaitForWindowExposed(popup));
    QTRY_VERIFY(search->hasActiveFocus());
    QTest::keyClick(popup, Qt::Key_Escape);
    QTRY_VERIFY(!popupObject->property("visible").toBool());

    const QString imagePath = QDir(AppDataPaths::root()).filePath(QStringLiteral("palette-editor.png"));
    QImage image(32, 20, QImage::Format_ARGB32); image.fill(Qt::blue); QVERIFY(image.save(imagePath));
    QString imageId;
    QVERIFY(controller.screenshots()->database()->addScreenshot(imagePath, image.size(),
        QFileInfo(imagePath).size(), QStringLiteral("test"), &imageId));
    QVERIFY(controller.executeCommandPaletteResult(QStringLiteral("screenshot"), imageId));
    QObject *editor = mainWindow->findChild<QObject *>(QStringLiteral("screenshotEditor"));
    QVERIFY(editor);
    QTRY_VERIFY(editor->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(editor, "close"));
    QCOMPARE(warnings.count(), 0);
}

void UnitTests::commandPaletteReportsMissingItemsFailuresAndShortcutConflicts()
{
    ApplicationController controller;
    QVERIFY(!controller.setCommandPaletteShortcut(Qt::Key_F,
        int(Qt::ControlModifier | Qt::ShiftModifier)));
    QVERIFY(controller.statusIsError());
    QVERIFY(controller.settings()->setCommandPaletteShortcut(Qt::Key_P,
        int(Qt::ControlModifier | Qt::ShiftModifier)));

    const QString screenshotPath = QDir(AppDataPaths::root()).filePath(QStringLiteral("palette-preview.png"));
    QImage screenshot(28, 18, QImage::Format_ARGB32); screenshot.fill(Qt::magenta);
    QVERIFY(screenshot.save(screenshotPath));
    QString screenshotId;
    QVERIFY(controller.screenshots()->database()->addScreenshot(screenshotPath, screenshot.size(),
        QFileInfo(screenshotPath).size(), QStringLiteral("test"), &screenshotId));
    QSignalSpy screenshotRequested(&controller, &ApplicationController::commandPaletteScreenshotRequested);
    QVERIFY(controller.executeCommandPaletteResult(QStringLiteral("screenshot"), screenshotId));
    QCOMPARE(controller.currentPage(), QStringLiteral("Screenshots"));
    QCOMPARE(screenshotRequested.count(), 1);
    QCOMPARE(screenshotRequested.first().at(2).toString(), screenshotId);

    const QString path = QDir(AppDataPaths::root()).filePath(QStringLiteral("missing-palette-reference.txt"));
    QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.close();
    QVERIFY(controller.fileShelf()->addPaths({path}));
    const QString shelfId = controller.screenshots()->database()->fileShelfEntries().first().id;
    QVERIFY(QFile::remove(path));
    controller.commandPalette()->setQuery(QStringLiteral("missing-palette-reference"));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.commandPalette()->busy(), 5000);
    QVERIFY(resultExists(controller.commandPalette(), QStringLiteral("shelf"), shelfId));
    QVariantMap missing;
    for (int row = 0; row < controller.commandPalette()->rowCount(); ++row) {
        const QVariantMap item = controller.commandPalette()->resultAt(row);
        if (item.value(QStringLiteral("id")).toString() == shelfId) missing = item;
    }
    QVERIFY(missing.value(QStringLiteral("missing")).toBool());
    QVERIFY(!missing.value(QStringLiteral("available")).toBool());
    QVERIFY(!controller.executeCommandPaletteResult(QStringLiteral("shelf"), shelfId));
    QVERIFY(controller.commandPalette()->message().contains(QStringLiteral("missing"), Qt::CaseInsensitive));

    QVERIFY(!controller.executeCommandPaletteResult(QStringLiteral("layout"), QStringLiteral("stale-layout")));
    QVERIFY(controller.commandPalette()->message().contains(QStringLiteral("layout"), Qt::CaseInsensitive));

    // The palette uses the same async layout apply path, which reports unresolved matches.
    SettingsManager settings;
    ProfileManager profiles(&settings);
    const QString profileId = profiles.activeProfileId();
    const QJsonObject slot{{QStringLiteral("executable"), QStringLiteral("definitely-not-running.exe")},
                           {QStringLiteral("titleRule"), QString()}, {QStringLiteral("monitor"), QStringLiteral("missing-monitor")},
                           {QStringLiteral("x"), 0.1}, {QStringLiteral("y"), 0.1},
                           {QStringLiteral("width"), 0.5}, {QStringLiteral("height"), 0.5},
                           {QStringLiteral("maximized"), false}};
    const QJsonObject layout{{QStringLiteral("id"), QStringLiteral("unresolved-test-layout")},
                             {QStringLiteral("name"), QStringLiteral("Unresolved Test Layout")},
                             {QStringLiteral("profileId"), profileId},
                             {QStringLiteral("slots"), QJsonArray{slot}}};
    QFile layoutsFile(QDir(AppDataPaths::root()).filePath(QStringLiteral("window-layouts.json")));
    QVERIFY(layoutsFile.open(QIODevice::WriteOnly));
    layoutsFile.write(QJsonDocument(QJsonObject{{QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("layouts"), QJsonArray{layout}}}).toJson());
    layoutsFile.close();
    WindowArrangementManager arrangements(&profiles);
    QSignalSpy finished(&arrangements, &WindowArrangementManager::layoutApplyFinished);
    const quint64 requestId = arrangements.requestApplyLayout(QStringLiteral("unresolved-test-layout"));
    QVERIFY(requestId != 0);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
    QCOMPARE(finished.first().at(0).toULongLong(), requestId);
    QVERIFY(!finished.first().at(1).toBool());
    QVERIFY(finished.first().at(2).toString().contains(QStringLiteral("matched"), Qt::CaseInsensitive));
}
