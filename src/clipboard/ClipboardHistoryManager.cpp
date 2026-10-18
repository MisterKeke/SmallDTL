#include "ClipboardHistoryManager.h"

#include "../app/AppDataPaths.h"
#include "../storage/ThumbnailCache.h"
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
#include <QDirIterator>
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
    case FileCountRole: return record.fileCount >= 0 ? record.fileCount : record.fileUrls.size();
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
    QSet<QString> wanted;
    for (const auto &record : records) wanted.insert(record.id);
    for (int row = m_records.size() - 1; row >= 0; --row) {
        if (!wanted.contains(m_records[row].id)) {
            beginRemoveRows({}, row, row); m_records.removeAt(row); endRemoveRows();
        }
    }
    for (int row = 0; row < records.size(); ++row) {
        int existing = -1;
        for (int index = row; index < m_records.size(); ++index)
            if (m_records[index].id == records[row].id) { existing = index; break; }
        if (existing < 0) {
            beginInsertRows({}, row, row); m_records.insert(row, records[row]); endInsertRows();
        } else {
            if (existing != row) {
                beginMoveRows({}, existing, existing, {}, row); m_records.move(existing, row); endMoveRows();
            }
            m_records[row] = records[row];
            emit dataChanged(index(row), index(row));
        }
    }
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

ClipboardHistoryManager::ClipboardHistoryManager(ApplicationDatabase *database, ClipboardService *clipboard, SettingsManager *settings, ScreenshotManager *gallery, QObject *parent)
    : QObject(parent), m_screenshots(gallery), m_settings(settings),
      m_database(database), m_clipboard(clipboard),
      m_model(this), m_storageDirectory(QDir(AppDataPaths::root()).filePath(QStringLiteral("clipboard-history")))
{
    connect(&m_model, &ClipboardHistoryModel::countChanged, this, &ClipboardHistoryManager::countChanged);
    if (m_clipboard)
        connect(m_clipboard, &ClipboardService::externalContentAvailable,
                this, &ClipboardHistoryManager::acceptSnapshot);
    connect(&m_refreshWatcher, &QFutureWatcher<RefreshResult>::finished, this, [this] {
        const auto result = m_refreshWatcher.result();
        m_refreshBusy = false;
        if (result.revision != m_refreshRevision) { startRefresh(); return; }
        if (!result.error.isEmpty()) emit messageChanged(result.error, true);
        else {
            m_hasMore = result.hasMore;
            m_model.setRecords(result.records);
        }
        emit countChanged();
        emit loadingChanged();
    });
    connect(&m_maintenanceWatcher, &QFutureWatcher<MaintenanceResult>::finished, this, [this] {
        const auto result = m_maintenanceWatcher.result();
        m_maintenanceBusy = false;
        m_storageUsage = result.usage;
        emit storageUsageChanged();
        if (!result.error.isEmpty() || !result.failures.isEmpty())
            emit messageChanged(QStringLiteral("Storage maintenance needs attention: %1%2")
                .arg(result.error, result.failures.join(QLatin1Char('\n'))), true);
    });
    m_searchTimer.setSingleShot(true);
    m_searchTimer.setInterval(150);
    connect(&m_searchTimer, &QTimer::timeout, this, &ClipboardHistoryManager::refresh);
    m_retentionTimer.setInterval(5 * 60 * 1000);
    m_retentionTimer.setParent(this);
    m_retentionTimer.setObjectName(QStringLiteral("clipboardRetentionTimer"));
    connect(&m_retentionTimer, &QTimer::timeout, this, [this] { maintainStorage(); });
    m_retentionTimer.start();
    if (m_settings) {
        m_sessionOnly = m_settings->clipboardHistorySessionOnly();
        m_paused = !m_settings->clipboardHistoryEnabled();
        connect(m_settings, &SettingsManager::preferencesChanged, this, &ClipboardHistoryManager::applyPreferences);
    }
    if (m_sessionOnly) clearHistory();
    cleanupRetention();
    refresh();
    maintainStorage();
}

ClipboardHistoryManager::~ClipboardHistoryManager()
{
    shutdown();
    m_refreshWatcher.waitForFinished();
    m_maintenanceWatcher.waitForFinished();
}

void ClipboardHistoryManager::setSearch(const QString &search)
{
    const QString cleaned = search.trimmed();
    if (m_search == cleaned)
        return;
    m_search = cleaned;
    m_pageLimit = 100;
    ++m_refreshRevision; // Immediately invalidate results for the old search.
    emit searchChanged();
    m_searchTimer.start();
}

void ClipboardHistoryManager::refresh()
{
    ++m_refreshRevision;
    if (!m_refreshBusy) startRefresh();
}

