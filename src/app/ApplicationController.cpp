#include "ApplicationController.h"

#include "../hotkeys/HotkeyUtils.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QUuid>
#include <QWindow>
#include <QCursor>
#include <QScreen>
#include <QDesktopServices>
#include <QUrl>

namespace {
bool isReservedPasteShortcut(int key, int modifiers)
{
    return key == Qt::Key_V && modifiers == int(Qt::ControlModifier);
}
}

ApplicationController::ApplicationController(QObject *parent)
    : QObject(parent), m_settings(this), m_profiles(&m_settings, this),
      m_screenshots(&m_profiles, &m_settings, this),
      m_qrCode(&m_screenshots, this),
      m_clipboardHistory(&m_screenshots, &m_settings, this), m_fileShelf(&m_screenshots, this), m_actions(&m_profiles, this),
      m_actionRunner(&m_actions, &m_profiles, &m_screenshots, &m_qrCode, this), m_hotkeys(&m_profiles, &m_settings, this)
{
    createTrayIcon();
    connect(&m_settings,&SettingsManager::errorOccurred,this,[this](const QString &message){setStatus(message,true,true);});
    connect(&m_hotkeys,&HotkeyManager::registrationFailed,this,[this](const QString &message){setStatus(message,true,true);});
    connect(&m_hotkeys, &HotkeyManager::fileShelfTriggered, this, &ApplicationController::toggleFileShelfPopup);
    connect(QCoreApplication::instance(),&QCoreApplication::aboutToQuit,this,[this]{m_actionRunner.cancel();m_hotkeys.setRecording(true);m_clipboardHistory.finishPendingSaves();m_screenshots.finishPendingSaves();});
    const QStringList startup=m_settings.startupMessages()+m_profiles.startupMessages()+m_actions.startupMessages();
    if(!startup.isEmpty())QTimer::singleShot(0,this,[this,startup]{setStatus(startup.join(QLatin1Char('\n')),true,true);});
    connect(&m_screenshots, &ScreenshotManager::messageChanged,
            this, &ApplicationController::onScreenshotMessage);
    connect(&m_screenshots, &ScreenshotManager::automaticClipboardSavingChanged,
            this, &ApplicationController::rebuildTrayMenu);
    connect(&m_clipboardHistory, &ClipboardHistoryManager::messageChanged, this,
            [this](const QString &message, bool isError) { setStatus(message, isError, true); });
    connect(&m_clipboardHistory, &ClipboardHistoryManager::pausedChanged,
            this, &ApplicationController::rebuildTrayMenu);
    connect(&m_hotkeys, &HotkeyManager::clipboardHistoryTriggered,
            this, &ApplicationController::toggleClipboardHistoryPopup);
    connect(&m_profiles, &ProfileManager::errorOccurred, this, [this](const QString &message) {
        setStatus(message, true, true);
    });
    connect(&m_profiles, &ProfileManager::profilesChanged, this, &ApplicationController::rebuildTrayMenu);
    connect(&m_profiles, &ProfileManager::activeProfileChanged, this, [this] {
        const int failures=m_hotkeys.failureCount();
        if(failures>0)setStatus(QStringLiteral("Switched to %1. %2 shortcut(s) could not be registered; see Hotkeys.").arg(m_profiles.activeProfileName()).arg(failures),true);
        else setStatus(QStringLiteral("Switched to %1.").arg(m_profiles.activeProfileName()), false, true);
        emit generalScreenshotDirectoryChanged();
        rebuildTrayMenu();
    });
    connect(&m_settings, &SettingsManager::generalScreenshotDirectoryChanged,
            this, &ApplicationController::generalScreenshotDirectoryChanged);
    connect(&m_settings, &SettingsManager::closeToTrayChanged, this, &ApplicationController::closeToTrayChanged);
    connect(&m_settings, &SettingsManager::startMinimizedChanged, this, &ApplicationController::startMinimizedChanged);
    connect(&m_hotkeys, &HotkeyManager::profileActivationRequested, this, [this](const QString &profileId) {
        m_profiles.setActiveProfile(profileId);
    });
    connect(&m_hotkeys, &HotkeyManager::actionTriggered, this,
            [this](const QString &actionType, const QString &targetProfileId,
                   const QString &sequenceId) {
        if (actionType == QStringLiteral("saveScreenshot")) {
            saveClipboardScreenshot();
        } else if (actionType == QStringLiteral("switchProfile")) {
            if (!m_profiles.setActiveProfile(targetProfileId))
                setStatus(QStringLiteral("The hotkey target profile is unavailable."), true, true);
        } else if (actionType == QStringLiteral("sequence")) {
            runActionSequence(sequenceId);
        }
    });
    connect(&m_hotkeys, &HotkeyManager::hotkeyTriggered, this, [this](const QString &description) {
        setStatus(QStringLiteral("Shortcut activated: %1").arg(description));
    });
    connect(&m_actions, &ActionManager::errorOccurred, this, [this](const QString &message) {
        setStatus(message, true, true);
    });
    connect(&m_actionRunner, &ActionSequenceRunner::failed, this, [this](const QString &message) {
        setStatus(message, true, true);
    });
    connect(&m_actionRunner, &ActionSequenceRunner::completed, this, [this](const QString &name) {
        Q_UNUSED(name);
        setStatus(m_actionRunner.status());
    });
    connect(&m_actionRunner, &ActionSequenceRunner::runningChanged,
            this, &ApplicationController::actionSequenceRunningChanged);
    connect(&m_actionRunner, &ActionSequenceRunner::statusChanged,
            this, &ApplicationController::actionSequenceStatusChanged);
    rebuildTrayMenu();
}

