#include "UnitTests.h"

void UnitTests::windowArrangementGeometryUsesMonitorWorkAreas()
{
    const QRect workArea(-1920, 80, 1920, 1000);
    QCOMPARE(WindowArrangementGeometry::denormalize(WindowArrangementGeometry::placement(QStringLiteral("leftHalf")), workArea), QRect(-1920,80,960,1000));
    QCOMPARE(WindowArrangementGeometry::denormalize(WindowArrangementGeometry::placement(QStringLiteral("bottomRight")), workArea), QRect(-960,580,960,500));
    QCOMPARE(WindowArrangementGeometry::denormalize(WindowArrangementGeometry::placement(QStringLiteral("center")), workArea), QRect(-1536,230,1152,700));
    const QRect original(-1440, 180, 900, 720);
    const QRect restored=WindowArrangementGeometry::denormalize(WindowArrangementGeometry::normalize(original,workArea),workArea);
    QCOMPARE(restored,original);
    const QRect changedDpiArea(1920,0,2560,1440);
    const QRect moved=WindowArrangementGeometry::denormalize(WindowArrangementGeometry::normalize(original,workArea),changedDpiArea);
    QVERIFY(moved.x()>=changedDpiArea.x());
    QVERIFY(moved.y()>=changedDpiArea.y());
    QVERIFY(changedDpiArea.contains(moved));
    // A partly or wholly off-screen saved window must remain usable, with
    // its size preserved rather than being clipped to an empty rectangle.
    QCOMPARE(WindowArrangementGeometry::denormalize({-4,-4,0.5,0.5},workArea),QRect(-1920,80,960,500));
    QCOMPARE(WindowArrangementGeometry::denormalize({4,4,0.5,0.5},workArea),QRect(-960,580,960,500));
    QCOMPARE(WindowArrangementGeometry::denormalize({0,0,4,4},workArea),workArea);
}

void UnitTests::windowArrangementMatchingReportsAmbiguityAndFiltersTitles()
{
    const QStringList executables{QStringLiteral("C:/Apps/Editor.exe"),QStringLiteral("c:\\apps\\EDITOR.EXE"),QStringLiteral("C:/Apps/Other.exe")};
    const QStringList titles{QStringLiteral("Document A - Editor"),QStringLiteral("Settings - Editor"),QStringLiteral("Document B - Other")};
    QCOMPARE(WindowArrangementMatching::matchingRows(QStringLiteral("c:\\apps\\editor.exe"),{},executables,titles),QVector<int>({0,1}));
    QCOMPARE(WindowArrangementMatching::matchingRows(QStringLiteral("C:/Apps/Editor.exe"),QStringLiteral("Settings"),executables,titles),QVector<int>({1}));
    QVERIFY(WindowArrangementMatching::matchingRows(QStringLiteral("missing.exe"),{},executables,titles).isEmpty());
}

void UnitTests::windowArrangementLayoutStorageSurvivesRestart()
{
    SettingsManager settings;ProfileManager profiles(&settings);
    WindowLayout layout;layout.id=QStringLiteral("test-layout");layout.name=QStringLiteral("Coding");layout.profileId=profiles.activeProfileId();
    layout.rules.append({QStringLiteral("C:/Apps/Editor.exe"),QStringLiteral("Project"),QStringLiteral("\\\\.\\DISPLAY2"),{-1.0,0.0,0.5,1.0},false});
    WindowLayout parsed;QVERIFY(WindowArrangementManager::parseLayout(WindowArrangementManager::serializeLayout(layout),&parsed));
    QCOMPARE(parsed.id,layout.id);QCOMPARE(parsed.profileId,layout.profileId);QCOMPARE(parsed.rules.first().titleRule,QStringLiteral("Project"));
    QDir().mkpath(AppDataPaths::root());QFile file(QDir(AppDataPaths::root()).filePath(QStringLiteral("window-layouts.json")));QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(QJsonObject{{QStringLiteral("schemaVersion"),1},{QStringLiteral("layouts"),QJsonArray{WindowArrangementManager::serializeLayout(layout)}}}).toJson());file.close();
    {WindowArrangementManager manager(&profiles);QCOMPARE(manager.layouts().size(),1);QCOMPARE(manager.layoutInfo(layout.id).value(QStringLiteral("count")).toInt(),1);QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(),5000);}
    {WindowArrangementManager restarted(&profiles);QCOMPARE(restarted.layoutOptions().size(),1);QCOMPARE(restarted.layoutInfo(layout.id).value(QStringLiteral("name")).toString(),layout.name);QTRY_VERIFY_WITH_TIMEOUT(!restarted.busy(),5000);}
}

