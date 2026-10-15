#include "FileShelfManager.h"

#include "../clipboard/ClipboardService.h"
#include "../app/LocalFileUrl.h"
#include "../screenshots/ScreenshotManager.h"

#include <QDir>
#include <QDirIterator>
#include <QDrag>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMimeData>
#include <QDesktopServices>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QUuid>
#include <QWindow>
#include <QtConcurrent/QtConcurrentRun>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
QString normalized(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

struct ComparablePath {
    QString path;
    QString fileIdentity;
};

QString finalPathForExistingNode(const QString &path, QString *fileIdentity)
{
#ifdef Q_OS_WIN
    const HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), 0,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle != INVALID_HANDLE_VALUE) {
        const DWORD required = GetFinalPathNameByHandleW(handle, nullptr, 0,
                                                          FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        QString resolved;
        if (required > 0) {
            QVector<wchar_t> buffer(static_cast<qsizetype>(required) + 1);
            const DWORD written = GetFinalPathNameByHandleW(handle, buffer.data(),
                                                              static_cast<DWORD>(buffer.size()),
                                                              FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
            if (written > 0 && written < buffer.size())
                resolved = QString::fromWCharArray(buffer.constData(), static_cast<qsizetype>(written));
        }
        if (fileIdentity) {
            FILE_ID_INFO info{};
            if (GetFileInformationByHandleEx(handle, FileIdInfo, &info, sizeof(info))) {
                QByteArray identity = QByteArray::number(info.VolumeSerialNumber, 16) + ':';
                identity.append(reinterpret_cast<const char *>(info.FileId.Identifier), sizeof(info.FileId.Identifier));
                *fileIdentity = QString::fromLatin1(identity.toHex());
            }
        }
        CloseHandle(handle);
        if (!resolved.isEmpty()) {
            if (resolved.startsWith(QStringLiteral("\\\\?\\UNC\\"), Qt::CaseInsensitive))
                resolved = QStringLiteral("\\\\") + resolved.mid(8);
            else if (resolved.startsWith(QStringLiteral("\\\\?\\")))
                resolved.remove(0, 4);
            return resolved;
        }
    }
#else
    Q_UNUSED(fileIdentity)
#endif
    return QFileInfo(path).canonicalFilePath();
}

bool comparablePath(const QString &path, ComparablePath *result)
{
    if (!result || path.trimmed().isEmpty())
        return false;
    QFileInfo node(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
    QStringList suffix;
    while (!node.exists()) {
        if (node.isSymLink())
            return false;
        const QString name = node.fileName();
        const QString parent = node.dir().absolutePath();
        if (name.isEmpty() || parent == node.absoluteFilePath())
            return false;
        suffix.prepend(name);
        node.setFile(parent);
    }
    QString identity;
    QString resolved = finalPathForExistingNode(node.absoluteFilePath(), &identity);
    if (resolved.isEmpty())
        return false;
    for (const QString &part : suffix)
        resolved = QDir(resolved).filePath(part);
    result->path = QDir::fromNativeSeparators(QDir::cleanPath(resolved)).toCaseFolded();
    result->fileIdentity = identity;
    return true;
}

bool sameLocation(const QString &left, const QString &right)
{
    ComparablePath a, b;
    if (!comparablePath(left, &a) || !comparablePath(right, &b))
        return false;
    return a.path == b.path || (!a.fileIdentity.isEmpty() && a.fileIdentity == b.fileIdentity);
}

bool rejectUnsafeFolderDestination(const QString &source, const QString &target, QString *error)
{
    ComparablePath resolvedSource, resolvedTarget;
    if (!comparablePath(source, &resolvedSource) || !comparablePath(target, &resolvedTarget)) {
        if (error) *error = QStringLiteral("Could not resolve the source or destination through filesystem aliases; the operation was rejected.");
        return true;
    }
    if (resolvedSource.path == resolvedTarget.path ||
        (!resolvedSource.fileIdentity.isEmpty() && resolvedSource.fileIdentity == resolvedTarget.fileIdentity)) {
        if (error) *error = QStringLiteral("Source and destination resolve to the same filesystem location; the operation was rejected.");
        return true;
    }
    if (resolvedTarget.path.startsWith(resolvedSource.path.endsWith(QLatin1Char('/'))
                                           ? resolvedSource.path
                                           : resolvedSource.path + QLatin1Char('/'))) {
        if (error) *error = QStringLiteral("Destination resolves inside the source folder; the operation was rejected.");
        return true;
    }
    return false;
}

bool copyStream(QIODevice *input, QIODevice *output, QString *error)
{
    QByteArray buffer(1024 * 1024, Qt::Uninitialized);
    while (true) {
        const qint64 count = input->read(buffer.data(), buffer.size());
        if (count < 0) {
            if (error) *error = input->errorString();
            return false;
        }
        if (count == 0)
            return true;
        if (output->write(buffer.constData(), count) != count) {
            if (error) *error = output->errorString();
            return false;
        }
    }
}

bool copyFileSafely(const QString &source, const QString &target, bool replace, QString *error)
{
    if (sameLocation(source, target)) {
        if (error) *error = QStringLiteral("Source and destination resolve to the same filesystem location; the operation was rejected.");
        return false;
    }
    QFile input(source);
    if (!input.open(QIODevice::ReadOnly)) {
        if (error) *error = input.errorString();
        return false;
    }
    if (QFileInfo::exists(target)) {
        if (!replace) {
            if (error) *error = QStringLiteral("A file with this name appeared after the conflict preview.");
            return false;
        }
        QSaveFile output(target);
        output.setDirectWriteFallback(false);
        if (!output.open(QIODevice::WriteOnly)) {
            if (error) *error = output.errorString();
            return false;
        }
        if (!copyStream(&input, &output, error) || !output.commit()) {
            if (error && error->isEmpty()) *error = output.errorString();
            output.cancelWriting();
            return false;
        }
        return true;
    }

    const QString parent = QFileInfo(target).absolutePath();
    QString stagedPath;
    {
        QTemporaryFile output(QDir(parent).filePath(QStringLiteral(".smalldtl-copy-XXXXXX")));
        if (!output.open()) {
            if (error) *error = output.errorString();
            return false;
        }
        stagedPath = output.fileName();
        if (!copyStream(&input, &output, error))
            return false;
        output.setAutoRemove(false);
        output.close();
    }
    if (QFileInfo::exists(target)) {
        QFile::remove(stagedPath);
        if (error) *error = QStringLiteral("The destination appeared after preview; no existing file was replaced.");
        return false;
    }
    QFile stagedFile(stagedPath);
    if (!stagedFile.rename(target)) {
        const QString detail = stagedFile.errorString();
        QFile::remove(stagedPath);
        if (error) *error = QStringLiteral("Could not commit the staged file to %1: %2").arg(target, detail);
        return false;
    }
    return true;
}

bool copyDirectoryContents(const QString &source, const QString &target, QString *error)
{
    if (!QDir().mkpath(target)) {
        if (error) *error = QStringLiteral("Could not create destination folder.");
        return false;
    }
    const QDir sourceDir(source);
    QDirIterator it(source, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString from = it.next();
        const QFileInfo info = it.fileInfo();
        if (info.isSymLink()) {
            if (error) *error = QStringLiteral("Folder contains a symbolic link that cannot be copied safely: %1").arg(from);
            return false;
        }
        const QString relative = sourceDir.relativeFilePath(from);
        const QString to = QDir(target).filePath(relative);
        if (info.isDir()) {
            if (!QDir().mkpath(to)) {
                if (error) *error = QStringLiteral("Could not create folder: %1").arg(to);
                return false;
            }
        } else if (!copyFileSafely(from, to, false, error)) {
            return false;
        }
    }
    return true;
}

bool movePath(const QString &source, const QString &target)
{
    return QDir().rename(source, target);
}

bool pathIsSame(const QString &left, const QString &right)
{
    return sameLocation(left, right);
}

QString availableRename(const QString &target)
{
    const QFileInfo info(target);
    const QString base = info.completeBaseName().isEmpty() ? info.fileName() : info.completeBaseName();
    const QString suffix = info.suffix();
    for (int index = 2; index < 1000000; ++index) {
        QString name = QStringLiteral("%1 (%2)").arg(base).arg(index);
        if (!suffix.isEmpty())
            name += QLatin1Char('.') + suffix;
        const QString candidate = QDir(info.absolutePath()).filePath(name);
        if (!QFileInfo::exists(candidate))
            return candidate;
    }
    return {};
}

bool copyDirectorySafely(const QString &source, const QString &target, bool replace, QString *error)
{
    if (rejectUnsafeFolderDestination(source, target, error))
        return false;
    const QString parent = QFileInfo(target).absolutePath();
    const QString stage = QDir(parent).filePath(QStringLiteral(".smalldtl-folder-%1")
                                                    .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    if (!copyDirectoryContents(source, stage, error)) {
        QDir(stage).removeRecursively();
        return false;
    }
    QString backup;
    if (QFileInfo::exists(target)) {
        if (!replace) {
            QDir(stage).removeRecursively();
            if (error) *error = QStringLiteral("A folder with this name appeared after the conflict preview.");
            return false;
        }
        if (rejectUnsafeFolderDestination(source, target, error)) {
            QDir(stage).removeRecursively();
            return false;
        }
        backup = QDir(parent).filePath(QStringLiteral(".smalldtl-replaced-%1")
                                           .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        if (!movePath(target, backup)) {
            QDir(stage).removeRecursively();
            if (error) *error = QStringLiteral("Could not stage the existing destination folder for replacement.");
            return false;
        }
    }
    if (!movePath(stage, target)) {
        QDir(stage).removeRecursively();
        const bool restored = backup.isEmpty() || movePath(backup, target);
        if (error) {
            if (!restored)
                *error = QStringLiteral("Could not commit the copied folder; the previous destination is preserved at %1 and needs manual recovery.").arg(backup);
            else
                *error = QStringLiteral("Could not commit the copied folder; the previous destination was restored.");
        }
        return false;
    }
    if (!backup.isEmpty() && !QDir(backup).removeRecursively()) {
        if (error) *error = QStringLiteral("The new folder was committed, but the replaced destination could not be cleaned up: %1").arg(backup);
        return false;
    }
    return true;
}

bool removeSource(const QString &path, bool directory)
{
    return directory ? QDir(path).removeRecursively() : QFile::remove(path);
}

FileShelfManager::TransferSummary executeTransfers(const QVector<FileShelfManager::TransferItem> &items,
                                                   const QString &destination, bool move)
{
    FileShelfManager::TransferSummary result;
    for (const auto &item : items) {
        const QFileInfo sourceInfo(item.source);
        if (!sourceInfo.exists() || (item.directory ? !sourceInfo.isDir() : !sourceInfo.isFile())) {
            ++result.failed;
            result.details.append(QStringLiteral("%1: the source is unavailable.").arg(item.source));
            continue;
        }
        if (item.targetName.isEmpty() || QFileInfo(item.targetName).fileName() != item.targetName) {
            ++result.failed;
            result.details.append(QStringLiteral("%1: invalid destination name.").arg(item.source));
            continue;
        }
        QString target = QDir(destination).filePath(item.targetName);
        QString safetyError;
        if (item.directory && rejectUnsafeFolderDestination(item.source, target, &safetyError)) {
            ++result.failed;
            result.details.append(QStringLiteral("%1: %2").arg(item.source, safetyError));
            continue;
        }
        ComparablePath resolvedSource, resolvedTarget;
        if (!comparablePath(item.source, &resolvedSource) || !comparablePath(target, &resolvedTarget)) {
            ++result.failed;
            result.details.append(QStringLiteral("%1: source or destination could not be resolved safely through filesystem aliases; the operation was rejected.").arg(item.source));
            continue;
        }
        if (pathIsSame(target, item.source)) {
            ++result.failed;
            result.details.append(QStringLiteral("%1: source and destination resolve to the same filesystem location; the operation was rejected.").arg(item.source));
            continue;
        }
        QString resolution = item.resolution;
        if (QFileInfo::exists(target)) {
            if (resolution == QStringLiteral("skip")) {
                ++result.skipped;
                continue;
            }
            if (resolution == QStringLiteral("rename")) {
                target = availableRename(target);
                if (target.isEmpty()) {
                    ++result.failed;
                    result.details.append(QStringLiteral("%1: could not find an unused destination name.").arg(item.source));
                    continue;
                }
            } else if (resolution != QStringLiteral("replace")) {
                ++result.failed;
                result.details.append(QStringLiteral("%1: an existing destination requires skip, rename, or explicit replacement.").arg(item.source));
                continue;
            }
        }
        const bool replace = resolution == QStringLiteral("replace");
        QString error;
        if (item.directory && rejectUnsafeFolderDestination(item.source, target, &error)) {
            ++result.failed;
            result.details.append(QStringLiteral("%1: %2").arg(item.source, error));
            continue;
        }
        if (!comparablePath(item.source, &resolvedSource) || !comparablePath(target, &resolvedTarget)) {
            ++result.failed;
            result.details.append(QStringLiteral("%1: source or destination could not be resolved safely before staging; the operation was rejected.").arg(item.source));
            continue;
        }
        const bool copied = item.directory
            ? copyDirectorySafely(item.source, target, replace, &error)
            : copyFileSafely(item.source, target, replace, &error);
        if (!copied) {
            ++result.failed;
            result.details.append(QStringLiteral("%1: %2").arg(item.source, error));
            continue;
        }
        if (move && (item.directory ? rejectUnsafeFolderDestination(item.source, target, &error)
                                    : pathIsSame(item.source, target))) {
            ++result.failed;
            result.details.append(QStringLiteral("%1: %2").arg(item.source,
                error.isEmpty() ? QStringLiteral("source and destination resolve to the same filesystem location; the source was preserved.") : error));
            continue;
        }
        if (move && !removeSource(item.source, item.directory)) {
            ++result.failed;
            result.details.append(QStringLiteral("%1: copied to %2, but the source could not be removed.").arg(item.source, target));
            continue;
        }
        if (move)
            result.movedPaths.append(qMakePair(item.id, target));
        ++result.succeeded;
    }
    return result;
}
}

FileShelfManager::FileShelfManager(ScreenshotManager *screenshots, QObject *parent)
    : QAbstractListModel(parent), m_screenshots(screenshots)
{
    connect(&m_transferWatcher, &QFutureWatcher<TransferSummary>::finished, this, [this] {
        TransferSummary result = m_transferWatcher.result();
        for (const auto &entry : result.movedPaths) {
            if (!m_screenshots->database()->updateFileShelfPath(entry.first, entry.second)) {
                --result.succeeded;
                ++result.failed;
                result.details.append(QStringLiteral("%1: moved successfully but its shelf reference could not be updated: %2")
                                          .arg(entry.second, m_screenshots->database()->lastError()));
            }
        }
        setBusy(false);
        QString summary = QStringLiteral("Completed: %1, skipped: %2, failed: %3.")
                              .arg(result.succeeded).arg(result.skipped).arg(result.failed);
        setMessage(summary, result.failed > 0);
        if (!result.details.isEmpty())
            summary += QLatin1Char('\n') + result.details.join(QLatin1Char('\n'));
        emit operationFinished(result.succeeded, result.skipped, result.failed, result.details);
        refresh();
    });
    refresh();
}

FileShelfManager::~FileShelfManager()
{
    if (m_transferWatcher.isRunning())
        m_transferWatcher.waitForFinished();
    if (m_busy && m_screenshots) {
        const TransferSummary result = m_transferWatcher.result();
        for (const auto &entry : result.movedPaths)
            m_screenshots->database()->updateFileShelfPath(entry.first, entry.second);
    }
}

int FileShelfManager::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_records.size();
}

QVariant FileShelfManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_records.size())
        return {};
    const FileShelfRecord &record = m_records.at(index.row());
    const QFileInfo info(record.path);
    switch (role) {
    case IdRole: return record.id;
    case PathRole: return record.path;
    case DisplayNameRole: return record.displayName;
    case SourceFolderRole: return record.sourceFolder;
    case ItemTypeRole: return record.itemType;
    case FileTypeRole: {
        if (record.itemType == QStringLiteral("folder")) return QStringLiteral("Folder");
        const QString suffix = info.suffix();
        return suffix.isEmpty() ? QStringLiteral("File") : suffix.toUpper() + QStringLiteral(" file");
    }
    case SizeBytesRole: return info.exists() && info.isFile() ? info.size() : record.sizeBytes;
    case AvailableRole: return info.exists();
    default: return {};
    }
}

