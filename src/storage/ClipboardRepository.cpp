#include "ApplicationDatabase.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFileInfo>
#include <QDir>
#include <QUuid>
#include <QUrl>

namespace {
QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QStringList fileUrlsFromJson(const QString &json)
{
    QStringList urls;
    const auto document = QJsonDocument::fromJson(json.toUtf8());
    for (const auto &value : document.array()) if (value.isString()) urls.append(value.toString());
    return urls;
}
}

bool ApplicationDatabase::clipboardHistoryFilesMissing(const QString &id) const
{
    if (!checkThread() || !m_ready) return true;
    QSqlQuery query(*m_database);
    query.setForwardOnly(true);
    query.prepare(QStringLiteral("SELECT value FROM clipboard_history,json_each(file_urls) WHERE clipboard_history.id=?"));
    query.addBindValue(id);
    if (!query.exec()) return true;
    while (query.next()) {
        const auto url = QUrl::fromEncoded(query.value(0).toString().toUtf8());
        if (url.isLocalFile() && !QFileInfo::exists(url.toLocalFile())) return true;
    }
    return false;
}

QVector<ClipboardHistoryRecord> ApplicationDatabase::clipboardHistory(const QString &search, int limit,
                                                                      bool previewsOnly, const QString &id) const
{
    QVector<ClipboardHistoryRecord> result;
    if (!checkThread() || !m_ready)
        return result;
    QSqlQuery query(*m_database);
    query.setForwardOnly(true);
    query.prepare(QStringLiteral("SELECT id, content_type, captured_at, fingerprint, %1, %2, payload_path, size_bytes, width, height, pinned, json_array_length(file_urls) "
                                 "FROM clipboard_history WHERE (?='' OR text_content LIKE ? OR file_urls LIKE ?) AND (?='' OR id=?) "
                                 "ORDER BY pinned DESC, captured_at DESC, id LIMIT ?")
                      .arg(previewsOnly ? QStringLiteral("substr(text_content,1,240)") : QStringLiteral("text_content"),
                           previewsOnly ? QStringLiteral("(SELECT json_group_array(value) FROM (SELECT value FROM json_each(file_urls) LIMIT 3))")
                                        : QStringLiteral("file_urls")));
    query.addBindValue(search);
    query.addBindValue(QStringLiteral("%") + search + QLatin1Char('%'));
    query.addBindValue(QStringLiteral("%") + search + QLatin1Char('%'));
    // A default-constructed QString binds as SQL NULL. The optional ID filter
    // must bind a non-null empty string so an unfiltered page matches rows.
    query.addBindValue(id.isEmpty() ? QStringLiteral("") : id);
    query.addBindValue(id.isEmpty() ? QStringLiteral("") : id);
    query.addBindValue(limit);
    if (!query.exec()) {
        transactionError(QStringLiteral("Could not read clipboard history"), query.lastError().text());
        return result;
    }
    while (query.next()) {
        ClipboardHistoryRecord record;
        record.id = query.value(0).toString();
        record.type = query.value(1).toString();
        record.capturedAt = query.value(2).toString();
        record.fingerprint = query.value(3).toString();
        record.text = query.value(4).toString();
        record.fileUrls = fileUrlsFromJson(query.value(5).toString());
        record.payloadPath = query.value(6).toString();
        record.sizeBytes = query.value(7).toLongLong();
        record.width = query.value(8).toInt();
        record.height = query.value(9).toInt();
        record.pinned = query.value(10).toBool();
        record.fileCount = query.value(11).toInt();
        result.append(record);
    }
    m_lastError.clear();
    return result;
}

