#include "ScreenshotManager.h"
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
#include <QScopeGuard>
#include <QStandardPaths>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QDesktopServices>
#include <QUrl>
#include <QDebug>
#include <QByteArray>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
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
    default: return {};
    }
}

QHash<int, QByteArray> ScreenshotGalleryModel::roleNames() const
{
    return {{FileNameRole, "fileName"}, {PathRole, "path"}, {ImageUrlRole, "imageUrl"},
            {ModifiedRole, "modified"}, {WidthRole, "pixelWidth"}, {HeightRole, "pixelHeight"},
            {SizeBytesRole, "sizeBytes"}, {MissingRole, "missing"}, {ThumbnailUrlRole,"thumbnailUrl"}};
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
    m_managedPaths = paths;
    refresh();
}

void ScreenshotGalleryModel::refresh()
{
    ++m_generation;
    if(m_loading)return;
    const quint64 generation=m_generation;
    if(!m_loading){m_loading=true;emit loadingChanged();}
    const QString directory=QDir(m_directory).absolutePath();
    const QStringList paths=m_managedPaths;
    auto *watcher=new QFutureWatcher<QVector<Item>>(this);
    connect(watcher,&QFutureWatcher<QVector<Item>>::finished,this,[this,watcher,generation]{
        const auto items=watcher->result();watcher->deleteLater();
        if(generation!=m_generation){m_loading=false;refresh();return;}
        beginResetModel();m_items=items;endResetModel();m_loading=false;emit loadingChanged();emit countChanged();
    });
    watcher->setFuture(QtConcurrent::run([directory,paths]{
        QVector<Item> fresh;
        for(const QString &path:paths) {
            const QFileInfo file(path);if(file.absolutePath().compare(directory,Qt::CaseInsensitive)!=0)continue;
            if(!file.exists() || !file.isFile()){fresh.append({file.absoluteFilePath(),file.fileName(),{},0,{},true});continue;}
            QImageReader reader(file.absoluteFilePath());
            fresh.append({file.absoluteFilePath(),file.fileName(),file.lastModified(),file.size(),reader.size(),false});
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

ScreenshotManager::ScreenshotManager(ProfileManager *profiles, SettingsManager *settings, QObject *parent)
    : QObject(parent), m_profiles(profiles), m_settings(settings),
      m_clipboard(QGuiApplication::clipboard()), m_gallery(this)
{
    loadCollectionIndex();
    m_gallery.setManagedPaths(m_collectionPaths);
    connect(m_clipboard, &QClipboard::dataChanged, this, &ScreenshotManager::updateClipboardState);
    connect(m_profiles, &ProfileManager::activeProfileChanged, this, [this] {
        m_gallery.setDirectory(resolvedDestination());
        emit destinationChanged();
    });
    connect(m_profiles, &ProfileManager::profileDataChanged, this, [this](const QString &id) {
        if (id == m_profiles->activeProfileId() && m_destinationOverride.isEmpty()) {
            m_gallery.setDirectory(resolvedDestination());
            emit destinationChanged();
        }
    });
    connect(m_settings, &SettingsManager::generalScreenshotDirectoryChanged, this, [this] {
        if (m_destinationOverride.isEmpty() && m_profiles->activeScreenshotDirectory().isEmpty()) {
            m_gallery.setDirectory(resolvedDestination());
            emit destinationChanged();
        }
    });
    m_gallery.setDirectory(resolvedDestination());
    updateClipboardState();
}

QString ScreenshotManager::destination() const { return resolvedDestination(); }
bool ScreenshotManager::destinationOverridden() const { return !m_destinationOverride.isEmpty(); }
bool ScreenshotManager::clipboardHasImage() const { return m_clipboardHasImage; }
ScreenshotGalleryModel *ScreenshotManager::gallery() { return &m_gallery; }

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
    qint64 queuedBytes=0;for(auto bytes:m_pendingBytes)queuedBytes+=bytes;
    if(image.sizeInBytes()>maximumBytes || qint64(image.width())*image.height()>64*1024*1024 || queuedBytes+image.sizeInBytes()>512LL*1024*1024 || m_pendingPaths.size()>=2) {
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
    m_pendingPaths.insert(path);m_pendingBytes.insert(path,image.sizeInBytes());
    auto *watcher = new QFutureWatcher<SaveResult>(this);
    connect(watcher, &QFutureWatcher<SaveResult>::finished, this, [this, watcher, path] {
        const SaveResult result = watcher->result();
        watcher->deleteLater();
        completeSave(path,result);
    });
    const QFuture<SaveResult> future=QtConcurrent::run([image,path]{return writeImage(image,path);});
    m_jobs.insert(path,future);
    watcher->setFuture(future);
    return path;
}

void ScreenshotManager::completeSave(const QString &path,const SaveResult &result) {
        if(!m_pendingPaths.contains(path))return;
        m_jobs.remove(path);
        m_pendingPaths.remove(path);m_pendingBytes.remove(path);
        if (!result.success) {
            const QString message = QStringLiteral("Could not save screenshot: %1").arg(result.error);
            emit messageChanged(message, true);
            emit saveFinished(path, false, message);
            return;
        }
        if (!addToCollection(path)) {
            const QString message = QStringLiteral("Screenshot was saved, but the gallery index could not be updated: %1")
                                        .arg(path);
            emit messageChanged(message, true);
            emit saveFinished(path, false, message);
            return;
        }
        const QString message = QStringLiteral("Screenshot saved to %1").arg(QFileInfo(path).fileName());
        emit messageChanged(message, false);
        emit saveFinished(path, true, message);
}

void ScreenshotManager::finishPendingSaves() {
    const auto jobs=m_jobs;
    for(auto it=jobs.begin();it!=jobs.end();++it){auto future=it.value();future.waitForFinished();completeSave(it.key(),future.result());}
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

bool ScreenshotManager::copyImageToClipboard(const QString &path) const
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
    m_clipboard->setImage(image);
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
    const int index = m_collectionPaths.indexOf(oldPath);
    if (index < 0 || !QFileInfo::exists(oldPath)) {
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

    m_collectionPaths[index] = newPath;
    if (!saveCollectionIndex()) {
        m_collectionPaths[index] = oldPath;
        QFile::rename(newPath, oldPath);
        emit messageChanged(QStringLiteral("Could not update the screenshot gallery index."), true);
        return {};
    }
    m_gallery.setManagedPaths(m_collectionPaths);
    emit messageChanged(QStringLiteral("Screenshot renamed."), false);
    return newPath;
}

bool ScreenshotManager::removeFromCollection(const QString &path)
{
    const QString normalized = QFileInfo(path).absoluteFilePath();
    const int index = m_collectionPaths.indexOf(normalized);
    if (index < 0)
        return false;
    const QString previous = m_collectionPaths.takeAt(index);
    if (!saveCollectionIndex()) {
        m_collectionPaths.insert(index, previous);
        emit messageChanged(QStringLiteral("Could not update the screenshot gallery index."), true);
        return false;
    }
    m_gallery.setManagedPaths(m_collectionPaths);
    emit messageChanged(QStringLiteral("Removed from the gallery. The screenshot file was kept."), false);
    return true;
}

bool ScreenshotManager::deleteScreenshotFile(const QString &path)
{
    const QString normalized = QFileInfo(path).absoluteFilePath();
    const int index = m_collectionPaths.indexOf(normalized);
    if (index < 0 || !QFileInfo::exists(normalized)) {
        emit messageChanged(QStringLiteral("That screenshot file is no longer available."), true);
        return false;
    }
    const QString previous = m_collectionPaths.takeAt(index);
    if (!saveCollectionIndex()) {
        m_collectionPaths.insert(index, previous);
        emit messageChanged(QStringLiteral("Could not update the screenshot gallery index."), true);
        return false;
    }
    if (!QFile::remove(normalized)) {
        m_collectionPaths.insert(index, previous);
        saveCollectionIndex();
        emit messageChanged(QStringLiteral("Could not delete the screenshot file."), true);
        return false;
    }
    m_gallery.setManagedPaths(m_collectionPaths);
    emit messageChanged(QStringLiteral("Screenshot file deleted."), false);
    return true;
}

void ScreenshotManager::updateClipboardState()
{
    const bool hasImage = m_clipboard && m_clipboard->mimeData() && m_clipboard->mimeData()->hasImage();
    if (hasImage == m_clipboardHasImage)
        return;
    m_clipboardHasImage = hasImage;
    emit clipboardHasImageChanged();
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

void ScreenshotManager::loadCollectionIndex()
{
    const QString path = QDir(AppDataPaths::root())
                             .filePath(QStringLiteral("screenshot-index.json"));
    QFile file(path);
    if (!file.exists())
        return;
    if (!file.open(QIODevice::ReadOnly)) {
        m_collectionIndexValid = false;
        qWarning().noquote() << "Cannot read screenshot collection index; preserving it.";
        return;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject() ||
        document.object().value(QStringLiteral("schemaVersion")).toInt() != 1 ||
        !document.object().value(QStringLiteral("paths")).isArray()) {
        m_collectionIndexValid = false;
        qWarning().noquote() << "Screenshot collection index is invalid; preserving it.";
        return;
    }
    const QJsonArray paths = document.object().value(QStringLiteral("paths")).toArray();
    for (const QJsonValue &value : paths) {
        const QString itemPath = value.toString();
        if (!itemPath.isEmpty() && !m_collectionPaths.contains(itemPath))
            m_collectionPaths.append(itemPath);
    }
}

bool ScreenshotManager::saveCollectionIndex()
{
    const QString path = QDir(AppDataPaths::root())
                             .filePath(QStringLiteral("screenshot-index.json"));
    if (!m_collectionIndexValid && QFileInfo::exists(path)) {
        const QString backupPath = path + QStringLiteral(".invalid-") +
                                   QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss"));
        if (!QFile::rename(path, backupPath))
            return false;
        m_collectionIndexValid = true;
    }
    QJsonArray paths;
    for (const QString &itemPath : m_collectionPaths)
        paths.append(itemPath);
    const QJsonObject root{{QStringLiteral("schemaVersion"), 1}, {QStringLiteral("paths"), paths}};
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit())
        return false;
    m_collectionIndexValid = true;
    return true;
}

bool ScreenshotManager::addToCollection(const QString &path)
{
    const QString normalized = QFileInfo(path).absoluteFilePath();
    for (const QString &existing : std::as_const(m_collectionPaths)) {
        if (existing.compare(normalized, Qt::CaseInsensitive) == 0)
            return true;
    }
    m_collectionPaths.append(normalized);
    if (!saveCollectionIndex()) {
        m_collectionPaths.removeLast();
        return false;
    }
    m_gallery.setManagedPaths(m_collectionPaths);
    return true;
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