ApplicationController::~ApplicationController()
{
    m_actionRunner.cancel();
    m_screenshots.finishPendingSaves();
    if (m_tray) {
        m_tray->hide();
        m_tray->setContextMenu(nullptr);
    }
    delete m_trayMenu;
    m_trayMenu = nullptr;
}

SettingsManager *ApplicationController::settings() { return &m_settings; }
ProfileManager *ApplicationController::profiles() { return &m_profiles; }
ScreenshotManager *ApplicationController::screenshots() { return &m_screenshots; }
ClipboardHistoryManager *ApplicationController::clipboardHistory() { return &m_clipboardHistory; }
FileShelfManager *ApplicationController::fileShelf() { return &m_fileShelf; }
QrCodeManager *ApplicationController::qrCode() { return &m_qrCode; }
HotkeyManager *ApplicationController::hotkeys() { return &m_hotkeys; }
ActionManager *ApplicationController::actions() { return &m_actions; }
ActionSequenceRunner *ApplicationController::actionRunner() { return &m_actionRunner; }

QString ApplicationController::currentPage() const { return m_currentPage; }
QString ApplicationController::statusMessage() const { return m_statusMessage; }
bool ApplicationController::statusIsError() const { return m_statusIsError; }
bool ApplicationController::trayAvailable() const { return m_tray && m_tray->isVisible(); }
bool ApplicationController::closeToTray() const { return m_settings.closeToTray(); }
bool ApplicationController::startMinimized() const { return m_settings.startMinimized(); }
QString ApplicationController::generalScreenshotDirectory() const { return m_settings.generalScreenshotDirectory(); }
bool ApplicationController::actionSequenceRunning() const { return m_actionRunner.running(); }
QString ApplicationController::actionSequenceStatus() const { return m_actionRunner.status(); }

void ApplicationController::initializeWindow(QWindow *window)
{
    m_window = window;
    m_initialWindowPositioned = false;
    if (m_window) {
        m_hotkeys.setOwnerWindow(m_window);
        connect(m_window,&QWindow::windowStateChanged,this,[this](Qt::WindowState state){
            if(state==Qt::WindowMinimized && m_settings.minimizeToTray() && trayAvailable())hideToTray();
        });
    }
}

