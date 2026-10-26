#include "ApplicationDatabase.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFileInfo>
#include <QDir>
#include <QUuid>

namespace { QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); } }

QVector<FileShelfRecord> ApplicationDatabase::fileShelfEntries() const
{
    QVector<FileShelfRecord> records;
    if (!checkThread() || !m_ready)
        return records;
    QSqlQuery query(*m_database);
    if (!query.exec(QStringLiteral("SELECT id, path, added_at, display_name, source_folder, item_type, size_bytes, favorite "
                                   "FROM file_shelf ORDER BY added_at DESC, display_name COLLATE NOCASE"))) {
        transactionError(QStringLiteral("Could not load file shelf"), query.lastError().text());
        return records;
    }
    while (query.next()) {
        FileShelfRecord record;
        record.id = query.value(0).toString();
        record.path = query.value(1).toString();
        record.addedAt = query.value(2).toString();
        record.displayName = query.value(3).toString();
        record.sourceFolder = query.value(4).toString();
        record.itemType = query.value(5).toString();
        record.sizeBytes = query.value(6).toLongLong();
        record.favorite = query.value(7).toBool();
        records.append(record);
    }
    query.finish();
    QHash<QString, qsizetype> rowById;
    for (qsizetype i = 0; i < records.size(); ++i)
        rowById.insert(records.at(i).id, i);
    if (!query.exec(QStringLiteral("SELECT entry_id, collection_id FROM file_shelf_collection_items"))) {
        transactionError(QStringLiteral("Could not load file shelf collection memberships"), query.lastError().text());
        return {};
    }
    while (query.next()) {
        const auto found = rowById.constFind(query.value(0).toString());
        if (found != rowById.cend())
            records[*found].collectionIds.append(query.value(1).toString());
    }
    return records;
}

QVector<FileShelfCollection> ApplicationDatabase::fileShelfCollections() const
{
    QVector<FileShelfCollection> collections;
    if (!checkThread() || !m_ready)
        return collections;
    QSqlQuery query(*m_database);
    if (!query.exec(QStringLiteral("SELECT id,name,favorite,last_used_at FROM file_shelf_collections "
                                   "ORDER BY favorite DESC,last_used_at DESC,name COLLATE NOCASE"))) {
        transactionError(QStringLiteral("Could not load file shelf collections"), query.lastError().text());
        return collections;
    }
    while (query.next())
        collections.append({query.value(0).toString(), query.value(1).toString(),
                            query.value(2).toBool(), query.value(3).toString()});
    return collections;
}

bool ApplicationDatabase::addFileShelfEntries(const QVector<FileShelfRecord> &records,
                                              int *inserted, int *duplicates,
                                              const QString &collectionId)
{
    if (inserted)
        *inserted = 0;
    if (duplicates)
        *duplicates = 0;
    if (!checkThread() || !m_ready)
        return false;
    if (records.isEmpty())
        return true;
    if (!collectionId.isEmpty()) {
        QSqlQuery collection(*m_database);
        collection.prepare(QStringLiteral("SELECT 1 FROM file_shelf_collections WHERE id=?"));
        collection.addBindValue(collectionId);
        if (!collection.exec() || !collection.next())
            return transactionError(QStringLiteral("The selected file shelf collection no longer exists"), collection.lastError().text());
    }
    if (!m_database->transaction())
        return transactionError(QStringLiteral("Could not start file shelf update"), m_database->lastError().text());
    int addedCount = 0;
    int duplicateCount = 0;
    for (const FileShelfRecord &record : records) {
        const QString path = normalizedPath(record.path);
        const QString type = record.itemType == QStringLiteral("folder") ? QStringLiteral("folder") : QStringLiteral("file");
        QSqlQuery query(*m_database);
        query.prepare(QStringLiteral("INSERT OR IGNORE INTO file_shelf"
                                     "(id,path,path_key,added_at,display_name,source_folder,item_type,size_bytes)"
                                     " VALUES(?,?,?,?,?,?,?,?)"));
        query.addBindValue(record.id.isEmpty() ? newId() : record.id);
        query.addBindValue(path);
        query.addBindValue(normalizedPathKey(path));
        query.addBindValue(record.addedAt.isEmpty()
                               ? QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)
                               : record.addedAt);
        query.addBindValue(record.displayName.isEmpty() ? QFileInfo(path).fileName() : record.displayName);
        query.addBindValue(record.sourceFolder.isEmpty() ? QFileInfo(path).absolutePath() : normalizedPath(record.sourceFolder));
        query.addBindValue(type);
        query.addBindValue(qMax<qint64>(0, record.sizeBytes));
        if (!query.exec()) {
            const QString detail = query.lastError().text();
            m_database->rollback();
            return transactionError(QStringLiteral("Could not add a file shelf reference"), detail);
        }
        if (query.numRowsAffected() == 0)
            ++duplicateCount;
        else
            ++addedCount;
        if (!collectionId.isEmpty()) {
            QSqlQuery existing(*m_database);
            existing.prepare(QStringLiteral("SELECT id FROM file_shelf WHERE path_key=?"));
            existing.addBindValue(normalizedPathKey(path));
            if (!existing.exec() || !existing.next()) {
                const QString detail = existing.lastError().text();
                m_database->rollback();
                return transactionError(QStringLiteral("Could not find the file shelf item for its collection"), detail);
            }
            QSqlQuery membership(*m_database);
            membership.prepare(QStringLiteral("INSERT OR IGNORE INTO file_shelf_collection_items(entry_id,collection_id) VALUES(?,?)"));
            membership.addBindValue(existing.value(0).toString());
            membership.addBindValue(collectionId);
            if (!membership.exec()) {
                const QString detail = membership.lastError().text();
                m_database->rollback();
                return transactionError(QStringLiteral("Could not add the file shelf collection membership"), detail);
            }
        }
    }
    if (!collectionId.isEmpty()) {
        QSqlQuery touch(*m_database);
        touch.prepare(QStringLiteral("UPDATE file_shelf_collections SET last_used_at=? WHERE id=?"));
        touch.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
        touch.addBindValue(collectionId);
        if (!touch.exec()) {
            const QString detail = touch.lastError().text();
            m_database->rollback();
            return transactionError(QStringLiteral("Could not update recent file shelf collection"), detail);
        }
    }
    if (!m_database->commit()) {
        const QString detail = m_database->lastError().text();
        m_database->rollback();
        return transactionError(QStringLiteral("Could not commit file shelf references"), detail);
    }
    if (inserted)
        *inserted = addedCount;
    if (duplicates)
        *duplicates = duplicateCount;
    m_lastError.clear();
    return true;
}

