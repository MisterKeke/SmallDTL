#pragma once
#include <QObject>
#include <Qt>
#include <QStringList>
#include <QJsonObject>
#include <QVariant>
class SettingsManager final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString generalScreenshotDirectory READ generalScreenshotDirectory WRITE setGeneralScreenshotDirectory NOTIFY generalScreenshotDirectoryChanged)
    Q_PROPERTY(QStringList knownScreenshotDirectories READ knownScreenshotDirectories NOTIFY knownScreenshotDirectoriesChanged)
    Q_PROPERTY(QString activeProfileId READ activeProfileId WRITE setActiveProfileId NOTIFY activeProfileIdChanged)
    Q_PROPERTY(bool darkTheme READ darkTheme NOTIFY preferencesChanged)
    Q_PROPERTY(QVariantMap palette READ palette NOTIFY preferencesChanged)
    Q_PROPERTY(bool closeToTray READ closeToTray WRITE setCloseToTray NOTIFY preferencesChanged)
    Q_PROPERTY(bool startMinimized READ startMinimized WRITE setStartMinimized NOTIFY preferencesChanged)
    Q_PROPERTY(bool minimizeToTray READ minimizeToTray WRITE setMinimizeToTray NOTIFY preferencesChanged)
    Q_PROPERTY(bool startWithWindows READ startWithWindows WRITE setStartWithWindows NOTIFY preferencesChanged)
    Q_PROPERTY(bool notificationsEnabled READ notificationsEnabled WRITE setNotificationsEnabled NOTIFY preferencesChanged)
    Q_PROPERTY(QString filenamePattern READ filenamePattern WRITE setFilenamePattern NOTIFY preferencesChanged)
    Q_PROPERTY(QString appearance READ appearance WRITE setAppearance NOTIFY preferencesChanged)
    Q_PROPERTY(QString accentColor READ accentColor WRITE setAccentColor NOTIFY preferencesChanged)
    Q_PROPERTY(bool automaticClipboardSavingEnabled READ automaticClipboardSavingEnabled WRITE setAutomaticClipboardSavingEnabled NOTIFY preferencesChanged)
    Q_PROPERTY(QString automaticClipboardSaveTagId READ automaticClipboardSaveTagId WRITE setAutomaticClipboardSaveTagId NOTIFY preferencesChanged)
    Q_PROPERTY(int clipboardHistoryShortcutKey READ clipboardHistoryShortcutKey NOTIFY preferencesChanged)
    Q_PROPERTY(int clipboardHistoryShortcutModifiers READ clipboardHistoryShortcutModifiers NOTIFY preferencesChanged)
    Q_PROPERTY(int fileShelfShortcutKey READ fileShelfShortcutKey NOTIFY preferencesChanged)
    Q_PROPERTY(int fileShelfShortcutModifiers READ fileShelfShortcutModifiers NOTIFY preferencesChanged)
    Q_PROPERTY(int clipboardHistoryRetentionDays READ clipboardHistoryRetentionDays WRITE setClipboardHistoryRetentionDays NOTIFY preferencesChanged)
    Q_PROPERTY(int clipboardHistoryRetentionCount READ clipboardHistoryRetentionCount WRITE setClipboardHistoryRetentionCount NOTIFY preferencesChanged)
    Q_PROPERTY(int clipboardHistoryStorageLimitMB READ clipboardHistoryStorageLimitMB WRITE setClipboardHistoryStorageLimitMB NOTIFY preferencesChanged)
public:
    explicit SettingsManager(QObject *parent = nullptr);
    QString generalScreenshotDirectory() const;
    QStringList knownScreenshotDirectories() const;
    QString activeProfileId() const;
    bool darkTheme() const;
    QVariantMap palette() const;
    QStringList startupMessages() const { return m_startupMessages; }
    bool addKnownScreenshotDirectory(const QString &directory);
    Q_INVOKABLE bool removeKnownScreenshotDirectory(const QString &directory);
public slots:
    bool setGeneralScreenshotDirectory(const QString &directory);
    bool setActiveProfileId(const QString &id);
    bool closeToTray() const;
    bool setCloseToTray(bool value);
    bool startMinimized() const;
    bool setStartMinimized(bool value);
    bool minimizeToTray() const;
    bool setMinimizeToTray(bool value);
    bool startWithWindows() const;
    bool setStartWithWindows(bool value);
    bool notificationsEnabled() const;
    bool setNotificationsEnabled(bool value);
    QString filenamePattern() const;
    bool setFilenamePattern(const QString &value);
    QString appearance() const;
    bool setAppearance(const QString &value);
    QString accentColor() const;
    bool setAccentColor(const QString &value);
    bool automaticClipboardSavingEnabled() const;
    bool setAutomaticClipboardSavingEnabled(bool enabled);
    QString automaticClipboardSaveTagId() const;
    bool setAutomaticClipboardSaveTagId(const QString &tagId);
    int clipboardHistoryShortcutKey() const;
    int clipboardHistoryShortcutModifiers() const;
    bool setClipboardHistoryShortcut(int key, int modifiers);
    int fileShelfShortcutKey() const;
    int fileShelfShortcutModifiers() const;
    bool setFileShelfShortcut(int key, int modifiers);
    int clipboardHistoryRetentionDays() const;
    bool setClipboardHistoryRetentionDays(int days);
    int clipboardHistoryRetentionCount() const;
    bool setClipboardHistoryRetentionCount(int count);
    int clipboardHistoryStorageLimitMB() const;
    bool setClipboardHistoryStorageLimitMB(int megabytes);
signals:
    void preferencesChanged();
    void generalScreenshotDirectoryChanged();
    void knownScreenshotDirectoriesChanged();
    void activeProfileIdChanged();
    void closeToTrayChanged();
    void startMinimizedChanged();
    void errorOccurred(const QString &message);
private:
    void load();
    bool commit(const QJsonObject &candidate);
    bool update(const QString &key, const QJsonValue &value);
    bool startupRegistration(bool enabled);
    void report(const QString &message);
    QString settingsFilePath() const;
    QJsonObject m_data;
    bool m_invalid = false;
    bool m_loading = true;
    bool m_futureSchema = false;
    QStringList m_startupMessages;
    mutable QVariantMap m_cachedPalette;
};