bool ApplicationDatabase::saveClipboardHistory(const ClipboardHistoryRecord &record, QString *id, bool *inserted)
{
    if (!checkThread() || !m_ready)
        return false;
    if (record.fingerprint.isEmpty() ||
        !QStringList{QStringLiteral("text"), QStringLiteral("image"), QStringLiteral("files")}.contains(record.type))
        return transactionError(QStringLiteral("Clipboard history record is invalid"));
    if (!m_database->transaction())
        return transactionError(QStringLiteral("Could not start clipboard history save"), m_database->lastError().text());
    QSqlQuery existing(*m_database);
    existing.prepare(QStringLiteral("SELECT id FROM clipboard_history WHERE fingerprint=?"));
    existing.addBindValue(record.fingerprint);
    if (!existing.exec()) {
        const QString detail = existing.lastError().text();
        m_database->rollback();
        return transactionError(QStringLiteral("Could not check clipboard history duplicates"), detail);
    }
    const bool hasExisting = existing.next();
    const QString existingId = hasExisting ? existing.value(0).toString() : QString();
    existing.finish();
    if (hasExisting) {
        QSqlQuery touch(*m_database);
        touch.prepare(QStringLiteral("UPDATE clipboard_history SET captured_at=CASE WHEN captured_at<? THEN ? ELSE captured_at END WHERE id=?"));
        touch.addBindValue(record.capturedAt);
        touch.addBindValue(record.capturedAt);
        touch.addBindValue(existingId);
        if (!touch.exec() || !m_database->commit()) {
            const QString detail = touch.lastError().text().isEmpty() ? m_database->lastError().text() : touch.lastError().text();
            m_database->rollback();
            return transactionError(QStringLiteral("Could not update duplicate clipboard history entry"), detail);
        }
        if (id)
            *id = existingId;
        if (inserted)
            *inserted = false;
        return true;
    }
    QJsonArray urls;
    for (const QString &url : record.fileUrls)
        urls.append(url);
    QSqlQuery insert(*m_database);
    insert.prepare(QStringLiteral("INSERT INTO clipboard_history(id, content_type, captured_at, fingerprint, text_content, file_urls, payload_path, size_bytes, width, height, pinned) "
                                  "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    const QString recordId = record.id.isEmpty() ? newId() : record.id;
    insert.addBindValue(recordId);
    insert.addBindValue(record.type);
    insert.addBindValue(record.capturedAt);
    insert.addBindValue(record.fingerprint);
    insert.addBindValue(record.text.isNull() ? QStringLiteral("") : record.text);
    insert.addBindValue(QString::fromUtf8(QJsonDocument(urls).toJson(QJsonDocument::Compact)));
    insert.addBindValue(record.payloadPath.isNull() ? QStringLiteral("") : record.payloadPath);
    insert.addBindValue(qMax<qint64>(0, record.sizeBytes));
    insert.addBindValue(qMax(0, record.width));
    insert.addBindValue(qMax(0, record.height));
    insert.addBindValue(record.pinned);
    if (!insert.exec() || !m_database->commit()) {
        const QString detail = insert.lastError().text().isEmpty() ? m_database->lastError().text() : insert.lastError().text();
        m_database->rollback();
        return transactionError(QStringLiteral("Could not save clipboard history entry"), detail);
    }
    if (id)
        *id = recordId;
    if (inserted)
        *inserted = true;
    return true;
}

bool ApplicationDatabase::setClipboardHistoryPinned(const QString &id, bool pinned)
{
    if (!checkThread() || !m_ready)
        return false;
    QSqlQuery query(*m_database);
    query.prepare(QStringLiteral("UPDATE clipboard_history SET pinned=? WHERE id=?"));
    query.addBindValue(pinned);
    query.addBindValue(id);
    if (!query.exec() || query.numRowsAffected() != 1)
        return transactionError(QStringLiteral("Could not update clipboard history pin"), query.lastError().text());
    return true;
}

bool ApplicationDatabase::deleteClipboardHistory(const QString &id, ClipboardHistoryRecord *removed)
{
    QVector<ClipboardHistoryRecord> records;
    if (!deleteClipboardHistory(QStringList{id}, &records))
        return false;
    if (records.isEmpty())
        return transactionError(QStringLiteral("Clipboard history entry was not found"));
    if (removed)
        *removed = records.first();
    return true;
}

bool ApplicationDatabase::deleteClipboardHistory(const QStringList &ids, QVector<ClipboardHistoryRecord> *removed)
{
    if (!checkThread() || !m_ready)
        return false;
    if (ids.isEmpty())
        return true;
    if (!m_database->transaction())
        return transactionError(QStringLiteral("Could not start clipboard history removal"), m_database->lastError().text());
    QVector<ClipboardHistoryRecord> records;
    for (const QString &id : ids) {
        QSqlQuery find(*m_database);
        // Deletion only needs identity and owned image paths, not multi-MiB text
        // or file URL payloads. Keep removal cost independent of their size.
        find.prepare(QStringLiteral("SELECT id, content_type, captured_at, fingerprint, '', '[]', payload_path, size_bytes, width, height, pinned FROM clipboard_history WHERE id=?"));
        find.addBindValue(id);
        if (!find.exec()) {
            const QString detail = find.lastError().text();
            m_database->rollback();
            return transactionError(QStringLiteral("Could not read clipboard history before removal"), detail);
        }
        if (!find.next())
            continue;
        ClipboardHistoryRecord record;
        record.id = find.value(0).toString(); record.type = find.value(1).toString();
        record.capturedAt = find.value(2).toString(); record.fingerprint = find.value(3).toString();
        record.text = find.value(4).toString(); record.fileUrls = fileUrlsFromJson(find.value(5).toString());
        record.payloadPath = find.value(6).toString(); record.sizeBytes = find.value(7).toLongLong();
        record.width = find.value(8).toInt(); record.height = find.value(9).toInt(); record.pinned = find.value(10).toBool();
        find.finish();
        records.append(record);
        QSqlQuery remove(*m_database);
        remove.prepare(QStringLiteral("DELETE FROM clipboard_history WHERE id=?"));
        remove.addBindValue(id);
        if (!remove.exec()) {
            const QString detail = remove.lastError().text();
            m_database->rollback();
            return transactionError(QStringLiteral("Could not remove clipboard history entry"), detail);
        }
    }
    if (!m_database->commit()) {
        const QString detail = m_database->lastError().text();
        m_database->rollback();
        return transactionError(QStringLiteral("Could not commit clipboard history removal"), detail);
    }
    if (removed)
        *removed = records;
    return true;
}

