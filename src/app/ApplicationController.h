#pragma once

#include <QObject>
#include <QPointer>

#include "../actions/ActionManager.h"
#include "../actions/ActionSequenceRunner.h"
#include "../clipboard/ClipboardHistoryManager.h"
#include "../hotkeys/HotkeyManager.h"
#include "../profiles/ProfileManager.h"
#include "../screenshots/ScreenshotManager.h"
#include "../settings/SettingsManager.h"

class QMenu;
class QSystemTrayIcon;
class QWindow;

class ApplicationController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentPage READ currentPage WRITE setCurrentPage NOTIFY currentPageChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(bool statusIsError READ statusIsError NOTIFY statusMessageChanged)
    Q_PROPERTY(bool trayAvailable READ trayAvailable CONSTANT)
    Q_PROPERTY(bool closeToTray READ closeToTray WRITE setCloseToTray NOTIFY closeToTrayChanged)
    Q_PROPERTY(bool startMinimized READ startMinimized WRITE setStartMinimized NOTIFY startMinimizedChanged)
    Q_PROPERTY(QString generalScreenshotDirectory READ generalScreenshotDirectory NOTIFY generalScreenshotDirectoryChanged)
    Q_PROPERTY(bool actionSequenceRunning READ actionSequenceRunning NOTIFY actionSequenceRunningChanged)
    Q_PROPERTY(QString actionSequenceStatus READ actionSequenceStatus NOTIFY actionSequenceStatusChanged)
    Q_PROPERTY(bool clipboardHistoryPopupVisible READ clipboardHistoryPopupVisible WRITE setClipboardHistoryPopupVisible NOTIFY clipboardHistoryPopupVisibleChanged)

public:
    explicit ApplicationController(QObject *parent = nullptr);
    ~ApplicationController() override;

    SettingsManager *settings();
    ProfileManager *profiles();
    ScreenshotManager *screenshots();
    ClipboardHistoryManager *clipboardHistory();
    HotkeyManager *hotkeys();
    ActionManager *actions();
    ActionSequenceRunner *actionRunner();

    QString currentPage() const;
    QString statusMessage() const;
    bool statusIsError() const;
    bool trayAvailable() const;
    bool closeToTray() const;
    bool startMinimized() const;
    QString generalScreenshotDirectory() const;
    bool actionSequenceRunning() const;
    QString actionSequenceStatus() const;
    bool clipboardHistoryPopupVisible() const { return m_clipboardHistoryPopupVisible; }

    void initializeWindow(QWindow *window);
    void showAtStartup();

    Q_INVOKABLE void setCurrentPage(const QString &page);
    Q_INVOKABLE void showPage(const QString &page);
    Q_INVOKABLE void hideToTray();
    Q_INVOKABLE void showWindow();
    Q_INVOKABLE void exitApplication();
    Q_INVOKABLE void saveClipboardScreenshot();
    Q_INVOKABLE void toggleClipboardHistoryPopup();
    Q_INVOKABLE void setClipboardHistoryPopupVisible(bool visible);
    Q_INVOKABLE bool setClipboardHistoryShortcut(int key, int modifiers);
    Q_INVOKABLE void chooseScreenshotDestination();
    Q_INVOKABLE void useProfileScreenshotDestination();
    Q_INVOKABLE void chooseGeneralScreenshotDirectory();
    Q_INVOKABLE void chooseProfileScreenshotDirectory(const QString &profileId);
    Q_INVOKABLE QString chooseActionPath(const QString &actionType, const QString &currentPath);
    Q_INVOKABLE void openScreenshotDestination();
    Q_INVOKABLE bool setProfileActivationShortcut(const QString &profileId, int key, int modifiers);
    Q_INVOKABLE bool addHotkey(int key, int modifiers, const QString &actionType,
                               const QString &targetProfileId, const QString &sequenceId);
    Q_INVOKABLE bool updateHotkey(const QString &assignmentId, int key, int modifiers,
                                  const QString &actionType, const QString &targetProfileId,
                                  const QString &sequenceId, bool enabled);
    Q_INVOKABLE bool setHotkeyEnabled(const QString &assignmentId, bool enabled);
    Q_INVOKABLE bool removeHotkey(const QString &assignmentId);
    Q_INVOKABLE QString formatShortcut(int key, int modifiers) const;
    Q_INVOKABLE void setCloseToTray(bool enabled);
    Q_INVOKABLE void setStartMinimized(bool enabled);
    Q_INVOKABLE void setHotkeyRecording(bool recording);
    Q_INVOKABLE void setGeneralScreenshotDirectory(const QString &directory);
    Q_INVOKABLE void dismissMessage();
    Q_INVOKABLE bool runActionSequence(const QString &sequenceId);
    Q_INVOKABLE void cancelActionSequence();
    Q_INVOKABLE void setHotkeyScope(bool global);
    Q_INVOKABLE void importProfile();
    Q_INVOKABLE void exportProfile(const QString &profileId);

signals:
    void currentPageChanged();
    void statusMessageChanged();
    void closeToTrayChanged();
    void startMinimizedChanged();
    void generalScreenshotDirectoryChanged();
    void actionSequenceRunningChanged();
    void actionSequenceStatusChanged();
    void clipboardHistoryPopupVisibleChanged();

private slots:
    void rebuildTrayMenu();
    void onScreenshotMessage(const QString &message, bool isError);

private:
    void setStatus(const QString &message, bool isError = false, bool notifyTray = false);
    bool hasBindingConflict(int key, int modifiers, const QString &exceptId = {}) const;
    bool validAssignment(const QString &actionType, const QString &targetProfileId,
                         const QString &sequenceId) const;
    void createTrayIcon();

    SettingsManager m_settings;
    ProfileManager m_profiles;
    ScreenshotManager m_screenshots;
    ClipboardHistoryManager m_clipboardHistory;
    ActionManager m_actions;
    ActionSequenceRunner m_actionRunner;
    HotkeyManager m_hotkeys;
    QPointer<QWindow> m_window;
    bool m_initialWindowPositioned = false;
    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_trayMenu = nullptr;
    QString m_currentPage = QStringLiteral("Dashboard");
    QString m_statusMessage;
    bool m_statusIsError = false;
    bool m_clipboardHistoryPopupVisible = false;
};
