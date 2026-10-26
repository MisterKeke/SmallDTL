#include "CommandPaletteManager.h"

#include "AppDataPaths.h"
#include "../actions/ActionManager.h"
#include "../profiles/ProfileManager.h"
#include "../settings/SettingsManager.h"
#include "../storage/ApplicationDatabase.h"
#include "../windows/WindowArrangementManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <limits>
#include <utility>

namespace {
constexpr int kMaximumResults = 50;
constexpr int kMaximumRecent = 40;
constexpr int kDebounceMs = 110;

QString resultKey(const QString &type, const QString &id)
{
    return type + QLatin1Char(':') + id;
}

QVariantMap makeCandidate(const QString &type, const QString &id, const QString &name,
                          const QString &detail, const QString &action, const QString &scope,
                          const QString &path = {}, bool available = true,
                          const QString &reason = {}, bool missing = false,
                          const QString &searchable = {})
{
    return {{QStringLiteral("type"), type}, {QStringLiteral("id"), id},
            {QStringLiteral("name"), name}, {QStringLiteral("detail"), detail},
            {QStringLiteral("action"), action}, {QStringLiteral("scope"), scope},
            {QStringLiteral("path"), path}, {QStringLiteral("available"), available},
            {QStringLiteral("reason"), reason}, {QStringLiteral("missing"), missing},
            {QStringLiteral("_searchable"), searchable}};
}
}

CommandPaletteManager::CommandPaletteManager(ProfileManager *profiles, ActionManager *actions,
                                             WindowArrangementManager *layouts,
                                             ApplicationDatabase *database,
                                             SettingsManager *settings, QObject *parent)
    : QAbstractListModel(parent), m_profiles(profiles), m_actions(actions), m_layouts(layouts),
      m_database(database), m_settings(settings)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(kDebounceMs);
    connect(&m_debounce, &QTimer::timeout, this, &CommandPaletteManager::startSearch);
    if (m_profiles) {
        connect(m_profiles, &ProfileManager::profilesChanged, this, &CommandPaletteManager::refresh);
        connect(m_profiles, &ProfileManager::profileDataChanged, this, [this] { refresh(); });
        connect(m_profiles, &ProfileManager::activeProfileChanged, this, &CommandPaletteManager::refresh);
    }
    if (m_actions)
        connect(m_actions, &ActionManager::sequencesChanged, this, &CommandPaletteManager::refresh);
    if (m_layouts)
        connect(m_layouts, &WindowArrangementManager::layoutsChanged, this, &CommandPaletteManager::refresh);
    if (m_settings)
        connect(m_settings, &SettingsManager::preferencesChanged, this, &CommandPaletteManager::refresh);
    loadRecents();
}

int CommandPaletteManager::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_results.size();
}

QVariant CommandPaletteManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_results.size())
        return {};
    const QVariantMap &item = m_results.at(index.row());
    switch (role) {
    case TypeRole: return item.value(QStringLiteral("type"));
    case IdRole: return item.value(QStringLiteral("id"));
    case NameRole: return item.value(QStringLiteral("name"));
    case DetailRole: return item.value(QStringLiteral("detail"));
    case ActionRole: return item.value(QStringLiteral("action"));
    case AvailableRole: return item.value(QStringLiteral("available"));
    case MissingRole: return item.value(QStringLiteral("missing"));
    case ReasonRole: return item.value(QStringLiteral("reason"));
    case ScopeRole: return item.value(QStringLiteral("scope"));
    case PathRole: return item.value(QStringLiteral("path"));
    default: return {};
    }
}

QHash<int, QByteArray> CommandPaletteManager::roleNames() const
{
    return {{TypeRole, "type"}, {IdRole, "itemId"}, {NameRole, "name"},
            {DetailRole, "detail"}, {ActionRole, "action"},
            {AvailableRole, "available"}, {MissingRole, "missing"},
            {ReasonRole, "reason"}, {ScopeRole, "scope"}, {PathRole, "path"}};
}

QVariantMap CommandPaletteManager::resultAt(int row) const
{
    if (row < 0 || row >= m_results.size())
        return {};
    QVariantMap result = m_results.at(row);
    result.remove(QStringLiteral("_searchable"));
    return result;
}

