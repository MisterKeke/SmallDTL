#include "ScreenshotDatabase.h"

#include "../app/AppDataPaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QUuid>

namespace {
QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString timestampForPath(const QFileInfo &file)
{
    const QDateTime modified = file.exists() ? file.lastModified() : QDateTime();
    return modified.isValid() ? modified.toUTC().toString(Qt::ISODateWithMs) : QStringLiteral("");
}
}

ScreenshotDatabase::ScreenshotDatabase(const QString &databasePath, QObject *parent)
    : QObject(parent), m_databasePath(databasePath),
      m_connectionName(QStringLiteral("SmallDTL-ScreenshotDb-") + newId()),
      m_ownerThread(QThread::currentThread())
{
    if (m_databasePath.isEmpty())
        m_databasePath = QDir(AppDataPaths::root()).filePath(QStringLiteral("screenshots.db"));
    m_databasePath = QDir::cleanPath(QFileInfo(m_databasePath).absoluteFilePath());
}

ScreenshotDatabase::~ScreenshotDatabase()
{
    if (!m_database)
        return;
    if (QThread::currentThread() != m_ownerThread)
        qFatal("ScreenshotDatabase must be destroyed on its owning thread.");
    m_database->close();
    delete m_database;
    m_database = nullptr;
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool ScreenshotDatabase::checkThread() const
{
    if (QThread::currentThread() == m_ownerThread)
        return true;
    m_lastError = QStringLiteral("Screenshot database access must stay on its owning thread.");
    return false;
}

bool ScreenshotDatabase::initialize(const QString &legacyIndexPath)
{
    if (!checkThread())
        return false;
    if (m_ready)
        return true;
    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        m_lastError = QStringLiteral("The SQLite driver (QSQLITE) is unavailable.");
        return false;
    }
    const QFileInfo databaseInfo(m_databasePath);
    if (!QDir().mkpath(databaseInfo.absolutePath())) {
        m_lastError = QStringLiteral("Could not create the screenshot database directory: %1")
                          .arg(databaseInfo.absolutePath());
        return false;
    }
    m_database = new QSqlDatabase(QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName));
    m_database->setDatabaseName(m_databasePath);
    if (!m_database->open()) {
        m_lastError = QStringLiteral("Could not open screenshot database: %1").arg(m_database->lastError().text());
        return false;
    }
    QSqlQuery foreignKeys(*m_database);
    if (!foreignKeys.exec(QStringLiteral("PRAGMA foreign_keys = ON")))
        return transactionError(QStringLiteral("Could not enable screenshot database foreign keys"), foreignKeys.lastError().text());
    if (!foreignKeys.exec(QStringLiteral("PRAGMA foreign_keys")) || !foreignKeys.next() || foreignKeys.value(0).toInt() != 1)
        return transactionError(QStringLiteral("Screenshot database foreign-key enforcement is unavailable"), foreignKeys.lastError().text());

    if (!migrateSchema() || !importLegacyIndex(legacyIndexPath))
        return false;
    m_ready = true;
    m_lastError.clear();
    return true;
}