QString ApplicationDatabase::createFileShelfCollection(const QString &name)
{
    if (!checkThread() || !m_ready)
        return {};
    const QString cleanName = name.trimmed();
    if (cleanName.isEmpty() || cleanName.size() > 100) {
        transactionError(QStringLiteral("Collection names must contain between 1 and 100 characters"));
        return {};
    }
    const QString id = newId();
    QSqlQuery query(*m_database);
    query.prepare(QStringLiteral("INSERT INTO file_shelf_collections(id,name,last_used_at) VALUES(?,?,?)"));
    query.addBindValue(id);
    query.addBindValue(cleanName);
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    if (!query.exec()) {
        transactionError(QStringLiteral("Could not create file shelf collection"), query.lastError().text());
        return {};
    }
    m_lastError.clear();
    return id;
}

bool ApplicationDatabase::renameFileShelfCollection(const QString &id, const QString &name)
{
    if (!checkThread() || !m_ready)
        return false;
    const QString cleanName = name.trimmed();
    if (cleanName.isEmpty() || cleanName.size() > 100)
        return transactionError(QStringLiteral("Collection names must contain between 1 and 100 characters"));
    QSqlQuery query(*m_database);
    query.prepare(QStringLiteral("UPDATE file_shelf_collections SET name=? WHERE id=?"));
    query.addBindValue(cleanName);
    query.addBindValue(id);
    if (!query.exec() || query.numRowsAffected() != 1)
        return transactionError(QStringLiteral("Could not rename file shelf collection"), query.lastError().text());
    m_lastError.clear();
    return true;
}

bool ApplicationDatabase::deleteFileShelfCollection(const QString &id)
{
    if (!checkThread() || !m_ready)
        return false;
    QSqlQuery query(*m_database);
    query.prepare(QStringLiteral("DELETE FROM file_shelf_collections WHERE id=?"));
    query.addBindValue(id);
    if (!query.exec() || query.numRowsAffected() != 1)
        return transactionError(QStringLiteral("Could not delete file shelf collection"), query.lastError().text());
    m_lastError.clear();
    return true;
}

bool ApplicationDatabase::setFileShelfCollectionFavorite(const QString &id, bool favorite)
{
    if (!checkThread() || !m_ready)
        return false;
    QSqlQuery query(*m_database);
    query.prepare(QStringLiteral("UPDATE file_shelf_collections SET favorite=? WHERE id=?"));
    query.addBindValue(favorite);
    query.addBindValue(id);
    if (!query.exec() || query.numRowsAffected() != 1)
        return transactionError(QStringLiteral("Could not update collection favorite"), query.lastError().text());
    m_lastError.clear();
    return true;
}

bool ApplicationDatabase::setFileShelfFavorite(const QString &id, bool favorite)
{
    if (!checkThread() || !m_ready)
        return false;
    QSqlQuery query(*m_database);
    query.prepare(QStringLiteral("UPDATE file_shelf SET favorite=? WHERE id=?"));
    query.addBindValue(favorite);
    query.addBindValue(id);
    if (!query.exec() || query.numRowsAffected() != 1)
        return transactionError(QStringLiteral("Could not update file shelf favorite"), query.lastError().text());
    m_lastError.clear();
    return true;
}

