#include "ClipboardHistoryManager.h"

#include "../app/AppDataPaths.h"
#include "../screenshots/ScreenshotManager.h"
#include "../settings/SettingsManager.h"

#include <QCryptographicHash>
#include <QByteArrayView>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QRegularExpression>
#include <QUrl>
#include <QUuid>
#include <QtConcurrent>

namespace {
constexpr qint64 MaximumImageBytes = 256LL * 1024 * 1024;
constexpr qint64 MaximumImagePixels = 64LL * 1024 * 1024;
constexpr qint64 MaximumPendingImageBytes = 512LL * 1024 * 1024;
constexpr int MaximumImageQueue = 8;

QString utcNow()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QString filePreview(const ClipboardHistoryRecord &record)
{
    QStringList names;
    for (const QString &encoded : record.fileUrls) {
        const QUrl url = QUrl::fromEncoded(encoded.toUtf8());
        const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
        names.append(QFileInfo(path).fileName().isEmpty() ? path : QFileInfo(path).fileName());
        if (names.size() == 3)
            break;
    }
    return names.join(QStringLiteral(", "));
}

bool isLocalUrlMissing(const QString &encoded)
{
    const QUrl url = QUrl::fromEncoded(encoded.toUtf8());
    return url.isLocalFile() && !QFileInfo::exists(url.toLocalFile());
}
}

ClipboardHistoryModel::ClipboardHistoryModel(QObject *parent) : QAbstractListModel(parent) {}

int ClipboardHistoryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_records.size();
}

QVariant ClipboardHistoryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_records.size())
        return {};
    const ClipboardHistoryRecord &record = m_records.at(index.row());
    switch (role) {
    case EntryIdRole: return record.id;
    case TypeRole: return record.type;
    case CapturedAtRole: return QDateTime::fromString(record.capturedAt, Qt::ISODateWithMs).toLocalTime().toString(QStringLiteral("yyyy-MM-dd  HH:mm:ss"));
    case PreviewRole:
        if (record.type == QStringLiteral("text"))
            return record.text.left(240);
        if (record.type == QStringLiteral("files"))
            return filePreview(record);
        return QStringLiteral("Image · %1 × %2 · %3 KB").arg(record.width).arg(record.height).arg(record.sizeBytes / 1024);
    case ImageUrlRole: return record.type == QStringLiteral("image") && !record.missing && !record.payloadPath.isEmpty() ? QUrl::fromLocalFile(record.payloadPath) : QUrl();
    case FileCountRole: return record.fileUrls.size();
    case PinnedRole: return record.pinned;
    case MissingRole: return record.missing;
    case SizeBytesRole: return record.sizeBytes;
    default: return {};
    }
}

QHash<int, QByteArray> ClipboardHistoryModel::roleNames() const
{
    return {{EntryIdRole, "entryId"}, {TypeRole, "contentType"}, {CapturedAtRole, "capturedAt"},
            {PreviewRole, "preview"}, {ImageUrlRole, "imageUrl"}, {FileCountRole, "fileCount"},
            {PinnedRole, "pinned"}, {MissingRole, "missing"}, {SizeBytesRole, "sizeBytes"}};
}

void ClipboardHistoryModel::setRecords(const QVector<ClipboardHistoryRecord> &records)
{
    beginResetModel();
    m_records = records;
    endResetModel();
    emit countChanged();
}

ClipboardHistoryRecord ClipboardHistoryModel::recordAt(int row) const
{
    return row >= 0 && row < m_records.size() ? m_records.at(row) : ClipboardHistoryRecord{};
}

ClipboardHistoryRecord ClipboardHistoryModel::recordById(const QString &id) const
{
    for (const ClipboardHistoryRecord &record : m_records)
        if (record.id == id)
            return record;
    return {};
}

ClipboardHistoryManager::ClipboardHistoryManager(ScreenshotManager *screenshots, SettingsManager *settings, QObject *parent)
    : QObject(parent), m_screenshots(screenshots), m_settings(settings),
      m_database(screenshots ? screenshots->database() : nullptr),
      m_clipboard(screenshots ? screenshots->clipboardService() : nullptr),
      m_model(this), m_storageDirectory(QDir(AppDataPaths::root()).filePath(QStringLiteral("clipboard-history")))
{
    connect(&m_model, &ClipboardHistoryModel::countChanged, this, &ClipboardHistoryManager::countChanged);
    if (m_clipboard)
        connect(m_clipboard, &ClipboardService::externalContentAvailable,
                this, &ClipboardHistoryManager::acceptSnapshot);
    if (m_settings)
        connect(m_settings, &SettingsManager::preferencesChanged, this, &ClipboardHistoryManager::cleanupRetention);
    refresh();
    cleanupRetention();
}

