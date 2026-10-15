#include "../src/actions/ActionManager.h"
#include "../src/actions/ActionSequenceRunner.h"
#include "../src/app/AppDataPaths.h"
#include "../src/app/ApplicationController.h"
#include "../src/hotkeys/HotkeyUtils.h"
#include "../src/profiles/ProfileManager.h"
#include "../src/screenshots/ScreenshotManager.h"
#include "../src/screenshots/ScreenshotNaming.h"
#include "../src/settings/SettingsManager.h"

#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QScopeGuard>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQmlApplicationEngine>
#include <QTimer>
#include <QElapsedTimer>
#include <QThreadPool>
#include <QSettings>
#include <QCursor>
#include <QScreen>
#include "../src/screenshots/ThumbnailProvider.h"
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif
#include <QtTest>
#include <functional>
#include <memory>

static std::unique_ptr<QTemporaryDir> g_testData;

static QQuickItem *findItem(QQuickItem *root, const QString &name)
{
    if (root->objectName() == name)
        return root;
    for (QQuickItem *child : root->childItems()) {
        if (QQuickItem *found = findItem(child, name))
            return found;
    }
    return nullptr;
}

class UnitTests final : public QObject
{
    Q_OBJECT
private slots:
    void init()
    {
        QCoreApplication::setApplicationName(QString::fromLatin1(QTest::currentTestFunction()));
        const QString testName = QString::fromLatin1(QTest::currentTestFunction()) +
                                 QLatin1Char('-') + QString::fromLatin1(QTest::currentDataTag());
        AppDataPaths::setTestRoot(QDir(g_testData->path()).filePath(testName));
    }

    void profileSerializationRoundTrip()
    {
        SettingsManager settings;
        ProfileManager profiles(&settings);
        const QString profileId = profiles.createProfile(QStringLiteral("Serialization profile"));
        QVERIFY(!profileId.isEmpty());
        QVERIFY(profiles.setActiveProfile(profileId));
        ActionManager actions(&profiles);
        const QString sequence=actions.createSequence(QStringLiteral("Serialized actions"));
        QVERIFY(!actions.addStep(sequence,QStringLiteral("wait")).isEmpty());
        HotkeyAssignment assignment;
        assignment.id = QStringLiteral("sequence-assignment");
        assignment.key = Qt::Key_K;
        assignment.modifiers = Qt::ControlModifier;
        assignment.actionType = QStringLiteral("sequence");
        assignment.sequenceId = sequence;
        QVERIFY(profiles.addHotkey(profileId, assignment));

        ProfileManager restored(&settings);
        const Profile *profile = restored.profileById(profileId);
        QVERIFY(profile);
        QCOMPARE(profile->hotkeys.size(), 1);
        QCOMPARE(profile->hotkeys.first().actionType, QStringLiteral("sequence"));
        QCOMPARE(profile->hotkeys.first().sequenceId, sequence);
        QCOMPARE(profile->sequences.size(),1);
    }

    void profileCrudAndActivation()
    {
        SettingsManager settings;
        ProfileManager profiles(&settings);
        const QString profileId = profiles.createProfile(QStringLiteral("Work"));
        QVERIFY(!profileId.isEmpty());
        QVERIFY(profiles.renameProfile(profileId, QStringLiteral("Focused work")));
        QCOMPARE(profiles.profileInfo(profileId).value(QStringLiteral("name")).toString(),
                 QStringLiteral("Focused work"));
        const QString copyId = profiles.duplicateProfile(profileId);
        QVERIFY(!copyId.isEmpty());
        QVERIFY(profiles.setActiveProfile(profileId));
        QCOMPARE(profiles.activeProfileId(), profileId);
        QVERIFY(profiles.deleteProfile(profileId));
        QVERIFY(!profiles.profileById(profileId));
        QVERIFY(!profiles.activeProfileId().isEmpty());
        QVERIFY(profiles.profileById(profiles.activeProfileId()));
        QVERIFY(!profiles.deleteProfile(profiles.activeProfileId()));
        QCOMPARE(profiles.rowCount(), 2);
    }

    void activeProfileNameBinding()
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

    void hotkeyConflictAndScope()
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

    void screenshotFilenameCollision()
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

    void screenshotDestinationResolution()
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

    void actionSequenceValidation()
    {
        SettingsManager settings;
        ProfileManager profiles(&settings);
        ActionManager actions(&profiles);
        const QString id = actions.createSequence(QStringLiteral("Test sequence"));
        QVERIFY(!id.isEmpty());
        const QString waitId = actions.addStep(id, QStringLiteral("wait"));
        QVERIFY(!waitId.isEmpty());
        QVERIFY(actions.validationError(id).isEmpty());
        QVERIFY(actions.updateStep(id, waitId, {{QStringLiteral("delayMs"), 600001}}));
        QVERIFY(actions.validationError(id).contains(QStringLiteral("600000")));
        QVERIFY(actions.updateStep(id, waitId, {{QStringLiteral("delayMs"), 0}}));
        QVERIFY(actions.validationError(id).isEmpty());
        QVERIFY(actions.setStepEnabled(id, waitId, false));
        QVERIFY(actions.validationError(id).contains(QStringLiteral("at least one")));
        QVERIFY(actions.setStepEnabled(id, waitId, true));
        const QString urlId = actions.addStep(id, QStringLiteral("openUrl"));
        QVERIFY(!urlId.isEmpty());
        QVERIFY(actions.validationError(id).contains(QStringLiteral("HTTP or HTTPS")));
        QVERIFY(actions.updateStep(id, urlId, {{QStringLiteral("url"), QStringLiteral("https://example.com")}}));
        QVERIFY(actions.validationError(id).isEmpty());
        QVERIFY(actions.moveStep(id, urlId, -1));
        QCOMPARE(actions.steps(id).first().toMap().value(QStringLiteral("stepId")).toString(), urlId);
        QVERIFY(actions.moveStep(id, urlId, 1));

        ActionManager restored(&profiles);
        QCOMPARE(restored.sequenceOptions().size(), 1);
        QCOMPARE(restored.steps(id).size(), 2);
    }