void UnitTests::windowArrangementRecoversMinimizedLayout_data()
{
    QTest::addColumn<bool>("otherInvalidData");
    QTest::newRow("recover-minimized-position")<<false;
    QTest::newRow("preserve-unrelated-invalid-data")<<true;
}

void UnitTests::windowArrangementRecoversMinimizedLayout()
{
    QFETCH(bool,otherInvalidData);
    SettingsManager settings;ProfileManager profiles(&settings);
    WindowLayout layout;layout.id=QStringLiteral("existing-layout");layout.name=QStringLiteral("Testing");layout.profileId=profiles.activeProfileId();
    layout.rules.append({QStringLiteral("C:/Apps/Chrome.exe"),QString(),QStringLiteral("\\\\.\\DISPLAY5"),
                         {-17.094017094017094,-29.62962962962963,0.08547008547008547,0.025925925925925925},false});
    auto object=WindowArrangementManager::serializeLayout(layout);
    if(otherInvalidData)object.insert(QStringLiteral("name"),42);
    const QJsonObject names{{QStringLiteral("monitor-device-path"),QStringLiteral("BigMonitor")}};
    const QByteArray original=QJsonDocument(QJsonObject{{QStringLiteral("schemaVersion"),1},
        {QStringLiteral("layouts"),QJsonArray{object}},{QStringLiteral("monitorNicknames"),names}}).toJson();
    QVERIFY(QDir().mkpath(AppDataPaths::root()));
    const QString path=QDir(AppDataPaths::root()).filePath(QStringLiteral("window-layouts.json"));
    QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(original),qint64(original.size()));file.close();
    {
        WindowArrangementManager manager(&profiles);QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(),5000);
        QCOMPARE(manager.containsLayout(layout.id),!otherInvalidData);
        if(!otherInvalidData){
            QCOMPARE(manager.layouts().size(),1);
            QVERIFY(manager.status().contains(QStringLiteral("Recovered saved layouts")));
            QCOMPARE(manager.layoutSlots(layout.id).first().toMap().value(QStringLiteral("width")).toDouble(),0.6);
            QVERIFY(manager.renameLayout(layout.id,QStringLiteral("Recovered testing")));
        }else if(!manager.monitors().isEmpty()){
            QVERIFY(!manager.setMonitorNickname(manager.monitors().first().toMap().value(QStringLiteral("device")).toString(),QStringLiteral("Do not write")));
        }
    }
    const QStringList backups=QDir(AppDataPaths::root()).entryList({QStringLiteral("window-layouts.json.before-minimized-recovery-*.bak")},QDir::Files);
    QCOMPARE(backups.size(),otherInvalidData?0:1);
    QVERIFY(file.open(QIODevice::ReadOnly));const QByteArray current=file.readAll();file.close();
    if(otherInvalidData){QCOMPARE(current,original);return;}
    QFile backup(QDir(AppDataPaths::root()).filePath(backups.first()));QVERIFY(backup.open(QIODevice::ReadOnly));QCOMPARE(backup.readAll(),original);
    const auto root=QJsonDocument::fromJson(current).object();
    QCOMPARE(root.value(QStringLiteral("monitorNicknames")).toObject(),names);
    WindowLayout restored;QVERIFY(WindowArrangementManager::parseLayout(root.value(QStringLiteral("layouts")).toArray().first().toObject(),&restored));
    QCOMPARE(restored.id,layout.id);QCOMPARE(restored.profileId,layout.profileId);QCOMPARE(restored.rules.first().executable,layout.rules.first().executable);
    {
        WindowArrangementManager restarted(&profiles);QTRY_VERIFY_WITH_TIMEOUT(!restarted.busy(),5000);
        QCOMPARE(restarted.layoutInfo(layout.id).value(QStringLiteral("name")).toString(),QStringLiteral("Recovered testing"));
        QVERIFY(restarted.renameLayout(layout.id,QStringLiteral("Still writable")));
    }
    QCOMPARE(QDir(AppDataPaths::root()).entryList({QStringLiteral("*.bak")},QDir::Files).size(),1);
}