bool ScreenshotDatabase::migrateSchema()
{
    QSqlQuery versionQuery(*m_database);
    if (!versionQuery.exec(QStringLiteral("PRAGMA user_version")) || !versionQuery.next())
        return transactionError(QStringLiteral("Could not read screenshot database schema version"), versionQuery.lastError().text());
    int version = versionQuery.value(0).toInt();
    if (version > 3)
        return transactionError(QStringLiteral("Screenshot database was created by a newer SmallDTL version"));

    while (version < 3) {
        if (!m_database->transaction())
            return transactionError(QStringLiteral("Could not start screenshot database migration"), m_database->lastError().text());
        QSqlQuery query(*m_database);
        bool ok = true;
        if (version == 0) {
            ok = query.exec(QStringLiteral("CREATE TABLE screenshots ("
                                           "id TEXT PRIMARY KEY NOT NULL,"
                                           "path TEXT NOT NULL COLLATE NOCASE UNIQUE,"
                                           "captured_at TEXT NOT NULL DEFAULT '',"
                                           "width INTEGER NOT NULL DEFAULT 0 CHECK(width >= 0),"
                                           "height INTEGER NOT NULL DEFAULT 0 CHECK(height >= 0),"
                                           "size_bytes INTEGER NOT NULL DEFAULT 0 CHECK(size_bytes >= 0),"
                                           "source TEXT NOT NULL DEFAULT ''"
                                           ")")) &&
                 query.exec(QStringLiteral("CREATE TABLE tags ("
                                           "id TEXT PRIMARY KEY NOT NULL,"
                                           "name TEXT NOT NULL COLLATE NOCASE UNIQUE"
                                           ")")) &&
                 query.exec(QStringLiteral("CREATE TABLE screenshot_tags ("
                                           "screenshot_id TEXT NOT NULL REFERENCES screenshots(id) ON DELETE CASCADE,"
                                           "tag_id TEXT NOT NULL REFERENCES tags(id) ON DELETE CASCADE,"
                                           "PRIMARY KEY(screenshot_id, tag_id)"
                                           ")")) &&
                 query.exec(QStringLiteral("CREATE TABLE migration_state (key TEXT PRIMARY KEY NOT NULL, value TEXT NOT NULL)")) &&
                 query.exec(QStringLiteral("PRAGMA user_version = 1"));
            if (ok)
                version = 1;
        } else if (version == 1) {
            ok = query.exec(QStringLiteral("CREATE INDEX screenshots_captured_at_idx ON screenshots(captured_at DESC)")) &&
                 query.exec(QStringLiteral("CREATE INDEX screenshot_tags_tag_idx ON screenshot_tags(tag_id, screenshot_id)")) &&
                 query.exec(QStringLiteral("PRAGMA user_version = 2"));
            if (ok)
                version = 2;
        } else if (version == 2) {
            ok = query.exec(QStringLiteral("ALTER TABLE screenshots ADD COLUMN source_screenshot_id TEXT "
                                           "REFERENCES screenshots(id) ON DELETE SET NULL")) &&
                 query.exec(QStringLiteral("PRAGMA user_version = 3"));
            if (ok)
                version = 3;
        }
        if (!ok) {
            const QString detail = query.lastError().text();
            m_database->rollback();
            return transactionError(QStringLiteral("Screenshot database migration failed"), detail);
        }
        if (!m_database->commit()) {
            const QString detail = m_database->lastError().text();
            m_database->rollback();
            return transactionError(QStringLiteral("Could not commit screenshot database migration"), detail);
        }
    }
    return true;
}

bool ScreenshotDatabase::importLegacyIndex(const QString &legacyIndexPath)
{
    if (legacyIndexPath.isEmpty() || !QFileInfo::exists(legacyIndexPath))
        return true;

    QSqlQuery marker(*m_database);
    marker.prepare(QStringLiteral("SELECT value FROM migration_state WHERE key = ?"));
    marker.addBindValue(QStringLiteral("screenshot-index-json-v1"));
    if (!marker.exec())
        return transactionError(QStringLiteral("Could not inspect legacy screenshot migration state"), marker.lastError().text());
    if (marker.next())
        return true;

    QFile file(legacyIndexPath);
    if (!file.open(QIODevice::ReadOnly))
        return transactionError(QStringLiteral("Could not read legacy screenshot index; it was preserved"), file.errorString());
    const QByteArray original = file.readAll();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(original, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject() ||
        document.object().value(QStringLiteral("schemaVersion")).toInt() != 1 ||
        !document.object().value(QStringLiteral("paths")).isArray()) {
        return transactionError(QStringLiteral("Legacy screenshot index is invalid; it was preserved"), parseError.errorString());
    }
    const QJsonArray paths = document.object().value(QStringLiteral("paths")).toArray();
    for (const QJsonValue &value : paths) {
        if (!value.isString())
            return transactionError(QStringLiteral("Legacy screenshot index contains a non-string path; it was preserved"));
    }

    if (!m_database->transaction())
        return transactionError(QStringLiteral("Could not start legacy screenshot import"), m_database->lastError().text());
    for (const QJsonValue &value : paths) {
        const QString rawPath = value.toString();
        if (rawPath.isEmpty())
            continue;
        const QString path = normalizedPath(rawPath);
        QSqlQuery existing(*m_database);
        existing.prepare(QStringLiteral("SELECT 1 FROM screenshots WHERE path=?"));
        existing.addBindValue(path);
        if (!existing.exec()) {
            const QString detail = existing.lastError().text();
            m_database->rollback();
            return transactionError(QStringLiteral("Could not check a legacy screenshot entry; the original index was preserved"), detail);
        }
        if (existing.next())
            continue;
        const QFileInfo fileInfo(path);
        QImageReader reader(path);
        const QSize dimensions = fileInfo.exists() && fileInfo.isFile() ? reader.size() : QSize();
        QSqlQuery insert(*m_database);
        insert.prepare(QStringLiteral("INSERT INTO screenshots"
                                      "(id, path, captured_at, width, height, size_bytes, source)"
                                      " VALUES(?, ?, ?, ?, ?, ?, ?)"));
        insert.bindValue(0, newId());
        insert.bindValue(1, path);
        insert.bindValue(2, timestampForPath(fileInfo));
        insert.bindValue(3, qMax(0, dimensions.width()));
        insert.bindValue(4, qMax(0, dimensions.height()));
        insert.bindValue(5, fileInfo.exists() && fileInfo.isFile() ? fileInfo.size() : 0);
        insert.bindValue(6, QStringLiteral("legacy-index"));
        if (!insert.exec()) {
            const QString detail = insert.lastError().text();
            m_database->rollback();
            return transactionError(QStringLiteral("Could not import a legacy screenshot entry; the original index was preserved"), detail);
        }
    }
    QSqlQuery saveMarker(*m_database);
    saveMarker.prepare(QStringLiteral("INSERT INTO migration_state(key, value) VALUES(?, ?)"));
    saveMarker.addBindValue(QStringLiteral("screenshot-index-json-v1"));
    saveMarker.addBindValue(QString::number(paths.size()));
    if (!saveMarker.exec() || !m_database->commit()) {
        const QString detail = saveMarker.lastError().text().isEmpty() ? m_database->lastError().text() : saveMarker.lastError().text();
        m_database->rollback();
        return transactionError(QStringLiteral("Could not commit legacy screenshot import; it will be retried"), detail);
    }
    return true;
}