    void actionSequenceQmlEditor()
    {
        ApplicationController controller;
        ActionManager &actions = *controller.actions();
        const QString firstId = actions.createSequence(QStringLiteral("Existing sequence"));
        const QString secondId = actions.createSequence(QStringLiteral("Second sequence"));
        QVERIFY(!firstId.isEmpty());
        QVERIFY(!secondId.isEmpty());
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("app"), &controller);
        engine.rootContext()->setContextProperty(QStringLiteral("settings"), controller.settings());
        engine.rootContext()->setContextProperty(QStringLiteral("actions"), &actions);
        engine.rootContext()->setContextProperty(QStringLiteral("profiles"), controller.profiles());
        engine.rootContext()->setContextProperty(QStringLiteral("actionRunner"), controller.actionRunner());
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQuickWindow window;
        QQmlComponent component(&engine, QUrl::fromLocalFile(
            QStringLiteral(TEST_QML_DIR "/pages/ActionsPage.qml")));
        QTRY_VERIFY_WITH_TIMEOUT(component.status() != QQmlComponent::Loading, 5000);
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> page(component.create());
        QVERIFY2(page, qPrintable(component.errorString()));
        auto *item = qobject_cast<QQuickItem *>(page.get());
        QVERIFY(item);
        window.resize(1080, 680);
        item->setParentItem(window.contentItem());
        item->setSize(QSizeF(1080, 680));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto find = [item](const QString &name) {
            return findItem(item, name);
        };
        auto click = [&window](QQuickItem *control) {
            QTest::qWait(50); // Let QML polish the layout after model mutations.
            QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                             control->mapToScene(QPointF(control->width() / 2, control->height() / 2)).toPoint());
        };
        QTRY_COMPARE(page->property("selectedSequenceId").toString(), firstId);
        auto *editor = find(QStringLiteral("sequenceEditor"));
        QVERIFY(editor);
        QVERIFY(editor->isVisible());
        auto *secondCard = find(QStringLiteral("sequence-") + secondId);
        QVERIFY(secondCard);
        click(secondCard);
        QTRY_COMPARE(page->property("selectedSequenceId").toString(), secondId);
        auto *name = find(QStringLiteral("sequenceNameEditor"));
        QVERIFY(name);
        name->setProperty("text", QStringLiteral("Edited sequence"));
        QVERIFY(QMetaObject::invokeMethod(name, "editingFinished"));
        QCOMPARE(actions.sequenceInfo(secondId).value(QStringLiteral("name")).toString(),
                 QStringLiteral("Edited sequence"));

        auto *picker = find(QStringLiteral("stepTypePicker"));
        auto *add = find(QStringLiteral("addStepButton"));
        QVERIFY(picker);
        QVERIFY(add);
        picker->setProperty("currentIndex", 3); // Open URL
        click(add);
        QTRY_COMPARE(actions.steps(secondId).size(), 1);
        const QString urlId = actions.steps(secondId).first().toMap().value(QStringLiteral("stepId")).toString();
        QTRY_VERIFY(find(QStringLiteral("step-") + urlId));
        auto *urlCard = find(QStringLiteral("step-") + urlId);
        auto *url = findItem(urlCard, QStringLiteral("stepUrlEditor"));
        QVERIFY(url);
        QPointer<QQuickItem> originalUrlEditor(url);
        url->setProperty("text", QStringLiteral("https://example.com/edited"));
        QVERIFY(QMetaObject::invokeMethod(url, "editingFinished"));
        QTRY_COMPARE(actions.steps(secondId).first().toMap().value(QStringLiteral("url")).toString(),
                     QStringLiteral("https://example.com/edited"));
        QCoreApplication::processEvents();
        QVERIFY(originalUrlEditor); // Editing must not destroy the focused delegate.

        picker->setProperty("currentIndex", 6); // Wait
        click(add);
        QTRY_COMPARE(actions.steps(secondId).size(), 2);
        const QString waitId = actions.steps(secondId).last().toMap().value(QStringLiteral("stepId")).toString();
        QTRY_VERIFY(find(QStringLiteral("step-") + waitId));
        auto *waitCard = find(QStringLiteral("step-") + waitId);
        auto *up = findItem(waitCard, QStringLiteral("moveStepUpButton"));
        QVERIFY(up);
        QVERIFY(up->isEnabled());
        QTest::qWait(50);
        QVERIFY(waitCard->width() > 500);
        window.grabWindow().save(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("actions-editor-test.png")));
        QSignalSpy upClicked(up, SIGNAL(clicked()));
        click(up);
        QCOMPARE(upClicked.count(), 1);
        QTRY_COMPARE(actions.steps(secondId).first().toMap().value(QStringLiteral("stepId")).toString(), waitId);
        QTRY_VERIFY(!up->isEnabled());
        auto *toggle = findItem(waitCard, QStringLiteral("stepEnabledSwitch"));
        QVERIFY(toggle);
        click(toggle);
        QTRY_VERIFY(!actions.steps(secondId).first().toMap().value(QStringLiteral("enabled")).toBool());
        auto *remove = findItem(waitCard, QStringLiteral("removeStepButton"));
        QVERIFY(remove);
        click(remove);
        QTRY_COMPARE(actions.steps(secondId).size(), 1);
        QVERIFY(actions.deleteSequence(secondId));
        QTRY_COMPARE(page->property("selectedSequenceId").toString(), firstId);
        QVERIFY(actions.deleteSequence(firstId));
        QTRY_VERIFY(!editor->isVisible());
        QVERIFY(QMetaObject::invokeMethod(page.get(), "createSequence"));
        QTRY_VERIFY(editor->isVisible());
        QVERIFY(actions.contains(page->property("selectedSequenceId").toString()));
        QCOMPARE(warnings.count(), 0);
    }

    void actionRunnerScreenshotCompletion_data()
    {
        QTest::addColumn<bool>("failIndexWrite");
        QTest::newRow("save-before-next-action") << false;
        QTest::newRow("save-failure-stops-sequence") << true;
    }

    void actionRunnerScreenshotCompletion()
    {
        QFETCH(bool, failIndexWrite);
        QClipboard *clipboard = QGuiApplication::clipboard();
        auto previous = std::make_unique<QMimeData>();
        if (const QMimeData *contents = clipboard->mimeData()) {
            for (const QString &format : contents->formats())
                previous->setData(format, contents->data(format));
        }
        const auto restoreClipboard = qScopeGuard([&] { clipboard->setMimeData(previous.release()); });
        QImage image(32, 24, QImage::Format_ARGB32);
        image.fill(Qt::magenta);
        clipboard->setImage(image);

        SettingsManager settings;
        ProfileManager profiles(&settings);
        ScreenshotManager screenshots(&profiles, &settings);
        ActionManager actions(&profiles);
        ActionSequenceRunner runner(&actions, &profiles, &screenshots);
        const QString originalProfile = profiles.activeProfileId();
        const QString targetProfile = profiles.createProfile(QStringLiteral("After saving"));
        const QString sequence = actions.createSequence(QStringLiteral("Save then switch"));
        QVERIFY(!actions.addStep(sequence, QStringLiteral("saveScreenshot")).isEmpty());
        const QString switchStep = actions.addStep(sequence, QStringLiteral("switchProfile"));
        QVERIFY(actions.updateStep(sequence, switchStep, {{QStringLiteral("targetProfileId"), targetProfile}}));
        if (failIndexWrite)
            QVERIFY(QDir().mkpath(QDir(AppDataPaths::root()).filePath(QStringLiteral("screenshot-index.json"))));
        QSignalSpy saved(&screenshots, &ScreenshotManager::saveFinished);
        QSignalSpy completed(&runner, &ActionSequenceRunner::completed);
        QSignalSpy failed(&runner, &ActionSequenceRunner::failed);
        bool savedBeforeSwitch = false;
        connect(&profiles, &ProfileManager::activeProfileChanged, this, [&] {
            savedBeforeSwitch = saved.count() == 1 && saved.first().at(1).toBool();
        });
        QVERIFY(runner.start(sequence));
        QTRY_VERIFY_WITH_TIMEOUT(!runner.running(), 5000);
        QCOMPARE(saved.count(), 1);
        QVERIFY2(QFileInfo::exists(saved.first().at(0).toString()),qPrintable(saved.first().at(2).toString()));
        QCOMPARE(clipboard->image(), image);
        if (failIndexWrite) {
            QCOMPARE(failed.count(), 1);
            QCOMPARE(completed.count(), 0);
            QCOMPARE(profiles.activeProfileId(), originalProfile);
        } else {
            QCOMPARE(failed.count(), 0);
            QCOMPARE(completed.count(), 1);
            QCOMPARE(profiles.activeProfileId(), targetProfile);
            QVERIFY(savedBeforeSwitch);
        }
    }

    void cancelledRunCannotAdvanceReplacement()
    {
        SettingsManager settings;
        ProfileManager profiles(&settings);
        ScreenshotManager screenshots(&profiles, &settings);
        ActionManager actions(&profiles);
        ActionSequenceRunner runner(&actions, &profiles, &screenshots);
        const QString sequence = actions.createSequence(QStringLiteral("Wait then switch"));
        const QString wait = actions.addStep(sequence, QStringLiteral("wait"));
        QVERIFY(actions.updateStep(sequence, wait, {{QStringLiteral("delayMs"), 600000}}));
        const QString target = profiles.createProfile(QStringLiteral("Later"));
        const QString switchStep = actions.addStep(sequence, QStringLiteral("switchProfile"));
        QVERIFY(actions.updateStep(sequence, switchStep, {{QStringLiteral("targetProfileId"), target}}));
        const QString original = profiles.activeProfileId();
        QVERIFY(runner.start(sequence));
        runner.cancel(); // The first run still has a queued callback.
        QVERIFY(runner.start(sequence));
        QTest::qWait(30);
        QVERIFY(runner.running());
        QCOMPARE(profiles.activeProfileId(), original);
        runner.cancel();
    }

    void actionRunnerWaitAndCancellation()
    {
        SettingsManager settings;
        ProfileManager profiles(&settings);
        ScreenshotManager screenshots(&profiles, &settings);
        ActionManager actions(&profiles);
        ActionSequenceRunner runner(&actions, &profiles, &screenshots);
        const QString sequenceId = actions.createSequence(QStringLiteral("Async wait"));
        QVERIFY(!sequenceId.isEmpty());
        QVERIFY(!actions.addStep(sequenceId, QStringLiteral("wait")).isEmpty());

        QSignalSpy completed(&runner, &ActionSequenceRunner::completed);
        QVERIFY(runner.start(sequenceId));
        QVERIFY(runner.running());
        QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 1000);
        QVERIFY(!runner.running());

        const QString cancellableId = actions.createSequence(QStringLiteral("Cancellable wait"));
        QVERIFY(!cancellableId.isEmpty());
        const QString waitId = actions.addStep(cancellableId, QStringLiteral("wait"));
        QVERIFY(!waitId.isEmpty());
        QVERIFY(actions.updateStep(cancellableId, waitId, {{QStringLiteral("delayMs"), 600000}}));
        QVERIFY(runner.start(cancellableId));
        QVERIFY(runner.running());
        runner.cancel();
        QVERIFY(!runner.running());
        QVERIFY(runner.status().contains(QStringLiteral("cancelled")));
    }

    void malformedConfigurationRecovery()
    {
        const QString appData = AppDataPaths::root();
        QVERIFY(QDir().mkpath(appData));
        const QString actionsPath = QDir(appData).filePath(QStringLiteral("actions.json"));
        QFile malformedActions(actionsPath);
        QVERIFY(malformedActions.open(QIODevice::WriteOnly));
        malformedActions.write("{broken json");
        malformedActions.close();

        SettingsManager settings;
        ProfileManager profiles(&settings);
        ActionManager actions(&profiles);
        QVERIFY(!actions.createSequence(QStringLiteral("Recovered" )).isEmpty());
        QCOMPARE(QDir(appData).entryList({QStringLiteral("actions.json.corrupt-*")}, QDir::Files).size(), 1);
        QFile preserved(actionsPath);
        QVERIFY(preserved.open(QIODevice::ReadOnly));
        QCOMPARE(preserved.readAll(),QByteArrayLiteral("{broken json"));
        ActionManager restored(&profiles);
        QCOMPARE(restored.rowCount(),1);

        const QString settingsPath = QDir(appData).filePath(QStringLiteral("settings.json"));
        QFile malformedSettings(settingsPath);
        QVERIFY(malformedSettings.open(QIODevice::WriteOnly));
        malformedSettings.write("not json");
        malformedSettings.close();
        SettingsManager recoveredSettings;
        recoveredSettings.setCloseToTray(false);
        QVERIFY(QDir(appData).entryList({QStringLiteral("settings.json.invalid-*")}, QDir::Files).size() >= 1);
    }

    void independentSequencesAndReferencedDeletion()
    {
        SettingsManager settings; ProfileManager profiles(&settings); ActionManager actions(&profiles);
        const QString original=profiles.activeProfileId();
        const QString sequence=actions.createSequence(QStringLiteral("Original"));
        QVERIFY(!actions.addStep(sequence,QStringLiteral("wait")).isEmpty());
        HotkeyAssignment binding;binding.id=QStringLiteral("sequence-binding");binding.key=Qt::Key_F20;binding.modifiers=Qt::ControlModifier;binding.actionType=QStringLiteral("sequence");binding.sequenceId=sequence;
        QVERIFY(profiles.addHotkey(original,binding));
        QVERIFY(!actions.deleteSequence(sequence));
        const QString copy=profiles.duplicateProfile(original);QVERIFY(!copy.isEmpty());
        const auto *p=profiles.profileById(copy);QVERIFY(p);QCOMPARE(p->sequences.size(),1);
        const QString copiedSequence=p->sequences.first().id;
        QVERIFY(copiedSequence!=sequence);QCOMPARE(p->hotkeys.first().sequenceId,copiedSequence);
        QVERIFY(profiles.setActiveProfile(copy));QVERIFY(actions.renameSequence(copiedSequence,QStringLiteral("Copy changed")));
        QCOMPARE(actions.sequenceInfo(sequence).value(QStringLiteral("name")).toString(),QStringLiteral("Original"));
        QVERIFY(profiles.removeHotkey(copy,p->hotkeys.first().id));QVERIFY(actions.deleteSequence(copiedSequence));
        QVERIFY(actions.sequenceById(sequence));
        ProfileManager restored(&settings);QCOMPARE(restored.profileById(original)->sequences.size(),1);QCOMPARE(restored.profileById(copy)->sequences.size(),0);
    }

    void profileExportImportAndDetails()
    {
        SettingsManager settings;ProfileManager profiles(&settings);ActionManager actions(&profiles);
        const QString original=profiles.activeProfileId();
        QVERIFY(profiles.setDetails(original,QStringLiteral("Development environment"),QStringLiteral("D")));
        const QString sequence=actions.createSequence(QStringLiteral("Switch back"));
        const QString step=actions.addStep(sequence,QStringLiteral("switchProfile"));
        QVERIFY(actions.updateStep(sequence,step,{{QStringLiteral("targetProfileId"),original}}));
        HotkeyAssignment binding;binding.id=QStringLiteral("export-binding");binding.key=Qt::Key_F19;binding.modifiers=Qt::AltModifier;binding.actionType=QStringLiteral("sequence");binding.sequenceId=sequence;
        QVERIFY(profiles.addHotkey(original,binding));
        QVERIFY(profiles.setActivationShortcut(original,Qt::Key_F20,Qt::AltModifier));
        const QString path=QDir(AppDataPaths::root()).filePath(QStringLiteral("export.json"));
        QVERIFY(profiles.exportProfile(original,path));
        const QString imported=profiles.importProfile(path);QVERIFY(!imported.isEmpty());QVERIFY(imported!=original);
        const auto *p=profiles.profileById(imported);QVERIFY(p);QCOMPARE(p->description,QStringLiteral("Development environment"));QCOMPARE(p->icon,QStringLiteral("D"));QCOMPARE(p->activationKey,0);QVERIFY(!p->isDefault);QVERIFY(!p->hotkeys.first().enabled);
        QCOMPARE(p->sequences.first().steps.first().targetProfileId,imported);
        QCOMPARE(p->hotkeys.first().sequenceId,p->sequences.first().id);
        QVERIFY(p->sequences.first().id!=sequence);
    }

    void legacyActionsMigrateWithoutSharing()
    {
        SettingsManager settings;ProfileManager initial(&settings);
        const QString original=initial.activeProfileId();const QString second=initial.createProfile(QStringLiteral("Second"));
        const QJsonObject legacyStep{{QStringLiteral("id"),QStringLiteral("wait-step")},{QStringLiteral("type"),QStringLiteral("wait")}};
        QJsonObject sequence{{QStringLiteral("id"),QStringLiteral("legacy-sequence")},{QStringLiteral("name"),QStringLiteral("Legacy")},{QStringLiteral("steps"),QJsonArray{legacyStep}}};
        const QString actionsPath=QDir(AppDataPaths::root()).filePath(QStringLiteral("actions.json"));
        QFile actionsFile(actionsPath);QVERIFY(actionsFile.open(QIODevice::WriteOnly));const QByteArray originalBytes=QJsonDocument(QJsonObject{{QStringLiteral("schemaVersion"),1},{QStringLiteral("sequences"),QJsonArray{sequence}}}).toJson();actionsFile.write(originalBytes);actionsFile.close();
        for(const QString &id:{original,second}) {
            QJsonObject profile=ProfileManager::serializeProfile(*initial.profileById(id));profile.insert(QStringLiteral("schemaVersion"),1);profile.remove(QStringLiteral("sequences"));
            const QJsonObject validBinding{{QStringLiteral("id"),QStringLiteral("legacy-binding")},{QStringLiteral("key"),int(Qt::Key_F24)},{QStringLiteral("modifiers"),int(Qt::ControlModifier)},{QStringLiteral("actionType"),QStringLiteral("sequence")},{QStringLiteral("sequenceId"),QStringLiteral("legacy-sequence")}};
            QJsonObject missingBinding=validBinding;missingBinding.insert(QStringLiteral("id"),QStringLiteral("missing-binding"));missingBinding.insert(QStringLiteral("sequenceId"),QStringLiteral("missing-sequence"));
            profile.insert(QStringLiteral("hotkeys"),QJsonArray{validBinding,missingBinding});
            const QString path=QDir(AppDataPaths::root()).filePath(QStringLiteral("profiles/%1.json").arg(id));QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(profile).toJson());
        }
        ProfileManager profiles(&settings);ActionManager actions(&profiles);
        QCOMPARE(profiles.profileById(original)->sequences.size(),1);QCOMPARE(profiles.profileById(second)->sequences.size(),1);
        QVERIFY(profiles.profileById(original)->sequences.first().id!=profiles.profileById(second)->sequences.first().id);
        QCOMPARE(profiles.profileById(original)->hotkeys.first().sequenceId,profiles.profileById(original)->sequences.first().id);
        QVERIFY(!profiles.profileById(original)->hotkeys.last().enabled);QVERIFY(profiles.profileById(original)->hotkeys.last().sequenceId.isEmpty());
        QFile preserved(actionsPath);QVERIFY(preserved.open(QIODevice::ReadOnly));QCOMPARE(preserved.readAll(),originalBytes);
        QCOMPARE(QDir(QDir(AppDataPaths::root()).filePath(QStringLiteral("profiles"))).entryList({QStringLiteral("*.pre-sequences-*")},QDir::Files).size(),2);
        ProfileManager restored(&settings);QVERIFY(!restored.profileById(original)->legacyActions);
    }

    void configurationRejectsUnsafeAndDuplicateRecords()
    {
        SettingsManager settings;ProfileManager initial(&settings);
        QJsonObject valid=ProfileManager::serializeProfile(*initial.activeProfile());bool accepted=false;
        auto unsafe=valid;unsafe.insert(QStringLiteral("id"),QStringLiteral("../outside"));ProfileManager::parseProfile(unsafe,&accepted);QVERIFY(!accepted);
        unsafe=valid;unsafe.insert(QStringLiteral("schemaVersion"),999);ProfileManager::parseProfile(unsafe,&accepted);QVERIFY(!accepted);
        unsafe=valid;unsafe.insert(QStringLiteral("isDefault"),QStringLiteral("true"));ProfileManager::parseProfile(unsafe,&accepted);QVERIFY(!accepted);
        unsafe=valid;unsafe.insert(QStringLiteral("hotkeys"),QJsonArray{42});ProfileManager::parseProfile(unsafe,&accepted);QVERIFY(!accepted);
        const QJsonObject step{{QStringLiteral("id"),QStringLiteral("duplicate")},{QStringLiteral("type"),QStringLiteral("wait")}};
        ActionSequence sequence;
        QVERIFY(!ActionManager::parseSequence(QJsonObject{{QStringLiteral("id"),QStringLiteral("sequence")},{QStringLiteral("name"),QStringLiteral("Test")},{QStringLiteral("steps"),QJsonArray{step,step}}},&sequence));
        QFile mismatch(QDir(AppDataPaths::root()).filePath(QStringLiteral("profiles/mismatch.json")));QVERIFY(mismatch.open(QIODevice::WriteOnly));mismatch.write(QJsonDocument(valid).toJson());mismatch.close();
        QFile corrupted(QDir(AppDataPaths::root()).filePath(QStringLiteral("profiles/corrupt.json")));QVERIFY(corrupted.open(QIODevice::WriteOnly));corrupted.write("{broken");corrupted.close();
        ProfileManager restored(&settings);QCOMPARE(restored.rowCount(),1);QCOMPARE(restored.startupMessages().size(),2);QVERIFY(QFileInfo::exists(corrupted.fileName()));QVERIFY(QFileInfo::exists(mismatch.fileName()));
    }

    void settingsFailureAndFutureSchemaPreserveState()
    {
        SettingsManager settings;
        const QString file=QDir(AppDataPaths::root()).filePath(QStringLiteral("settings.json"));
        QVERIFY(QFile::rename(file,file+QStringLiteral(".original")));QVERIFY(QDir().mkpath(file));
        QSignalSpy errors(&settings,&SettingsManager::errorOccurred);
        const bool before=settings.closeToTray();QVERIFY(!settings.setCloseToTray(!before));QCOMPARE(settings.closeToTray(),before);QCOMPARE(errors.count(),1);
        QVERIFY(QDir().rmdir(file));
        QFile future(file);QVERIFY(future.open(QIODevice::WriteOnly));const QByteArray bytes=QByteArrayLiteral("{\"schemaVersion\":99,\"closeToTray\":false}");future.write(bytes);future.close();
        SettingsManager newer;QVERIFY(!newer.startupMessages().isEmpty());QVERIFY(!newer.setCloseToTray(false));
        QVERIFY(future.open(QIODevice::ReadOnly));QCOMPARE(future.readAll(),bytes);
    }

    void startupErrorsReachTheInterface()
    {
        QFile file(QDir(AppDataPaths::root()).filePath(QStringLiteral("actions.json")));QVERIFY(file.open(QIODevice::WriteOnly));file.write("{broken");file.close();
        ApplicationController controller;QTRY_VERIFY(controller.statusIsError());QVERIFY(controller.statusMessage().contains(QStringLiteral("Legacy action")));
    }

    void invalidGlobalConfigurationIsPreserved()
    {
        SettingsManager settings;ProfileManager initial(&settings);
        const QString path=QDir(AppDataPaths::root()).filePath(QStringLiteral("profiles/global.json"));
        QFile future(path);QVERIFY(future.open(QIODevice::WriteOnly));const QByteArray bytes=QByteArrayLiteral("{\"schemaVersion\":99}");future.write(bytes);future.close();
        ProfileManager profiles(&settings);QVERIFY(!profiles.startupMessages().isEmpty());
        HotkeyAssignment assignment;assignment.id=QStringLiteral("safe-id");assignment.key=Qt::Key_F20;assignment.modifiers=Qt::ControlModifier;
        QVERIFY(!profiles.addHotkey(QStringLiteral("global"),assignment));QVERIFY(future.open(QIODevice::ReadOnly));QCOMPARE(future.readAll(),bytes);
    }

    void screenshotShutdownCompletesPendingSaves()
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

    void settingsPreferencesRoundTrip()
    {
        SettingsManager settings;
        QVERIFY(settings.setFilenamePattern(QStringLiteral("Capture_{date}_{time}")));
        QVERIFY(!settings.setFilenamePattern(QStringLiteral("../{timestamp}")));
        QVERIFY(settings.setAppearance(QStringLiteral("light")));QVERIFY(!settings.darkTheme());
        QVERIFY(settings.setAccentColor(QStringLiteral("#4287f5")));
        QVERIFY(settings.setNotificationsEnabled(false));QVERIFY(settings.setMinimizeToTray(true));
#ifdef Q_OS_WIN
        QVERIFY(settings.setStartWithWindows(true));
        QSettings startup(QDir(AppDataPaths::root()).filePath(QStringLiteral("test-startup.ini")),QSettings::IniFormat);
        QVERIFY(startup.value(QStringLiteral("ScreenshotHotkeyManager")).toString().startsWith(QLatin1Char('"')));
#endif
        SettingsManager restored;QCOMPARE(restored.filenamePattern(),settings.filenamePattern());QCOMPARE(restored.accentColor(),settings.accentColor());QVERIFY(!restored.notificationsEnabled());QVERIFY(restored.minimizeToTray());QVERIFY(!restored.darkTheme());
        const QColor accent=restored.palette().value(QStringLiteral("#e96782")).value<QColor>();QCOMPARE(accent,QColor(QStringLiteral("#4287f5")));
    }

    void screenshotCommitDoesNotReplaceExistingFile()
    {
        const QString path=QDir(AppDataPaths::root()).filePath(QStringLiteral("competing.png"));QFile existing(path);QVERIFY(existing.open(QIODevice::WriteOnly));existing.write("competitor");existing.close();
        QImage image(16,16,QImage::Format_ARGB32);image.fill(Qt::cyan);
        QVERIFY(!ScreenshotManager::writeImage(image,path).success);
        QVERIFY(existing.open(QIODevice::ReadOnly));QCOMPARE(existing.readAll(),QByteArrayLiteral("competitor"));
        QCOMPARE(QDir(AppDataPaths::root()).entryList({QStringLiteral(".capture-*.tmp")},QDir::Files|QDir::Hidden).size(),0);
    }

    void thumbnailCacheAndRecentLimit()
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

    void globalHotkeysScopeAndNativeRegistration()
    {
#ifdef Q_OS_WIN
        SettingsManager settings;ProfileManager profiles(&settings);HotkeyManager manager(&profiles);QWindow owner;manager.setOwnerWindow(&owner);
        const QString original=profiles.activeProfileId();const QString second=profiles.createProfile(QStringLiteral("Other"));
        HotkeyAssignment global;global.id=QStringLiteral("global-action");global.key=Qt::Key_F20;global.modifiers=Qt::ControlModifier|Qt::AltModifier|Qt::ShiftModifier;
        QVERIFY(profiles.addHotkey(QStringLiteral("global"),global));QVERIFY(manager.isRegistered(global.id));const int globalNativeId=manager.nativeIdFor(global.id);
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

    void globalShortcutConflictValidation()
    {
        ApplicationController controller;
        const int modifiers=Qt::ControlModifier|Qt::AltModifier|Qt::ShiftModifier;
        controller.setHotkeyScope(true);QVERIFY(controller.addHotkey(Qt::Key_F20,modifiers,QStringLiteral("saveScreenshot"),QString(),QString()));
        controller.setHotkeyScope(false);QVERIFY(!controller.addHotkey(Qt::Key_F20,modifiers,QStringLiteral("saveScreenshot"),QString(),QString()));
        QVERIFY(!controller.setProfileActivationShortcut(controller.profiles()->activeProfileId(),Qt::Key_F20,modifiers));
    }

    void screenshotRenameKeepsNormalPaste()
    {
        ApplicationController controller;controller.setCurrentPage(QStringLiteral("Screenshots"));
        QQmlEngine engine;engine.addImageProvider(QStringLiteral("thumbnail"),new ThumbnailProvider);
        for(const auto &entry:QList<QPair<QString,QObject *>>{{QStringLiteral("app"),&controller},{QStringLiteral("settings"),controller.settings()},{QStringLiteral("profiles"),controller.profiles()},{QStringLiteral("screenshots"),controller.screenshots()}})engine.rootContext()->setContextProperty(entry.first,entry.second);
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

    void recorderFocusAndModifiedControlKeys()
    {
        ApplicationController controller;QQmlEngine engine;engine.rootContext()->setContextProperty(QStringLiteral("app"),&controller);engine.rootContext()->setContextProperty(QStringLiteral("settings"),controller.settings());
        QQuickWindow window;window.resize(400,200);QQmlComponent component(&engine,QUrl::fromLocalFile(QStringLiteral(TEST_QML_DIR "/components/HotkeyRecorder.qml")));QTRY_VERIFY(component.status()!=QQmlComponent::Loading);
        std::unique_ptr<QObject> object(component.create());QVERIFY2(object,qPrintable(component.errorString()));auto *recorder=qobject_cast<QQuickItem *>(object.get());recorder->setParentItem(window.contentItem());window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
        recorder->forceActiveFocus();recorder->setProperty("recording",true);QSignalSpy recorded(recorder,SIGNAL(recorded(int,int)));
        QTest::keyClick(&window,Qt::Key_Delete,Qt::ControlModifier);QCOMPARE(recorded.count(),1);QCOMPARE(recorded.first().at(0).toInt(),int(Qt::Key_Delete));QCOMPARE(recorded.first().at(1).toInt(),int(Qt::ControlModifier));
        recorder->setProperty("recording",true);recorder->setVisible(false);QVERIFY(!recorder->property("recording").toBool());
        recorder->setVisible(true);recorder->forceActiveFocus();recorder->setProperty("recording",true);QQuickItem other(window.contentItem());other.setParentItem(window.contentItem());other.forceActiveFocus();QVERIFY(!recorder->property("recording").toBool());
    }

    void initialWindowIsCenteredAndReopeningPreservesPosition()
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

    void actionDropdownLabelsFit_data()
    {
        QTest::addColumn<int>("windowWidth");
        QTest::newRow("minimum-width") << 1040;
        QTest::newRow("default-width") << 1360;
    }

    void actionDropdownLabelsFit()
    {
        QFETCH(int, windowWidth);
        ApplicationController controller;
        QVERIFY(!controller.actions()->createSequence(QStringLiteral("Dropdown layout")).isEmpty());
        controller.setCurrentPage(QStringLiteral("Actions"));
        QQmlApplicationEngine engine;
        engine.addImageProvider(QStringLiteral("thumbnail"), new ThumbnailProvider);
        for (const auto &entry : QList<QPair<QString, QObject *>>{
                 {QStringLiteral("app"), &controller}, {QStringLiteral("settings"), controller.settings()},
                 {QStringLiteral("profiles"), controller.profiles()}, {QStringLiteral("screenshots"), controller.screenshots()},
                 {QStringLiteral("hotkeys"), controller.hotkeys()}, {QStringLiteral("actions"), controller.actions()},
                 {QStringLiteral("actionRunner"), controller.actionRunner()}})
            engine.rootContext()->setContextProperty(entry.first, entry.second);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.loadFromModule(QStringLiteral("ScreenshotHotkeyManagerTest"), QStringLiteral("Main"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        window->resize(windowWidth, 680);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        for (const QString &name : {QStringLiteral("sequenceScopePicker"), QStringLiteral("stepTypePicker"), QStringLiteral("hotkeyScopePicker")}) {
            controller.setCurrentPage(name == QStringLiteral("hotkeyScopePicker") ? QStringLiteral("Hotkeys") : QStringLiteral("Actions"));
            auto *picker = findItem(window->contentItem(), name);
            QVERIFY(picker);
            QVERIFY(picker->isVisible());
            QTest::qWait(80);
            QTRY_VERIFY(picker->mapToScene(QPointF(picker->width(), 0)).x() <= window->width() + 1);
            // Check the selected label too: a wide popup alone does not prevent
            // the closed Material control from clipping behind its arrow.
            auto *selectedLabel = picker->property("contentItem").value<QQuickItem *>();
            QVERIFY(selectedLabel);
            for (int index = 0; index < picker->property("count").toInt(); ++index) {
                picker->setProperty("currentIndex", index);
                QVERIFY(QMetaObject::invokeMethod(picker, "activated", Q_ARG(int, index)));
                QTest::qWait(30);
                const qreal available = selectedLabel->width() - selectedLabel->property("leftPadding").toReal()
                                        - selectedLabel->property("rightPadding").toReal();
                QVERIFY2(selectedLabel->property("contentWidth").toReal() <= available + 1,
                         qPrintable(name + QStringLiteral(": ") + picker->property("displayText").toString()));
            }
            window->grabWindow().save(QDir(QCoreApplication::applicationDirPath()).filePath(
                QStringLiteral("%1-selected-%2.png").arg(name).arg(windowWidth)));
            auto *popup = picker->property("popup").value<QObject *>();
            QVERIFY(popup);
            QVERIFY(QMetaObject::invokeMethod(popup, "open"));
            QTest::qWait(300);
            auto *content = popup->property("contentItem").value<QQuickItem *>();
            QVERIFY(content);
            int labels = 0;
            std::function<void(QQuickItem *)> checkLabels = [&](QQuickItem *item) {
                if (item->property("index").isValid() && item->property("text").isValid()) {
                    auto *label = item->property("contentItem").value<QQuickItem *>();
                    QVERIFY(label);
                    QVERIFY2(label->width() + 1 >= label->implicitWidth(),
                             qPrintable(item->property("text").toString()));
                    ++labels;
                }
                for (auto *child : item->childItems())
                    checkLabels(child);
            };
            checkLabels(content);
            QCOMPARE(labels, picker->property("count").toInt());
            window->grabWindow().save(QDir(QCoreApplication::applicationDirPath()).filePath(
                QStringLiteral("%1-%2.png").arg(name).arg(windowWidth)));
            QVERIFY(QMetaObject::invokeMethod(popup, "close"));
            QTest::qWait(180);
            picker->setProperty("currentIndex", 0);
            QVERIFY(QMetaObject::invokeMethod(picker, "activated", Q_ARG(int, 0)));
        }
        QCOMPARE(warnings.count(), 0);
    }

    void fullShellPagesAndPreferences()
    {
        ApplicationController controller;QQmlApplicationEngine engine;engine.addImageProvider(QStringLiteral("thumbnail"),new ThumbnailProvider);
        for(const auto &entry:QList<QPair<QString,QObject *>>{{QStringLiteral("app"),&controller},{QStringLiteral("settings"),controller.settings()},{QStringLiteral("profiles"),controller.profiles()},{QStringLiteral("screenshots"),controller.screenshots()},{QStringLiteral("hotkeys"),controller.hotkeys()},{QStringLiteral("actions"),controller.actions()},{QStringLiteral("actionRunner"),controller.actionRunner()}})engine.rootContext()->setContextProperty(entry.first,entry.second);
        QSignalSpy warnings(&engine,&QQmlEngine::warnings);engine.loadFromModule(QStringLiteral("ScreenshotHotkeyManagerTest"),QStringLiteral("Main"));QVERIFY(!engine.rootObjects().isEmpty());
        auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first());QVERIFY(window);window->show();QVERIFY(QTest::qWaitForWindowExposed(window));
        for(const QString &page:{QStringLiteral("Dashboard"),QStringLiteral("Screenshots"),QStringLiteral("Hotkeys"),QStringLiteral("Profiles"),QStringLiteral("Actions"),QStringLiteral("Settings")}){controller.setCurrentPage(page);QTest::qWait(40);}
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

    void galleryAndIdleMeasurements()
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

    void hotkeyEditorRestoresRejectedEdits()
    {
        ApplicationController controller;
        QVERIFY(controller.addHotkey(Qt::Key_F19,Qt::ControlModifier,QStringLiteral("saveScreenshot"),QString(),QString()));
        controller.setCurrentPage(QStringLiteral("Hotkeys"));
        QQmlApplicationEngine engine;engine.addImageProvider(QStringLiteral("thumbnail"),new ThumbnailProvider);
        for(const auto &entry:QList<QPair<QString,QObject *>>{{QStringLiteral("app"),&controller},{QStringLiteral("settings"),controller.settings()},{QStringLiteral("profiles"),controller.profiles()},{QStringLiteral("screenshots"),controller.screenshots()},{QStringLiteral("hotkeys"),controller.hotkeys()},{QStringLiteral("actions"),controller.actions()},{QStringLiteral("actionRunner"),controller.actionRunner()}})engine.rootContext()->setContextProperty(entry.first,entry.second);
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
};

int main(int argc, char **argv)
{
#ifdef Q_OS_WIN
    const QString fonts=QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("Fonts"));
    if(QDir(fonts).exists())qputenv("QT_QPA_FONTDIR",QFile::encodeName(fonts));
#endif
    QApplication app(argc, argv);
    // Offscreen QPA does not automatically load Windows system fonts.
    for (const QString &directory : QStandardPaths::standardLocations(QStandardPaths::FontsLocation)) {
        const QString font = QDir(directory).filePath(QStringLiteral("segoeui.ttf"));
        if (QFileInfo::exists(font)) {
            QFontDatabase::addApplicationFont(font);
            app.setFont(QFont(QStringLiteral("Segoe UI")));
            break;
        }
    }
    QQuickStyle::setStyle(QStringLiteral("Material"));
    QImageReader::setAllocationLimit(256);
    g_testData = std::make_unique<QTemporaryDir>(
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("test-data-XXXXXX")));
    if (!g_testData->isValid())
        return 2;
    QCoreApplication::setOrganizationName(QStringLiteral("SmallDTL"));
    QCoreApplication::setApplicationName(QStringLiteral("UnitTests"));
    UnitTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "UnitTests.moc"
