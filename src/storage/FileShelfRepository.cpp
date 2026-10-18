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
    if (!query.exec(QStringLiteral("SELECT id, path, added_at, display_name, source_folder, item_type, size_bytes "
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
        records.append(record);
    }
    return records;
}

bool ApplicationDatabase::addFileShelfEntries(const QVector<FileShelfRecord> &records,
                                              int *inserted, int *duplicates)
{
    if (inserted)
        *inserted = 0;
    if (duplicates)
        *duplicates = 0;
    if (!checkThread() || !m_ready)
        return false;
    if (records.isEmpty())
        return true;
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