bool ApplicationDatabase::setFileShelfCollectionMembership(const QStringList &entryIds,
                                                            const QString &collectionId, bool assigned)
{
    if (!checkThread() || !m_ready)
        return false;
    if (entryIds.isEmpty())
        return true;
    if (!m_database->transaction())
        return transactionError(QStringLiteral("Could not start collection membership update"), m_database->lastError().text());
    for (const QString &entryId : entryIds) {
        QSqlQuery query(*m_database);
        if (assigned) {
            query.prepare(QStringLiteral("INSERT OR IGNORE INTO file_shelf_collection_items(entry_id,collection_id) "
                                         "SELECT id,? FROM file_shelf WHERE id=?"));
            query.addBindValue(collectionId);
            query.addBindValue(entryId);
        } else {
            query.prepare(QStringLiteral("DELETE FROM file_shelf_collection_items WHERE entry_id=? AND collection_id=?"));
            query.addBindValue(entryId);
            query.addBindValue(collectionId);
        }
        if (!query.exec()) {
            const QString detail = query.lastError().text();
            m_database->rollback();
            return transactionError(QStringLiteral("Could not update file shelf collection memberships"), detail);
        }
    }
    if (!m_database->commit()) {
        const QString detail = m_database->lastError().text();
        m_database->rollback();
        return transactionError(QStringLiteral("Could not commit file shelf collection memberships"), detail);
    }
    m_lastError.clear();
    return true;
}

bool ApplicationDatabase::touchFileShelfCollection(const QString &id)
{
    if (!checkThread() || !m_ready)
        return false;
    QSqlQuery query(*m_database);
    query.prepare(QStringLiteral("UPDATE file_shelf_collections SET last_used_at=? WHERE id=?"));
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    query.addBindValue(id);
    if (!query.exec() || query.numRowsAffected() != 1)
        return transactionError(QStringLiteral("Could not update recent file shelf collection"), query.lastError().text());
    m_lastError.clear();
    return true;
}

bool ApplicationDatabase::removeFileShelfEntries(const QStringList &ids, int *removed)
{
    if (removed)
        *removed = 0;
    if (!checkThread() || !m_ready)
        return false;
    if (ids.isEmpty())
        return true;
    if (!m_database->transaction())
        return transactionError(QStringLiteral("Could not start file shelf removal"), m_database->lastError().text());
    int removedCount = 0;
    for (const QString &id : ids) {
        QSqlQuery query(*m_database);
        query.prepare(QStringLiteral("DELETE FROM file_shelf WHERE id=?"));
        query.addBindValue(id);
        if (!query.exec()) {
            const QString detail = query.lastError().text();
            m_database->rollback();
            return transactionError(QStringLiteral("Could not remove a file shelf reference"), detail);
        }
        removedCount += query.numRowsAffected();
    }
    if (!m_database->commit()) {
        const QString detail = m_database->lastError().text();
        m_database->rollback();
        return transactionError(QStringLiteral("Could not commit file shelf removal"), detail);
    }
    if (removed)
        *removed = removedCount;
    m_lastError.clear();
    return true;
}

bool ApplicationDatabase::clearFileShelf(int *removed)
{
    if (removed)
        *removed = 0;
    if (!checkThread() || !m_ready)
        return false;
    QSqlQuery query(*m_database);
    if (!query.exec(QStringLiteral("DELETE FROM file_shelf")))
        return transactionError(QStringLiteral("Could not clear file shelf"), query.lastError().text());
    if (removed)
        *removed = query.numRowsAffected();
    m_lastError.clear();
    return true;
}

bool ApplicationDatabase::updateFileShelfPath(const QString &id, const QString &path)
{
    if (!checkThread() || !m_ready)
        return false;
    const QString normalizedValue = normalizedPath(path);
    const QFileInfo info(normalizedValue);
    if (!info.exists() || (!info.isFile() && !info.isDir()))
        return transactionError(QStringLiteral("The moved file shelf destination is unavailable"));
    QSqlQuery collision(*m_database);
    collision.prepare(QStringLiteral("SELECT id FROM file_shelf WHERE path_key=? AND id<>?"));
    collision.addBindValue(normalizedPathKey(normalizedValue));
    collision.addBindValue(id);
    if (!collision.exec())
        return transactionError(QStringLiteral("Could not check the replacement file shelf path"), collision.lastError().text());
    if (collision.next())
        return transactionError(QStringLiteral("A shelf reference already exists at the replacement path. Both items and their organization were preserved; choose another location or remove the existing reference explicitly."));
    QSqlQuery query(*m_database);
    query.prepare(QStringLiteral("UPDATE file_shelf SET path=?, path_key=?, display_name=?, source_folder=?, item_type=?, size_bytes=? WHERE id=?"));
    query.addBindValue(normalizedValue);
    query.addBindValue(normalizedPathKey(normalizedValue));
    query.addBindValue(info.fileName().isEmpty() ? normalizedValue : info.fileName());
    query.addBindValue(info.absolutePath());
    query.addBindValue(info.isDir() ? QStringLiteral("folder") : QStringLiteral("file"));
    query.addBindValue(info.isFile() ? info.size() : 0);
    query.addBindValue(id);
    if (!query.exec() || query.numRowsAffected() != 1)
        return transactionError(QStringLiteral("Could not update the moved file shelf reference"), query.lastError().text());
    m_lastError.clear();
    return true;
}