ClipboardHistoryManager::~ClipboardHistoryManager()
{
    finishPendingSaves();
}

void ClipboardHistoryManager::setSearch(const QString &search)
{
    const QString cleaned = search.trimmed();
    if (m_search == cleaned)
        return;
    m_search = cleaned;
    emit searchChanged();
    refresh();
}

void ClipboardHistoryManager::refresh()
{
    if (!m_database || !m_database->isReady()) {
        m_model.setRecords({});
        return;
    }
    QVector<ClipboardHistoryRecord> records = m_database->clipboardHistory(m_search);
    for (ClipboardHistoryRecord &record : records) {
        if (record.type == QStringLiteral("image"))
            record.missing = !ownsPayload(record) || !QFileInfo::exists(record.payloadPath);
        else if (record.type == QStringLiteral("files"))
            record.missing = hasMissingFiles(record);
    }
    m_model.setRecords(records);
}

void ClipboardHistoryManager::setPaused(bool paused)
{
    if (m_paused == paused)
        return;
    m_paused = paused;
    emit pausedChanged();
    emit messageChanged(paused ? QStringLiteral("Clipboard history paused.") : QStringLiteral("Clipboard history resumed."), false);
}

void ClipboardHistoryManager::acceptSnapshot(const ClipboardSnapshot &snapshot)
{
    if (m_paused || !m_database || !m_database->isReady())
        return;
    if (snapshot.type == QStringLiteral("image")) {
        const QImage image = snapshot.image;
        if (image.isNull() || image.width() <= 0 || image.height() <= 0 ||
            qint64(image.width()) * image.height() > MaximumImagePixels || image.sizeInBytes() > MaximumImageBytes) {
            emit messageChanged(QStringLiteral("Clipboard image exceeds the 64 megapixel / 256 MiB history limit."), true);
            return;
        }
        const qint64 bytes = image.sizeInBytes();
        if (m_pendingImageBytes + bytes > MaximumPendingImageBytes ||
            (m_activeImageJobs >= 2 && m_imageQueue.size() >= MaximumImageQueue)) {
            emit messageChanged(QStringLiteral("Clipboard history image queue is full; this image was not recorded."), true);
            return;
        }
        PendingImage event;
        event.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        event.image = image;
        event.capturedAt = utcNow();
        event.reservedBytes = bytes;
        m_imageReservations.insert(event.id, bytes);
        m_pendingImageBytes += bytes;
        m_imageQueue.enqueue(event);
        dispatchImageWrites();
        return;
    }

    ClipboardHistoryRecord record;
    record.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    record.capturedAt = utcNow();
    if (snapshot.type == QStringLiteral("text")) {
        const QByteArray bytes = snapshot.text.toUtf8();
        if (bytes.size() > 4 * 1024 * 1024) {
            emit messageChanged(QStringLiteral("Clipboard text exceeds the 4 MiB history limit."), true);
            return;
        }
        record.type = QStringLiteral("text");
        record.text = snapshot.text;
        record.fingerprint = QString::fromLatin1(QCryptographicHash::hash(QByteArrayLiteral("text:") + bytes, QCryptographicHash::Sha256).toHex());
        record.sizeBytes = bytes.size();
    } else if (snapshot.type == QStringLiteral("files")) {
        if (snapshot.urls.isEmpty())
            return;
        QJsonArray urls;
        for (const QUrl &url : snapshot.urls)
            urls.append(url.toString(QUrl::FullyEncoded));
        const QByteArray canonical = QJsonDocument(urls).toJson(QJsonDocument::Compact);
        if (canonical.size() > 4 * 1024 * 1024) {
            emit messageChanged(QStringLiteral("Clipboard file list exceeds the 4 MiB history limit."), true);
            return;
        }
        record.type = QStringLiteral("files");
        for (const QUrl &url : snapshot.urls)
            record.fileUrls.append(url.toString(QUrl::FullyEncoded));
        record.fingerprint = QString::fromLatin1(QCryptographicHash::hash(QByteArrayLiteral("files:") + canonical, QCryptographicHash::Sha256).toHex());
        record.sizeBytes = canonical.size();
    } else {
        return;
    }
    if (!m_database->saveClipboardHistory(record)) {
        emit messageChanged(QStringLiteral("Could not save clipboard history: %1").arg(m_database->lastError()), true);
        return;
    }
    cleanupRetention();
    refresh();
}