QHash<int, QByteArray> FileShelfManager::roleNames() const
{
    return {{IdRole, "entryId"}, {PathRole, "path"}, {DisplayNameRole, "displayName"},
            {SourceFolderRole, "sourceFolder"}, {ItemTypeRole, "itemType"},
            {FileTypeRole, "fileType"}, {SizeBytesRole, "sizeBytes"}, {AvailableRole, "available"}};
}

void FileShelfManager::refresh()
{
    if (!m_screenshots || !m_screenshots->database()->isReady())
        return;
    beginResetModel();
    m_records = m_screenshots->database()->fileShelfEntries();
    QSet<QString> currentIds;
    for (const FileShelfRecord &record : m_records)
        currentIds.insert(record.id);
    for (auto it = m_selected.begin(); it != m_selected.end();) {
        if (!currentIds.contains(*it)) it = m_selected.erase(it); else ++it;
    }
    if (!currentIds.contains(m_anchorId))
        m_anchorId.clear();
    endResetModel();
    emit countChanged();
    emit selectionChanged();
}

bool FileShelfManager::addPaths(const QStringList &paths)
{
    if (!m_screenshots || !m_screenshots->database()->isReady()) {
        setMessage(QStringLiteral("The file shelf database is unavailable."), true);
        return false;
    }
    QVector<FileShelfRecord> records;
    QStringList failures;
    for (const QString &rawPath : paths) {
        const QString path = normalized(rawPath);
        if (rawPath.trimmed().isEmpty())
            continue;
        const QFileInfo info(path);
        if (!info.exists() || (!info.isFile() && !info.isDir())) {
            failures.append(QStringLiteral("%1: path is unavailable or unsupported.").arg(rawPath));
            continue;
        }
        FileShelfRecord record;
        record.path = path;
        record.displayName = info.fileName();
        if (record.displayName.isEmpty())
            record.displayName = path;
        record.sourceFolder = info.isDir() ? info.absolutePath() : info.absolutePath();
        record.itemType = info.isDir() ? QStringLiteral("folder") : QStringLiteral("file");
        record.sizeBytes = info.isFile() ? info.size() : 0;
        records.append(record);
    }
    int inserted = 0;
    int duplicates = 0;
    if (!m_screenshots->database()->addFileShelfEntries(records, &inserted, &duplicates)) {
        setMessage(m_screenshots->database()->lastError(), true);
        return false;
    }
    refresh();
    QString message = QStringLiteral("Added %1 reference(s); %2 duplicate(s) skipped.").arg(inserted).arg(duplicates);
    if (!failures.isEmpty())
        message += QStringLiteral(" %1 path(s) failed: %2").arg(failures.size()).arg(failures.join(QStringLiteral("; ")));
    setMessage(message, !failures.isEmpty());
    return failures.isEmpty();
}

