#include "ScreenshotManager.h"
#include "ScreenshotEditorImage.h"
#include "ScreenshotNaming.h"
#include "../app/AppDataPaths.h"

#include <QClipboard>
#include <QMimeData>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QImageWriter>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QCryptographicHash>
#include <QByteArrayView>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QDesktopServices>
#include <QUrl>
#include <QDebug>
#include <QTimer>
#include <QByteArray>
#include <QtMath>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cmath>
#include <utility>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

ScreenshotGalleryModel::ScreenshotGalleryModel(QObject *parent)
    : QAbstractListModel(parent), m_recent(this)
{
    m_recent.setSourceModel(this);
}

int ScreenshotGalleryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant ScreenshotGalleryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return {};
    const Item &item = m_items.at(index.row());
    switch (role) {
    case FileNameRole: return item.fileName;
    case PathRole: return item.path;
    case ImageUrlRole: return QUrl::fromLocalFile(item.path);
    case ModifiedRole: return item.modified.toString(QStringLiteral("yyyy-MM-dd  HH:mm"));
    case WidthRole: return item.dimensions.width();
    case HeightRole: return item.dimensions.height();
    case SizeBytesRole: return item.sizeBytes;
    case MissingRole: return item.missing;
    case ThumbnailUrlRole: return QUrl(QStringLiteral("image://thumbnail/")+QString::fromLatin1(item.path.toUtf8().toBase64(QByteArray::Base64UrlEncoding|QByteArray::OmitTrailingEquals))+QLatin1Char('/')+QString::number(item.modified.toMSecsSinceEpoch())+QLatin1Char('-')+QString::number(item.sizeBytes));
    case IdRole: return item.id;
    case TagsRole: return item.tags;
    case SourceRole: return item.source;
    case SourceScreenshotIdRole: return item.sourceScreenshotId;
    default: return {};
    }
}

QHash<int, QByteArray> ScreenshotGalleryModel::roleNames() const
{
    return {{FileNameRole, "fileName"}, {PathRole, "path"}, {ImageUrlRole, "imageUrl"},
            {ModifiedRole, "modified"}, {WidthRole, "pixelWidth"}, {HeightRole, "pixelHeight"},
            {SizeBytesRole, "sizeBytes"}, {MissingRole, "missing"}, {ThumbnailUrlRole,"thumbnailUrl"},
            {IdRole,"screenshotId"}, {TagsRole,"tags"}, {SourceRole,"source"},
            {SourceScreenshotIdRole,"sourceScreenshotId"}};
}

void ScreenshotGalleryModel::setDirectory(const QString &directory)
{
    if (m_directory == directory)
        return;
    m_directory = directory;
    refresh();
}

void ScreenshotGalleryModel::setManagedPaths(const QStringList &paths)
{
    QVector<ScreenshotRecord> records;
    records.reserve(paths.size());
    for (const QString &path : paths) {
        ScreenshotRecord record;
        record.id = path;
        record.path = path;
        records.append(record);
    }
    setRecords(records);
}

void ScreenshotGalleryModel::setRecords(const QVector<ScreenshotRecord> &records)
{
    m_records = records;
    refresh();
}

void ScreenshotGalleryModel::refresh()
{
    ++m_generation;
    if(m_loading)return;
    const quint64 generation=m_generation;
    if(!m_loading){m_loading=true;emit loadingChanged();}
    const QString directory=QDir(m_directory).absolutePath();
    const QVector<ScreenshotRecord> records=m_records;
    auto *watcher=new QFutureWatcher<QVector<Item>>(this);
    connect(watcher,&QFutureWatcher<QVector<Item>>::finished,this,[this,watcher,generation]{
        const auto items=watcher->result();watcher->deleteLater();
        if(generation!=m_generation){m_loading=false;refresh();return;}
        beginResetModel();m_items=items;endResetModel();m_loading=false;emit loadingChanged();emit countChanged();
    });
    watcher->setFuture(QtConcurrent::run([directory,records]{
        QVector<Item> fresh;
        for(const ScreenshotRecord &record:records) {
            const QString &path=record.path;
            const QFileInfo file(path);if(file.absolutePath().compare(directory,Qt::CaseInsensitive)!=0)continue;
            if(!file.exists() || !file.isFile()){
                Item item;item.id=record.id;item.path=file.absoluteFilePath();item.fileName=file.fileName();
                item.modified=QDateTime::fromString(record.timestamp,Qt::ISODateWithMs);item.sizeBytes=record.sizeBytes;
                item.dimensions=QSize(record.width,record.height);item.missing=true;item.tags=record.tags;item.source=record.source;item.sourceScreenshotId=record.sourceScreenshotId;
                fresh.append(item);continue;
            }
            QImageReader reader(file.absoluteFilePath());
            Item item;item.id=record.id;item.path=file.absoluteFilePath();item.fileName=file.fileName();
            item.modified=file.lastModified();item.sizeBytes=file.size();item.dimensions=reader.size();item.tags=record.tags;item.source=record.source;item.sourceScreenshotId=record.sourceScreenshotId;
            fresh.append(item);
        }
        std::stable_sort(fresh.begin(),fresh.end(),[](const Item &a,const Item &b){return a.modified>b.modified;});return fresh;
    }));
}

QString ScreenshotGalleryModel::pathAt(int row) const
{
    if (row < 0 || row >= m_items.size())
        return {};
    return m_items.at(row).path;
}