void ClipboardHistoryManager::dispatchImageWrites()
{
    while (m_activeImageJobs < 2 && !m_imageQueue.isEmpty()) {
        const PendingImage event = m_imageQueue.dequeue();
        auto *watcher = new QFutureWatcher<ImageResult>(this);
        m_imageWatchers.insert(event.id, watcher);
        connect(watcher, &QFutureWatcher<ImageResult>::finished, this, [this, watcher, id = event.id] {
            if (!m_imageJobs.contains(id)) {
                watcher->deleteLater();
                return;
            }
            const ImageResult result = watcher->result();
            m_imageJobs.remove(id);
            m_imageWatchers.remove(id);
            watcher->deleteLater();
            completeImageWrite(id, result);
        });
        const QFuture<ImageResult> future = QtConcurrent::run([event, directory = m_storageDirectory] {
            return writeHistoryImage(event, directory);
        });
        m_imageJobs.insert(event.id, future);
        ++m_activeImageJobs;
        watcher->setFuture(future);
    }
}

ClipboardHistoryManager::ImageResult ClipboardHistoryManager::writeHistoryImage(const PendingImage &event,
                                                                                  const QString &storageDirectory)
{
    ImageResult result;
    QImage image = event.image.convertToFormat(QImage::Format_RGBA8888);
    if (image.isNull() || qint64(image.width()) * image.height() > MaximumImagePixels || image.sizeInBytes() > MaximumImageBytes) {
        result.error = QStringLiteral("Clipboard image exceeds the 64 megapixel / 256 MiB history limit.");
        return result;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArrayLiteral("image:"));
    hash.addData(QByteArray::number(image.width()));
    hash.addData(QByteArrayLiteral("x"));
    hash.addData(QByteArray::number(image.height()));
    const qsizetype rowBytes = qsizetype(image.width()) * 4;
    for (int y = 0; y < image.height(); ++y)
        hash.addData(QByteArrayView(reinterpret_cast<const char *>(image.constScanLine(y)), rowBytes));

    QDir().mkpath(storageDirectory);
    const QString path = QDir(storageDirectory).filePath(event.id + QStringLiteral(".png"));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        result.error = file.errorString();
        return result;
    }
    QImageWriter writer(&file, QByteArrayLiteral("png"));
    if (!writer.write(image) || !file.commit()) {
        result.error = writer.errorString().isEmpty() ? file.errorString() : writer.errorString();
        file.cancelWriting();
        return result;
    }
    result.record.id = event.id;
    result.record.type = QStringLiteral("image");
    result.record.capturedAt = event.capturedAt;
    result.record.fingerprint = QString::fromLatin1(hash.result().toHex());
    result.record.payloadPath = QFileInfo(path).absoluteFilePath();
    result.record.sizeBytes = QFileInfo(path).size();
    result.record.width = image.width();
    result.record.height = image.height();
    return result;
}

void ClipboardHistoryManager::completeImageWrite(const QString &id, const ImageResult &result)
{
    --m_activeImageJobs;
    const auto watcher = m_imageWatchers.take(id);
    if (watcher)
        watcher->deleteLater();
    m_pendingImageBytes -= m_imageReservations.take(id);
    if (!result.error.isEmpty()) {
        emit messageChanged(QStringLiteral("Could not save clipboard history image: %1").arg(result.error), true);
        dispatchImageWrites();
        return;
    }
    QString savedId;
    bool inserted = false;
    if (!m_database->saveClipboardHistory(result.record, &savedId, &inserted)) {
        if (ownsPayload(result.record))
            QFile::remove(result.record.payloadPath);
        emit messageChanged(QStringLiteral("Could not save clipboard history: %1").arg(m_database->lastError()), true);
    } else if (!inserted && ownsPayload(result.record)) {
        QFile::remove(result.record.payloadPath);
    }
    cleanupRetention();
    refresh();
    dispatchImageWrites();
}