void ClipboardHistoryManager::startRefresh()
{
    if (!m_database || !m_database->isReady()) { m_model.setRecords({}); return; }
    const QString path = m_database->databasePath();
    const QString payloadDirectory = m_storageDirectory;
    const QString search = m_search;
    const int limit = m_pageLimit;
    const quint64 revision = m_refreshRevision;
    m_refreshBusy = true;
    m_refreshWatcher.setFuture(QtConcurrent::run([path, payloadDirectory, search, limit, revision] {
        RefreshResult result;
        result.revision = revision;
        ApplicationDatabase reader(path);
        if (!reader.initialize()) { result.error = reader.lastError(); return result; }
        result.records = reader.clipboardHistory(search, limit + 1, true);
        result.error = reader.lastError();
        result.hasMore = result.records.size() > limit;
        if (result.hasMore) result.records.resize(limit);
        for (auto &record : result.records) {
            if (record.type == QStringLiteral("image")) {
                const QFileInfo file(record.payloadPath);
                record.missing = file.absolutePath().compare(payloadDirectory, Qt::CaseInsensitive) != 0 ||
                    file.fileName() != record.id + QStringLiteral(".png") || !file.exists();
            }
            else if (record.type == QStringLiteral("files"))
                record.missing = reader.clipboardHistoryFilesMissing(record.id);
        }
        return result;
    }));
    emit loadingChanged();
}

void ClipboardHistoryManager::loadMore()
{
    if (!m_hasMore || loading()) return;
    m_pageLimit += 100;
    refresh();
}

void ClipboardHistoryManager::invalidatePendingCaptures()
{
    ++m_captureGeneration;
    while (!m_imageQueue.isEmpty()) {
        const PendingImage invalidated = m_imageQueue.dequeue();
        m_pendingImageBytes -= m_imageReservations.take(invalidated.id);
        m_imageGenerations.remove(invalidated.id);
    }
}

void ClipboardHistoryManager::applyPreferences()
{
    if (!m_settings) return;
    const bool paused = !m_settings->clipboardHistoryEnabled();
    if (paused != m_paused) {
        m_paused = paused;
        if (paused) invalidatePendingCaptures();
        emit pausedChanged();
    }
    ThumbnailCache::setLimitMB(m_settings->thumbnailCacheLimitMB());
    const bool sessionOnly = m_settings->clipboardHistorySessionOnly();
    if (sessionOnly != m_sessionOnly) {
        m_sessionOnly = sessionOnly;
    }
    cleanupRetention();
}

int ClipboardHistoryManager::clearCount(bool includePinned) const
{
    return m_database ? m_database->clipboardHistoryCount(includePinned) : 0;
}

