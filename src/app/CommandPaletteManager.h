#pragma once

#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QTimer>
#include <QVariantMap>
#include <QStringList>

class ActionManager;
class ApplicationDatabase;
class ProfileManager;
class SettingsManager;
class WindowArrangementManager;

class CommandPaletteManager final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString query READ query NOTIFY queryChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool messageIsError READ messageIsError NOTIFY messageChanged)

public:
    enum Role { TypeRole = Qt::UserRole + 1, IdRole, NameRole, DetailRole, ActionRole,
                AvailableRole, MissingRole, ReasonRole, ScopeRole, PathRole };
    Q_ENUM(Role)

    explicit CommandPaletteManager(ProfileManager *profiles, ActionManager *actions,
                                   WindowArrangementManager *layouts, ApplicationDatabase *database,
                                   SettingsManager *settings, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool busy() const { return m_busy; }
    QString query() const { return m_query; }
    QString message() const { return m_message; }
    bool messageIsError() const { return m_messageIsError; }
    Q_INVOKABLE QVariantMap resultAt(int row) const;
    QStringList recentKeys() const { return m_recentKeys; }

    Q_INVOKABLE void setQuery(const QString &query);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void remember(const QString &type, const QString &id);
    Q_INVOKABLE void setMessage(const QString &message, bool isError = false);
    Q_INVOKABLE static int scoreForTesting(const QString &query, const QString &name,
                                           const QString &searchableText, int recentRank = -1);

signals:
    void countChanged();
    void busyChanged();
    void queryChanged();
    void resultsChanged();
    void messageChanged();

private:
    struct Candidate {
        QVariantMap value;
        QString name;
        QString searchable;
        bool available = true;
        bool missing = false;
    };
    struct SearchBatch {
        quint64 generation = 0;
        QVector<QVariantMap> results;
        QString error;
    };

    void startSearch();
    QVector<Candidate> baseCandidates() const;
    void loadRecents();
    void saveRecents() const;
    void setBusy(bool busy);
    static int score(const QString &query, const QString &name,
                     const QString &searchableText, int recentRank);
    static SearchBatch search(const QString &query, quint64 generation,
                              const QVector<QVariantMap> &base,
                              const QString &databasePath, const QStringList &recentKeys);

    ProfileManager *m_profiles = nullptr;
    ActionManager *m_actions = nullptr;
    WindowArrangementManager *m_layouts = nullptr;
    ApplicationDatabase *m_database = nullptr;
    SettingsManager *m_settings = nullptr;
    QTimer m_debounce;
    QString m_query;
    QString m_message;
    bool m_messageIsError = false;
    bool m_busy = false;
    quint64 m_generation = 0;
    QStringList m_recentKeys;
    QVector<QVariantMap> m_results;
    QVector<QFutureWatcher<SearchBatch> *> m_watchers;
};