void CommandPaletteManager::setQuery(const QString &query)
{
    if (m_query == query)
        return;
    m_query = query;
    emit queryChanged();
    ++m_generation;
    beginResetModel();
    m_results.clear();
    endResetModel();
    emit countChanged();
    emit resultsChanged();
    setBusy(true);
    if (m_messageIsError)
        setMessage({}, false);
    m_debounce.start();
}

void CommandPaletteManager::refresh()
{
    ++m_generation;
    beginResetModel();
    m_results.clear();
    endResetModel();
    emit countChanged();
    emit resultsChanged();
    setBusy(true);
    m_debounce.start();
}

void CommandPaletteManager::startSearch()
{
    const quint64 generation = m_generation;
    const QString query = m_query;
    QVector<QVariantMap> base;
    for (const Candidate &candidate : baseCandidates())
        base.append(candidate.value);
    const QString databasePath = m_database ? m_database->databasePath() : QString();
    const QStringList recent = m_recentKeys;
    auto *watcher = new QFutureWatcher<SearchBatch>(this);
    m_watchers.append(watcher);
    connect(watcher, &QFutureWatcher<SearchBatch>::finished, this, [this, watcher] {
        const SearchBatch batch = watcher->result();
        m_watchers.removeAll(watcher);
        watcher->deleteLater();
        if (batch.generation != m_generation)
            return;
        beginResetModel();
        m_results = batch.results;
        endResetModel();
        emit countChanged();
        emit resultsChanged();
        setBusy(false);
        if (!batch.error.isEmpty())
            setMessage(batch.error, true);
        else if (m_messageIsError)
            setMessage({}, false);
    });
    watcher->setFuture(QtConcurrent::run([query, generation, base, databasePath, recent] {
        return search(query, generation, base, databasePath, recent);
    }));
}

QVector<CommandPaletteManager::Candidate> CommandPaletteManager::baseCandidates() const
{
    QVector<Candidate> result;
    if (!m_profiles)
        return result;
    const QString activeId = m_profiles->activeProfileId();
    const Profile *active = m_profiles->activeProfile();
    const QString activeName = active ? active->name : QStringLiteral("Unavailable profile");

    for (const Profile &profile : m_profiles->profiles()) {
        const bool current = profile.id == activeId;
        const QString detail = current ? QStringLiteral("Active profile") : QStringLiteral("Profile");
        const QString reason = current ? QStringLiteral("This profile is already active") : QString();
        result.append({makeCandidate(QStringLiteral("profile"), profile.id, profile.name, detail,
                                    current ? QStringLiteral("Already active") : QStringLiteral("Activate profile"),
                                    QStringLiteral("Profile"), {}, !current, reason, false,
                                    profile.name + QLatin1Char(' ') + detail + QLatin1Char(' ') + reason),
                       profile.name, {}, !current, false});
    }

    auto addSequences = [this, &result, &activeId, &activeName](const QVector<ActionSequence> &sequences,
                                                               const QString &ownerId,
                                                               const QString &ownerName) {
        const bool inScope = ownerId == QStringLiteral("global") || ownerId == activeId;
        if (!inScope)
            return;
        const QString scope = ownerId == QStringLiteral("global") ? QStringLiteral("Application-wide") : ownerName;
        for (const ActionSequence &sequence : sequences) {
            QString reason;
            if (!sequence.enabled)
                reason = QStringLiteral("Sequence is disabled");
            else if (m_actions)
                reason = m_actions->validationError(sequence.id);
            const bool available = sequence.enabled && reason.isEmpty();
            const QString detail = QStringLiteral("Action sequence · %1").arg(scope);
            result.append({makeCandidate(QStringLiteral("sequence"), sequence.id, sequence.name, detail,
                                        available ? QStringLiteral("Run sequence") : QStringLiteral("Unavailable"),
                                        scope, {}, available, reason, false,
                                        sequence.name + QLatin1Char(' ') + detail + QLatin1Char(' ') + reason),
                           sequence.name, {}, available, false});
        }
    };
    addSequences(m_profiles->globalProfile().sequences, QStringLiteral("global"), QStringLiteral("Application-wide"));
    if (active)
        addSequences(active->sequences, activeId, activeName);

    if (m_layouts) {
        for (const QVariant &entry : m_layouts->layoutOptions()) {
            const QVariantMap layout = entry.toMap();
            const QString id = layout.value(QStringLiteral("id")).toString();
            const QString profileId = layout.value(QStringLiteral("profileId")).toString();
            const QString profileName = layout.value(QStringLiteral("profileName")).toString();
            const bool inScope = profileId == QStringLiteral("global") || profileId == activeId;
            const QString scope = profileId == QStringLiteral("global") ? QStringLiteral("Application-wide") : profileName;
            const QString reason = inScope ? QString() : QStringLiteral("This layout belongs to another profile");
            const QString name = layout.value(QStringLiteral("name")).toString();
            const QString detail = QStringLiteral("Window layout · %1").arg(scope);
            result.append({makeCandidate(QStringLiteral("layout"), id, name, detail,
                                        inScope ? QStringLiteral("Apply layout") : QStringLiteral("Unavailable"),
                                        scope, {}, inScope, reason, false,
                                        name + QLatin1Char(' ') + detail + QLatin1Char(' ') + reason),
                           name, {}, inScope, false});
        }
    }
    return result;
}

