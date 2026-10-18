#include "UnitTests.h"

void UnitTests::activeProfileNameBinding()
{
    SettingsManager settings;
    ProfileManager profiles(&settings);
    const QString work = profiles.createProfile(QStringLiteral("Work"));
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("profiles"), &profiles);
    QQmlComponent component(&engine);
    component.setData(QByteArrayLiteral("import QtQml\nQtObject { property string displayedName: profiles.activeProfileName }"), QUrl());
    std::unique_ptr<QObject> display(component.create());
    QVERIFY2(display, qPrintable(component.errorString()));
    QVERIFY(profiles.setActiveProfile(work));
    QCOMPARE(display->property("displayedName").toString(), QStringLiteral("Work"));
    QVERIFY(profiles.renameProfile(work, QStringLiteral("Development")));
    QCOMPARE(display->property("displayedName").toString(), QStringLiteral("Development"));
}

void UnitTests::hotkeyConflictAndScope()
{
    Profile active;
    active.id = QStringLiteral("active");
    HotkeyAssignment used;
    used.id = QStringLiteral("one");
    used.key = Qt::Key_K;
    used.modifiers = Qt::ControlModifier;
    active.hotkeys.append(used);
    QVERIFY(HotkeyUtils::assignmentConflict(active.hotkeys, Qt::Key_K, Qt::ControlModifier));
    QVERIFY(!HotkeyUtils::assignmentConflict(active.hotkeys, Qt::Key_L, Qt::ControlModifier));

    Profile other;
    other.id = QStringLiteral("other");
    other.activationKey = Qt::Key_F8;
    other.activationModifiers = Qt::AltModifier;
    QVector<Profile> profiles{active, other};
    QVERIFY(HotkeyUtils::profileActivationConflict(profiles, Qt::Key_F8, Qt::AltModifier));
    QVERIFY(!HotkeyUtils::profileActivationConflict(profiles, Qt::Key_F8, Qt::AltModifier,
                                                    QStringLiteral("other")));

    used.enabled = false;
    active.hotkeys[0] = used;
    QVERIFY(!HotkeyUtils::assignmentConflict(active.hotkeys, Qt::Key_K, Qt::ControlModifier));
    QVERIFY(HotkeyUtils::sameShortcut(Qt::Key_K, Qt::ControlModifier,
                                      Qt::Key_K, Qt::ControlModifier));
    other.hotkeys.append(HotkeyAssignment{QStringLiteral("inactive"), Qt::Key_L,
                                           Qt::ControlModifier});
    profiles[0] = active;
    profiles[1] = other;
    QVERIFY(!HotkeyUtils::anyProfileAssignmentConflict(profiles, Qt::Key_K, Qt::ControlModifier));
    QVERIFY(HotkeyUtils::anyProfileAssignmentConflict(profiles, Qt::Key_L, Qt::ControlModifier));
}