QString ScreenshotDatabase::normalizedPath(const QString &path) const
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool ScreenshotDatabase::transactionError(const QString &context, const QString &detail) const
{
    m_lastError = detail.isEmpty() ? context : QStringLiteral("%1: %2").arg(context, detail);
    return false;
}

QVector<ScreenshotRecord> ScreenshotDatabase::screenshots(const QString &directory, const QString &tagId) const
{
    QVector<ScreenshotRecord> result;
    if (!checkThread() || !m_ready)
        return result;
    QSqlQuery query(*m_database);
    const QString sql = tagId.isEmpty()
        ? QStringLiteral("SELECT id, path, captured_at, width, height, size_bytes, source, source_screenshot_id FROM screenshots ORDER BY captured_at DESC, path COLLATE NOCASE")
        : QStringLiteral("SELECT s.id, s.path, s.captured_at, s.width, s.height, s.size_bytes, s.source, s.source_screenshot_id FROM screenshots s "
                         "WHERE EXISTS(SELECT 1 FROM screenshot_tags st WHERE st.screenshot_id=s.id AND st.tag_id=?) "
                         "ORDER BY s.captured_at DESC, s.path COLLATE NOCASE");
    query.prepare(sql);
    if (!tagId.isEmpty())
        query.addBindValue(tagId);
    if (!query.exec()) {
        transactionError(QStringLiteral("Could not read screenshot records"), query.lastError().text());
        return result;
    }
    while (query.next()) {
        ScreenshotRecord record;
        record.id = query.value(0).toString();
        record.path = query.value(1).toString();
        record.timestamp = query.value(2).toString();
        record.width = query.value(3).toInt();
        record.height = query.value(4).toInt();
        record.sizeBytes = query.value(5).toLongLong();
        record.source = query.value(6).toString();
        record.sourceScreenshotId = query.value(7).toString();
        if (!directory.isEmpty() && QFileInfo(record.path).absolutePath().compare(directory, Qt::CaseInsensitive) != 0)
            continue;
        result.append(record);
    }
    QSqlQuery tagQuery(*m_database);
    if (!tagQuery.exec(QStringLiteral("SELECT screenshot_id, tags.name FROM screenshot_tags "
                                      "JOIN tags ON tags.id=screenshot_tags.tag_id ORDER BY tags.name COLLATE NOCASE"))) {
        transactionError(QStringLiteral("Could not read screenshot tags"), tagQuery.lastError().text());
        return result;
    }
    QHash<QString, int> rowForId;
    for (int i = 0; i < result.size(); ++i)
        rowForId.insert(result.at(i).id, i);
    while (tagQuery.next()) {
        const auto it = rowForId.constFind(tagQuery.value(0).toString());
        if (it != rowForId.cend())
            result[*it].tags.append(tagQuery.value(1).toString());
    }
    return result;
}