bool FileShelfManager::addFileUrls(const QVariantList &urls)
{
    if (urls.isEmpty()) {
        setMessage(QStringLiteral("No file URLs were dropped."), true);
        return false;
    }
    QStringList paths;
    paths.reserve(urls.size());
    for (const QVariant &value : urls) {
        QString path, error;
        if (!LocalFileUrl::toPath(value, &path, &error)) {
            setMessage(error, true);
            return false;
        }
        paths.append(path);
    }
    return addPaths(paths);
}

void FileShelfManager::addFilesDialog()
{
    const QStringList paths = QFileDialog::getOpenFileNames(nullptr, QStringLiteral("Add files to shelf"));
    if (!paths.isEmpty())
        addPaths(paths);
}

void FileShelfManager::addFolderDialog()
{
    const QString path = QFileDialog::getExistingDirectory(nullptr, QStringLiteral("Add folder to shelf"), {},
                                                            QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!path.isEmpty())
        addPaths({path});
}

int FileShelfManager::rowForId(const QString &id) const
{
    for (int row = 0; row < m_records.size(); ++row)
        if (m_records.at(row).id == id)
            return row;
    return -1;
}

void FileShelfManager::toggleSelection(const QString &id)
{
    if (rowForId(id) < 0)
        return;
    if (m_selected.contains(id)) m_selected.remove(id); else m_selected.insert(id);
    m_anchorId = id;
    emit selectionChanged();
}

