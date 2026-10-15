#pragma once

#include <QAbstractListModel>
#include <QVector>
#include <QJsonObject>
#include "../actions/ActionDefinition.h"

#include "../settings/SettingsManager.h"

struct HotkeyAssignment
{
    QString id;
    int key = 0;
    int modifiers = 0;
    QString actionType = QStringLiteral("saveScreenshot");
    QString targetProfileId;
    QString sequenceId;
    bool enabled = true;
};

struct Profile
{
    QString id;
    QString name;
    QString description;
    QString icon = QStringLiteral("◉");
    QString screenshotDirectory;
    int activationKey = 0;
    int activationModifiers = 0;
    bool isDefault = false;
    QVector<HotkeyAssignment> hotkeys;
    QVector<ActionSequence> sequences;
    bool legacyActions = false;
};

class ProfileManager final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY profilesChanged)
    Q_PROPERTY(QString activeProfileId READ activeProfileId NOTIFY activeProfileChanged)
    Q_PROPERTY(QString activeProfileName READ activeProfileName NOTIFY activeProfileNameChanged)
    Q_PROPERTY(QString activeScreenshotDirectory READ activeScreenshotDirectory NOTIFY activeProfileChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        DescriptionRole,
        ScreenshotDirectoryRole,
        ActivationKeyRole,
        ActivationModifiersRole,
        ActivationShortcutRole,
        IsDefaultRole,
        IsActiveRole,
        HotkeyCountRole,
        IconRole
    };
    Q_ENUM(Role)

    explicit ProfileManager(SettingsManager *settings, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString activeProfileId() const;
    QString activeProfileName() const;
    QString activeScreenshotDirectory() const;
    const QVector<Profile> &profiles() const;
    const Profile *profileById(const QString &id) const;
    Profile *profileById(const QString &id);
    const Profile *activeProfile() const;
    Q_INVOKABLE QVariantMap profileInfo(const QString &id) const;
    Q_INVOKABLE QVariantList profileOptions() const;

    Q_INVOKABLE bool setActiveProfile(const QString &id);
    Q_INVOKABLE QString createProfile(const QString &name);
    Q_INVOKABLE QString duplicateProfile(const QString &id);
    Q_INVOKABLE bool renameProfile(const QString &id, const QString &name);
    Q_INVOKABLE bool deleteProfile(const QString &id);
    Q_INVOKABLE bool setScreenshotDirectory(const QString &id, const QString &directory);
    Q_INVOKABLE bool setActivationShortcut(const QString &id, int key, int modifiers);
    bool addHotkey(const QString &profileId, const HotkeyAssignment &assignment);
    bool updateHotkey(const QString &profileId, const HotkeyAssignment &assignment);
    bool removeHotkey(const QString &profileId, const QString &assignmentId);
    bool replaceProfile(const Profile &profile);
    const Profile &globalProfile() const { return m_globalProfile; }
    QStringList startupMessages() const { return m_startupMessages; }
    Q_INVOKABLE bool setDetails(const QString &id, const QString &description, const QString &icon);
    bool exportProfile(const QString &id, const QString &path);
    QString importProfile(const QString &path);
    static Profile parseProfile(const QJsonObject &object, bool *valid);
    static QJsonObject serializeProfile(const Profile &profile);

signals:
    void profilesChanged();
    void activeProfileChanged();
    void activeProfileNameChanged();
    void profileDataChanged(const QString &profileId);
    void errorOccurred(const QString &message);

private:
    void loadProfiles();
    bool saveProfile(const Profile &profile) const;
    QString profilesDirectory() const;
    QString profileFilePath(const QString &id) const;
    void ensureDefaultProfile();
    void notifyProfileChanged(const QString &id);

    SettingsManager *m_settings = nullptr;
    QVector<Profile> m_profiles;
    Profile m_globalProfile;
    QString m_activeId;
    QStringList m_startupMessages;
    bool m_globalInvalid = false;
    bool m_globalFutureSchema = false;
};