ScreenshotRecord ScreenshotDatabase::screenshotById(const QString &id) const
{
    if (!checkThread() || !m_ready)
        return {};
    QSqlQuery query(*m_database);
    query.prepare(QStringLiteral("SELECT id, path, captured_at, width, height, size_bytes, source, source_screenshot_id "
                                 "FROM screenshots WHERE id=?"));
    query.addBindValue(id);
    if (!query.exec() || !query.next())
        return {};
    ScreenshotRecord record;
    record.id = query.value(0).toString();
    record.path = query.value(1).toString();
    record.timestamp = query.value(2).toString();
    record.width = query.value(3).toInt();
    record.height = query.value(4).toInt();
    record.sizeBytes = query.value(5).toLongLong();
    record.source = query.value(6).toString();
    record.sourceScreenshotId = query.value(7).toString();
    QSqlQuery tagsQuery(*m_database);
    tagsQuery.prepare(QStringLiteral("SELECT tags.name FROM screenshot_tags JOIN tags ON tags.id=screenshot_tags.tag_id "
                                     "WHERE screenshot_tags.screenshot_id=? ORDER BY tags.name COLLATE NOCASE"));
    tagsQuery.addBindValue(id);
    if (tagsQuery.exec())
        while (tagsQuery.next())
            record.tags.append(tagsQuery.value(0).toString());
    return record;
}

QVector<ScreenshotTag> ScreenshotDatabase::tags() const
{
    QVector<ScreenshotTag> result;
    if (!checkThread() || !m_ready)
        return result;
    QSqlQuery query(*m_database);
    if (!query.exec(QStringLiteral("SELECT id, name FROM tags ORDER BY name COLLATE NOCASE"))) {
        transactionError(QStringLiteral("Could not read tags"), query.lastError().text());
        return result;
    }
    while (query.next())
        result.append({query.value(0).toString(), query.value(1).toString()});
    return result;
}

QString ScreenshotDatabase::screenshotIdForPath(const QString &path) const
{
    if (!checkThread() || !m_ready)
        return {};
    QSqlQuery query(*m_database);
    query.prepare(QStringLiteral("SELECT id FROM screenshots WHERE path = ?"));
    query.addBindValue(normalizedPath(path));
    if (!query.exec() || !query.next())
        return {};
    return query.value(0).toString();
}

bool ScreenshotDatabase::addScreenshot(const QString &path, const QSize &dimensions, qint64 sizeBytes,
                                       const QString &source, QString *id,
                                       const QString &sourceScreenshotId, bool inheritTags)
{
    if (!checkThread() || !m_ready)
        return false;
    const QString normalized = normalizedPath(path);
    const QString existing = screenshotIdForPath(normalized);
    if (!existing.isEmpty()) {
        if (id)
            *id = existing;
        return true;
    }
    const QFileInfo file(normalized);
    if (!sourceScreenshotId.isEmpty() && screenshotById(sourceScreenshotId).id.isEmpty())
        return transactionError(QStringLiteral("Source screenshot record was not found"));
    if (!m_database->transaction())
        return transactionError(QStringLiteral("Could not start screenshot registration"), m_database->lastError().text());
    QSqlQuery query(*m_database);
    query.prepare(QStringLiteral("INSERT INTO screenshots(id, path, captured_at, width, height, size_bytes, source, source_screenshot_id) "
                                 "VALUES(?, ?, ?, ?, ?, ?, ?, ?)"));
    const QString screenshotId = newId();
    query.addBindValue(screenshotId);
    query.addBindValue(normalized);
    query.addBindValue(timestampForPath(file));
    query.addBindValue(qMax(0, dimensions.width()));
    query.addBindValue(qMax(0, dimensions.height()));
    query.addBindValue(qMax<qint64>(0, sizeBytes));
    query.addBindValue(source);
    query.addBindValue(sourceScreenshotId.isEmpty() ? QVariant() : QVariant(sourceScreenshotId));
    if (!query.exec()) {
        m_database->rollback();
        return transactionError(QStringLiteral("Could not add screenshot to the gallery"), query.lastError().text());
    }
    if (inheritTags) {
        QSqlQuery tagsQuery(*m_database);
        tagsQuery.prepare(QStringLiteral("INSERT INTO screenshot_tags(screenshot_id, tag_id) "
                                         "SELECT ?, tag_id FROM screenshot_tags WHERE screenshot_id=?"));
        tagsQuery.addBindValue(screenshotId);
        tagsQuery.addBindValue(sourceScreenshotId);
        if (sourceScreenshotId.isEmpty() || !tagsQuery.exec()) {
            const QString detail = tagsQuery.lastError().text();
            m_database->rollback();
            return transactionError(QStringLiteral("Could not copy source screenshot tags"), detail);
        }
    }
    if (!m_database->commit()) {
        const QString detail = m_database->lastError().text();
        m_database->rollback();
        return transactionError(QStringLiteral("Could not commit screenshot registration"), detail);
    }
    if (id)
        *id = screenshotId;
    return true;
}