void UnitTests::windowLayoutHotkeyAndSequenceReferencesPersist()
{
    SettingsManager settings;ProfileManager profiles(&settings);const QString profileId=profiles.activeProfileId();
    const QString layoutId=QStringLiteral("layout-reference");
    WindowLayout layout; layout.id=layoutId; layout.name=QStringLiteral("Referenced layout"); layout.profileId=profileId;
    layout.rules.append({QStringLiteral("C:/Apps/Editor.exe"),QString(),QStringLiteral("display"),{0,0,1,1},false});
    QFile layoutFile(QDir(AppDataPaths::root()).filePath(QStringLiteral("window-layouts.json")));
    QVERIFY(layoutFile.open(QIODevice::WriteOnly));
    layoutFile.write(QJsonDocument(QJsonObject{{QStringLiteral("schemaVersion"),1},{QStringLiteral("layouts"),QJsonArray{WindowArrangementManager::serializeLayout(layout)}}}).toJson()); layoutFile.close();
    WindowArrangementManager layouts(&profiles);
    QTRY_VERIFY_WITH_TIMEOUT(!layouts.busy(),5000);
    QVERIFY2(layouts.containsLayout(layoutId), qPrintable(layouts.status()));
    HotkeyAssignment binding;binding.id=QStringLiteral("layout-hotkey");binding.key=Qt::Key_F8;binding.modifiers=Qt::ControlModifier;binding.actionType=QStringLiteral("applyWindowLayout");binding.layoutId=layoutId;
    QVERIFY(profiles.addHotkey(profileId,binding));
    ActionManager actions(&profiles);const QString sequence=actions.createSequence(QStringLiteral("Arrange on request"));
    actions.setWindowArrangementManager(&layouts);
    const QString step=actions.addStep(sequence,QStringLiteral("applyWindowLayout"));QVERIFY(!step.isEmpty());
    QVERIFY(actions.updateStep(sequence,step,{{QStringLiteral("layoutId"),layoutId}}));
    QVERIFY2(actions.validationError(sequence).isEmpty(),qPrintable(actions.validationError(sequence)));
    bool valid=false;const Profile roundTrip=ProfileManager::parseProfile(ProfileManager::serializeProfile(*profiles.profileById(profileId)),&valid);QVERIFY(valid);
    QCOMPARE(roundTrip.hotkeys.first().layoutId,layoutId);
    QCOMPARE(actions.steps(sequence).first().toMap().value(QStringLiteral("layoutId")).toString(),layoutId);
    QVERIFY(!layouts.deleteLayout(layoutId));
    QVERIFY(profiles.removeHotkey(profileId,binding.id));
    QVERIFY(!layouts.deleteLayout(layoutId));
    QVERIFY(actions.updateStep(sequence,step,{{QStringLiteral("layoutId"),QStringLiteral("missing-layout")}}));
    QVERIFY(!actions.validationError(sequence).isEmpty());
    QVERIFY(layouts.deleteLayout(layoutId));
}

#ifdef Q_OS_WIN