void CommandPaletteManager::remember(const QString &type, const QString &id)
{
    if (type.isEmpty() || id.isEmpty())
        return;
    const QString key = resultKey(type, id);
    m_recentKeys.removeAll(key);
    m_recentKeys.prepend(key);
    while (m_recentKeys.size() > kMaximumRecent)
        m_recentKeys.removeLast();
    saveRecents();
    refresh();
}

void CommandPaletteManager::setMessage(const QString &message, bool isError)
{
    if (m_message == message && m_messageIsError == isError)
        return;
    m_message = message;
    m_messageIsError = isError;
    emit messageChanged();
}

int CommandPaletteManager::scoreForTesting(const QString &query, const QString &name,
                                           const QString &searchableText, int recentRank)
{
    return score(query, name, searchableText, recentRank);
}

int CommandPaletteManager::score(const QString &query, const QString &name,
                                 const QString &searchableText, int recentRank)
{
    const QString foldedQuery = query.trimmed().toCaseFolded();
    if (foldedQuery.isEmpty())
        return recentRank < 0 ? 0 : 100000 - recentRank;
    const QString foldedName = name.toCaseFolded();
    const QString foldedSearchable = searchableText.toCaseFolded();
    const QStringList tokens = foldedQuery.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    for (const QString &token : tokens)
        if (!foldedSearchable.contains(token))
            return -1;

    int value = 800;
    if (foldedName == foldedQuery)
        value = 10000;
    else if (foldedName.startsWith(foldedQuery))
        value = 8000;
    else {
        bool wordPrefix = false;
        for (const QString &token : foldedName.split(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]+")), Qt::SkipEmptyParts))
            if (token.startsWith(foldedQuery)) { wordPrefix = true; break; }
        if (wordPrefix)
            value = 6000;
        else if (foldedName.contains(foldedQuery))
            value = 4000;
        else if (foldedSearchable.contains(foldedQuery))
            value = 1200;
    }
    if (recentRank >= 0)
        value += qMax(0, 200 - recentRank * 5);
    return value;
}

CommandPaletteManager::SearchBatch CommandPaletteManager::search(const QString &query,
                                                                 quint64 generation,
                                                                 const QVector<QVariantMap> &base,
                                                                 const QString &databasePath,
                                                                 const QStringList &recentKeys)
{
    SearchBatch batch;
    batch.generation = generation;
    QVector<QVariantMap> candidates = base;
    if (!databasePath.isEmpty()) {
        ApplicationDatabase database(databasePath);
        if (!database.initialize()) {
            batch.error = QStringLiteral("Search could not read local items: %1").arg(database.lastError());
        } else {
            const QVector<FileShelfCollection> collections = database.fileShelfCollections();
            QHash<QString, QString> collectionNames;
            for (const FileShelfCollection &collection : collections)
                collectionNames.insert(collection.id, collection.name);
            for (const ScreenshotRecord &record : database.screenshots()) {
                const QFileInfo file(record.path);
                const bool available = file.exists() && file.isFile();
                const QString name = file.fileName();
                const QString tags = record.tags.join(QStringLiteral(" "));
                const QString reason = available ? QString() : QStringLiteral("Screenshot file is missing");
                const QString detail = record.path + (tags.isEmpty() ? QString() : QStringLiteral(" · ") + tags);
                candidates.append(makeCandidate(QStringLiteral("screenshot"), record.id, name, detail,
                                                available ? QStringLiteral("Preview or edit") : QStringLiteral("Missing"),
                                                QStringLiteral("Screenshot"), record.path, available, reason,
                                                !available, name + QLatin1Char(' ') + record.path + QLatin1Char(' ') + tags));
            }
            for (const FileShelfRecord &record : database.fileShelfEntries()) {
                const QFileInfo file(record.path);
                const bool folder = record.itemType == QStringLiteral("folder");
                const bool available = file.exists() && (folder ? file.isDir() : file.isFile());
                QStringList names;
                for (const QString &collectionId : record.collectionIds)
                    if (collectionNames.contains(collectionId)) names.append(collectionNames.value(collectionId));
                const QString collectionText = names.join(QStringLiteral(", "));
                const QString name = record.displayName.isEmpty() ? file.fileName() : record.displayName;
                const QString reason = available ? QString() : QStringLiteral("Shelf reference is missing");
                const QString scope = collectionText.isEmpty() ? QStringLiteral("Uncollected") : collectionText;
                const QString detail = record.path + QStringLiteral(" · ") + scope + (record.favorite ? QStringLiteral(" · Favorite") : QString());
                candidates.append(makeCandidate(QStringLiteral("shelf"), record.id, name, detail,
                                                available ? QStringLiteral("Open reference") : QStringLiteral("Missing"),
                                                scope, record.path, available, reason, !available,
                                                name + QLatin1Char(' ') + record.path + QLatin1Char(' ') + scope));
            }
        }
    }

    const QString foldedQuery = query.trimmed().toCaseFolded();
    struct Ranked { int score; int recent; QString name; QVariantMap item; };
    QVector<Ranked> ranked;
    ranked.reserve(candidates.size());
    for (QVariantMap item : candidates) {
        const QString type = item.value(QStringLiteral("type")).toString();
        const QString id = item.value(QStringLiteral("id")).toString();
        const QString name = item.value(QStringLiteral("name")).toString();
        const QString searchable = item.value(QStringLiteral("_searchable")).toString();
        const int recent = recentKeys.indexOf(resultKey(type, id));
        const int score = CommandPaletteManager::score(foldedQuery, name, searchable, recent);
        if (score < 0)
            continue;
        ranked.append({score, recent < 0 ? std::numeric_limits<int>::max() : recent,
                       name.toCaseFolded(), std::move(item)});
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const Ranked &a, const Ranked &b) {
        if (a.score != b.score) return a.score > b.score;
        if (a.recent != b.recent) return a.recent < b.recent;
        if (a.name != b.name) return a.name < b.name;
        return a.item.value(QStringLiteral("id")).toString() < b.item.value(QStringLiteral("id")).toString();
    });
    const int count = qMin(kMaximumResults, ranked.size());
    batch.results.reserve(count);
    for (int i = 0; i < count; ++i) {
        QVariantMap item = std::move(ranked[i].item);
        item.remove(QStringLiteral("_searchable"));
        batch.results.append(std::move(item));
    }
    return batch;
}

void CommandPaletteManager::loadRecents()
{
    QFile file(QDir(AppDataPaths::root()).filePath(QStringLiteral("command-palette-recents.json")));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024)
        return;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray())
        return;
    for (const QJsonValue &value : document.array()) {
        if (!value.isString() || value.toString().size() > 1024)
            continue;
        const QString key = value.toString();
        if (!m_recentKeys.contains(key))
            m_recentKeys.append(key);
        if (m_recentKeys.size() >= kMaximumRecent)
            break;
    }
}

void CommandPaletteManager::saveRecents() const
{
    QSaveFile file(QDir(AppDataPaths::root()).filePath(QStringLiteral("command-palette-recents.json")));
    if (!file.open(QIODevice::WriteOnly))
        return;
    QJsonArray array;
    for (const QString &key : m_recentKeys)
        array.append(key);
    file.write(QJsonDocument(array).toJson(QJsonDocument::Compact));
    file.commit();
}

void CommandPaletteManager::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}