bool ScreenshotDatabase::updateScreenshotPath(const QString &id, const QString &path)
{
    if (!checkThread() || !m_ready)
        return false;
    QSqlQuery query(*m_database);
    query.prepare(QStringLiteral("UPDATE screenshots SET path = ? WHERE id = ?"));
    query.addBindValue(normalizedPath(path));
    query.addBindValue(id);
    if (!query.exec() || query.numRowsAffected() != 1)
        return transactionError(QStringLiteral("Could not update screenshot path"), query.lastError().text());
    return true;
}

bool ScreenshotDatabase::updateScreenshotMetadata(const QString &id, const QString &timestamp,
                                                   const QSize &dimensions, qint64 sizeBytes)
{
    if (!checkThread() || !m_ready)
        return false;
    QSqlQuery query(*m_database);
    query.prepare(QStringLiteral("UPDATE screenshots SET captured_at=?, width=?, height=?, size_bytes=? WHERE id=?"));
    query.addBindValue(timestamp);
    query.addBindValue(qMax(0, dimensions.width()));
    query.addBindValue(qMax(0, dimensions.height()));
    query.addBindValue(qMax<qint64>(0, sizeBytes));
    query.addBindValue(id);
    if (!query.exec())
        return transactionError(QStringLiteral("Could not update screenshot metadata"), query.lastError().text());
    return true;
}

bool ScreenshotDatabase::removeScreenshot(const QString &id, ScreenshotRecord *removed)
{
    if (!checkThread() || !m_ready)
        return false;
    const auto existing = screenshots();
    ScreenshotRecord record;
    bool found = false;
    for (const auto &item : existing) {
        if (item.id == id) {
            record = item;
            found = true;
            break;
        }
    }
    if (!found)
        return transactionError(QStringLiteral("Screenshot record was not found"));
    if (!m_database->transaction())
        return transactionError(QStringLiteral("Could not start screenshot removal"), m_database->lastError().text());
    QSqlQuery query(*m_database);
    query.prepare(QStringLiteral("DELETE FROM screenshots WHERE id=?"));
    query.addBindValue(id);
    if (!query.exec() || query.numRowsAffected() != 1 || !m_database->commit()) {
        const QString detail = query.lastError().text().isEmpty() ? m_database->lastError().text() : query.lastError().text();
        m_database->rollback();
        return transactionError(QStringLiteral("Could not remove screenshot from gallery"), detail);
    }
    if (removed)
        *removed = record;
    return true;
}

bool ScreenshotDatabase::restoreScreenshot(const ScreenshotRecord &record)
{
    if (!checkThread() || !m_ready)
        return false;
    if (!m_database->transaction())
        return transactionError(QStringLiteral("Could not start screenshot restoration"), m_database->lastError().text());
    QSqlQuery insert(*m_database);
    insert.prepare(QStringLiteral("INSERT INTO screenshots(id, path, captured_at, width, height, size_bytes, source, source_screenshot_id) VALUES(?, ?, ?, ?, ?, ?, ?, ?)"));
    insert.addBindValue(record.id);
    insert.addBindValue(normalizedPath(record.path));
    insert.addBindValue(record.timestamp);
    insert.addBindValue(record.width);
    insert.addBindValue(record.height);
    insert.addBindValue(record.sizeBytes);
    insert.addBindValue(record.source);
    insert.addBindValue(record.sourceScreenshotId.isEmpty() ? QVariant() : QVariant(record.sourceScreenshotId));
    if (!insert.exec()) {
        m_database->rollback();
        return transactionError(QStringLiteral("Could not restore screenshot record"), insert.lastError().text());
    }
    // Removed records carry their tags for restoration in the record snapshot.
    QSqlQuery tagsQuery(*m_database);
    QString placeholders;
    for (int i = 0; i < record.tags.size(); ++i)
        placeholders += (i ? QStringLiteral(",?") : QStringLiteral("?"));
    tagsQuery.prepare(QStringLiteral("INSERT INTO screenshot_tags(screenshot_id, tag_id) "
                                    "SELECT ?, id FROM tags WHERE name IN (%1)").arg(placeholders.isEmpty() ? QStringLiteral("NULL") : placeholders));
    tagsQuery.addBindValue(record.id);
    for (const QString &tag : record.tags)
        tagsQuery.addBindValue(tag);
    if (!tagsQuery.exec() || !m_database->commit()) {
        const QString detail = tagsQuery.lastError().text().isEmpty() ? m_database->lastError().text() : tagsQuery.lastError().text();
        m_database->rollback();
        return transactionError(QStringLiteral("Could not restore screenshot tags"), detail);
    }
    return true;
}