void UnitTests::windowLayoutSavesRestoredGeometryForMinimizedWindow()
{
    QProcess helperProcess;
    const auto closeHelper=qScopeGuard([&]{helperProcess.kill();helperProcess.waitForFinished(3000);});
    helperProcess.start(QString::fromUtf8(SMALLDTL_TEST_WINDOW_HELPER));
    QVERIFY(helperProcess.waitForStarted());
    HWND helper=nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((helper=FindWindowW(nullptr,L"SmallDTL Test Arrangement Window"))!=nullptr,5000);
    SettingsManager settings;ProfileManager profiles(&settings);
    QString minimizedId,maximizedId;
    QRect expectedApplied;
    {
        WindowArrangementManager manager(&profiles);QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(),5000);
        const QString windowId=QString::number(reinterpret_cast<quintptr>(helper));
        manager.setWindowSelected(windowId,true);
        // Change bounds after enumeration to verify save reads live geometry.
        QVERIFY(SetWindowPos(helper,nullptr,100,120,640,480,SWP_NOZORDER|SWP_NOACTIVATE));
        WINDOWPLACEMENT placement{};placement.length=sizeof(placement);QVERIFY(GetWindowPlacement(helper,&placement));
        MONITORINFO info{};info.cbSize=sizeof(info);QVERIFY(GetMonitorInfoW(MonitorFromWindow(helper,MONITOR_DEFAULTTONEAREST),&info));
        const QRect workArea(info.rcWork.left,info.rcWork.top,info.rcWork.right-info.rcWork.left,info.rcWork.bottom-info.rcWork.top);
        RECT restoredRect=placement.rcNormalPosition;
        OffsetRect(&restoredRect,info.rcWork.left-info.rcMonitor.left,info.rcWork.top-info.rcMonitor.top);
        const QRect expected(restoredRect.left,restoredRect.top,restoredRect.right-restoredRect.left,restoredRect.bottom-restoredRect.top);
        expectedApplied=WindowArrangementGeometry::denormalize(WindowArrangementGeometry::normalize(expected,workArea),workArea);
        QVERIFY(ShowWindowAsync(helper,SW_MINIMIZE));QTRY_VERIFY_WITH_TIMEOUT(IsIconic(helper),5000);
        minimizedId=manager.saveSelectedLayout(QStringLiteral("Minimized"));QVERIFY2(!minimizedId.isEmpty(),qPrintable(manager.status()));
        QVERIFY(IsIconic(helper));
        const auto saved=manager.layoutSlots(minimizedId).first().toMap();
        const WindowArrangementGeometry::NormalizedRect rect{saved.value(QStringLiteral("x")).toDouble(),saved.value(QStringLiteral("y")).toDouble(),saved.value(QStringLiteral("width")).toDouble(),saved.value(QStringLiteral("height")).toDouble()};
        QCOMPARE(WindowArrangementGeometry::denormalize(rect,workArea),WindowArrangementGeometry::denormalize(WindowArrangementGeometry::normalize(expected,workArea),workArea));
        QVERIFY(!saved.value(QStringLiteral("maximized")).toBool());
        manager.refreshWindows();QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(),5000);
        QVERIFY(!manager.saveSelectedLayout(QStringLiteral("Enumerated while minimized")).isEmpty());
        QVERIFY(ShowWindowAsync(helper,SW_MAXIMIZE));QTRY_VERIFY_WITH_TIMEOUT(IsZoomed(helper),5000);
        QVERIFY(ShowWindowAsync(helper,SW_MINIMIZE));QTRY_VERIFY_WITH_TIMEOUT(IsIconic(helper),5000);
        maximizedId=manager.saveSelectedLayout(QStringLiteral("Minimized from maximized"));QVERIFY(!maximizedId.isEmpty());
        QVERIFY(manager.layoutSlots(maximizedId).first().toMap().value(QStringLiteral("maximized")).toBool());
    }
    WindowArrangementManager restarted(&profiles);QTRY_VERIFY_WITH_TIMEOUT(!restarted.busy(),5000);
    QCOMPARE(restarted.layouts().size(),3);QVERIFY(restarted.containsLayout(minimizedId));QVERIFY(restarted.containsLayout(maximizedId));
    QVERIFY(restarted.renameLayout(minimizedId,QStringLiteral("Survives restart")));
    QSignalSpy applied(&restarted,&WindowArrangementManager::layoutApplyFinished);
    QVERIFY(restarted.applyLayout(minimizedId));QTRY_COMPARE_WITH_TIMEOUT(applied.count(),1,5000);
    QVERIFY(applied.first().at(1).toBool());QTRY_VERIFY_WITH_TIMEOUT(!IsIconic(helper)&&!IsZoomed(helper),5000);
    QTRY_VERIFY_WITH_TIMEOUT([&]{
        RECT actual{};if(!GetWindowRect(helper,&actual))return false;
        return QRect(actual.left,actual.top,actual.right-actual.left,actual.bottom-actual.top)==expectedApplied;
    }(),5000);
}