QString ScreenshotGalleryModel::idAt(int row) const
{
    if (row < 0 || row >= m_items.size())
        return {};
    return m_items.at(row).id;
}

ScreenshotManager::ScreenshotManager(ProfileManager *profiles, SettingsManager *settings, QObject *parent)
    : QObject(parent), m_profiles(profiles), m_settings(settings),
      m_clipboard(QGuiApplication::clipboard()), m_clipboardService(m_clipboard, this),
      m_database({}, nullptr), m_gallery(this)
{
    const QString legacyIndex = QDir(AppDataPaths::root()).filePath(QStringLiteral("screenshot-index.json"));
    m_databaseReady = m_database.initialize(legacyIndex);
    if (!m_databaseReady) {
        m_lastSaveError = m_database.lastError();
        qWarning().noquote() << m_lastSaveError;
        QTimer::singleShot(0, this, [this] {
            emit messageChanged(QStringLiteral("Screenshot database unavailable: %1").arg(m_database.lastError()), true);
        });
    }
    connect(&m_clipboardService, &ClipboardService::hasImageChanged, this, [this] {
        emit clipboardHasImageChanged();
    });
    connect(&m_clipboardService, &ClipboardService::externalImageAvailable,
            this, &ScreenshotManager::acceptAutomaticClipboardImage);
    connect(m_settings, &SettingsManager::preferencesChanged, this, [this] {
        emit automaticClipboardSavingChanged();
    });
    connect(m_profiles, &ProfileManager::activeProfileChanged, this, [this] {
        m_gallery.setDirectory(resolvedDestination());refreshGallery();
        emit destinationChanged();
    });
    connect(m_profiles, &ProfileManager::profileDataChanged, this, [this](const QString &id) {
        if (id == m_profiles->activeProfileId() && m_destinationOverride.isEmpty()) {
            m_gallery.setDirectory(resolvedDestination());refreshGallery();
            emit destinationChanged();
        }
    });
    connect(m_settings, &SettingsManager::generalScreenshotDirectoryChanged, this, [this] {
        if (m_destinationOverride.isEmpty() && m_profiles->activeScreenshotDirectory().isEmpty()) {
            m_gallery.setDirectory(resolvedDestination());refreshGallery();
            emit destinationChanged();
        }
    });
    m_gallery.setDirectory(resolvedDestination());
    refreshGallery();
}

QString ScreenshotManager::destination() const { return resolvedDestination(); }
bool ScreenshotManager::destinationOverridden() const { return !m_destinationOverride.isEmpty(); }
bool ScreenshotManager::clipboardHasImage() const { return m_clipboardService.hasImage(); }
bool ScreenshotManager::automaticClipboardSavingEnabled() const { return m_settings->automaticClipboardSavingEnabled(); }

void ScreenshotManager::setAutomaticClipboardSavingPaused(bool paused)
{
    if (m_automaticClipboardSavingPaused == paused)
        return;
    m_automaticClipboardSavingPaused = paused;
    emit automaticClipboardSavingChanged();
}
ScreenshotGalleryModel *ScreenshotManager::gallery() { return &m_gallery; }

QVariantList ScreenshotManager::tags() const
{
    QVariantList result;
    for (const ScreenshotTag &tag : m_database.tags())
        result.append(QVariantMap{{QStringLiteral("tagId"), tag.id}, {QStringLiteral("name"), tag.name}});
    return result;
}

QString ScreenshotManager::profileDestination(const QString &profileId) const
{
    return m_profiles->profileInfo(profileId).value(QStringLiteral("screenshotDirectory")).toString();
}

QString ScreenshotManager::generalDestination() const { return m_settings->generalScreenshotDirectory(); }

void ScreenshotManager::setDestinationOverride(const QString &directory)
{
    const QString normalized = QDir::cleanPath(QDir::fromNativeSeparators(directory.trimmed()));
    if (normalized.isEmpty() || normalized == m_destinationOverride)
        return;
    m_destinationOverride = normalized;
    m_settings->addKnownScreenshotDirectory(normalized);
    m_gallery.setDirectory(resolvedDestination());
    refreshGallery();
    emit destinationChanged();
}

void ScreenshotManager::selectDestination(const QString &directory) {
    if(!QDir::isAbsolutePath(directory) || !QDir(directory).exists()) {emit messageChanged(QStringLiteral("That screenshot destination is unavailable."),true);return;}
    setDestinationOverride(directory);
}

void ScreenshotManager::clearDestinationOverride()
{
    if (m_destinationOverride.isEmpty())
        return;
    m_destinationOverride.clear();
    m_gallery.setDirectory(resolvedDestination());
    refreshGallery();
    emit destinationChanged();
}