QString ScreenshotDatabase::createTag(const QString &name)
{
    if (!checkThread() || !m_ready)
        return {};
    const QString cleaned = name.trimmed();
    if (cleaned.isEmpty()) {
        transactionError(QStringLiteral("Tag name cannot be empty"));
        return {};
    }
    const QString id = newId();
    QSqlQuery query(*m_database);
    query.prepare(QStringLiteral("INSERT INTO tags(id, name) VALUES(?, ?)"));
    query.addBindValue(id);
    query.addBindValue(cleaned);
    if (!query.exec()) {
        transactionError(QStringLiteral("Could not create tag"), query.lastError().text());
        return {};
    }
    return id;
}

bool ScreenshotDatabase::renameTag(const QString &id, const QString &name)
{
    if (!checkThread() || !m_ready)
        return false;
    const QString cleaned = name.trimmed();
    if (cleaned.isEmpty())
        return transactionError(QStringLiteral("Tag name cannot be empty"));
    QSqlQuery query(*m_database);
    query.prepare(QStringLiteral("UPDATE tags SET name=? WHERE id=?"));
    query.addBindValue(cleaned);
    query.addBindValue(id);
    if (!query.exec() || query.numRowsAffected() != 1)
        return transactionError(QStringLiteral("Could not rename tag"), query.lastError().text());
    return true;
}

bool ScreenshotDatabase::deleteTag(const QString &id)
{
    if (!checkThread() || !m_ready)
        return false;
    QSqlQuery query(*m_database);
    query.prepare(QStringLiteral("DELETE FROM tags WHERE id=?"));
    query.addBindValue(id);
    if (!query.exec() || query.numRowsAffected() != 1)
        return transactionError(QStringLiteral("Could not delete tag"), query.lastError().text());
    return true;
}

int ScreenshotDatabase::tagAssignmentState(const QString &tagId, const QStringList &screenshotIds) const
{
    if (!checkThread() || !m_ready || screenshotIds.isEmpty())
        return 0;
    QSqlQuery query(*m_database);
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM screenshot_tags WHERE tag_id=? AND screenshot_id=?"));
    int assignedCount = 0;
    for (const QString &id : screenshotIds) {
        query.bindValue(0, tagId);
        query.bindValue(1, id);
        if (!query.exec() || !query.next()) {
            transactionError(QStringLiteral("Could not read tag assignment state"), query.lastError().text());
            return 0;
        }
        if (query.value(0).toInt() > 0)
            ++assignedCount;
    }
    if (assignedCount == 0)
        return 0;
    return assignedCount == screenshotIds.size() ? 1 : 2;
}

bool ScreenshotDatabase::setTagForScreenshots(const QStringList &screenshotIds, const QString &tagId, bool assigned)
{
    if (!checkThread() || !m_ready)
        return false;
    if (screenshotIds.isEmpty())
        return true;
    if (!m_database->transaction())
        return transactionError(QStringLiteral("Could not start bulk tag update"), m_database->lastError().text());
    QSqlQuery query(*m_database);
    query.prepare(assigned
                      ? QStringLiteral("INSERT OR IGNORE INTO screenshot_tags(screenshot_id, tag_id) VALUES(?, ?)")
                      : QStringLiteral("DELETE FROM screenshot_tags WHERE screenshot_id=? AND tag_id=?"));
    for (const QString &id : screenshotIds) {
        query.bindValue(0, id);
        query.bindValue(1, tagId);
        if (!query.exec()) {
            const QString detail = query.lastError().text();
            m_database->rollback();
            return transactionError(QStringLiteral("Bulk tag update failed; no assignments were changed"), detail);
        }
    }
    if (!m_database->commit()) {
        const QString detail = m_database->lastError().text();
        m_database->rollback();
        return transactionError(QStringLiteral("Bulk tag update could not be committed"), detail);
    }
    return true;
}
