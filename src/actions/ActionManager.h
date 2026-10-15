#pragma once

#include <QAbstractListModel>
#include <QJsonObject>
#include <QVariantList>
#include <QVector>
#include "ActionDefinition.h"

class ProfileManager;

class ActionManager final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY sequencesChanged)
    Q_PROPERTY(bool globalScope READ globalScope WRITE setGlobalScope NOTIFY sequencesChanged)

public:
    enum Role { IdRole = Qt::UserRole + 1, NameRole, EnabledRole, StepCountRole };
    Q_ENUM(Role)

    explicit ActionManager(ProfileManager *profiles, QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE bool contains(const QString &sequenceId) const;
    const ActionSequence *sequenceById(const QString &sequenceId) const;
    Q_INVOKABLE QVariantList sequenceOptions() const;
    Q_INVOKABLE QVariantMap sequenceInfo(const QString &sequenceId) const;
    Q_INVOKABLE QVariantList steps(const QString &sequenceId) const;
    QString validateSequence(const QString &sequenceId, QString *error = nullptr) const;

    Q_INVOKABLE QString createSequence(const QString &name);
    Q_INVOKABLE bool renameSequence(const QString &sequenceId, const QString &name);
    Q_INVOKABLE bool deleteSequence(const QString &sequenceId);
    Q_INVOKABLE bool setSequenceEnabled(const QString &sequenceId, bool enabled);
    Q_INVOKABLE QString addStep(const QString &sequenceId, const QString &type);
    Q_INVOKABLE bool updateStep(const QString &sequenceId, const QString &stepId,
                                const QVariantMap &changes);
    Q_INVOKABLE bool setStepEnabled(const QString &sequenceId, const QString &stepId, bool enabled);
    Q_INVOKABLE bool moveStep(const QString &sequenceId, const QString &stepId, int direction);
    Q_INVOKABLE bool removeStep(const QString &sequenceId, const QString &stepId);
    Q_INVOKABLE QString validationError(const QString &sequenceId) const;
    bool globalScope() const { return m_globalScope; }
    Q_INVOKABLE void setGlobalScope(bool global);
    QString sequenceOwner(const QString &id) const;
    QStringList startupMessages() const { return m_startupMessages; }
    static QJsonObject serializeSequence(const ActionSequence &sequence);
    static bool parseSequence(const QJsonObject &object, ActionSequence *sequence);

signals:
    void sequencesChanged();
    void errorOccurred(const QString &message);

private:
    int indexOfSequence(const QString &sequenceId) const;
    static int indexOfStep(const ActionSequence &sequence, const QString &stepId);
    bool commitChange(int sequenceIndex, const ActionSequence &updated);
    void load();
    QString configPath() const;
    QVariantMap stepToVariant(const ActionStep &step) const;
    QString scopeId() const;
    void refreshModel();

    ProfileManager *m_profiles = nullptr;
    QVector<ActionSequence> m_sequences;
    bool m_globalScope = false;
    QStringList m_startupMessages;
};