QString ScreenshotManager::saveClipboardImage()
{
    m_lastSaveError.clear();
    if (!m_clipboard || !m_clipboard->mimeData() || !m_clipboard->mimeData()->hasImage()) {
        const QString message = QStringLiteral("The clipboard does not contain an image.");
        m_lastSaveError=message;
        emit messageChanged(message, true);
        return {};
    }
    const QImage image = qvariant_cast<QImage>(m_clipboard->mimeData()->imageData());
    if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
        const QString message = QStringLiteral("The clipboard image is empty or invalid.");
        m_lastSaveError=message;
        emit messageChanged(message, true);
        return {};
    }

    constexpr qint64 maximumBytes=256LL*1024*1024;
    qint64 queuedBytes=m_fingerprintBytes;for(auto bytes:m_pendingBytes)queuedBytes+=bytes;
    if(image.sizeInBytes()>maximumBytes || qint64(image.width())*image.height()>64*1024*1024 || queuedBytes+image.sizeInBytes()>512LL*1024*1024 || m_pendingPaths.size()+m_fingerprintJobs.size()>=2) {
        m_lastSaveError=QStringLiteral("Image exceeds the 64 megapixel / 256 MiB limit, or two saves are already pending. Try again after saving finishes.");
        emit messageChanged(m_lastSaveError,true);return {};
    }
    const QString directoryPath = resolvedDestination();
    const QDir directory(directoryPath);
    if (!directory.exists() || !QFileInfo(directoryPath).isDir()) {
        const QString message = QStringLiteral("Screenshot destination is unavailable: %1").arg(directoryPath);
        m_lastSaveError=message;
        emit messageChanged(message, true);
        return {};
    }

    const QString path = nextFilePath(directoryPath);
    SaveMetadata metadata;
    metadata.source = QStringLiteral("clipboard");
    queueImageWrite(image, path, metadata);
    return path;
}

bool ScreenshotManager::saveHistoryImage(const QString &payloadPath, const QString &tagId)
{
    m_lastSaveError.clear();
    if (!m_databaseReady) {
        m_lastSaveError = m_database.lastError().isEmpty() ? QStringLiteral("Screenshot database is unavailable.") : m_database.lastError();
        emit messageChanged(m_lastSaveError, true);
        return false;
    }
    const QFileInfo payload(payloadPath);
    if (!payload.exists() || !payload.isFile()) {
        m_lastSaveError = QStringLiteral("The clipboard history image payload is missing.");
        emit messageChanged(m_lastSaveError, true);
        return false;
    }
    QImageReader reader(payload.absoluteFilePath());
    const QSize dimensions = reader.size();
    constexpr qint64 maximumBytes = 256LL * 1024 * 1024;
    if (!dimensions.isValid() || qint64(dimensions.width()) * dimensions.height() > 64LL * 1024 * 1024 ||
        qint64(dimensions.width()) * dimensions.height() * 4 > maximumBytes) {
        m_lastSaveError = QStringLiteral("Image exceeds the 64 megapixel / 256 MiB limit.");
        emit messageChanged(m_lastSaveError, true);
        return false;
    }
    if (!tagId.isEmpty()) {
        bool found = false;
        for (const ScreenshotTag &tag : m_database.tags())
            found = found || tag.id == tagId;
        if (!found) {
            m_lastSaveError = QStringLiteral("The selected screenshot tag is unavailable.");
            emit messageChanged(m_lastSaveError, true);
            return false;
        }
    }
    const QString directoryPath = resolvedDestination();
    if (!QFileInfo(directoryPath).isDir()) {
        m_lastSaveError = QStringLiteral("Screenshot destination is unavailable: %1").arg(directoryPath);
        emit messageChanged(m_lastSaveError, true);
        return false;
    }
    qint64 queuedBytes = m_fingerprintBytes;
    for (const qint64 bytes : m_pendingBytes)
        queuedBytes += bytes;
    const qint64 reservedBytes = qint64(dimensions.width()) * dimensions.height() * 4;
    if (queuedBytes + reservedBytes > 512LL * 1024 * 1024 || m_pendingPaths.size() + m_fingerprintJobs.size() >= 2) {
        m_lastSaveError = QStringLiteral("Two screenshot saves are already pending. Try again when a save finishes.");
        emit messageChanged(m_lastSaveError, true);
        return false;
    }
    const QString targetPath = nextFilePath(directoryPath);
    SaveMetadata metadata;
    metadata.source = QStringLiteral("clipboard-history");
    metadata.tagId = tagId;
    m_pendingPaths.insert(targetPath);
    m_pendingBytes.insert(targetPath, reservedBytes);
    m_saveMetadata.insert(targetPath, metadata);
    auto *watcher = new QFutureWatcher<SaveResult>(this);
    connect(watcher, &QFutureWatcher<SaveResult>::finished, this, [this, watcher, targetPath] {
        const SaveResult result = watcher->result();
        watcher->deleteLater();
        completeSave(targetPath, result);
    });
    const QString sourcePath = payload.absoluteFilePath();
    const QFuture<SaveResult> future = QtConcurrent::run([sourcePath, targetPath] {
        QImageReader sourceReader(sourcePath);
        const QImage image = sourceReader.read();
        if (image.isNull())
            return SaveResult{false, sourceReader.errorString()};
        if (qint64(image.width()) * image.height() > 64LL * 1024 * 1024 || image.sizeInBytes() > 256LL * 1024 * 1024)
            return SaveResult{false, QStringLiteral("Image exceeds the 64 megapixel / 256 MiB limit.")};
        return writeImage(image, targetPath);
    });
    m_jobs.insert(targetPath, future);
    watcher->setFuture(future);
    return true;
}