void FileShelfManager::clickItem(const QString &id, bool control, bool shift)
{
    const int row = rowForId(id);
    if (row < 0)
        return;
    if (shift && !m_anchorId.isEmpty() && rowForId(m_anchorId) >= 0) {
        const int anchor = rowForId(m_anchorId);
        if (!control)
            m_selected.clear();
        for (int current = qMin(anchor, row); current <= qMax(anchor, row); ++current)
            m_selected.insert(m_records.at(current).id);
    } else if (control) {
        if (m_selected.contains(id)) m_selected.remove(id); else m_selected.insert(id);
        m_anchorId = id;
    } else {
        m_selected = {id};
        m_anchorId = id;
    }
    emit selectionChanged();
}

void FileShelfManager::selectAll()
{
    m_selected.clear();
    for (const FileShelfRecord &record : m_records)
        m_selected.insert(record.id);
    if (!m_records.isEmpty())
        m_anchorId = m_records.first().id;
    emit selectionChanged();
}

void FileShelfManager::clearSelection()
{
    if (m_selected.isEmpty())
        return;
    m_selected.clear();
    m_anchorId.clear();
    emit selectionChanged();
}

bool FileShelfManager::removeSelected()
{
    const QStringList ids = selectedIds();
    if (!m_screenshots->database()->removeFileShelfEntries(ids)) {
        setMessage(m_screenshots->database()->lastError(), true);
        return false;
    }
    clearSelection();
    refresh();
    setMessage(QStringLiteral("Removed %1 shelf reference(s). Source files were left in place.").arg(ids.size()));
    return true;
}