void ApplicationController::showAtStartup()
{
    if (!m_window)
        return;
    if (m_settings.startMinimized() && trayAvailable()) {
        m_window->hide();
        return;
    }
    showWindow();
}

void ApplicationController::setCurrentPage(const QString &page)
{
    if (m_currentPage == page)
        return;
    m_currentPage = page;
    emit currentPageChanged();
}

void ApplicationController::showPage(const QString &page)
{
    setCurrentPage(page);
    showWindow();
}

void ApplicationController::hideToTray()
{
    if (m_window && trayAvailable())
        m_window->hide();
}

void ApplicationController::showWindow()
{
    if (!m_window)
        return;
    const bool firstShow = !m_initialWindowPositioned;
    QRect initialAvailableGeometry;
    if (firstShow) {
        QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        if (screen) {
            initialAvailableGeometry = screen->availableGeometry();
            m_window->setScreen(screen);
            // Native creation uses the position to choose a monitor. Move into
            // the target screen first, including monitors with a different DPI.
            m_window->setPosition(initialAvailableGeometry.center()
                                  - QPoint(m_window->width() / 2, m_window->height() / 2));
        }
        // Create the native window so its title bar and borders are included.
        m_window->create();
        if (initialAvailableGeometry.isValid()) {
            QRect frame = m_window->frameGeometry();
            frame.moveCenter(initialAvailableGeometry.center());
            m_window->setFramePosition(frame.topLeft());
        }
    }
    m_window->showNormal();
    if (firstShow && initialAvailableGeometry.isValid()) {
        // Some platforms finalize their frame margins only when shown.
        QRect frame = m_window->frameGeometry();
        frame.moveCenter(initialAvailableGeometry.center());
        m_window->setFramePosition(frame.topLeft());
        m_initialWindowPositioned = true;
    }
    m_window->raise();
    m_window->requestActivate();
}

void ApplicationController::exitApplication()
{
    if (m_tray)
        m_tray->hide();
    QCoreApplication::quit();
}

void ApplicationController::saveClipboardScreenshot()
{
    m_screenshots.saveClipboardImage();
}

void ApplicationController::toggleClipboardHistoryPopup()
{
    setClipboardHistoryPopupVisible(!m_clipboardHistoryPopupVisible);
}

void ApplicationController::setClipboardHistoryPopupVisible(bool visible)
{
    if (m_clipboardHistoryPopupVisible == visible)
        return;
    m_clipboardHistoryPopupVisible = visible;
    if (visible)
        m_clipboardHistory.refresh();
    emit clipboardHistoryPopupVisibleChanged();
}

bool ApplicationController::setClipboardHistoryShortcut(int key, int modifiers)
{
    if (key != 0 && isReservedPasteShortcut(key, modifiers)) {
        setStatus(QStringLiteral("Ctrl+V is reserved for screenshot paste while the Screenshots page is open."), true);
        return false;
    }
    if (key != 0 && !HotkeyManager::isSupportedKey(key, modifiers)) {
        setStatus(QStringLiteral("Choose a supported shortcut with at least one modifier."), true);
        return false;
    }
    if (key != 0 && key == m_settings.fileShelfShortcutKey() && modifiers == m_settings.fileShelfShortcutModifiers()) {
        setStatus(QStringLiteral("That shortcut is already assigned to the file shelf."), true);
        return false;
    }
    if (key != 0) {
        auto conflicts = [key, modifiers](const QVector<HotkeyAssignment> &assignments) {
            for (const HotkeyAssignment &assignment : assignments)
                if (assignment.enabled && assignment.key == key && assignment.modifiers == modifiers)
                    return true;
            return false;
        };
        bool conflict = conflicts(m_profiles.globalProfile().hotkeys);
        for (const Profile &profile : m_profiles.profiles()) {
            conflict = conflict || (profile.activationKey == key && profile.activationModifiers == modifiers) || conflicts(profile.hotkeys);
        }
        conflict = conflict || (m_profiles.globalProfile().activationKey == key &&
                                m_profiles.globalProfile().activationModifiers == modifiers);
        if (conflict) {
            setStatus(QStringLiteral("That shortcut is already assigned. Choose a different combination."), true);
            return false;
        }
    }
    if (!m_settings.setClipboardHistoryShortcut(key, modifiers))
        return false;
    return true;
}