void UnitTests::qrGenerationUnicodeAndErrorCorrectionRoundTrips()
{
    const QString content = QStringLiteral("https://small.example/qr?city=İstanbul&lang=日本語 🚀");
    for (const QString &level : {QStringLiteral("L"), QStringLiteral("M"), QStringLiteral("Q"), QStringLiteral("H")}) {
        QString error;
        const QImage image = QrCodeManager::generateImage(content, 5, 4, level, &error);
        QVERIFY2(!image.isNull(), qPrintable(error));
        QCOMPARE(image.width() % 5, 0);
        QCOMPARE(image.height() % 5, 0);
        QCOMPARE(image.pixelColor(0, 0), QColor(Qt::white));
        const QStringList results = QrCodeManager::decodeImage(image, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(results.contains(content));
    }
}

void UnitTests::qrDecodeRotatedMultipleAndInvalidImages()
{
    QString error;
    const QImage first = QrCodeManager::generateImage(QStringLiteral("first QR"), 5, 4, QStringLiteral("M"), &error);
    QVERIFY2(!first.isNull(), qPrintable(error));
    const QImage second = QrCodeManager::generateImage(QStringLiteral("second QR"), 5, 4, QStringLiteral("Q"), &error);
    QVERIFY2(!second.isNull(), qPrintable(error));

    const QImage rotated = first.transformed(QTransform().rotate(90));
    const QStringList rotatedResults = QrCodeManager::decodeImage(rotated, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(rotatedResults.contains(QStringLiteral("first QR")));

    QImage combined(first.width() + second.width() + 40,
                    qMax(first.height(), second.height()), QImage::Format_RGB32);
    combined.fill(Qt::white);
    QPainter painter(&combined);
    painter.drawImage(0, 0, first);
    painter.drawImage(first.width() + 40, 0, second);
    painter.end();
    const QStringList multiple = QrCodeManager::decodeImage(combined, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(multiple.contains(QStringLiteral("first QR")));
    QVERIFY(multiple.contains(QStringLiteral("second QR")));

    QImage blank(64, 64, QImage::Format_RGB32);
    blank.fill(Qt::white);
    QVERIFY(QrCodeManager::decodeImage(blank, &error).isEmpty());
    QVERIFY(error.contains(QStringLiteral("No QR code")));
    QVERIFY(QrCodeManager::generateImage(QString(), 8, 4, QStringLiteral("M"), &error).isNull());
    QVERIFY(error.contains(QStringLiteral("Enter text")));
    QVERIFY(QrCodeManager::generateImage(QStringLiteral("ok"), 0, 4, QStringLiteral("M"), &error).isNull());
    QVERIFY(error.contains(QStringLiteral("module size")));
}

void UnitTests::qrFileDecodeClipboardOwnershipAndGalleryRegistration()
{
    QString error;
    const QImage generated = QrCodeManager::generateImage(QStringLiteral("https://small.example/local"), 4, 4,
                                                          QStringLiteral("M"), &error);
    QVERIFY2(!generated.isNull(), qPrintable(error));
    const QString path = QDir(AppDataPaths::root()).filePath(QStringLiteral("qr-decode-source.png"));
    QVERIFY(generated.save(path));
    const QStringList fileResults = QrCodeManager::decodeFileImage(path, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(fileResults, QStringList{QStringLiteral("https://small.example/local")});

    ApplicationController controller;
    QrCodeManager *qr = controller.qrCode();
    QSignalSpy external(controller.screenshots()->clipboardService(),
                        &ClipboardService::externalContentAvailable);
    QVERIFY(qr->generate(QStringLiteral("clipboard-owned QR"), 3, 4, QStringLiteral("L")));
    QTRY_VERIFY_WITH_TIMEOUT(!qr->busy(), 5000);
    QVERIFY(qr->copyGeneratedImage());
    QTest::qWait(120);
    QCOMPARE(external.count(), 0);
    QVERIFY(!QGuiApplication::clipboard()->image().isNull());

    controller.screenshots()->setDestinationOverride(
        QDir(AppDataPaths::root()).filePath(QStringLiteral("unavailable-qr-gallery")));
    QVERIFY(qr->saveGeneratedToGallery().isEmpty());
    QVERIFY(qr->statusIsError());
    QVERIFY(qr->status().contains(QStringLiteral("destination"), Qt::CaseInsensitive));
    controller.screenshots()->setDestinationOverride(AppDataPaths::root());
    QVERIFY(qr->saveGeneratedToGallery(QStringLiteral("missing-qr-tag")).isEmpty());
    QVERIFY(qr->statusIsError());
    QVERIFY(qr->status().contains(QStringLiteral("tag"), Qt::CaseInsensitive));
    const QString tagId = controller.screenshots()->createTag(QStringLiteral("QR export"));
    QVERIFY(!tagId.isEmpty());
    controller.screenshots()->setDestinationOverride(AppDataPaths::root());
    QSignalSpy saves(controller.screenshots(), &ScreenshotManager::saveFinished);
    const QString saved = qr->saveGeneratedToGallery(tagId);
    QVERIFY(!saved.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(saves.count(), 1, 5000);
    QVERIFY(saves.first().at(1).toBool());
    bool found = false;
    for (const ScreenshotRecord &record : controller.screenshots()->database()->screenshots()) {
        if (QFileInfo(record.path).absoluteFilePath() == QFileInfo(saved).absoluteFilePath()) {
            found = true;
            QCOMPARE(record.source, QStringLiteral("qr-generated"));
            QCOMPARE(record.tags, QStringList{QStringLiteral("QR export")});
        }
    }
    QVERIFY(found);
    QVERIFY(QFileInfo::exists(path));
}

void UnitTests::qrSequenceActionsPersistAndRunSafely()
{
    ApplicationController controller;
    ActionManager *actions = controller.actions();
    const QString generateSequence = actions->createSequence(QStringLiteral("Generate QR"));
    QVERIFY(!generateSequence.isEmpty());
    const QString generateStep = actions->addStep(generateSequence, QStringLiteral("generateQr"));
    QVERIFY(!generateStep.isEmpty());
    QVERIFY(actions->updateStep(generateSequence, generateStep,
                               {{QStringLiteral("qrText"), QStringLiteral("sequence Unicode ✓")},
                                {QStringLiteral("qrPixelsPerModule"), 4},
                                {QStringLiteral("qrMarginModules"), 5},
                                {QStringLiteral("qrErrorCorrection"), QStringLiteral("H")}}));
    const ActionSequence *stored = actions->sequenceById(generateSequence);
    QVERIFY(stored);
    const QJsonObject encoded = ActionManager::serializeSequence(*stored);
    ActionSequence restored;
    QVERIFY(ActionManager::parseSequence(encoded, &restored));
    QCOMPARE(restored.steps.first().qrText, QStringLiteral("sequence Unicode ✓"));
    QCOMPARE(restored.steps.first().qrErrorCorrection, QStringLiteral("H"));
    QCOMPARE(restored.steps.first().qrPixelsPerModule, 4);
    QCOMPARE(restored.steps.first().qrMarginModules, 5);
    QVERIFY(actions->validationError(generateSequence).isEmpty());
    QVERIFY(controller.actionRunner()->start(generateSequence));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.actionRunner()->running(), 5000);
    QVERIFY(!QGuiApplication::clipboard()->image().isNull());

    QString error;
    const QImage qrImage = QrCodeManager::generateImage(QStringLiteral("decode sequence result"), 4, 4,
                                                        QStringLiteral("M"), &error);
    QVERIFY2(!qrImage.isNull(), qPrintable(error));
    const QString imagePath = QDir(AppDataPaths::root()).filePath(QStringLiteral("qr-sequence-input.png"));
    QVERIFY(qrImage.save(imagePath));
    const QString decodeSequence = actions->createSequence(QStringLiteral("Decode QR"));
    QVERIFY(!decodeSequence.isEmpty());
    const QString decodeStep = actions->addStep(decodeSequence, QStringLiteral("decodeQrImage"));
    QVERIFY(!decodeStep.isEmpty());
    QVERIFY(actions->updateStep(decodeSequence, decodeStep, {{QStringLiteral("path"), imagePath}}));
    QVERIFY(actions->validationError(decodeSequence).isEmpty());
    QVERIFY(controller.actionRunner()->start(decodeSequence));
    QTRY_VERIFY_WITH_TIMEOUT(!controller.actionRunner()->running(), 5000);
    QVERIFY(controller.actionRunner()->status().contains(QStringLiteral("decode sequence result")));
    QCOMPARE(controller.qrCode()->results()->resultAt(0), QStringLiteral("decode sequence result"));
}

void UnitTests::globalHotkeysScopeAndNativeRegistration()
{
#ifdef Q_OS_WIN
    SettingsManager settings;
    const int testModifiers = Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier;
    QVERIFY(settings.setClipboardHistoryShortcut(Qt::Key_F19, testModifiers));
    QVERIFY(settings.setFileShelfShortcut(Qt::Key_F18, testModifiers));
    ProfileManager profiles(&settings);HotkeyManager manager(&profiles,&settings,nullptr);QWindow owner;manager.setOwnerWindow(&owner);
    const QString original=profiles.activeProfileId();const QString second=profiles.createProfile(QStringLiteral("Other"));
    HotkeyAssignment global;global.id=QStringLiteral("global-action");global.key=Qt::Key_F20;global.modifiers=Qt::ControlModifier|Qt::AltModifier|Qt::ShiftModifier;
    QVERIFY(profiles.addHotkey(QStringLiteral("global"),global));
    if (!manager.isRegistered(global.id))
        QSKIP("Windows denied native hotkey registration in this execution environment.");
    QVERIFY(manager.isRegistered(QStringLiteral("clipboard-history")));
    QCOMPARE(manager.clipboardHistoryStatus(), QStringLiteral("Registered globally"));
    const int historyNativeId=manager.nativeIdFor(QStringLiteral("clipboard-history"));
    QSignalSpy historyTriggered(&manager,&HotkeyManager::clipboardHistoryTriggered);
    QVERIFY(PostThreadMessageW(GetCurrentThreadId(),WM_HOTKEY,WPARAM(historyNativeId),0));QTRY_COMPARE(historyTriggered.count(),1);
    QVERIFY(manager.isRegistered(QStringLiteral("file-shelf")));
    QCOMPARE(manager.fileShelfStatus(), QStringLiteral("Registered globally"));
    const int shelfNativeId = manager.nativeIdFor(QStringLiteral("file-shelf"));
    QSignalSpy shelfTriggered(&manager, &HotkeyManager::fileShelfTriggered);
    QVERIFY(PostThreadMessageW(GetCurrentThreadId(), WM_HOTKEY, WPARAM(shelfNativeId), 0));
    QTRY_COMPARE(shelfTriggered.count(), 1);
    const int globalNativeId=manager.nativeIdFor(global.id);
    QVERIFY(profiles.setActivationShortcut(second,Qt::Key_F21,Qt::ControlModifier|Qt::AltModifier|Qt::ShiftModifier));const QString activation=QStringLiteral("activation:")+second;QVERIFY(manager.isRegistered(activation));const int activationNativeId=manager.nativeIdFor(activation);
    HotkeyAssignment active=global;active.id=QStringLiteral("profile-action");active.key=Qt::Key_F22;QVERIFY(profiles.addHotkey(original,active));QVERIFY(manager.isRegistered(active.id));
    QVERIFY(profiles.setActiveProfile(second));QVERIFY(!manager.isRegistered(active.id));QCOMPARE(manager.nativeIdFor(global.id),globalNativeId);QCOMPARE(manager.nativeIdFor(activation),activationNativeId);
    QVERIFY(profiles.setActiveProfile(original));QVERIFY(manager.isRegistered(active.id));
    QSignalSpy triggered(&manager,&HotkeyManager::actionTriggered);
    QVERIFY(PostThreadMessageW(GetCurrentThreadId(),WM_HOTKEY,WPARAM(globalNativeId),0));QTRY_COMPARE(triggered.count(),1);
    manager.setRecording(true);QVERIFY(!manager.isRegistered(global.id));manager.setRecording(false);QTRY_VERIFY(manager.isRegistered(global.id));
    constexpr int competingId=0xbffe;QVERIFY(RegisterHotKey(nullptr,competingId,MOD_CONTROL|MOD_ALT|MOD_SHIFT|MOD_NOREPEAT,VK_F23));
    const auto release=qScopeGuard([]{UnregisterHotKey(nullptr,competingId);});
    active.id=QStringLiteral("conflicting");active.key=Qt::Key_F23;QVERIFY(profiles.addHotkey(original,active));QVERIFY(!manager.isRegistered(active.id));
#else
    QSKIP("Windows native hotkey registration requires Windows.");
#endif
}

void UnitTests::globalShortcutConflictValidation()
{
    ApplicationController controller;
    const int modifiers=Qt::ControlModifier|Qt::AltModifier|Qt::ShiftModifier;
    controller.setHotkeyScope(true);QVERIFY(controller.addHotkey(Qt::Key_F20,modifiers,QStringLiteral("saveScreenshot"),QString(),QString()));
    controller.setHotkeyScope(false);QVERIFY(!controller.addHotkey(Qt::Key_F20,modifiers,QStringLiteral("saveScreenshot"),QString(),QString()));
    QVERIFY(!controller.setProfileActivationShortcut(controller.profiles()->activeProfileId(),Qt::Key_F20,modifiers));
}

void UnitTests::recorderFocusAndModifiedControlKeys()
{
    ApplicationController controller;QQmlEngine engine;engine.rootContext()->setContextProperty(QStringLiteral("app"),&controller);engine.rootContext()->setContextProperty(QStringLiteral("settings"),controller.settings());
    QQuickWindow window;window.resize(400,200);QQmlComponent component(&engine,QUrl::fromLocalFile(QStringLiteral(TEST_QML_DIR "/components/HotkeyRecorder.qml")));QTRY_VERIFY(component.status()!=QQmlComponent::Loading);
    std::unique_ptr<QObject> object(component.create());QVERIFY2(object,qPrintable(component.errorString()));auto *recorder=qobject_cast<QQuickItem *>(object.get());recorder->setParentItem(window.contentItem());window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
    recorder->forceActiveFocus();recorder->setProperty("recording",true);QSignalSpy recorded(recorder,SIGNAL(recorded(int,int)));
    QTest::keyClick(&window,Qt::Key_Delete,Qt::ControlModifier);QCOMPARE(recorded.count(),1);QCOMPARE(recorded.first().at(0).toInt(),int(Qt::Key_Delete));QCOMPARE(recorded.first().at(1).toInt(),int(Qt::ControlModifier));
    recorder->setProperty("recording",true);recorder->setVisible(false);QVERIFY(!recorder->property("recording").toBool());
    recorder->setVisible(true);recorder->forceActiveFocus();recorder->setProperty("recording",true);QQuickItem other(window.contentItem());other.setParentItem(window.contentItem());other.forceActiveFocus();QVERIFY(!recorder->property("recording").toBool());
}

void UnitTests::initialWindowIsCenteredAndReopeningPreservesPosition()
{
    ApplicationController controller;
    QQuickWindow window;
    window.resize(640, 480);
    window.setPosition(17, 29);
    controller.initializeWindow(&window);
    QScreen *expectedScreen = QGuiApplication::screenAt(QCursor::pos());
    if (!expectedScreen)
        expectedScreen = QGuiApplication::primaryScreen();
    QVERIFY(expectedScreen);
    controller.showAtStartup();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_COMPARE(window.screen(), expectedScreen);
    // Mixed-DPI monitors round logical coordinates to physical pixels.
    QTRY_VERIFY((window.frameGeometry().center() - expectedScreen->availableGeometry().center()).manhattanLength() <= 2);
    const QPoint movedPosition = window.position() + QPoint(23, 31);
    window.setPosition(movedPosition);
    window.hide();
    controller.showWindow();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QCOMPARE(window.position(), movedPosition);
}

void UnitTests::fullShellPagesAndPreferences()
{
    ApplicationController controller;QQmlApplicationEngine engine;engine.addImageProvider(QStringLiteral("thumbnail"),new ThumbnailProvider);engine.addImageProvider(QStringLiteral("qrpreview"),new QrImageProvider(controller.qrCode()));
    QTemporaryDir droppedAssets(g_testData->filePath(QStringLiteral("qml-drop-assets-XXXXXX")));
    QVERIFY(droppedAssets.isValid());
    const QString shelfDropPath = QDir(droppedAssets.path()).filePath(QStringLiteral("café space % dropped.txt"));
    QFile shelfDropFile(shelfDropPath);
    QVERIFY(shelfDropFile.open(QIODevice::WriteOnly));
    shelfDropFile.close();
    const QString qrDropPath = QDir(droppedAssets.path()).filePath(QStringLiteral("dropped-qr.png"));
    const QImage qrDropImage = QrCodeManager::generateImage(QStringLiteral("https://example.test/QR"), 5, 4,
                                                             QStringLiteral("M"));
    QVERIFY(!qrDropImage.isNull());
    QVERIFY(qrDropImage.save(qrDropPath));
    QVERIFY(!controller.screenshots()->createTag(QStringLiteral("QML tag check")).isEmpty());
    for(const auto &entry:QList<QPair<QString,QObject *>>{{QStringLiteral("app"),&controller},{QStringLiteral("settings"),controller.settings()},{QStringLiteral("profiles"),controller.profiles()},{QStringLiteral("screenshots"),controller.screenshots()},{QStringLiteral("clipboardHistory"),controller.clipboardHistory()},{QStringLiteral("fileShelf"),controller.fileShelf()},{QStringLiteral("qrCode"),controller.qrCode()},{QStringLiteral("hotkeys"),controller.hotkeys()},{QStringLiteral("actions"),controller.actions()},{QStringLiteral("actionRunner"),controller.actionRunner()},{QStringLiteral("windowArrangements"),controller.windowArrangements()}})engine.rootContext()->setContextProperty(entry.first,entry.second);
    QSignalSpy warnings(&engine,&QQmlEngine::warnings);engine.loadFromModule(QStringLiteral("ScreenshotHotkeyManagerTest"),QStringLiteral("Main"));QVERIFY(!engine.rootObjects().isEmpty());
    auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first());QVERIFY(window);window->show();QVERIFY(QTest::qWaitForWindowExposed(window));
    for(const QString &page:{QStringLiteral("Dashboard"),QStringLiteral("Screenshots"),QStringLiteral("Clipboard History"),QStringLiteral("File Shelf"),QStringLiteral("QR Codes"),QStringLiteral("Window Arrangements"),QStringLiteral("Hotkeys"),QStringLiteral("Profiles"),QStringLiteral("Actions"),QStringLiteral("Settings")}){
        controller.setCurrentPage(page);
        QTest::qWait(80);
        if (page == QStringLiteral("QR Codes")) {
            QVERIFY(findItem(window->contentItem(), QStringLiteral("generateQrButton")));
            QVERIFY(findItem(window->contentItem(), QStringLiteral("qrResultsList")));
            QQuickItem *qrPage = findItem(window->contentItem(), QStringLiteral("qrCodePage"));
            QVERIFY(qrPage);
            QVariant droppedResult;
            const QVariantList droppedUrls{QVariant::fromValue(QUrl::fromLocalFile(qrDropPath))};
            QVERIFY(QMetaObject::invokeMethod(qrPage, "handleDroppedUrls", Q_RETURN_ARG(QVariant, droppedResult),
                                              Q_ARG(QVariant, QVariant::fromValue(droppedUrls))));
            QVERIFY(droppedResult.toBool());
            QTRY_COMPARE_WITH_TIMEOUT(controller.qrCode()->results()->rowCount(), 1, 5000);
            QVERIFY(controller.qrCode()->results()->resultAt(0).contains(QStringLiteral("https://example.test/QR")));
            QVERIFY(window->grabWindow().save(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("qr-code-page.png"))));
        }
        if (page == QStringLiteral("File Shelf")) {
            QQuickItem *shelfPage = findItem(window->contentItem(), QStringLiteral("fileShelfPage"));
            QVERIFY(shelfPage);
            QVariant droppedResult;
            const QVariantList droppedUrls{QVariant::fromValue(QUrl::fromLocalFile(shelfDropPath))};
            QVERIFY(QMetaObject::invokeMethod(shelfPage, "handleDroppedUrls", Q_RETURN_ARG(QVariant, droppedResult),
                                              Q_ARG(QVariant, QVariant::fromValue(droppedUrls))));
            QVERIFY(droppedResult.toBool());
            QCOMPARE(controller.fileShelf()->rowCount(), 1);
            QCOMPARE(controller.fileShelf()->data(controller.fileShelf()->index(0), FileShelfManager::PathRole).toString(),
                     QFileInfo(shelfDropPath).absoluteFilePath());
            const QVariantList remoteUrls{QVariant::fromValue(QUrl(QStringLiteral("https://example.test/not-local.png")))};
            QVERIFY(QMetaObject::invokeMethod(shelfPage, "handleDroppedUrls", Q_RETURN_ARG(QVariant, droppedResult),
                                              Q_ARG(QVariant, QVariant::fromValue(remoteUrls))));
            QVERIFY(!droppedResult.toBool());
            QCOMPARE(controller.fileShelf()->rowCount(), 1);
        }
        if(page==QStringLiteral("Window Arrangements")){
            QVERIFY(findItem(window->contentItem(),QStringLiteral("windowArrangementWindowList")));
            QVERIFY(findItem(window->contentItem(),QStringLiteral("windowArrangementPreview")));
            QVERIFY(window->grabWindow().save(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("window-arrangements-page.png"))));
        }
    }
    window->hide();
    controller.setClipboardHistoryPopupVisible(true);
    QObject *historyPopup = window->findChild<QObject *>(QStringLiteral("clipboardHistoryPopup"));
    QVERIFY(historyPopup);
    QTRY_VERIFY(historyPopup->property("visible").toBool());
    auto *popupWindow = qobject_cast<QQuickWindow *>(historyPopup);
    QVERIFY(popupWindow);
    auto *popupSearch = findItem(popupWindow->contentItem(), QStringLiteral("clipboardHistoryPopupSearch"));
    auto *popupList = findItem(popupWindow->contentItem(), QStringLiteral("clipboardHistoryPopupList"));
    QVERIFY(popupSearch);
    QVERIFY(popupList);
    QVERIFY(popupSearch->width() > 400);
    QVERIFY(popupList->height() > 300);
    QVERIFY(!window->isVisible());
    popupWindow->grabWindow().save(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("clipboard-history-popup.png")));
    controller.setClipboardHistoryPopupVisible(false);
    QTRY_VERIFY(!historyPopup->property("visible").toBool());
    controller.setFileShelfPopupVisible(true);
    QObject *shelfPopup = window->findChild<QObject *>(QStringLiteral("fileShelfPopup"));
    QVERIFY(shelfPopup);
    QTRY_VERIFY(shelfPopup->property("visible").toBool());
    auto *shelfPopupWindow = qobject_cast<QQuickWindow *>(shelfPopup);
    QVERIFY(shelfPopupWindow);
    QVERIFY(shelfPopupWindow->width() >= 540);
    QVERIFY(!window->isVisible());
    shelfPopupWindow->grabWindow().save(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("file-shelf-popup.png")));
    controller.setFileShelfPopupVisible(false);
    QTRY_VERIFY(!shelfPopup->property("visible").toBool());
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QVERIFY(controller.settings()->setAppearance(QStringLiteral("light")));QTest::qWait(80);window->grabWindow().save(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("settings-light.png")));
    QVERIFY(controller.settings()->setAppearance(QStringLiteral("dark")));QTest::qWait(80);window->grabWindow().save(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("settings-dark.png")));
    const QString settingsFile=QDir(AppDataPaths::root()).filePath(QStringLiteral("settings.json"));
    QVERIFY(QFile::rename(settingsFile,settingsFile+QStringLiteral(".original")));QVERIFY(QDir().mkpath(settingsFile));
    QQuickItem *closeSwitch=findItem(window->contentItem(),QStringLiteral("Close to tray"));QVERIFY(closeSwitch);
    const bool before=controller.settings()->closeToTray();
    QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,closeSwitch->mapToScene(QPointF(closeSwitch->width()/2,closeSwitch->height()/2)).toPoint());
    QCOMPARE(controller.settings()->closeToTray(),before);QTRY_COMPARE(closeSwitch->property("checked").toBool(),before);QVERIFY(controller.statusIsError());
    QCOMPARE(warnings.count(),0);
}