QString ScreenshotManager::saveGeneratedImage(const QImage &image, const QString &tagId)
{
    m_lastSaveError.clear();
    auto fail = [this](const QString &message) {
        m_lastSaveError = message;
        emit messageChanged(message, true);
        return QString();
    };
    if (!m_databaseReady)
        return fail(m_database.lastError().isEmpty() ? QStringLiteral("Screenshot database is unavailable.")
                                                     : m_database.lastError());
    if (image.isNull() || image.width() <= 0 || image.height() <= 0 ||
        qint64(image.width()) * image.height() > 64LL * 1024 * 1024 ||
        image.sizeInBytes() <= 0 || image.sizeInBytes() > 256LL * 1024 * 1024)
        return fail(QStringLiteral("Image exceeds the 64 megapixel / 256 MiB limit."));
    if (!tagId.isEmpty()) {
        bool found = false;
        for (const ScreenshotTag &tag : m_database.tags())
            found = found || tag.id == tagId;
        if (!found)
            return fail(QStringLiteral("The selected screenshot tag is unavailable."));
    }
    qint64 queuedBytes = m_fingerprintBytes;
    for (const qint64 bytes : std::as_const(m_pendingBytes))
        queuedBytes += bytes;
    if (m_pendingPaths.size() + m_fingerprintJobs.size() >= 2 ||
        queuedBytes + image.sizeInBytes() > 512LL * 1024 * 1024)
        return fail(QStringLiteral("Two screenshot saves are already pending. Try again when a save finishes."));
    const QString directoryPath = resolvedDestination();
    if (!QFileInfo(directoryPath).isDir())
        return fail(QStringLiteral("Screenshot destination is unavailable: %1").arg(directoryPath));

    const QString path = nextFilePath(directoryPath);
    if (path.isEmpty())
        return fail(m_lastSaveError.isEmpty() ? QStringLiteral("Could not allocate a screenshot path.") : m_lastSaveError);
    SaveMetadata metadata;
    metadata.tagId = tagId;
    metadata.source = QStringLiteral("qr-generated");
    queueImageWrite(image, path, metadata);
    return path;
}

void ScreenshotManager::queueImageWrite(const QImage &image, const QString &path, const SaveMetadata &metadata)
{
    m_pendingPaths.insert(path);
    m_pendingBytes.insert(path, image.sizeInBytes());
    m_saveMetadata.insert(path, metadata);
    auto *watcher = new QFutureWatcher<SaveResult>(this);
    connect(watcher, &QFutureWatcher<SaveResult>::finished, this, [this, watcher, path] {
        const SaveResult result = watcher->result();
        watcher->deleteLater();
        completeSave(path, result);
    });
    const QFuture<SaveResult> future = QtConcurrent::run([image, path] { return writeImage(image, path); });
    m_jobs.insert(path, future);
    watcher->setFuture(future);
}

void ScreenshotManager::completeSave(const QString &path,const SaveResult &result) {
        if(!m_pendingPaths.contains(path))return;
        m_jobs.remove(path);
        m_pendingPaths.remove(path);m_pendingBytes.remove(path);
        const SaveMetadata metadata = m_saveMetadata.take(path);
        const bool isEdited = !metadata.sourceScreenshotId.isEmpty();
        if (!result.success) {
            if (metadata.automatic)
                m_inFlightAutomaticFingerprints.remove(metadata.automaticFingerprint);
            const QString message = QStringLiteral("Could not save screenshot: %1").arg(result.error);
            emit messageChanged(message, true);
            emit saveFinished(path, false, message);
            return;
        }
        if (!addToCollection(path, metadata.sourceScreenshotId, metadata.inheritTags,
                             metadata.captureProfileId, metadata.tagId, metadata.source)) {
            if (isEdited || metadata.source == QStringLiteral("clipboard-history") ||
                metadata.source == QStringLiteral("qr-generated"))
                QFile::remove(path);
            if (metadata.automatic)
                m_inFlightAutomaticFingerprints.remove(metadata.automaticFingerprint);
            const QString message = QStringLiteral("Screenshot was saved, but could not be added to the gallery: %1")
                                        .arg(m_lastSaveError);
            emit messageChanged(message, true);
            emit saveFinished(path, false, message);
            return;
        }
        if (metadata.automatic) {
            m_inFlightAutomaticFingerprints.remove(metadata.automaticFingerprint);
            if (m_savedAutomaticFingerprints.size() >= 4096)
                m_savedAutomaticFingerprints.clear();
            m_savedAutomaticFingerprints.insert(metadata.automaticFingerprint);
        }
        const QString message = QStringLiteral("%1 saved to %2")
                                    .arg(isEdited ? QStringLiteral("Edited copy") : QStringLiteral("Screenshot"),
                                         QFileInfo(path).fileName());
        emit messageChanged(message, false);
        emit saveFinished(path, true, message);
}

void ScreenshotManager::finishPendingSaves() {
    while (!m_fingerprintJobs.isEmpty()) {
        const quint64 eventId = m_nextFingerprintToProcess;
        QFuture<QByteArray> future = m_fingerprintJobs.value(eventId);
        future.waitForFinished();
        finishAutomaticFingerprint(eventId, future.result());
    }
    const auto jobs = m_jobs;
    for (auto it = jobs.begin(); it != jobs.end(); ++it) {
        auto future = it.value();
        future.waitForFinished();
        completeSave(it.key(), future.result());
    }
}

bool ScreenshotManager::openImage(const QString &path) const
{
    const QFileInfo file(path);
    if (!file.exists() || !file.isFile()) {
        emit const_cast<ScreenshotManager *>(this)->messageChanged(
            QStringLiteral("That screenshot is no longer available."), true);
        return false;
    }
    return QDesktopServices::openUrl(QUrl::fromLocalFile(file.absoluteFilePath()));
}

bool ScreenshotManager::openContainingFolder(const QString &path) const
{
    const QFileInfo file(path);
    if (!file.exists()) {
        emit const_cast<ScreenshotManager *>(this)->messageChanged(
            QStringLiteral("That screenshot is no longer available."), true);
        return false;
    }
    return QDesktopServices::openUrl(QUrl::fromLocalFile(file.absolutePath()));
}