void UnitTests::windowLayoutSequenceRefreshesNewlyLaunchedWindows()
{
    const QString helperPath = QString::fromUtf8(SMALLDTL_TEST_WINDOW_HELPER);
    QVERIFY2(QFileInfo(helperPath).isExecutable(), qPrintable(helperPath));
    const auto closeHelper = qScopeGuard([] {
        if (HWND helper = FindWindowW(nullptr, L"SmallDTL Test Arrangement Window"))
            PostMessageW(helper, WM_CLOSE, 0, 0);
    });
    QString profileId;
    {
        SettingsManager settings;
        ProfileManager profiles(&settings);
        profileId = profiles.activeProfileId();
    }
    const QString layoutId = QStringLiteral("review-fresh-layout");
    WindowLayout layout;
    layout.id = layoutId;
    layout.name = QStringLiteral("Fresh window test");
    layout.profileId = profileId;
    layout.rules.append({helperPath, QStringLiteral("SmallDTL Test Arrangement Window"),
                         QStringLiteral("\\\\.\\DISPLAY-MISSING"),
                         {0.22, 0.18, 0.36, 0.42}, false});
    const QString layoutPath = QDir(AppDataPaths::root()).filePath(QStringLiteral("window-layouts.json"));
    QVERIFY(QDir().mkpath(AppDataPaths::root()));
    QFile layoutFile(layoutPath);
    QVERIFY(layoutFile.open(QIODevice::WriteOnly));
    const QByteArray layoutBytes = QJsonDocument(QJsonObject{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("layouts"), QJsonArray{WindowArrangementManager::serializeLayout(layout)}}
    }).toJson();
    QCOMPARE(layoutFile.write(layoutBytes), qint64(layoutBytes.size()));
    layoutFile.close();

    ApplicationController controller;
    WindowArrangementManager *arrangements = controller.windowArrangements();
    QTRY_VERIFY_WITH_TIMEOUT(!arrangements->busy(), 5000);
    for (int row = 0; row < arrangements->rowCount(); ++row)
        QVERIFY(!arrangements->data(arrangements->index(row), WindowArrangementManager::TitleRole)
                     .toString().contains(QStringLiteral("SmallDTL Test Arrangement Window")));

    ActionManager *actions = controller.actions();
    const QString sequenceId = actions->createSequence(QStringLiteral("Launch and arrange helper"));
    QVERIFY(!sequenceId.isEmpty());
    const QString launchId = actions->addStep(sequenceId, QStringLiteral("launchApplication"));
    const QString waitId = actions->addStep(sequenceId, QStringLiteral("wait"));
    const QString arrangeId = actions->addStep(sequenceId, QStringLiteral("applyWindowLayout"));
    QVERIFY(!launchId.isEmpty() && !waitId.isEmpty() && !arrangeId.isEmpty());
    QVERIFY(actions->updateStep(sequenceId, launchId, {{QStringLiteral("path"), helperPath}}));
    QVERIFY(actions->updateStep(sequenceId, waitId, {{QStringLiteral("delayMs"), 600}}));
    QVERIFY(actions->updateStep(sequenceId, arrangeId, {{QStringLiteral("layoutId"), layoutId}}));
    QSignalSpy completed(controller.actionRunner(), &ActionSequenceRunner::completed);
    QSignalSpy failed(controller.actionRunner(), &ActionSequenceRunner::failed);
    QVERIFY(controller.actionRunner()->start(sequenceId));
    QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 10000);
    QCOMPARE(failed.count(), 0);
    QVERIFY(!controller.actionRunner()->running());
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        for (int row = 0; row < arrangements->rowCount(); ++row)
            if (arrangements->data(arrangements->index(row), WindowArrangementManager::TitleRole)
                    .toString().contains(QStringLiteral("SmallDTL Test Arrangement Window")))
                return true;
        return false;
    }(), 5000);
    QVERIFY(arrangements->status().contains(QStringLiteral("Applied 1 window")));

    HWND helper = FindWindowW(nullptr, L"SmallDTL Test Arrangement Window");
    QVERIFY(helper);
    QRect expected;
    for (const QVariant &value : arrangements->monitors()) {
        const QVariantMap monitor = value.toMap();
        if (!monitor.value(QStringLiteral("primary")).toBool())
            continue;
        const QRect workArea(monitor.value(QStringLiteral("x")).toInt(),
                             monitor.value(QStringLiteral("y")).toInt(),
                             monitor.value(QStringLiteral("width")).toInt(),
                             monitor.value(QStringLiteral("height")).toInt());
        expected = WindowArrangementGeometry::denormalize(layout.rules.first().rect, workArea);
        break;
    }
    QVERIFY(!expected.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        RECT arranged{};
        if (!GetWindowRect(helper, &arranged))
            return false;
        return QRect(arranged.left, arranged.top, arranged.right - arranged.left,
                     arranged.bottom - arranged.top) == expected;
    }(), 5000);

    QSignalSpy applyFinished(arrangements, &WindowArrangementManager::layoutApplyFinished);
    const quint64 cancelledRequest = arrangements->requestApplyLayout(layoutId);
    QVERIFY(cancelledRequest != 0);
    arrangements->cancelApplyLayout(cancelledRequest);
    QTRY_COMPARE_WITH_TIMEOUT(applyFinished.count(), 1, 5000);
    QCOMPARE(applyFinished.first().at(0).toULongLong(), cancelledRequest);
    QVERIFY(!applyFinished.first().at(1).toBool());
    QVERIFY(applyFinished.first().at(2).toString().contains(QStringLiteral("cancelled"), Qt::CaseInsensitive));
    QTRY_VERIFY_WITH_TIMEOUT(!arrangements->busy(), 5000);
    QTest::qWait(80);
    RECT afterCancellation{};
    QVERIFY(GetWindowRect(helper, &afterCancellation));
    QCOMPARE(QRect(afterCancellation.left, afterCancellation.top,
                   afterCancellation.right - afterCancellation.left,
                   afterCancellation.bottom - afterCancellation.top), expected);

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("thumbnail"), new ThumbnailProvider);
    engine.addImageProvider(QStringLiteral("qrpreview"), new QrImageProvider(controller.qrCode()));
    for (const auto &entry : QList<QPair<QString, QObject *>>{
             {QStringLiteral("app"), &controller}, {QStringLiteral("settings"), controller.settings()},
             {QStringLiteral("profiles"), controller.profiles()}, {QStringLiteral("screenshots"), controller.screenshots()},
             {QStringLiteral("clipboardHistory"), controller.clipboardHistory()}, {QStringLiteral("fileShelf"), controller.fileShelf()},
             {QStringLiteral("qrCode"), controller.qrCode()}, {QStringLiteral("hotkeys"), controller.hotkeys()},
             {QStringLiteral("actions"), controller.actions()}, {QStringLiteral("actionRunner"), controller.actionRunner()},
             {QStringLiteral("windowArrangements"), arrangements},
             {QStringLiteral("commandPalette"), controller.commandPalette()}})
        engine.rootContext()->setContextProperty(entry.first, entry.second);
    QSignalSpy qmlWarnings(&engine, &QQmlEngine::warnings);
    engine.loadFromModule(QStringLiteral("ScreenshotHotkeyManagerTest"), QStringLiteral("Main"));
    QVERIFY(!engine.rootObjects().isEmpty());
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    controller.initializeWindow(window);
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window));
    controller.setCurrentPage(QStringLiteral("Window Arrangements"));
    QTRY_VERIFY(findItem(window->contentItem(), QStringLiteral("windowArrangementWindowList")));
    arrangements->previewLayout(layoutId);
    QTRY_VERIFY_WITH_TIMEOUT(!arrangements->busy(), 5000);
    QTRY_VERIFY(findItem(window->contentItem(), QStringLiteral("windowArrangementWindowDelegate")));
    QTRY_VERIFY(findItem(window->contentItem(), QStringLiteral("windowArrangementLayoutDelegate")));
    QTRY_VERIFY(findItem(window->contentItem(), QStringLiteral("windowArrangementPreviewDelegate")));
    QQuickItem *windowList = findItem(window->contentItem(), QStringLiteral("windowArrangementWindowList"));
    QQuickItem *windowDelegate = findItem(window->contentItem(), QStringLiteral("windowArrangementWindowDelegate"));
    QQuickItem *savedList = findItem(window->contentItem(), QStringLiteral("windowArrangementLayouts"));
    QQuickItem *savedDelegate = findItem(window->contentItem(), QStringLiteral("windowArrangementLayoutDelegate"));
    QQuickItem *previewList = findItem(window->contentItem(), QStringLiteral("windowArrangementPreview"));
    QQuickItem *previewDelegate = findItem(window->contentItem(), QStringLiteral("windowArrangementPreviewDelegate"));
    QVERIFY(windowList->width() > 0 && windowDelegate->width() > 0);
    QVERIFY(std::abs(windowDelegate->width() - windowList->width()) < 1.0);
    QVERIFY(savedList->width() > 0 && savedDelegate->width() > 0);
    QVERIFY(std::abs(savedDelegate->width() - savedList->width()) < 1.0);
    QVERIFY(previewList->width() > 0 && previewDelegate->width() > 0);
    QVERIFY(std::abs(previewDelegate->width() - previewList->width()) < 1.0);
    QVERIFY(findItem(window->contentItem(), QStringLiteral("windowArrangementWindowCheckBox"))->width() > 0);
    QVERIFY(findItem(window->contentItem(), QStringLiteral("windowArrangementWindowTitle"))->width() > 0);
    QCOMPARE(qmlWarnings.count(), 0);
    QVERIFY(window->grabWindow().save(QDir(QCoreApplication::applicationDirPath()).filePath(
        QStringLiteral("window-arrangements-populated-review.png"))));
    window->hide();
    PostMessageW(helper, WM_CLOSE, 0, 0);
    QTRY_VERIFY_WITH_TIMEOUT(FindWindowW(nullptr, L"SmallDTL Test Arrangement Window") == nullptr, 5000);
}