void ApplicationController::toggleFileShelfPopup()
{
    setFileShelfPopupVisible(!m_fileShelfPopupVisible);
}

void ApplicationController::setFileShelfPopupVisible(bool visible)
{
    if (m_fileShelfPopupVisible == visible)
        return;
    m_fileShelfPopupVisible = visible;
    if (visible)
        m_fileShelf.refresh();
    emit fileShelfPopupVisibleChanged();
}

bool ApplicationController::setFileShelfShortcut(int key, int modifiers)
{
    if (key != 0 && !HotkeyManager::isSupportedKey(key, modifiers)) {
        setStatus(QStringLiteral("Choose a supported shortcut with at least one modifier."), true);
        return false;
    }
    if (key != 0) {
        auto conflicts = [key, modifiers](const QVector<HotkeyAssignment> &assignments) {
            for (const HotkeyAssignment &assignment : assignments)
                if (assignment.enabled && assignment.key == key && assignment.modifiers == modifiers)
                    return true;
            return false;
        };
        bool conflict = (m_settings.clipboardHistoryShortcutKey() == key &&
                         m_settings.clipboardHistoryShortcutModifiers() == modifiers) ||
                        conflicts(m_profiles.globalProfile().hotkeys);
        for (const Profile &profile : m_profiles.profiles())
            conflict = conflict || (profile.activationKey == key && profile.activationModifiers == modifiers) ||
                       conflicts(profile.hotkeys);
        if (conflict || (m_profiles.globalProfile().activationKey == key &&
                         m_profiles.globalProfile().activationModifiers == modifiers)) {
            setStatus(QStringLiteral("That shortcut is already assigned. Choose a different combination."), true);
            return false;
        }
    }
    if (!m_settings.setFileShelfShortcut(key, modifiers))
        return false;
    return true;
}

