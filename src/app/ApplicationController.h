#pragma once

#include <QObject>
#include <QPointer>
#include <QUrl>

#include "../actions/ActionManager.h"
#include "../actions/ActionSequenceRunner.h"
#include "../clipboard/ClipboardHistoryManager.h"
#include "CommandPaletteManager.h"
#include "../fileshelf/FileShelfManager.h"
#include "../hotkeys/HotkeyManager.h"
#include "../profiles/ProfileManager.h"
#include "../screenshots/ScreenshotManager.h"
#include "../qrcode/QrCodeManager.h"
#include "../settings/SettingsManager.h"
#include "../windows/WindowArrangementManager.h"

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
    Q_PROPERTY(bool fileShelfPopupVisible READ fileShelfPopupVisible WRITE setFileShelfPopupVisible NOTIFY fileShelfPopupVisibleChanged)
    Q_PROPERTY(bool fileShelfQuickAddPopupVisible READ fileShelfQuickAddPopupVisible WRITE setFileShelfQuickAddPopupVisible NOTIFY fileShelfQuickAddPopupVisibleChanged)
    Q_PROPERTY(bool commandPalettePopupVisible READ commandPalettePopupVisible WRITE setCommandPalettePopupVisible NOTIFY commandPalettePopupVisibleChanged)
    Q_PROPERTY(bool commandPaletteExecutionPending READ commandPaletteExecutionPending NOTIFY commandPaletteExecutionPendingChanged)

public:
    explicit ApplicationController(QObject *parent = nullptr);
    ~ApplicationController() override;

    SettingsManager *settings();
    ProfileManager *profiles();
    ScreenshotManager *screenshots();
    ClipboardHistoryManager *clipboardHistory();
    FileShelfManager *fileShelf();
    QrCodeManager *qrCode();
    HotkeyManager *hotkeys();
    ActionManager *actions();
    ActionSequenceRunner *actionRunner();
    WindowArrangementManager *windowArrangements();
    CommandPaletteManager *commandPalette();

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
    bool fileShelfPopupVisible() const { return m_fileShelfPopupVisible; }
    bool fileShelfQuickAddPopupVisible() const { return m_fileShelfQuickAddPopupVisible; }
    bool commandPalettePopupVisible() const { return m_commandPalettePopupVisible; }
    bool commandPaletteExecutionPending() const { return m_commandPaletteExecutionPending; }

    void initializeWindow(QWindow *window);
    void showAtStartup();

    Q_INVOKABLE void setCurrentPage(const QString &page);
    Q_INVOKABLE void showPage(const QString &page);
    Q_INVOKABLE void hideToTray();
    Q_INVOKABLE void showWindow();
    Q_INVOKABLE void setWindowCursorHidden(QObject *window, bool hidden);
    Q_INVOKABLE void exitApplication();
    Q_INVOKABLE void saveClipboardScreenshot();
    Q_INVOKABLE void toggleClipboardHistoryPopup();
    Q_INVOKABLE void setClipboardHistoryPopupVisible(bool visible);
    Q_INVOKABLE bool setClipboardHistoryShortcut(int key, int modifiers);
    Q_INVOKABLE void toggleFileShelfPopup();
    Q_INVOKABLE void setFileShelfPopupVisible(bool visible);
    Q_INVOKABLE bool setFileShelfShortcut(int key, int modifiers);
    Q_INVOKABLE bool setFileShelfQuickAddShortcut(int key, int modifiers);
    Q_INVOKABLE void startFileShelfQuickAdd();
    Q_INVOKABLE void setFileShelfQuickAddPopupVisible(bool visible);
    Q_INVOKABLE void toggleCommandPalettePopup();
    Q_INVOKABLE void setCommandPalettePopupVisible(bool visible);
    Q_INVOKABLE bool setCommandPaletteShortcut(int key, int modifiers);
    Q_INVOKABLE bool executeCommandPaletteResult(const QString &type, const QString &id);
    Q_INVOKABLE void chooseScreenshotDestination();
    Q_INVOKABLE void useProfileScreenshotDestination();
    Q_INVOKABLE void chooseGeneralScreenshotDirectory();
    Q_INVOKABLE void chooseProfileScreenshotDirectory(const QString &profileId);
    Q_INVOKABLE QString chooseActionPath(const QString &actionType, const QString &currentPath);
    Q_INVOKABLE void openScreenshotDestination();
    Q_INVOKABLE bool setProfileActivationShortcut(const QString &profileId, int key, int modifiers);
    Q_INVOKABLE bool addHotkey(int key, int modifiers, const QString &actionType,
                               const QString &targetProfileId, const QString &sequenceId,
                               const QString &layoutId = QString());
    Q_INVOKABLE bool updateHotkey(const QString &assignmentId, int key, int modifiers,
                                  const QString &actionType, const QString &targetProfileId,
                                  const QString &sequenceId, bool enabled, const QString &layoutId = QString());
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
    void fileShelfPopupVisibleChanged();
    void fileShelfQuickAddPopupVisibleChanged();
    void commandPalettePopupVisibleChanged();
    void commandPaletteExecutionPendingChanged();
    void commandPaletteScreenshotRequested(const QString &path, const QUrl &imageUrl, const QString &id,
                                           int width, int height);

private slots:
    void rebuildTrayMenu();
    void onScreenshotMessage(const QString &message, bool isError);

private:
    void setStatus(const QString &message, bool isError = false, bool notifyTray = false);
    void showCommandPaletteExecutionError(const QString &message);
    void setCommandPaletteExecutionPending(bool pending);
    bool hasBindingConflict(int key, int modifiers, const QString &exceptId = {}) const;
    bool validAssignment(const QString &actionType, const QString &targetProfileId,
                         const QString &sequenceId, const QString &layoutId = QString()) const;
    void createTrayIcon();

    SettingsManager m_settings;
    ProfileManager m_profiles;
    ScreenshotManager m_screenshots;
    QrCodeManager m_qrCode;
    ClipboardHistoryManager m_clipboardHistory;
    FileShelfManager m_fileShelf;
    ActionManager m_actions;
    WindowArrangementManager m_windowArrangements;
    ActionSequenceRunner m_actionRunner;
    HotkeyManager m_hotkeys;
    CommandPaletteManager m_commandPalette;
    QPointer<QWindow> m_window;
    bool m_initialWindowPositioned = false;
    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_trayMenu = nullptr;
    QString m_currentPage = QStringLiteral("Dashboard");
    QString m_statusMessage;
    bool m_statusIsError = false;
    bool m_clipboardHistoryPopupVisible = false;
    bool m_fileShelfPopupVisible = false;
    bool m_fileShelfQuickAddPopupVisible = false;
    bool m_commandPalettePopupVisible = false;
    bool m_commandPaletteExecutionPending = false;
    bool m_commandPaletteSequencePending = false;
    quint64 m_commandPaletteLayoutRequestId = 0;
};