bool FileShelfManager::removeEntry(const QString &id)
{
    int removed = 0;
    if (!m_screenshots->database()->removeFileShelfEntries({id}, &removed)) {
        setMessage(m_screenshots->database()->lastError(), true);
        return false;
    }
    refresh();
    setMessage(QStringLiteral("Removed %1 shelf reference(s). Source files were left in place.").arg(removed));
    return true;
}

bool FileShelfManager::clearShelf()
{
    int removed = 0;
    if (!m_screenshots->database()->clearFileShelf(&removed)) {
        setMessage(m_screenshots->database()->lastError(), true);
        return false;
    }
    clearSelection();
    refresh();
    setMessage(QStringLiteral("Cleared %1 shelf reference(s). Source files were left in place.").arg(removed));
    return true;
}

bool FileShelfManager::openEntry(const QString &id)
{
    const int row = rowForId(id);
    if (row < 0) return false;
    const QString path = m_records.at(row).path;
    if (!QFileInfo::exists(path)) {
        setMessage(QStringLiteral("This shelf item is missing: %1").arg(path), true);
        return false;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
        setMessage(QStringLiteral("Windows could not open: %1").arg(path), true);
        return false;
    }
    return true;
}

bool FileShelfManager::openLocation(const QString &id)
{
    const int row = rowForId(id);
    if (row < 0) return false;
    const QString path = m_records.at(row).sourceFolder;
    if (!QFileInfo::exists(path)) {
        setMessage(QStringLiteral("This shelf location is missing: %1").arg(path), true);
        return false;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
        setMessage(QStringLiteral("Windows could not open this location: %1").arg(path), true);
        return false;
    }
    return true;
}