void ApplicationController::chooseScreenshotDestination()
{
    const QString selected = QFileDialog::getExistingDirectory(
        nullptr, QStringLiteral("Choose screenshot destination"), m_screenshots.destination(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!selected.isEmpty())
        m_screenshots.setDestinationOverride(selected);
}

void ApplicationController::useProfileScreenshotDestination()
{
    m_screenshots.clearDestinationOverride();
}

void ApplicationController::chooseGeneralScreenshotDirectory()
{
    const QString selected = QFileDialog::getExistingDirectory(
        nullptr, QStringLiteral("Choose general screenshot directory"), m_settings.generalScreenshotDirectory(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!selected.isEmpty())
        setGeneralScreenshotDirectory(selected);
}

void ApplicationController::chooseProfileScreenshotDirectory(const QString &profileId)
{
    const QString current = m_screenshots.profileDestination(profileId);
    const QString selected = QFileDialog::getExistingDirectory(
        nullptr, QStringLiteral("Choose profile screenshot directory"), current,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!selected.isEmpty())
        m_profiles.setScreenshotDirectory(profileId, selected);
}

QString ApplicationController::chooseActionPath(const QString &actionType, const QString &currentPath)
{
    if (actionType == QStringLiteral("decodeQrImage")) {
        return QFileDialog::getOpenFileName(nullptr, QStringLiteral("Choose image to decode"), currentPath,
            QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.tif *.tiff);;All files (*)"));
    }
    if (actionType == QStringLiteral("openFolder")) {
        return QFileDialog::getExistingDirectory(nullptr, QStringLiteral("Choose folder"), currentPath,
                                                  QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    }
    const QString filter = actionType == QStringLiteral("launchApplication")
        ? QStringLiteral("Applications (*.exe *.com);;All files (*)")
        : QStringLiteral("All files (*)");
    return QFileDialog::getOpenFileName(nullptr,
        actionType == QStringLiteral("launchApplication") ? QStringLiteral("Choose application")
                                                           : QStringLiteral("Choose file"),
        currentPath, filter);
}

void ApplicationController::openScreenshotDestination()
{
    const QString destination = m_screenshots.destination();
    if (!QDir(destination).exists()) {
        setStatus(QStringLiteral("Screenshot destination is unavailable: %1").arg(destination), true, true);
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(destination));
}

bool ApplicationController::setProfileActivationShortcut(const QString &profileId, int key, int modifiers)
{
    if (key != 0 && isReservedPasteShortcut(key, modifiers)) {
        setStatus(QStringLiteral("Ctrl+V is reserved for screenshot paste while the Screenshots page is open."), true);
        return false;
    }
    if (key != 0 && !HotkeyManager::isSupportedKey(key, modifiers)) {
        setStatus(QStringLiteral("Use a supported key with Ctrl, Alt, Shift, or Windows."), true);
        return false;
    }
    if (key != 0 && (hasBindingConflict(key, modifiers, QStringLiteral("activation:") + profileId)
                     || HotkeyUtils::anyProfileAssignmentConflict(m_profiles.profiles(), key, modifiers)
                     || HotkeyUtils::assignmentConflict(m_profiles.globalProfile().hotkeys,key,modifiers))) {
        setStatus(QStringLiteral("That shortcut is already assigned in a global or profile scope."), true);
        return false;
    }
    if (!m_profiles.setActivationShortcut(profileId, key, modifiers)) {
        setStatus(QStringLiteral("Could not save that profile activation shortcut."), true);
        return false;
    }
    setStatus(key == 0 ? QStringLiteral("Profile activation shortcut cleared.")
                       : QStringLiteral("Profile activation shortcut saved. Windows registration status is shown beside it."));
    return true;
}

bool ApplicationController::addHotkey(int key, int modifiers, const QString &actionType,
                                      const QString &targetProfileId, const QString &sequenceId)
{
    if (isReservedPasteShortcut(key, modifiers)) {
        setStatus(QStringLiteral("Ctrl+V is reserved for screenshot paste while the Screenshots page is open."), true);
        return false;
    }
    if (!HotkeyManager::isSupportedKey(key, modifiers)) {
        setStatus(QStringLiteral("Use a supported key with Ctrl, Alt, Shift, or Windows."), true);
        return false;
    }
    if (!validAssignment(actionType, targetProfileId, sequenceId)) {
        setStatus(QStringLiteral("Choose a valid shortcut action, profile, or sequence."), true);
        return false;
    }
    const Profile *profile = m_profiles.profileById(m_hotkeys.globalScope()?QStringLiteral("global"):m_profiles.activeProfileId());
    if (!profile)
        return false;
    if (HotkeyUtils::assignmentConflict(profile->hotkeys, key, modifiers) ||
        (m_hotkeys.globalScope()?HotkeyUtils::anyProfileAssignmentConflict(m_profiles.profiles(),key,modifiers):HotkeyUtils::assignmentConflict(m_profiles.globalProfile().hotkeys,key,modifiers))) {
        setStatus(QStringLiteral("That shortcut is already assigned in the active profile."), true);
        return false;
    }
    if (hasBindingConflict(key, modifiers)) {
        setStatus(QStringLiteral("That shortcut conflicts with a profile activation shortcut."), true);
        return false;
    }
    HotkeyAssignment assignment;
    assignment.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    assignment.key = key;
    assignment.modifiers = modifiers;
    assignment.actionType = actionType;
    assignment.targetProfileId = targetProfileId;
    assignment.sequenceId = sequenceId;
    if (!m_profiles.addHotkey(profile->id, assignment)) {
        setStatus(QStringLiteral("Could not save the shortcut."), true);
        return false;
    }
    setStatus(QStringLiteral("Shortcut saved. Check its registration status in the list."));
    return true;
}

bool ApplicationController::updateHotkey(const QString &assignmentId, int key, int modifiers,
                                         const QString &actionType, const QString &targetProfileId,
                                         const QString &sequenceId, bool enabled)
{
    const Profile *profile = m_profiles.profileById(m_hotkeys.globalScope()?QStringLiteral("global"):m_profiles.activeProfileId());
    if (!profile)
        return false;
    const HotkeyAssignment *current = nullptr;
    for (const HotkeyAssignment &assignment : profile->hotkeys) {
        if (assignment.id == assignmentId) {
            current = &assignment;
            break;
        }
    }
    if (!current)
        return false;
    if (key != 0 && !HotkeyManager::isSupportedKey(key, modifiers)) {
        setStatus(QStringLiteral("Use a supported key with Ctrl, Alt, Shift, or Windows."), true);
        return false;
    }
    if (enabled && isReservedPasteShortcut(key, modifiers)) {
        setStatus(QStringLiteral("Ctrl+V is reserved for screenshot paste while the Screenshots page is open."), true);
        return false;
    }
    if (!validAssignment(actionType, targetProfileId, sequenceId) &&
        (enabled || !QStringList{QStringLiteral("saveScreenshot"),QStringLiteral("switchProfile"),QStringLiteral("sequence")}.contains(actionType))) {
        setStatus(QStringLiteral("Choose a valid shortcut action, profile, or sequence."), true);
        return false;
    }
    if (enabled && key != 0) {
        if (HotkeyUtils::assignmentConflict(profile->hotkeys, key, modifiers, assignmentId) ||
            (m_hotkeys.globalScope()?HotkeyUtils::anyProfileAssignmentConflict(m_profiles.profiles(),key,modifiers):HotkeyUtils::assignmentConflict(m_profiles.globalProfile().hotkeys,key,modifiers))) {
            setStatus(QStringLiteral("That shortcut is already assigned in the active profile."), true);
            return false;
        }
        if (hasBindingConflict(key, modifiers)) {
            setStatus(QStringLiteral("That shortcut conflicts with a profile activation shortcut."), true);
            return false;
        }
    }
    HotkeyAssignment updated = *current;
    updated.key = key;
    updated.modifiers = modifiers;
    updated.actionType = actionType;
    updated.targetProfileId = targetProfileId;
    updated.sequenceId = sequenceId;
    updated.enabled = enabled;
    if (!m_profiles.updateHotkey(profile->id, updated)) {
        setStatus(QStringLiteral("Could not update the shortcut."), true);
        return false;
    }
    setStatus(QStringLiteral("Shortcut updated. Check its registration status in the list."));
    return true;
}

bool ApplicationController::setHotkeyEnabled(const QString &assignmentId, bool enabled)
{
    const Profile *profile = m_profiles.profileById(m_hotkeys.globalScope()?QStringLiteral("global"):m_profiles.activeProfileId());
    if (!profile)
        return false;
    for (const HotkeyAssignment &assignment : profile->hotkeys) {
        if (assignment.id == assignmentId)
            return updateHotkey(assignmentId, assignment.key, assignment.modifiers,
                                assignment.actionType, assignment.targetProfileId,
                                assignment.sequenceId, enabled);
    }
    return false;
}

bool ApplicationController::removeHotkey(const QString &assignmentId)
{
    const QString profileId = m_hotkeys.globalScope()?QStringLiteral("global"):m_profiles.activeProfileId();
    if (!m_profiles.removeHotkey(profileId, assignmentId)) {
        setStatus(QStringLiteral("Could not remove that shortcut."), true);
        return false;
    }
    setStatus(QStringLiteral("Shortcut removed."));
    return true;
}

QString ApplicationController::formatShortcut(int key, int modifiers) const
{
    return HotkeyUtils::formatShortcut(key, modifiers);
}

void ApplicationController::setCloseToTray(bool enabled) { m_settings.setCloseToTray(enabled); }
void ApplicationController::setStartMinimized(bool enabled) { m_settings.setStartMinimized(enabled); }
void ApplicationController::setHotkeyRecording(bool recording) { m_hotkeys.setRecording(recording); }

void ApplicationController::setGeneralScreenshotDirectory(const QString &directory)
{
    if (!QDir(directory).exists()) {
        setStatus(QStringLiteral("Choose an existing screenshot directory."), true);
        return;
    }
    if (!m_settings.setGeneralScreenshotDirectory(directory)) return;
    setStatus(QStringLiteral("General screenshot directory updated."));
}

void ApplicationController::dismissMessage()
{
    if (m_statusMessage.isEmpty())
        return;
    m_statusMessage.clear();
    m_statusIsError = false;
    emit statusMessageChanged();
}

bool ApplicationController::runActionSequence(const QString &sequenceId)
{
    return m_actionRunner.start(sequenceId);
}

void ApplicationController::cancelActionSequence()
{
    m_actionRunner.cancel();
}

void ApplicationController::rebuildTrayMenu()
{
    if (!m_trayMenu)
        return;
    m_trayMenu->clear();
    QAction *openAction = m_trayMenu->addAction(QStringLiteral("Open Application"));
    connect(openAction, &QAction::triggered, this, &ApplicationController::showWindow);
    QAction *saveAction = m_trayMenu->addAction(QStringLiteral("Save Clipboard Screenshot"));
    connect(saveAction, &QAction::triggered, this, &ApplicationController::saveClipboardScreenshot);
    QAction *historyAction = m_trayMenu->addAction(QStringLiteral("Open Clipboard History"));
    connect(historyAction, &QAction::triggered, this, [this] { showPage(QStringLiteral("Clipboard History")); });
    QAction *historyPauseAction = m_trayMenu->addAction(
        m_clipboardHistory.paused() ? QStringLiteral("Resume Clipboard History") : QStringLiteral("Pause Clipboard History"));
    connect(historyPauseAction, &QAction::triggered, this, [this] {
        m_clipboardHistory.setPaused(!m_clipboardHistory.paused());
    });
    QAction *shelfAction = m_trayMenu->addAction(QStringLiteral("Open File Shelf"));
    connect(shelfAction, &QAction::triggered, this, [this] { showPage(QStringLiteral("File Shelf")); });
    const bool autoEnabled = m_screenshots.automaticClipboardSavingEnabled();
    const bool paused = m_screenshots.automaticClipboardSavingPaused();
    QAction *autoSaveAction = m_trayMenu->addAction(
        !autoEnabled ? QStringLiteral("Automatic Clipboard Saving Off") :
        paused ? QStringLiteral("Resume Automatic Clipboard Saving") :
                 QStringLiteral("Pause Automatic Clipboard Saving"));
    autoSaveAction->setEnabled(autoEnabled);
    connect(autoSaveAction, &QAction::triggered, this, [this] {
        m_screenshots.setAutomaticClipboardSavingPaused(!m_screenshots.automaticClipboardSavingPaused());
    });
    m_trayMenu->addSeparator();
    QAction *activeAction = m_trayMenu->addAction(QStringLiteral("Active Profile: %1").arg(m_profiles.activeProfileName()));
    activeAction->setEnabled(false);
    QMenu *switchMenu = m_trayMenu->addMenu(QStringLiteral("Switch Profile"));
    for (const Profile &profile : m_profiles.profiles()) {
        QAction *action = switchMenu->addAction(profile.name);
        action->setCheckable(true);
        action->setChecked(profile.id == m_profiles.activeProfileId());
        connect(action, &QAction::triggered, this, [this, id = profile.id] { m_profiles.setActiveProfile(id); });
    }
    m_trayMenu->addSeparator();
    QAction *settingsAction = m_trayMenu->addAction(QStringLiteral("Settings"));
    connect(settingsAction, &QAction::triggered, this, [this] { showPage(QStringLiteral("Settings")); });
    QAction *exitAction = m_trayMenu->addAction(QStringLiteral("Exit"));
    connect(exitAction, &QAction::triggered, this, &ApplicationController::exitApplication);
}

void ApplicationController::onScreenshotMessage(const QString &message, bool isError)
{
    setStatus(message, isError, true);
}

void ApplicationController::setStatus(const QString &message, bool isError, bool notifyTray)
{
    m_statusMessage = message;
    m_statusIsError = isError;
    emit statusMessageChanged();
    if (notifyTray && trayAvailable() && (isError || m_settings.notificationsEnabled())) {
        m_tray->showMessage(QStringLiteral("SmallDTL"), message,
                            isError ? QSystemTrayIcon::Warning : QSystemTrayIcon::Information, 3500);
    }
}

bool ApplicationController::hasBindingConflict(int key, int modifiers, const QString &exceptId) const
{
    if (exceptId != QStringLiteral("clipboard-history") && m_settings.clipboardHistoryShortcutKey() == key &&
        m_settings.clipboardHistoryShortcutModifiers() == modifiers)
        return true;
    if (exceptId != QStringLiteral("file-shelf") && m_settings.fileShelfShortcutKey() == key &&
        m_settings.fileShelfShortcutModifiers() == modifiers)
        return true;
    QString exceptProfileId;
    if (exceptId.startsWith(QStringLiteral("activation:")))
        exceptProfileId = exceptId.mid(QStringLiteral("activation:").size());
    return HotkeyUtils::profileActivationConflict(m_profiles.profiles(), key, modifiers, exceptProfileId);
}

bool ApplicationController::validAssignment(const QString &actionType, const QString &targetProfileId,
                                            const QString &sequenceId) const
{
    if (actionType == QStringLiteral("saveScreenshot"))
        return true;
    if (actionType == QStringLiteral("switchProfile"))
        return targetProfileId!=QStringLiteral("global") && m_profiles.profileById(targetProfileId) != nullptr;
    return actionType == QStringLiteral("sequence") && m_actions.sequenceOwner(sequenceId)==(m_hotkeys.globalScope()?QStringLiteral("global"):m_profiles.activeProfileId());
}

void ApplicationController::createTrayIcon()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(QColor(QStringLiteral("#e96782")));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(QRectF(3, 3, 58, 58), 16, 16);
    painter.setPen(Qt::white);
    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(35);
    painter.setFont(font);
    painter.drawText(pixmap.rect(), Qt::AlignCenter, QStringLiteral("S"));
    painter.end();

    m_tray = new QSystemTrayIcon(QIcon(pixmap), this);
    m_tray->setToolTip(QStringLiteral("SmallDTL"));
    m_trayMenu = new QMenu;
    m_tray->setContextMenu(m_trayMenu);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
            showWindow();
    });
    m_tray->show();
}

void ApplicationController::setHotkeyScope(bool global) {m_actions.setGlobalScope(global);m_hotkeys.setGlobalScope(global);}
void ApplicationController::importProfile() {
    const QString path=QFileDialog::getOpenFileName(nullptr,QStringLiteral("Import profile"),QString(),QStringLiteral("Profile JSON (*.json)"));
    if(path.isEmpty())return;
    const QString id=m_profiles.importProfile(path);
    if(!id.isEmpty())setStatus(QStringLiteral("Profile imported. Shortcuts and unavailable profile-switch steps are disabled; review them before running."));
}
void ApplicationController::exportProfile(const QString &id) {
    const QString path=QFileDialog::getSaveFileName(nullptr,QStringLiteral("Export profile"),QStringLiteral("profile.json"),QStringLiteral("Profile JSON (*.json)"));
    if(!path.isEmpty() && m_profiles.exportProfile(id,path))setStatus(QStringLiteral("Profile exported with its action sequences."));
}