void UnitTests::windowArrangementNativeMonitorEnumeration()
{
    SettingsManager settings;ProfileManager profiles(&settings);WindowArrangementManager manager(&profiles);
    QVERIFY(!manager.monitors().isEmpty());
    bool primaryFound=false;
    for(const QVariant &monitorValue:manager.monitors()){
        const QVariantMap monitor=monitorValue.toMap();
        QVERIFY(monitor.value(QStringLiteral("width")).toInt()>0);
        QVERIFY(monitor.value(QStringLiteral("height")).toInt()>0);
        primaryFound=primaryFound||monitor.value(QStringLiteral("primary")).toBool();
    }
    QVERIFY(primaryFound);QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(),5000);
}
#endif

void UnitTests::monitorNamesAndNicknamesPersist()
{
    SettingsManager settings;ProfileManager profiles(&settings);
    QString device;
    {
        WindowArrangementManager manager(&profiles);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(),5000);
        if(manager.monitors().isEmpty())QSKIP("No monitors exposed by this environment");
        const auto monitor=manager.monitors().first().toMap();device=monitor.value(QStringLiteral("device")).toString();
        const QString detected=monitor.value(QStringLiteral("name")).toString();
        qInfo().noquote()<<QStringLiteral("Detected display label:")<<detected;
        QVERIFY(detected.contains(QStringLiteral(" × ")));
        QVERIFY(!detected.startsWith(QStringLiteral("\\\\.\\DISPLAY")));
        if(monitor.value(QStringLiteral("primary")).toBool())QVERIFY(detected.endsWith(QStringLiteral("Primary")));
        QVERIFY(manager.setMonitorNickname(device,QStringLiteral("Left monitor")));
        QVERIFY(manager.monitorLabel(device).startsWith(QStringLiteral("Left monitor · ")));
        QVERIFY(!manager.setMonitorNickname(device,QString(121,QLatin1Char('x'))));
        QCOMPARE(manager.monitorNickname(device),QStringLiteral("Left monitor"));
    }
    WindowArrangementManager restored(&profiles);
    QTRY_VERIFY_WITH_TIMEOUT(!restored.busy(),5000);
    QCOMPARE(restored.monitorNickname(device),QStringLiteral("Left monitor"));
    QVERIFY(restored.setMonitorNickname(device,QString()));
    QVERIFY(restored.monitorNickname(device).isEmpty());
    QVERIFY(!restored.monitorLabel(device).startsWith(QStringLiteral("Left monitor")));
    QVERIFY(!restored.setMonitorNickname(QStringLiteral("missing-display"),QStringLiteral("Wrong target")));
}