bool ScreenshotManager::copyImageToClipboard(const QString &path)
{
    QImageReader reader(path);
    const QSize dimensions=reader.size();
    QImage image;
    if(dimensions.isValid() && qint64(dimensions.width())*dimensions.height()<=64*1024*1024)image=reader.read();
    if (image.isNull()) {
        emit const_cast<ScreenshotManager *>(this)->messageChanged(
            QStringLiteral("Could not read that screenshot."), true);
        return false;
    }
    m_clipboardService.writeImage(image);
    emit const_cast<ScreenshotManager *>(this)->messageChanged(
        QStringLiteral("Screenshot copied to the clipboard."), false);
    return true;
}

void ScreenshotManager::copyPath(const QString &path) const
{
    QGuiApplication::clipboard()->setText(QDir::toNativeSeparators(path));
    emit const_cast<ScreenshotManager *>(this)->messageChanged(
        QStringLiteral("Screenshot path copied."), false);
}

QString ScreenshotManager::renameScreenshot(const QString &path, const QString &newName)
{
    const QString oldPath = QFileInfo(path).absoluteFilePath();
    const QString screenshotId = m_database.screenshotIdForPath(oldPath);
    if (screenshotId.isEmpty() || !QFileInfo::exists(oldPath)) {
        emit messageChanged(QStringLiteral("That screenshot is no longer available."), true);
        return {};
    }

    QString baseName = newName.trimmed();
    if (baseName.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive))
        baseName.chop(4);
    static const QRegularExpression invalidName(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1F]"));
    while (baseName.endsWith(QLatin1Char('.')) || baseName.endsWith(QLatin1Char(' ')))
        baseName.chop(1);
    if (baseName.isEmpty() || baseName.contains(invalidName)) {
        emit messageChanged(QStringLiteral("Choose a filename without Windows-reserved characters."), true);
        return {};
    }

    const QString newPath = QFileInfo(QDir(QFileInfo(oldPath).absolutePath())
                                          .filePath(baseName + QStringLiteral(".png")))
                                .absoluteFilePath();
    if (newPath.compare(oldPath, Qt::CaseInsensitive) == 0)
        return oldPath;
    if (QFileInfo::exists(newPath)) {
        emit messageChanged(QStringLiteral("A file with that name already exists."), true);
        return {};
    }
    if (!QFile::rename(oldPath, newPath)) {
        emit messageChanged(QStringLiteral("Could not rename the screenshot."), true);
        return {};
    }

    if (!m_database.updateScreenshotPath(screenshotId, newPath)) {
        QFile::rename(newPath, oldPath);
        emit messageChanged(QStringLiteral("Could not update the screenshot database: %1").arg(m_database.lastError()), true);
        return {};
    }
    refreshGallery();
    emit messageChanged(QStringLiteral("Screenshot renamed."), false);
    return newPath;
}

bool ScreenshotManager::removeFromCollection(const QString &path)
{
    const QString normalized = QFileInfo(path).absoluteFilePath();
    const QString screenshotId = m_database.screenshotIdForPath(normalized);
    if (screenshotId.isEmpty())
        return false;
    if (!m_database.removeScreenshot(screenshotId)) {
        emit messageChanged(QStringLiteral("Could not remove screenshot from the gallery: %1").arg(m_database.lastError()), true);
        return false;
    }
    refreshGallery();
    emit messageChanged(QStringLiteral("Removed from the gallery. The screenshot file was kept."), false);
    return true;
}

bool ScreenshotManager::deleteScreenshotFile(const QString &path)
{
    const QString normalized = QFileInfo(path).absoluteFilePath();
    const QString screenshotId = m_database.screenshotIdForPath(normalized);
    if (screenshotId.isEmpty() || !QFileInfo::exists(normalized)) {
        emit messageChanged(QStringLiteral("That screenshot file is no longer available."), true);
        return false;
    }
    ScreenshotRecord removed;
    if (!m_database.removeScreenshot(screenshotId, &removed)) {
        emit messageChanged(QStringLiteral("Could not remove screenshot from the database: %1").arg(m_database.lastError()), true);
        return false;
    }
    if (!QFile::remove(normalized)) {
        if (!m_database.restoreScreenshot(removed))
            emit messageChanged(QStringLiteral("Could not delete the screenshot file or restore its gallery record: %1").arg(m_database.lastError()), true);
        else
            emit messageChanged(QStringLiteral("Could not delete the screenshot file."), true);
        return false;
    }
    refreshGallery();
    emit messageChanged(QStringLiteral("Screenshot file deleted."), false);
    return true;
}

QString ScreenshotManager::createTag(const QString &name)
{
    const QString id = m_database.createTag(name);
    if (id.isEmpty()) {
        emit messageChanged(QStringLiteral("Could not create tag: %1").arg(m_database.lastError()), true);
        return {};
    }
    emit tagsChanged();
    refreshGallery();
    return id;
}

bool ScreenshotManager::renameTag(const QString &tagId, const QString &name)
{
    if (!m_database.renameTag(tagId, name)) {
        emit messageChanged(QStringLiteral("Could not rename tag: %1").arg(m_database.lastError()), true);
        return false;
    }
    emit tagsChanged();
    refreshGallery();
    return true;
}