bool FileShelfManager::copyPath(const QString &id)
{
    const int row = rowForId(id);
    if (row < 0) return false;
    m_screenshots->clipboardService()->writeText(m_records.at(row).path);
    setMessage(QStringLiteral("Copied the path to the clipboard."));
    return true;
}

bool FileShelfManager::copySelectedPaths()
{
    QStringList paths;
    for (const FileShelfRecord &record : m_records)
        if (m_selected.contains(record.id))
            paths.append(record.path);
    if (paths.isEmpty()) return false;
    m_screenshots->clipboardService()->writeText(paths.join(QLatin1Char('\n')));
    setMessage(QStringLiteral("Copied %1 path(s) to the clipboard.").arg(paths.size()));
    return true;
}

bool FileShelfManager::beginExternalDrag(const QStringList &ids)
{
    const QList<QUrl> urls = localFileUrlsForIds(ids);
    if (urls.isEmpty()) {
        setMessage(QStringLiteral("No available shelf items are selected for dragging."), true);
        return false;
    }
    QWindow *sourceWindow = QGuiApplication::focusWindow();
    if (!sourceWindow) {
        setMessage(QStringLiteral("The file shelf window is not active, so a native drag cannot start."), true);
        return false;
    }
    QDrag drag(sourceWindow);
    auto *mimeData = new QMimeData;
    mimeData->setUrls(urls);
    drag.setMimeData(mimeData);
    drag.exec(Qt::CopyAction, Qt::CopyAction);
    return true;
}

