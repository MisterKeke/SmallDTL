#include "ApplicationDatabase.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>

QVector<ClipboardHistoryRecord> ApplicationDatabase::clipboardHistoryMetadata() const
{
    QVector<ClipboardHistoryRecord> result;
    if (!checkThread() || !m_ready) return result;
    QSqlQuery query(*m_database);
    query.setForwardOnly(true);
    if (!query.exec(QStringLiteral("SELECT id,content_type,captured_at,payload_path,size_bytes,pinned FROM clipboard_history"))) {
        transactionError(QStringLiteral("Could not read history metadata"), query.lastError().text());
        return result;
    }
    while (query.next()) {
        ClipboardHistoryRecord record;
        record.id = query.value(0).toString();
        record.type = query.value(1).toString();
        record.capturedAt = query.value(2).toString();
        record.payloadPath = query.value(3).toString();
        record.sizeBytes = query.value(4).toLongLong();
        record.pinned = query.value(5).toBool();
        result.append(record);
    }
    m_lastError.clear();
    return result;
}

int ApplicationDatabase::clipboardHistoryCount(bool includePinned) const
{
    if (!checkThread() || !m_ready) return 0;
    QSqlQuery query(*m_database);
    if (!query.exec(includePinned ? QStringLiteral("SELECT count(*) FROM clipboard_history")
                                  : QStringLiteral("SELECT count(*) FROM clipboard_history WHERE pinned=0")) || !query.next()) {
        transactionError(QStringLiteral("Could not count clipboard history"), query.lastError().text());
        return 0;
    }
    return query.value(0).toInt();
}

bool ApplicationDatabase::compact()
{
    if (!checkThread() || !m_ready) return false;
    QSqlQuery query(*m_database);
    if (!query.exec(QStringLiteral("VACUUM")))
        return transactionError(QStringLiteral("Could not compact application storage"), query.lastError().text());
    return true;
}