void ClipboardHistoryManager::maintainStorage(bool compact, bool clearThumbnails)
{
    if (!m_database || !m_database->isReady() || maintaining()) return;
    cleanupRetention();
    QSet<QString> keep;
    for (const auto &record : m_database->clipboardHistoryMetadata()) keep.insert(record.id);
    for (auto it = m_imageReservations.cbegin(); it != m_imageReservations.cend(); ++it) keep.insert(it.key());
    const QString directory = m_storageDirectory;
    const QString databasePath = m_database->databasePath();
    const QString thumbnailDirectory = QDir(AppDataPaths::root()).filePath(QStringLiteral("cache/thumbnails"));
    ThumbnailCache::setLimitMB(m_settings ? m_settings->thumbnailCacheLimitMB() : 128);
    const QDateTime cutoff = QDateTime::currentDateTimeUtc();
    m_maintenanceBusy = true;
    m_maintenanceWatcher.setFuture(QtConcurrent::run([keep, directory, databasePath, thumbnailDirectory, cutoff, compact, clearThumbnails] {
        MaintenanceResult result;
        qint64 bytes = 0;
        static const QRegularExpression uuid(QStringLiteral("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}\\.png$"));
        if (QFileInfo(directory).isSymLink() || QFileInfo(directory).isJunction()) {
            result.error = QStringLiteral("The history payload directory is a link; automatic cleanup was refused.");
            return result;
        }
        QDirIterator files(directory, QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        while (files.hasNext()) {
            files.next();
            const QFileInfo file = files.fileInfo();
            if (file.isSymLink() || !uuid.match(file.fileName()).hasMatch()) continue;
            if (!keep.contains(file.completeBaseName()) && file.lastModified().toUTC() < cutoff) {
                if (QFile::remove(file.absoluteFilePath())) continue;
                result.failures.append(file.absoluteFilePath());
            }
            bytes += file.size();
        }
        if (compact) {
            ApplicationDatabase database(databasePath);
            if (!database.initialize() || !database.compact()) result.error = database.lastError();
        }
        const auto thumbnails = ThumbnailCache::trim(thumbnailDirectory, clearThumbnails);
        result.failures.append(thumbnails.value(QStringLiteral("failures")).toStringList());
        result.usage = {{QStringLiteral("thumbnailBytes"), thumbnails.value(QStringLiteral("bytes"))}, {QStringLiteral("imageBytes"), bytes},
                        {QStringLiteral("databaseBytes"), QFileInfo(databasePath).size()},
                        {QStringLiteral("failedFiles"), result.failures.size()}};
        return result;
    }));
    emit storageUsageChanged();
}

void ClipboardHistoryManager::setPaused(bool paused)
{
    if (m_paused == paused)
        return;
    if (m_settings && !m_settings->setClipboardHistoryEnabled(!paused)) return;
    if (m_paused != paused) {
        m_paused = paused;
        if (paused) invalidatePendingCaptures();
        emit pausedChanged();
    }
    emit messageChanged(paused ? QStringLiteral("Clipboard history paused.") : QStringLiteral("Clipboard history resumed."), false);
}

void ClipboardHistoryManager::acceptSnapshot(const ClipboardSnapshot &snapshot)
{
    if (m_paused || snapshot.sensitive || !m_database || !m_database->isReady())
        return;
    if (m_settings) {
        const QString source = QDir::fromNativeSeparators(snapshot.sourceApplication).toCaseFolded();
        const QString name = QFileInfo(source).fileName();
        for (const QString &entry : m_settings->clipboardHistoryExcludedApplications())
            if (!source.isEmpty() && (entry == source || entry == name)) return;
    }
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
        event.generation = m_captureGeneration;
        m_imageReservations.insert(event.id, bytes);
        m_imageGenerations.insert(event.id, event.generation);
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
    result.generation = event.generation;
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
    const quint64 generation = m_imageGenerations.take(id);
    if (generation != m_captureGeneration || result.generation != m_captureGeneration) {
        if (result.error.isEmpty() && ownsPayload(result.record))
            deleteOwnedPayloads({result.record});
        dispatchImageWrites();
        return;
    }
    if (!result.error.isEmpty()) {
        emit messageChanged(QStringLiteral("Could not save clipboard history image: %1").arg(result.error), true);
        dispatchImageWrites();
        return;
    }
    QString savedId;
    bool inserted = false;
    if (!m_database->saveClipboardHistory(result.record, &savedId, &inserted)) {
        if (ownsPayload(result.record))
            deleteOwnedPayloads({result.record});
        emit messageChanged(QStringLiteral("Could not save clipboard history: %1").arg(m_database->lastError()), true);
    } else if (!inserted && ownsPayload(result.record)) {
        deleteOwnedPayloads({result.record});
    }
    cleanupRetention();
    refresh();
    dispatchImageWrites();
}

void ClipboardHistoryManager::cleanupRetention()
{
    if (!m_database || !m_database->isReady() || !m_settings)
        return;
    const QVector<ClipboardHistoryRecord> records = m_database->clipboardHistoryMetadata();
    if (records.isEmpty())
        return;
    const QDateTime cutoff = QDateTime::currentDateTimeUtc().addDays(-m_settings->clipboardHistoryRetentionDays());
    QStringList removeIds;
    QSet<QString> selected;
    qint64 bytes = 0;
    for (const ClipboardHistoryRecord &record : records) {
        const QDateTime captured = QDateTime::fromString(record.capturedAt, Qt::ISODateWithMs).toUTC();
        if (!record.pinned && captured.isValid() && captured < cutoff) {
            selected.insert(record.id);
            removeIds.append(record.id);
        } else if (record.type == QStringLiteral("image")) {
            bytes += record.sizeBytes;
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
        if (ownsPayload(record) && QFileInfo::exists(record.payloadPath) && !QFile::remove(record.payloadPath)) {
            emit messageChanged(QStringLiteral("History entry removed, but its image could not be deleted. Storage maintenance will retry: %1").arg(record.payloadPath), true);
        }
}

bool ClipboardHistoryManager::ownsPayload(const ClipboardHistoryRecord &record) const
{
    if (QFileInfo(m_storageDirectory).isSymLink() || QFileInfo(m_storageDirectory).isJunction()) return false;
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

bool ClipboardHistoryManager::clearHistory(bool includePinned)
{
    if (!m_database)
        return false;
    QStringList ids;
    const auto records = m_database->clipboardHistoryMetadata();
    if (!m_database->lastError().isEmpty()) {
        emit messageChanged(m_database->lastError(), true);
        return false;
    }
    for (const ClipboardHistoryRecord &record : records)
        if (includePinned || !record.pinned) ids.append(record.id);
    QVector<ClipboardHistoryRecord> removed;
    if (!m_database->deleteClipboardHistory(ids, &removed)) {
        emit messageChanged(QStringLiteral("Could not clear clipboard history: %1").arg(m_database->lastError()), true);
        return false;
    }
    invalidatePendingCaptures();
    deleteOwnedPayloads(removed);
    refresh();
    return true;
}

bool ClipboardHistoryManager::restoreEntry(const QString &id)
{
    if (!m_database || !m_clipboard)
        return false;
    const auto records = m_database->clipboardHistory({}, 1, false, id);
    const ClipboardHistoryRecord entry = records.isEmpty() ? ClipboardHistoryRecord{} : records.first();
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
    const auto records = m_database->clipboardHistory({}, 1, false, id);
    const ClipboardHistoryRecord entry = records.isEmpty() ? ClipboardHistoryRecord{} : records.first();
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

void ClipboardHistoryManager::shutdown()
{
    m_retentionTimer.stop();
    m_searchTimer.stop();
    m_paused = true; // Shutdown does not change the saved capture preference.
    finishPendingSaves();
    if (m_sessionOnly) clearHistory();
}