bool ScreenshotManager::deleteTag(const QString &tagId)
{
    if (!m_database.deleteTag(tagId)) {
        emit messageChanged(QStringLiteral("Could not delete tag: %1").arg(m_database.lastError()), true);
        return false;
    }
    if (m_galleryTagFilter == tagId) {
        m_galleryTagFilter.clear();
        emit galleryTagFilterChanged();
    }
    if (m_settings->automaticClipboardSaveTagId() == tagId)
        m_settings->setAutomaticClipboardSaveTagId({});
    emit tagsChanged();
    refreshGallery();
    return true;
}

int ScreenshotManager::tagAssignmentState(const QString &tagId, const QStringList &screenshotIds) const
{
    return m_database.tagAssignmentState(tagId, screenshotIds);
}

bool ScreenshotManager::setTagForScreenshots(const QStringList &screenshotIds, const QString &tagId, bool assigned)
{
    if (!m_database.setTagForScreenshots(screenshotIds, tagId, assigned)) {
        emit messageChanged(QStringLiteral("Could not update screenshot tags: %1").arg(m_database.lastError()), true);
        return false;
    }
    refreshGallery();
    return true;
}

void ScreenshotManager::setGalleryTagFilter(const QString &tagId)
{
    if (m_galleryTagFilter == tagId)
        return;
    m_galleryTagFilter = tagId;
    emit galleryTagFilterChanged();
    refreshGallery();
}

QPointF ScreenshotManager::editorPointToImage(qreal x, qreal y, qreal displayWidth, qreal displayHeight,
                                               int imageWidth, int imageHeight) const
{
    return ScreenshotEditorImage::displayToImagePoint(QPointF(x, y), QSizeF(displayWidth, displayHeight),
                                                       QSize(imageWidth, imageHeight));
}

QVariantMap ScreenshotManager::editorCropFromDrag(qreal startX, qreal startY, qreal endX, qreal endY,
                                                   int imageWidth, int imageHeight, qreal aspectRatio) const
{
    const QRectF crop = ScreenshotEditorImage::cropFromDrag(QPointF(startX, startY), QPointF(endX, endY),
                                                            QSize(imageWidth, imageHeight), aspectRatio);
    return {{QStringLiteral("x"), crop.x()}, {QStringLiteral("y"), crop.y()},
            {QStringLiteral("width"), crop.width()}, {QStringLiteral("height"), crop.height()}};
}

QString ScreenshotManager::saveEditedScreenshot(const QString &sourceScreenshotId, const QVariantMap &cropMap,
                                                 const QVariantList &strokes, bool inheritTags)
{
    m_lastSaveError.clear();
    auto fail = [this](const QString &message) {
        m_lastSaveError = message;
        emit messageChanged(message, true);
        return QString();
    };
    if (!m_databaseReady)
        return fail(m_database.lastError().isEmpty() ? QStringLiteral("Screenshot database is unavailable.") : m_database.lastError());
    const ScreenshotRecord source = m_database.screenshotById(sourceScreenshotId);
    if (source.id.isEmpty() || !QFileInfo(source.path).isFile())
        return fail(QStringLiteral("The source screenshot is no longer available."));
    if (source.width <= 0 || source.height <= 0 ||
        qint64(source.width) * source.height > 64LL * 1024 * 1024 ||
        qint64(source.width) * source.height * 4 > 256LL * 1024 * 1024)
        return fail(QStringLiteral("The source image exceeds the 64 megapixel / 256 MiB editing limit."));

    QRect crop(0, 0, source.width, source.height);
    if (cropMap.contains(QStringLiteral("width")) || cropMap.contains(QStringLiteral("height"))) {
        const qreal x = cropMap.value(QStringLiteral("x")).toDouble();
        const qreal y = cropMap.value(QStringLiteral("y")).toDouble();
        const qreal width = cropMap.value(QStringLiteral("width")).toDouble();
        const qreal height = cropMap.value(QStringLiteral("height")).toDouble();
        const qreal right = x + width;
        const qreal bottom = y + height;
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) || !std::isfinite(height) ||
            !std::isfinite(right) || !std::isfinite(bottom))
            return fail(QStringLiteral("The crop area is invalid."));
        if (width <= 0.0 || height <= 0.0)
            return fail(QStringLiteral("The crop area is empty."));
        if (x < 0.0 || y < 0.0 || right > source.width || bottom > source.height)
            return fail(QStringLiteral("The crop area is outside the source image."));
        crop = QRect(qFloor(x), qFloor(y), qCeil(right) - qFloor(x), qCeil(bottom) - qFloor(y))
                   .intersected(QRect(0, 0, source.width, source.height));
    }
    if (crop.width() <= 0 || crop.height() <= 0)
        return fail(QStringLiteral("The crop area is empty."));
    const qint64 outputBytes = qint64(crop.width()) * crop.height() * 4;
    const qint64 sourceWorkingBytes = qint64(source.width) * source.height * 4;
    const qint64 processingBytes = sourceWorkingBytes + outputBytes;
    qint64 queuedBytes = 0;
    for (qint64 bytes : std::as_const(m_pendingBytes))
        queuedBytes += bytes;
    if (outputBytes > 256LL * 1024 * 1024 || processingBytes > 512LL * 1024 * 1024 ||
        queuedBytes + m_fingerprintBytes + processingBytes > 512LL * 1024 * 1024 ||
        m_pendingPaths.size() + m_fingerprintJobs.size() >= 2)
        return fail(QStringLiteral("The edited image exceeds the 64 megapixel / 256 MiB limit, or two saves are already pending."));

    const QString directoryPath = resolvedDestination();
    if (!QDir(directoryPath).exists() || !QFileInfo(directoryPath).isDir())
        return fail(QStringLiteral("Screenshot destination is unavailable: %1").arg(directoryPath));
    const QString targetPath = nextFilePath(directoryPath);
    m_pendingPaths.insert(targetPath);
    m_pendingBytes.insert(targetPath, processingBytes);
    SaveMetadata metadata;
    metadata.sourceScreenshotId = sourceScreenshotId;
    metadata.inheritTags = inheritTags;
    metadata.source = QStringLiteral("edited-copy");
    m_saveMetadata.insert(targetPath, metadata);

    auto *watcher = new QFutureWatcher<SaveResult>(this);
    connect(watcher, &QFutureWatcher<SaveResult>::finished, this, [this, watcher, targetPath] {
        const SaveResult result = watcher->result();
        watcher->deleteLater();
        completeSave(targetPath, result);
    });
    const QFuture<SaveResult> future = QtConcurrent::run([sourcePath = source.path, targetPath, crop, strokes] {
        QImage output;
        QString error;
        if (!ScreenshotEditorImage::render(sourcePath, crop, strokes, &output, &error))
            return SaveResult{false, error};
        return writeImage(output, targetPath);
    });
    m_jobs.insert(targetPath, future);
    watcher->setFuture(future);
    return targetPath;
}

