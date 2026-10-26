#pragma once

#include <QAbstractListModel>
#include <QAbstractNativeEventFilter>
#include <QHash>
#include <QPointer>

#include "../profiles/ProfileManager.h"

class QWindow;
class SettingsManager;

class HotkeyManager final : public QAbstractListModel, public QAbstractNativeEventFilter
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY assignmentsChanged)
    Q_PROPERTY(int registeredCount READ registeredCount NOTIFY registrationStatusesChanged)
    Q_PROPERTY(bool globalScope READ globalScope WRITE setGlobalScope NOTIFY assignmentsChanged)
    Q_PROPERTY(QString clipboardHistoryStatus READ clipboardHistoryStatus NOTIFY registrationStatusesChanged)
    Q_PROPERTY(QString fileShelfStatus READ fileShelfStatus NOTIFY registrationStatusesChanged)
    Q_PROPERTY(QString fileShelfQuickAddStatus READ fileShelfQuickAddStatus NOTIFY registrationStatusesChanged)
    Q_PROPERTY(QString commandPaletteStatus READ commandPaletteStatus NOTIFY registrationStatusesChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        KeyRole,
        ModifiersRole,
        ShortcutRole,
        ActionTypeRole,
        TargetProfileIdRole,
        SequenceIdRole,
        LayoutIdRole,
        EnabledRole,
        RegisteredRole,
        StatusRole
    };
    Q_ENUM(Role)

    explicit HotkeyManager(ProfileManager *profiles, QObject *parent = nullptr);
    HotkeyManager(ProfileManager *profiles, SettingsManager *settings, QObject *parent);
    ~HotkeyManager() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setOwnerWindow(QWindow *window);
    Q_INVOKABLE void setRecording(bool recording);
    Q_INVOKABLE QString activationStatus(const QString &profileId) const;
    QString clipboardHistoryStatus() const;
    QString fileShelfStatus() const;
    QString fileShelfQuickAddStatus() const;
    QString commandPaletteStatus() const;
    bool isRegistered(const QString &id) const;
    int registeredCount() const { return m_bindingByNativeId.size(); }
    int nativeIdFor(const QString &id) const;
    int failureCount() const;
    bool globalScope() const { return m_globalScope; }
    Q_INVOKABLE void setGlobalScope(bool global);
    static bool isSupportedKey(int key, int modifiers);
    static QString formatShortcut(int key, int modifiers);

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

signals:
    void assignmentsChanged();
    void registrationStatusesChanged();
    void profileActivationRequested(const QString &profileId);
    void actionTriggered(const QString &actionType, const QString &targetProfileId,
                         const QString &sequenceId, const QString &layoutId);
    void clipboardHistoryTriggered();
    void fileShelfTriggered();
    void fileShelfQuickAddTriggered();
    void commandPaletteTriggered();
    void hotkeyTriggered(const QString &description);
    void registrationFailed(const QString &message);

private:
    struct Binding {
        QString id;
        QString profileId;
        QString description;
        int key = 0;
        int modifiers = 0;
        bool isActivation = false;
        QString actionType;
        QString targetProfileId;
        QString sequenceId;
        QString layoutId;
    };
    struct Status {
        bool registered = false;
        QString text;
    };

    void refreshRegistrations();
    void unregisterAll();
    bool registerBinding(const Binding &binding, int nativeId);
    static bool toWindowsKey(int qtKey, int qtModifiers, quint32 *virtualKey);
    static quint32 toWindowsModifiers(int qtModifiers);

    ProfileManager *m_profiles = nullptr;
    SettingsManager *m_settings = nullptr;
    QPointer<QWindow> m_window;
    QVector<HotkeyAssignment> m_assignments;
    QHash<QString, Status> m_statusById;
    QHash<int, Binding> m_bindingByNativeId;
    bool m_filterInstalled = false;
    bool m_hasWindow = false;
    bool m_recording = false;
    bool m_globalScope = false;
    int m_nextNativeId = 1;
};