void ClipboardHistoryManager::cleanupRetention()
{
    if (!m_database || !m_database->isReady() || !m_settings)
        return;
    const QVector<ClipboardHistoryRecord> records = m_database->clipboardHistory();
    if (records.isEmpty())
        return;
    const QDateTime cutoff = QDateTime::currentDateTimeUtc().addDays(-m_settings->clipboardHistoryRetentionDays());
    QStringList removeIds;
    QSet<QString> selected;
    qint64 bytes = 0;
    for (const ClipboardHistoryRecord &record : records) {
        if (record.type == QStringLiteral("image"))
            bytes += record.sizeBytes;
        const QDateTime captured = QDateTime::fromString(record.capturedAt, Qt::ISODateWithMs).toUTC();
        if (!record.pinned && captured.isValid() && captured < cutoff) {
            selected.insert(record.id);
            removeIds.append(record.id);
        }
    }
    int count = records.size() - selected.size();
    const qint64 maximumBytes = qint64(m_settings->clipboardHistoryStorageLimitMB()) * 1024 * 1024;
    QVector<ClipboardHistoryRecord> oldest = records;
    std::stable_sort(oldest.begin(), oldest.end(), [](const auto &a, const auto &b) {
        return a.capturedAt < b.capturedAt;
    });
    for (const ClipboardHistoryRecord &record : oldest) {
        if (record.pinned || selected.contains(record.id))
            continue;
        if (count <= m_settings->clipboardHistoryRetentionCount() && bytes <= maximumBytes)
            break;
        selected.insert(record.id);
        removeIds.append(record.id);
        --count;
        if (record.type == QStringLiteral("image"))
            bytes -= record.sizeBytes;
    }
    if (removeIds.isEmpty())
        return;
    QVector<ClipboardHistoryRecord> removed;
    if (!m_database->deleteClipboardHistory(removeIds, &removed)) {
        emit messageChanged(QStringLiteral("Could not apply clipboard history retention: %1").arg(m_database->lastError()), true);
        return;
    }
    deleteOwnedPayloads(removed);
    refresh();
}

void ClipboardHistoryManager::deleteOwnedPayloads(const QVector<ClipboardHistoryRecord> &records)
{
    for (const ClipboardHistoryRecord &record : records)
        if (ownsPayload(record))
            QFile::remove(record.payloadPath);
}

bool ClipboardHistoryManager::ownsPayload(const ClipboardHistoryRecord &record) const
{
    if (record.type != QStringLiteral("image") || record.id.isEmpty() || record.payloadPath.isEmpty())
        return false;
    static const QRegularExpression uuidPattern(QStringLiteral("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}\\.png$"));
    const QFileInfo payload(record.payloadPath);
    if (!uuidPattern.match(payload.fileName()).hasMatch() || payload.fileName() != record.id + QStringLiteral(".png"))
        return false;
    const QString expectedDirectory = QDir::cleanPath(QFileInfo(m_storageDirectory).absoluteFilePath());
    const QString actualDirectory = QDir::cleanPath(payload.absolutePath());
    return actualDirectory.compare(expectedDirectory, Qt::CaseInsensitive) == 0 &&
           QDir::cleanPath(payload.absoluteFilePath()) == QDir(expectedDirectory).filePath(payload.fileName());
}

bool ClipboardHistoryManager::hasMissingFiles(const ClipboardHistoryRecord &record, QStringList *missing) const
{
    bool any = false;
    for (const QString &url : record.fileUrls) {
        if (!isLocalUrlMissing(url))
            continue;
        any = true;
        if (missing)
            missing->append(QUrl::fromEncoded(url.toUtf8()).toLocalFile());
    }
    return any;
}

bool ClipboardHistoryManager::setPinned(const QString &id, bool pinned)
{
    if (!m_database || !m_database->setClipboardHistoryPinned(id, pinned)) {
        emit messageChanged(QStringLiteral("Could not update clipboard history pin: %1").arg(m_database ? m_database->lastError() : QString()), true);
        return false;
    }
    refresh();
    return true;
}

bool ClipboardHistoryManager::deleteEntry(const QString &id)
{
    if (!m_database)
        return false;
    ClipboardHistoryRecord removed;
    if (!m_database->deleteClipboardHistory(id, &removed)) {
        emit messageChanged(QStringLiteral("Could not delete clipboard history entry: %1").arg(m_database->lastError()), true);
        return false;
    }
    deleteOwnedPayloads({removed});
    refresh();
    return true;
}