void ScreenshotManager::acceptAutomaticClipboardImage(const QImage &image)
{
    if (!automaticClipboardSavingEnabled() || m_automaticClipboardSavingPaused)
        return;
    constexpr qint64 maximumBytes = 256LL * 1024 * 1024;
    const qint64 imageBytes = image.sizeInBytes();
    const qint64 hashWorkingBytes = imageBytes + qint64(image.width()) * 4;
    if (image.isNull() || image.width() <= 0 || image.height() <= 0 ||
        imageBytes <= 0 || imageBytes > maximumBytes ||
        qint64(image.width()) * image.height() > 64LL * 1024 * 1024) {
        emit messageChanged(QStringLiteral("Automatic clipboard save skipped: image exceeds the 64 megapixel / 256 MiB limit."), true);
        return;
    }

    qint64 queuedBytes = m_fingerprintBytes;
    for (qint64 bytes : std::as_const(m_pendingBytes))
        queuedBytes += bytes;
    if (m_pendingPaths.size() + m_fingerprintJobs.size() >= 2 ||
        queuedBytes + hashWorkingBytes > 512LL * 1024 * 1024) {
        emit messageChanged(QStringLiteral("Automatic clipboard save queue is full. The image was not saved; try copying it again after a save finishes."), true);
        return;
    }

    AutomaticEvent event;
    event.image = image;
    event.destination = resolvedDestination();
    event.profileId = m_profiles->activeProfileId();
    event.tagId = m_settings->automaticClipboardSaveTagId();
    event.reservedBytes = hashWorkingBytes;
    if (!m_databaseReady) {
        emit messageChanged(QStringLiteral("Automatic clipboard save is unavailable: %1")
                                .arg(m_database.lastError().isEmpty() ? QStringLiteral("screenshot database is unavailable") : m_database.lastError()), true);
        return;
    }
    if (!QDir(event.destination).exists() || !QFileInfo(event.destination).isDir()) {
        emit messageChanged(QStringLiteral("Automatic clipboard save destination is unavailable: %1. No other folder was used.").arg(event.destination), true);
        return;
    }
    const quint64 eventId = m_nextAutomaticEventId++;
    m_automaticEvents.insert(eventId, event);
    m_fingerprintBytes += hashWorkingBytes;
    auto *watcher = new QFutureWatcher<QByteArray>(this);
    connect(watcher, &QFutureWatcher<QByteArray>::finished, this, [this, watcher, eventId] {
        const QByteArray fingerprint = watcher->result();
        watcher->deleteLater();
        finishAutomaticFingerprint(eventId, fingerprint);
    });
    const QFuture<QByteArray> future = QtConcurrent::run([image] { return imageFingerprint(image); });
    m_fingerprintJobs.insert(eventId, future);
    watcher->setFuture(future);
}

void ScreenshotManager::finishAutomaticFingerprint(quint64 eventId, const QByteArray &fingerprint)
{
    if (!m_automaticEvents.contains(eventId))
        return;
    m_readyFingerprints.insert(eventId, fingerprint);
    while (m_readyFingerprints.contains(m_nextFingerprintToProcess)) {
        const QByteArray nextFingerprint = m_readyFingerprints.take(m_nextFingerprintToProcess);
        processAutomaticFingerprint(m_nextFingerprintToProcess, nextFingerprint);
        ++m_nextFingerprintToProcess;
    }
}