void UnitTests::hotkeyEditorRestoresRejectedEdits()
{
    ApplicationController controller;
    QVERIFY(controller.addHotkey(Qt::Key_F19,Qt::ControlModifier,QStringLiteral("saveScreenshot"),QString(),QString()));
    controller.setCurrentPage(QStringLiteral("Hotkeys"));
    QQmlApplicationEngine engine;engine.addImageProvider(QStringLiteral("thumbnail"),new ThumbnailProvider);engine.addImageProvider(QStringLiteral("qrpreview"),new QrImageProvider(controller.qrCode()));
    for(const auto &entry:QList<QPair<QString,QObject *>>{{QStringLiteral("app"),&controller},{QStringLiteral("settings"),controller.settings()},{QStringLiteral("profiles"),controller.profiles()},{QStringLiteral("screenshots"),controller.screenshots()},{QStringLiteral("clipboardHistory"),controller.clipboardHistory()},{QStringLiteral("fileShelf"),controller.fileShelf()},{QStringLiteral("qrCode"),controller.qrCode()},{QStringLiteral("hotkeys"),controller.hotkeys()},{QStringLiteral("actions"),controller.actions()},{QStringLiteral("actionRunner"),controller.actionRunner()},{QStringLiteral("windowArrangements"),controller.windowArrangements()}})engine.rootContext()->setContextProperty(entry.first,entry.second);
    QSignalSpy warnings(&engine,&QQmlEngine::warnings);engine.loadFromModule(QStringLiteral("ScreenshotHotkeyManagerTest"),QStringLiteral("Main"));QVERIFY(!engine.rootObjects().isEmpty());
    auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first());QVERIFY(window);window->show();QVERIFY(QTest::qWaitForWindowExposed(window));QTest::qWait(80);
    auto *picker=findItem(window->contentItem(),QStringLiteral("hotkeyActionTypePicker"));QVERIFY(picker);
    picker->setProperty("currentIndex",2);QVERIFY(QMetaObject::invokeMethod(picker,"activated",Q_ARG(int,2)));
    QCOMPARE(picker->property("currentIndex").toInt(),0);QVERIFY(controller.statusIsError());
    const QString path=QDir(AppDataPaths::root()).filePath(QStringLiteral("profiles/%1.json").arg(controller.profiles()->activeProfileId()));
    QVERIFY(QFile::rename(path,path+QStringLiteral(".original")));QVERIFY(QDir().mkpath(path));
    auto *toggle=findItem(window->contentItem(),QStringLiteral("hotkeyEnabledSwitch"));QVERIFY(toggle);QTest::qWait(50);
    QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,toggle->mapToScene(QPointF(toggle->width()/2,toggle->height()/2)).toPoint());
    QTRY_VERIFY(toggle->property("checked").toBool());QVERIFY(controller.profiles()->activeProfile()->hotkeys.first().enabled);
    QVERIFY(QDir().rmdir(path));QVERIFY(QFile::rename(path+QStringLiteral(".original"),path));
    QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,toggle->mapToScene(QPointF(toggle->width()/2,toggle->height()/2)).toPoint());
    QTRY_VERIFY(!controller.profiles()->activeProfile()->hotkeys.first().enabled);
    QTRY_VERIFY(findItem(window->contentItem(),QStringLiteral("hotkeyEnabledSwitch")) && !findItem(window->contentItem(),QStringLiteral("hotkeyEnabledSwitch"))->property("checked").toBool());
    QCOMPARE(warnings.count(),0);
}