bool ClipboardHistoryManager::clearHistory()
{
    if (!m_database)
        return false;
    QStringList ids;
    for (const ClipboardHistoryRecord &record : m_database->clipboardHistory())
        ids.append(record.id);
    QVector<ClipboardHistoryRecord> removed;
    if (!m_database->deleteClipboardHistory(ids, &removed)) {
        emit messageChanged(QStringLiteral("Could not clear clipboard history: %1").arg(m_database->lastError()), true);
        return false;
    }
    deleteOwnedPayloads(removed);
    refresh();
    return true;
}

bool ClipboardHistoryManager::restoreEntry(const QString &id)
{
    if (!m_database || !m_clipboard)
        return false;
    ClipboardHistoryRecord entry = m_model.recordById(id);
    if (entry.id.isEmpty())
        for (const auto &candidate : m_database->clipboardHistory())
            if (candidate.id == id) { entry = candidate; break; }
    if (entry.id.isEmpty()) {
        emit messageChanged(QStringLiteral("Clipboard history entry was not found."), true);
        return false;
    }
    if (entry.type == QStringLiteral("text")) {
        m_clipboard->writeText(entry.text);
        emit messageChanged(QStringLiteral("Clipboard text restored."), false);
        return true;
    }
    if (entry.type == QStringLiteral("files")) {
        QStringList missing;
        hasMissingFiles(entry, &missing);
        if (!missing.isEmpty()) {
            emit messageChanged(QStringLiteral("Cannot restore file references because %1 item(s) are missing: %2")
                                    .arg(missing.size()).arg(missing.first()), true);
            return false;
        }
        QList<QUrl> urls;
        for (const QString &url : entry.fileUrls)
            urls.append(QUrl::fromEncoded(url.toUtf8()));
        m_clipboard->writeUrls(urls);
        emit messageChanged(QStringLiteral("Clipboard file references restored."), false);
        return true;
    }
    if (!ownsPayload(entry) || !QFileInfo::exists(entry.payloadPath)) {
        emit messageChanged(QStringLiteral("Clipboard history image payload is missing or unavailable."), true);
        return false;
    }
    if (m_restorePending) {
        emit messageChanged(QStringLiteral("Another clipboard image is still being restored."), true);
        return false;
    }
    QImageReader reader(entry.payloadPath);
    const QSize size = reader.size();
    if (!size.isValid() || qint64(size.width()) * size.height() > MaximumImagePixels ||
        qint64(size.width()) * size.height() * 4 > MaximumImageBytes) {
        emit messageChanged(QStringLiteral("Clipboard history image exceeds the 64 megapixel / 256 MiB limit."), true);
        return false;
    }
    m_restorePending = true;
    const QString path = entry.payloadPath;
    auto *watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher] {
        const QImage image = watcher->result();
        watcher->deleteLater();
        m_restorePending = false;
        if (image.isNull()) {
            emit messageChanged(QStringLiteral("Could not load clipboard history image."), true);
            return;
        }
        m_clipboard->writeImage(image);
        emit messageChanged(QStringLiteral("Clipboard image restored."), false);
    });
    watcher->setFuture(QtConcurrent::run([path] {
        QImageReader imageReader(path);
        return imageReader.read();
    }));
    return true;
}

bool ClipboardHistoryManager::saveToGallery(const QString &id, const QString &tagId)
{
    if (!m_database || !m_screenshots)
        return false;
    ClipboardHistoryRecord entry = m_model.recordById(id);
    if (entry.id.isEmpty())
        for (const auto &candidate : m_database->clipboardHistory())
            if (candidate.id == id) { entry = candidate; break; }
    if (entry.type != QStringLiteral("image") || !ownsPayload(entry) || !QFileInfo::exists(entry.payloadPath)) {
        emit messageChanged(QStringLiteral("This history image is missing or unavailable for gallery saving."), true);
        return false;
    }
    return m_screenshots->saveHistoryImage(entry.payloadPath, tagId);
}

void ClipboardHistoryManager::finishPendingSaves()
{
    while (!m_imageJobs.isEmpty()) {
        const QString id = m_imageJobs.constBegin().key();
        QFuture<ImageResult> future = m_imageJobs.value(id);
        future.waitForFinished();
        if (!m_imageJobs.contains(id))
            continue;
        const ImageResult result = future.result();
        m_imageJobs.remove(id);
        if (auto *watcher = m_imageWatchers.take(id))
            watcher->deleteLater();
        completeImageWrite(id, result);
    }
}