void ScreenshotManager::processAutomaticFingerprint(quint64 eventId, const QByteArray &fingerprint)
{
    const auto eventIt = m_automaticEvents.find(eventId);
    if (eventIt == m_automaticEvents.end())
        return;
    AutomaticEvent event = eventIt.value();
    m_fingerprintBytes -= event.reservedBytes;
    m_automaticEvents.erase(eventIt);
    m_fingerprintJobs.remove(eventId);
    if (fingerprint.isEmpty()) {
        emit messageChanged(QStringLiteral("Automatic clipboard save could not fingerprint the image."), true);
        return;
    }
    if (m_savedAutomaticFingerprints.contains(fingerprint) ||
        m_inFlightAutomaticFingerprints.contains(fingerprint))
        return;

    qint64 queuedBytes = 0;
    for (qint64 bytes : std::as_const(m_pendingBytes))
        queuedBytes += bytes;
    if (m_pendingPaths.size() >= 2 || queuedBytes + event.image.sizeInBytes() > 512LL * 1024 * 1024) {
        emit messageChanged(QStringLiteral("Automatic clipboard save queue is full. The image was not saved; try copying it again after a save finishes."), true);
        return;
    }
    if (!QDir(event.destination).exists() || !QFileInfo(event.destination).isDir()) {
        emit messageChanged(QStringLiteral("Automatic clipboard save destination is unavailable: %1. No other folder was used.").arg(event.destination), true);
        return;
    }
    const QString path = nextFilePath(event.destination);
    SaveMetadata metadata;
    metadata.source = QStringLiteral("clipboard-auto");
    metadata.captureProfileId = event.profileId;
    metadata.tagId = event.tagId;
    metadata.automatic = true;
    metadata.automaticFingerprint = fingerprint;
    m_inFlightAutomaticFingerprints.insert(fingerprint);
    queueImageWrite(event.image, path, metadata);
}

QByteArray ScreenshotManager::imageFingerprint(const QImage &image)
{
    if (image.isNull())
        return {};
    // Canonical RGBA bytes make fingerprints independent of QImage's source
    // pixel format. Convert one row at a time to keep the temporary memory
    // bounded even for wide images. This runs on a worker, never in the UI.
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray header;
    header.reserve(8);
    for (int i = 0; i < 4; ++i) {
        header.append(char((quint32(image.width()) >> (i * 8)) & 0xff));
        header.append(char((quint32(image.height()) >> (i * 8)) & 0xff));
    }
    hash.addData(header);
    QByteArray row(qsizetype(image.width()) * 4, Qt::Uninitialized);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor color = image.pixelColor(x, y);
            const qsizetype offset = qsizetype(x) * 4;
            row[offset] = char(color.red());
            row[offset + 1] = char(color.green());
            row[offset + 2] = char(color.blue());
            row[offset + 3] = char(color.alpha());
        }
        hash.addData(QByteArrayView(row.constData(), row.size()));
    }
    return hash.result();
}

QString ScreenshotManager::resolvedDestination() const
{
    if (!m_destinationOverride.isEmpty())
        return m_destinationOverride;
    const QString profileDirectory = m_profiles->activeScreenshotDirectory();
    return profileDirectory.isEmpty() ? m_settings->generalScreenshotDirectory() : profileDirectory;
}

QString ScreenshotManager::nextFilePath(const QString &directory) const
{
    return ScreenshotNaming::uniqueFilePath(directory, QDateTime::currentDateTime(), m_pendingPaths,m_settings->filenamePattern());
}

bool ScreenshotManager::addToCollection(const QString &path, const QString &sourceScreenshotId, bool inheritTags,
                                        const QString &captureProfileId, const QString &tagId, const QString &source)
{
    if (!m_databaseReady) {
        m_lastSaveError = m_database.lastError().isEmpty()
                              ? QStringLiteral("Screenshot database is unavailable.")
                              : m_database.lastError();
        return false;
    }
    const QFileInfo file(path);
    QImageReader reader(path);
    const QString recordSource = source.isEmpty()
        ? (sourceScreenshotId.isEmpty() ? QStringLiteral("clipboard") : QStringLiteral("edited-copy"))
        : source;
    if (!m_database.addScreenshot(file.absoluteFilePath(), reader.size(), file.size(), recordSource, nullptr,
                                  sourceScreenshotId, inheritTags, captureProfileId, tagId)) {
        m_lastSaveError = m_database.lastError();
        return false;
    }
    refreshGallery();
    return true;
}

void ScreenshotManager::refreshGallery()
{
    if (!m_databaseReady)
        return;
    m_gallery.setDirectory(resolvedDestination());
    m_gallery.setRecords(m_database.screenshots(resolvedDestination(), m_galleryTagFilter));
}

ScreenshotManager::SaveResult ScreenshotManager::writeImage(const QImage &image, const QString &path)
{
    // Stage in the same directory, then use a no-replace rename. A competitor
    // creating the target after name allocation must never be overwritten.
    QString staging;
    {
        QTemporaryFile file(QDir(QFileInfo(path).absolutePath()).filePath(QStringLiteral(".capture-XXXXXX.tmp")));
        if(!file.open())return {false,file.errorString()};
        QImageWriter writer(&file,QByteArrayLiteral("png"));
        if(!writer.write(image))return {false,writer.errorString()};
        if(!file.flush())return {false,file.errorString()};
        staging=file.fileName();file.setAutoRemove(false);
    } // Close the native handle before MoveFileEx; QTemporaryFile::close keeps it open.
    const auto cleanup=qScopeGuard([&]{QFile::remove(staging);});
#ifdef Q_OS_WIN
    if(!MoveFileExW(reinterpret_cast<LPCWSTR>(staging.utf16()),reinterpret_cast<LPCWSTR>(path.utf16()),MOVEFILE_WRITE_THROUGH))
        return {false,QStringLiteral("Cannot commit screenshot without replacing an existing file (Windows error %1).").arg(GetLastError())};
#else
    if(!QFile::rename(staging,path))return {false,QStringLiteral("Could not commit screenshot without overwriting another file.")};
#endif
    return {true,{}};
}