QList<QUrl> FileShelfManager::localFileUrlsForIds(const QStringList &ids) const
{
    QList<QUrl> urls;
    for (const FileShelfRecord &record : m_records)
        if (ids.contains(record.id) && QFileInfo::exists(record.path))
            urls.append(QUrl::fromLocalFile(record.path));
    return urls;
}

QString FileShelfManager::chooseTransferDestination()
{
    return QFileDialog::getExistingDirectory(nullptr, QStringLiteral("Choose destination folder"), {},
                                             QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
}

QVariantList FileShelfManager::previewTransfer(const QString &destination, bool move)
{
    Q_UNUSED(move)
    QVariantList plan;
    const QFileInfo destinationInfo(destination);
    if (!destinationInfo.exists() || !destinationInfo.isDir()) {
        setMessage(QStringLiteral("Choose an existing destination folder."), true);
        return plan;
    }
    QSet<QString> plannedNames;
    for (const FileShelfRecord &record : m_records) {
        if (!m_selected.contains(record.id)) continue;
        const QString target = QDir(normalized(destination)).filePath(record.displayName);
        const QString nameKey = record.displayName.toCaseFolded();
        const bool collision = QFileInfo::exists(target) || plannedNames.contains(nameKey);
        plannedNames.insert(nameKey);
        QVariantMap item;
        item.insert(QStringLiteral("id"), record.id);
        item.insert(QStringLiteral("source"), record.path);
        item.insert(QStringLiteral("name"), record.displayName);
        item.insert(QStringLiteral("directory"), record.itemType == QStringLiteral("folder"));
        item.insert(QStringLiteral("conflict"), collision);
        item.insert(QStringLiteral("resolution"), collision ? QStringLiteral("skip") : QString());
        plan.append(item);
    }
    return plan;
}

bool FileShelfManager::startTransfer(const QVariantList &plan, const QString &destination, bool move)
{
    if (m_busy) {
        setMessage(QStringLiteral("A file operation is already running."), true);
        return false;
    }
    const QString destinationPath = normalized(destination);
    if (!QFileInfo(destinationPath).isDir() || plan.isEmpty()) {
        setMessage(QStringLiteral("Choose a destination folder and at least one available shelf item."), true);
        return false;
    }
    QVector<TransferItem> items;
    for (const QVariant &value : plan) {
        const QVariantMap map = value.toMap();
        const QString id = map.value(QStringLiteral("id")).toString();
        const int row = rowForId(id);
        if (row < 0) continue;
        TransferItem item;
        item.id = id;
        item.source = m_records.at(row).path;
        item.targetName = map.value(QStringLiteral("name")).toString();
        item.resolution = map.value(QStringLiteral("resolution")).toString();
        item.directory = m_records.at(row).itemType == QStringLiteral("folder");
        items.append(item);
    }
    if (items.isEmpty()) {
        setMessage(QStringLiteral("The selected shelf items are no longer available."), true);
        return false;
    }
    setBusy(true);
    setMessage(move ? QStringLiteral("Moving selected items…") : QStringLiteral("Copying selected items…"));
    m_transferWatcher.setFuture(QtConcurrent::run([items, destinationPath, move] {
        return executeTransfers(items, destinationPath, move);
    }));
    return true;
}

void FileShelfManager::setMessage(const QString &message, bool isError)
{
    m_message = message;
    m_messageIsError = isError;
    emit messageChanged();
}

void FileShelfManager::setBusy(bool busy)
{
    if (m_busy == busy) return;
    m_busy = busy;
    emit busyChanged();
}
