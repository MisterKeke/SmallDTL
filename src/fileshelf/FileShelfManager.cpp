#include "FileShelfManager.h"

#include "../clipboard/ClipboardService.h"
#include "../app/LocalFileUrl.h"


#include <QDir>
#include <QDirIterator>
#include <QDrag>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QGuiApplication>
#include <QMimeData>
#include <QDesktopServices>
#include <QSaveFile>
#include <QScopeGuard>
#include <QTemporaryFile>
#include <QUuid>
#include <QWindow>
#include <QThread>
#include <QtConcurrent/QtConcurrentRun>
#ifdef SMALLDTL_TESTING
#include <utility>
#endif
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

// Hold the exact source objects through copy and deletion. Files deny writers
// and renames; directories deny renames. Delete only these objects, never a
// recursively re-enumerated tree that may contain newly created user data.
class MoveSource final : public QIODevice
{
public:
    QString path;
    bool directory = false;
    ~MoveSource() override { closeHandle(); }
    bool acquire(const QString &source, bool isDirectory, QString *error)
    {
        path = source; directory = isDirectory;
#ifdef Q_OS_WIN
        m_handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ | DELETE,
                              FILE_SHARE_READ | (directory ? FILE_SHARE_WRITE : 0), nullptr,
                              OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (m_handle == INVALID_HANDLE_VALUE) {
            const DWORD code = GetLastError();
            if (error) *error = QStringLiteral("Could not protect move source %1 (Win32 error %2). Close applications editing it and try again; source preserved.")
                                    .arg(path).arg(code);
            return false;
        }
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(m_handle, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
            bool(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != directory) {
            if (error) *error = QStringLiteral("Linked or unreadable move source was rejected: %1").arg(path);
            return false;
        }
        return QIODevice::open(QIODevice::ReadOnly);
#else
        if (error) *error = QStringLiteral("Protected file moves require Windows; source preserved.");
        return false;
#endif
    }
    bool removeProtected(QString *error)
    {
#ifdef Q_OS_WIN
        FILE_DISPOSITION_INFO disposition{TRUE};
        if (!SetFileInformationByHandle(m_handle, FileDispositionInfo, &disposition, sizeof(disposition))) {
            const DWORD code = GetLastError();
            if (error) *error = QStringLiteral("Copied data is at the destination, but source %1 could not be removed (Win32 error %2). Remaining source data was preserved.")
                                    .arg(path).arg(code);
            return false;
        }
        closeHandle();
        return true;
#else
        Q_UNUSED(error)
        return false;
#endif
    }
    bool isSequential() const override { return true; }
protected:
    qint64 readData(char *data, qint64 maximum) override
    {
#ifdef Q_OS_WIN
        DWORD count = 0;
        if (!ReadFile(m_handle, data, DWORD(qMin<qint64>(maximum, 1024 * 1024)), &count, nullptr)) {
            setErrorString(QStringLiteral("Could not read protected source (Win32 error %1).").arg(GetLastError()));
            return -1;
        }
        return count;
#else
        Q_UNUSED(data) Q_UNUSED(maximum)
        return -1;
#endif
    }
    qint64 writeData(const char *, qint64) override { return -1; }
private:
    void closeHandle()
    {
#ifdef Q_OS_WIN
        if (m_handle != INVALID_HANDLE_VALUE) { CloseHandle(m_handle); m_handle = INVALID_HANDLE_VALUE; }
#endif
    }
#ifdef Q_OS_WIN
    HANDLE m_handle = INVALID_HANDLE_VALUE;
#endif
};

struct MoveSnapshot {
    QVector<std::shared_ptr<MoveSource>> entries;
    QString root;
    bool acquire(const QString &source, bool directory, QString *error, FileShelfManager::TransferControl *control)
    {
        root = source;
        auto add = [&](const QString &path, bool isDirectory) {
            auto entry = std::make_shared<MoveSource>();
            if (!entry->acquire(path, isDirectory, error)) return false;
            entries.append(entry);
            return true;
        };
        if (!add(source, directory)) return false;
        if (!directory) return true;
        QDirIterator it(source, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            if (control->cancel.load()) return false;
            const QString path = it.next();
            if (!add(path, it.fileInfo().isDir())) return false;
        }
        return true;
    }
    bool unchanged(QString *error) const
    {
        if (entries.isEmpty() || !entries.first()->directory) return true;
        QSet<QString> expected;
        for (const auto &entry : entries) if (entry->path != root) expected.insert(entry->path);
        QSet<QString> current;
        QDirIterator it(root, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) current.insert(it.next());
        if (current == expected) return true;
        if (error) *error = QStringLiteral("The source folder changed during the move. Source data was preserved; review the destination copy before retrying.");
        return false;
    }
    bool removeSources(QString *error, FileShelfManager::TransferControl *control)
    {
        if (!unchanged(error)) return false;
        for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
            if (control->cancel.load()) {
                if (error) *error = QStringLiteral("Move cancelled; the destination copy and remaining source data were preserved.");
                return false;
            }
            // Windows refuses to delete a nonempty directory. A file created
            // after the manifest check therefore survives this final phase.
            if (!(*it)->removeProtected(error)) return false;
        }
        return true;
    }
};

bool copyStream(QIODevice *input, QIODevice *output, QString *error, FileShelfManager::TransferControl *control)
{
    QByteArray buffer(1024 * 1024, Qt::Uninitialized);
    while (true) {
        if (control->cancel.load()) { if (error) *error = QStringLiteral("Cancelled; source preserved."); return false; }
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
        control->bytes.fetch_add(count);
#ifdef SMALLDTL_TESTING
        // Run once on the worker so regressions can inject a change mid-copy.
        if (auto afterChunk = std::exchange(control->afterFirstCopyChunkForTesting, nullptr))
            afterChunk();
#endif
    }
}

bool copyFileSafely(const QString &source, const QString &target, bool replace, QString *error, FileShelfManager::TransferControl *control, MoveSource *protectedInput = nullptr)
{
    if (sameLocation(source, target)) {
        if (error) *error = QStringLiteral("Source and destination resolve to the same filesystem location; the operation was rejected.");
        return false;
    }
    QFile fileInput(source);
    QIODevice *input = protectedInput ? static_cast<QIODevice *>(protectedInput) : &fileInput;
    if (!protectedInput && !fileInput.open(QIODevice::ReadOnly)) {
        if (error) *error = fileInput.errorString();
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
        if (!copyStream(input, &output, error, control) || (control->cancel.load() || !output.commit())) {
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
        if (!copyStream(input, &output, error, control))
            return false;
        output.setAutoRemove(false);
        output.close();
    }
    if (control->cancel.load()) { QFile::remove(stagedPath); if (error) *error = QStringLiteral("Cancelled; source preserved."); return false; }
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

bool copyDirectoryContents(const QString &source, const QString &target, QString *error, FileShelfManager::TransferControl *control, const MoveSnapshot *snapshot = nullptr)
{
    if (!QDir().mkpath(target)) {
        if (error) *error = QStringLiteral("Could not create destination folder.");
        return false;
    }
    const QDir sourceDir(source);
    if (snapshot) {
        for (const auto &entry : snapshot->entries) {
            if (control->cancel.load()) return false;
            if (entry->path == source) continue;
            const QString to = QDir(target).filePath(sourceDir.relativeFilePath(entry->path));
            if (entry->directory) {
                if (!QDir().mkpath(to)) { if (error) *error = QStringLiteral("Could not create folder: %1").arg(to); return false; }
            } else if (!copyFileSafely(entry->path, to, false, error, control, entry.get())) return false;
        }
        return snapshot->unchanged(error);
    }
    QDirIterator it(source, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        if (control->cancel.load()) { if (error) *error = QStringLiteral("Cancelled; source preserved."); return false; }
        const QString from = it.next();
        const QFileInfo info = it.fileInfo();
        if (info.isSymLink() || info.isJunction()) {
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
        } else if (!copyFileSafely(from, to, false, error, control)) {
            return false;
        }
    }
    return true;
}

bool movePath(const QString &source, const QString &target, QString *error = nullptr)
{
#ifdef Q_OS_WIN
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (MoveFileExW(reinterpret_cast<LPCWSTR>(source.utf16()), reinterpret_cast<LPCWSTR>(target.utf16()), MOVEFILE_WRITE_THROUGH)) return true;
        const DWORD code = GetLastError();
        if (error) *error = QStringLiteral("Win32 error %1").arg(code);
        if (code != ERROR_SHARING_VIOLATION && code != ERROR_ACCESS_DENIED) return false;
        QThread::msleep(20);
    }
    return false;
#else
    Q_UNUSED(error)
    return QDir().rename(source, target);
#endif
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

bool copyDirectorySafely(const QString &source, const QString &target, bool replace, QString *error, FileShelfManager::TransferControl *control, const MoveSnapshot *snapshot = nullptr)
{
    if (rejectUnsafeFolderDestination(source, target, error))
        return false;
    const QString parent = QFileInfo(target).absolutePath();
    const QString stage = QDir(parent).filePath(QStringLiteral(".smalldtl-folder-%1")
                                                    .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    if (!copyDirectoryContents(source, stage, error, control, snapshot)) {
        QDir(stage).removeRecursively();
        return false;
    }
    if (control->cancel.load()) { QDir(stage).removeRecursively(); if (error) *error = QStringLiteral("Cancelled; source preserved."); return false; }
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
    QString commitError;
    if (!movePath(stage, target, &commitError)) {
        QDir(stage).removeRecursively();
        const bool restored = backup.isEmpty() || movePath(backup, target);
        if (error) {
            if (!restored)
                *error = QStringLiteral("Could not commit the copied folder; the previous destination is preserved at %1 and needs manual recovery.").arg(backup);
            else
                *error = QStringLiteral("Could not commit the copied folder (%1); the previous destination was restored.").arg(commitError);
        }
        return false;
    }
    if (!backup.isEmpty() && !QDir(backup).removeRecursively()) {
        if (error) *error = QStringLiteral("The new folder was committed, but the replaced destination could not be cleaned up: %1").arg(backup);
        return false;
    }
    return true;
}

FileShelfManager::TransferSummary executeTransfers(const QVector<FileShelfManager::TransferItem> &items,
                                                   const QString &destination, bool move, const std::shared_ptr<FileShelfManager::TransferControl> &control)
{
    FileShelfManager::TransferSummary result;
    // Count on the worker; never walk a large folder on the GUI thread.
    for (const auto &item : items) {
        if (control->cancel.load()) break;
        if (!item.directory) control->total.fetch_add(QFileInfo(item.source).size());
        else {
            QDirIterator entries(item.source, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
            while (entries.hasNext() && !control->cancel.load()) {
                entries.next();
                if (!entries.fileInfo().isSymLink()) control->total.fetch_add(entries.fileInfo().size());
            }
        }
    }
    for (const auto &item : items) {
        if (control->cancel.load()) { result.cancelled = true; break; }
        const auto completed = qScopeGuard([&] { control->completed.fetch_add(1); });
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
        MoveSnapshot snapshot;
        if (move && !snapshot.acquire(item.source, item.directory, &error, control.get())) {
            ++result.failed;
            result.details.append(QStringLiteral("%1: %2").arg(item.source, error));
            continue;
        }
        const bool copied = item.directory
            ? copyDirectorySafely(item.source, target, replace, &error, control.get(), move ? &snapshot : nullptr)
            : copyFileSafely(item.source, target, replace, &error, control.get(), move ? snapshot.entries.first().get() : nullptr);
        if (!copied) {
            if (control->cancel.load()) { result.cancelled = true; result.details.append(QStringLiteral("Cancelled; uncommitted source files were preserved.")); break; }
            ++result.failed;
            result.details.append(QStringLiteral("%1: %2").arg(item.source, error));
            continue;
        }
        if (move && control->cancel.load()) {
            result.cancelled = true;
            result.details.append(QStringLiteral("%1: copy completed at %2; move cancelled and source preserved.").arg(item.source, target));
            break;
        }
        if (move && (item.directory ? rejectUnsafeFolderDestination(item.source, target, &error)
                                    : pathIsSame(item.source, target))) {
            ++result.failed;
            result.details.append(QStringLiteral("%1: %2").arg(item.source,
                error.isEmpty() ? QStringLiteral("source and destination resolve to the same filesystem location; the source was preserved.") : error));
            continue;
        }
        if (move && !snapshot.removeSources(&error, control.get())) {
            ++result.failed;
            result.details.append(QStringLiteral("%1: destination copy at %2. %3").arg(item.source, target, error));
            continue;
        }
        if (move)
            result.movedPaths.append(qMakePair(item.id, target));
        ++result.succeeded;
    }
    result.cancelled |= control->cancel.load();
    return result;
}
}

FileShelfManager::FileShelfManager(ApplicationDatabase *database, ClipboardService *clipboard, QObject *parent)
    : QAbstractListModel(parent), m_database(database), m_clipboard(clipboard)
{
    m_progressTimer.setInterval(100);
    connect(&m_progressTimer, &QTimer::timeout, this, &FileShelfManager::progressChanged);
    connect(&m_metadataWatcher, &QFutureWatcher<MetadataResult>::finished, this, [this] {
        const auto result = m_metadataWatcher.result();
        m_metadataBusy = false;
        if (result.revision != m_metadataRevision) { refreshMetadata(); return; }
        m_availability = result.values;
        if (!m_visibleRows.isEmpty()) emit dataChanged(index(0), index(m_visibleRows.size()-1), {SizeBytesRole, AvailableRole});
    });
    connect(&m_transferWatcher, &QFutureWatcher<TransferSummary>::finished, this, [this] {
        if (!m_busy) return; // Shutdown may already have applied the result.
        TransferSummary result = m_transferWatcher.result();
        for (const auto &entry : result.movedPaths) {
            if (!m_database->updateFileShelfPath(entry.first, entry.second)) {
                --result.succeeded;
                ++result.failed;
                result.details.append(QStringLiteral("%1: moved successfully but its shelf reference could not be updated: %2")
                                          .arg(entry.second, m_database->lastError()));
            }
        }
        m_progressTimer.stop();
        emit progressChanged();
        setBusy(false);
        QString summary = QStringLiteral("Completed: %1, skipped: %2, failed: %3.")
                              .arg(result.succeeded).arg(result.skipped).arg(result.failed);
        if (result.cancelled) summary.prepend(QStringLiteral("Cancelled. "));
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
    finishTransfer();
    m_metadataWatcher.waitForFinished();
}

void FileShelfManager::cancelTransfer()
{
    if (m_transfer && busy()) {
        m_transfer->cancel.store(true);
        setMessage(QStringLiteral("Cancelling after the current filesystem operation; source files will be preserved where copying has not committed."));
        emit progressChanged();
    }
}

void FileShelfManager::finishTransfer()
{
    cancelTransfer();
    if (m_transferWatcher.isRunning()) m_transferWatcher.waitForFinished();
    if (m_busy && m_database) {
        for (const auto &entry : m_transferWatcher.result().movedPaths)
            m_database->updateFileShelfPath(entry.first, entry.second);
        m_progressTimer.stop();
        setBusy(false);
    }
}

void FileShelfManager::refreshMetadata()
{
    const auto records = m_records;
    const auto revision = m_metadataRevision;
    m_metadataBusy = true;
    m_metadataWatcher.setFuture(QtConcurrent::run([records, revision] {
        MetadataResult result;
        result.revision = revision;
        for (const auto &record : records) {
            const QFileInfo file(record.path);
            result.values.insert(record.id, {file.exists(), file.isFile() ? file.size() : record.sizeBytes});
        }
        return result;
    }));
}

int FileShelfManager::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_visibleRows.size();
}

QVariant FileShelfManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visibleRows.size())
        return {};
    const FileShelfRecord &record = m_records.at(m_visibleRows.at(index.row()));
    const auto metadata = m_availability.value(record.id, {false, record.sizeBytes});
    switch (role) {
    case IdRole: return record.id;
    case PathRole: return record.path;
    case DisplayNameRole: return record.displayName;
    case SourceFolderRole: return record.sourceFolder;
    case ItemTypeRole: return record.itemType;
    case FileTypeRole: {
        if (record.itemType == QStringLiteral("folder")) return QStringLiteral("Folder");
        const QString suffix = QFileInfo(record.path).suffix();
        return suffix.isEmpty() ? QStringLiteral("File") : suffix.toUpper() + QStringLiteral(" file");
    }
    case SizeBytesRole: return metadata.size;
    case AvailableRole: return metadata.available;
    case FavoriteRole: return record.favorite;
    case CollectionIdsRole: return record.collectionIds;
    case CollectionNamesRole: {
        QStringList names;
        for (const QVariant &value : m_collectionValues) {
            const QVariantMap collection = value.toMap();
            if (record.collectionIds.contains(collection.value(QStringLiteral("id")).toString()))
                names.append(collection.value(QStringLiteral("name")).toString());
        }
        return names.join(QStringLiteral(" · "));
    }
    default: return {};
    }
}

QHash<int, QByteArray> FileShelfManager::roleNames() const
{
    return {{IdRole, "entryId"}, {PathRole, "path"}, {DisplayNameRole, "displayName"},
            {SourceFolderRole, "sourceFolder"}, {ItemTypeRole, "itemType"},
            {FileTypeRole, "fileType"}, {SizeBytesRole, "sizeBytes"}, {AvailableRole, "available"},
            {FavoriteRole, "favorite"}, {CollectionIdsRole, "collectionIds"}, {CollectionNamesRole, "collectionNames"}};
}

void FileShelfManager::refresh()
{
    if (!m_database || !m_database->isReady())
        return;
    m_records = m_database->fileShelfEntries();
    QSet<QString> currentIds;
    for (const FileShelfRecord &record : m_records)
        currentIds.insert(record.id);
    for (auto it = m_selected.begin(); it != m_selected.end();) {
        if (!currentIds.contains(*it)) it = m_selected.erase(it); else ++it;
    }
    if (!currentIds.contains(m_anchorId))
        m_anchorId.clear();
    rebuildView();
    refreshCollections();
    emit selectionChanged();
    ++m_metadataRevision;
    if (!m_metadataBusy) refreshMetadata();
}

QVariantList FileShelfManager::collections() const
{
    return m_collectionValues;
}

void FileShelfManager::refreshCollections()
{
    m_collectionValues.clear();
    if (m_database && m_database->isReady()) {
        for (const FileShelfCollection &collection : m_database->fileShelfCollections()) {
            m_collectionValues.append(QVariantMap{{QStringLiteral("id"), collection.id},
                                                  {QStringLiteral("name"), collection.name},
                                                  {QStringLiteral("favorite"), collection.favorite},
                                                  {QStringLiteral("lastUsedAt"), collection.lastUsedAt}});
        }
    }
    emit collectionsChanged();
    if (!m_visibleRows.isEmpty())
        emit dataChanged(index(0), index(m_visibleRows.size() - 1), {CollectionNamesRole});
}

void FileShelfManager::rebuildView()
{
    beginResetModel();
    m_visibleRows.clear();
    const QString needle = m_searchText.trimmed().toCaseFolded();
    for (int row = 0; row < m_records.size(); ++row) {
        const FileShelfRecord &record = m_records.at(row);
        if (m_favoritesOnly && !record.favorite)
            continue;
        if (!m_collectionFilter.isEmpty() && !record.collectionIds.contains(m_collectionFilter))
            continue;
        if (!needle.isEmpty() && !record.displayName.toCaseFolded().contains(needle) &&
            !record.path.toCaseFolded().contains(needle))
            continue;
        m_visibleRows.append(row);
    }
    endResetModel();
    emit countChanged();
    emit viewChanged();
}

void FileShelfManager::setCollectionFilter(const QString &collectionId)
{
    if (m_collectionFilter == collectionId && !m_favoritesOnly)
        return;
    if (!collectionId.isEmpty() && m_database && m_database->touchFileShelfCollection(collectionId))
        refreshCollections();
    m_collectionFilter = collectionId;
    m_favoritesOnly = false;
    rebuildView();
}

void FileShelfManager::setSearchText(const QString &text)
{
    if (m_searchText == text)
        return;
    m_searchText = text;
    rebuildView();
}

void FileShelfManager::setFavoritesOnly(bool favoritesOnly)
{
    if (m_favoritesOnly == favoritesOnly && (!favoritesOnly || m_collectionFilter.isEmpty()))
        return;
    m_favoritesOnly = favoritesOnly;
    if (favoritesOnly)
        m_collectionFilter.clear();
    rebuildView();
}

int FileShelfManager::viewRowForId(const QString &id) const
{
    const int recordRow = rowForId(id);
    return m_visibleRows.indexOf(recordRow);
}

bool FileShelfManager::addPaths(const QStringList &paths)
{
    return addPathsToCollection(paths, {});
}

bool FileShelfManager::addPathsToCollection(const QStringList &paths, const QString &collectionId)
{
    if (!m_database || !m_database->isReady()) {
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
    if (!m_database->addFileShelfEntries(records, &inserted, &duplicates, collectionId)) {
        setMessage(m_database->lastError(), true);
        return false;
    }
    refresh();
    QString message = QStringLiteral("Added %1 reference(s); %2 duplicate(s) skipped.").arg(inserted).arg(duplicates);
    if (!failures.isEmpty())
        message += QStringLiteral(" %1 path(s) failed: %2").arg(failures.size()).arg(failures.join(QStringLiteral("; ")));
    setMessage(message, !failures.isEmpty());
    return failures.isEmpty();
}

QString FileShelfManager::createCollection(const QString &name)
{
    const QString id = m_database ? m_database->createFileShelfCollection(name) : QString();
    if (id.isEmpty()) {
        setMessage(m_database ? m_database->lastError() : QStringLiteral("The file shelf database is unavailable."), true);
        return {};
    }
    refreshCollections();
    setMessage(QStringLiteral("Created collection “%1”.").arg(name.trimmed()));
    return id;
}

bool FileShelfManager::renameCollection(const QString &id, const QString &name)
{
    if (!m_database || !m_database->renameFileShelfCollection(id, name)) {
        setMessage(m_database ? m_database->lastError() : QStringLiteral("The file shelf database is unavailable."), true);
        return false;
    }
    refreshCollections();
    setMessage(QStringLiteral("Collection renamed."));
    return true;
}

bool FileShelfManager::deleteCollection(const QString &id)
{
    if (!m_database || !m_database->deleteFileShelfCollection(id)) {
        setMessage(m_database ? m_database->lastError() : QStringLiteral("The file shelf database is unavailable."), true);
        return false;
    }
    if (m_collectionFilter == id)
        m_collectionFilter.clear();
    refreshCollections();
    rebuildView();
    setMessage(QStringLiteral("Collection deleted. Shelf items and source files were left in place."));
    return true;
}

bool FileShelfManager::setCollectionFavorite(const QString &id, bool favorite)
{
    if (!m_database || !m_database->setFileShelfCollectionFavorite(id, favorite)) {
        setMessage(m_database ? m_database->lastError() : QStringLiteral("The file shelf database is unavailable."), true);
        return false;
    }
    refreshCollections();
    return true;
}

bool FileShelfManager::setEntryFavorite(const QString &id, bool favorite)
{
    if (!m_database || !m_database->setFileShelfFavorite(id, favorite)) {
        setMessage(m_database ? m_database->lastError() : QStringLiteral("The file shelf database is unavailable."), true);
        return false;
    }
    refresh();
    return true;
}

bool FileShelfManager::setSelectedCollectionMembership(const QString &collectionId, bool assigned)
{
    if (collectionId.isEmpty()) {
        setMessage(QStringLiteral("Choose a collection first."), true);
        return false;
    }
    if (!m_database || !m_database->setFileShelfCollectionMembership(selectedIds(), collectionId, assigned)) {
        setMessage(m_database ? m_database->lastError() : QStringLiteral("The file shelf database is unavailable."), true);
        return false;
    }
    m_database->touchFileShelfCollection(collectionId);
    refresh();
    refreshCollections();
    setMessage(assigned ? QStringLiteral("Added selected references to the collection.")
                        : QStringLiteral("Removed selected references from the collection."));
    return true;
}

void FileShelfManager::locateEntryReplacement(const QString &id)
{
    const int row = rowForId(id);
    if (row < 0)
        return;
    const FileShelfRecord &record = m_records.at(row);
    const QString path = record.itemType == QStringLiteral("folder")
        ? QFileDialog::getExistingDirectory(nullptr, QStringLiteral("Locate moved folder"), record.sourceFolder,
                                            QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks)
        : QFileDialog::getOpenFileName(nullptr, QStringLiteral("Locate moved file"), record.sourceFolder);
    if (!path.isEmpty())
        repairEntry(id, path);
}

bool FileShelfManager::repairEntry(const QString &id, const QString &replacementPath)
{
    const int row = rowForId(id);
    if (row < 0) {
        setMessage(QStringLiteral("The shelf item no longer exists."), true);
        return false;
    }
    const QFileInfo replacement(replacementPath);
    const FileShelfRecord &record = m_records.at(row);
    if (!replacement.exists() || (!replacement.isFile() && !replacement.isDir()) ||
        (record.itemType == QStringLiteral("folder")) != replacement.isDir()) {
        setMessage(QStringLiteral("Choose an available %1 as the replacement.")
                       .arg(record.itemType == QStringLiteral("folder") ? QStringLiteral("folder") : QStringLiteral("file")), true);
        return false;
    }
    if (!m_database || !m_database->updateFileShelfPath(id, replacementPath)) {
        setMessage(m_database ? m_database->lastError() : QStringLiteral("The file shelf database is unavailable."), true);
        return false;
    }
    refresh();
    setMessage(QStringLiteral("Shelf reference repaired. Its ID, favorites, and collection memberships were preserved."));
    return true;
}

bool FileShelfManager::beginQuickAdd()
{
    const ClipboardSnapshot snapshot = m_clipboard ? m_clipboard->currentSnapshot() : ClipboardSnapshot();
    if (snapshot.type != QStringLiteral("files") || snapshot.urls.isEmpty()) {
        setMessage(QStringLiteral("The current clipboard contains no file or folder references."), true);
        return false;
    }
    m_pendingQuickUrls = snapshot.urls;
    bool localReference = false;
    QStringList unsupported;
    for (const QUrl &url : m_pendingQuickUrls) {
        QString path, error;
        if (LocalFileUrl::toPath(QVariant::fromValue(url), &path, &error))
            localReference = true;
        else
            unsupported.append(url.toString());
    }
    emit quickAddChanged();
    if (!localReference) {
        m_pendingQuickUrls.clear();
        emit quickAddChanged();
        setMessage(QStringLiteral("Clipboard items are not local file or folder URLs: %1")
                       .arg(unsupported.join(QStringLiteral("; "))), true);
        return false;
    }
    if (!unsupported.isEmpty())
        setMessage(QStringLiteral("Some clipboard items are unsupported and will be reported when added: %1")
                       .arg(unsupported.join(QStringLiteral("; "))), true);
    else
        setMessage(QStringLiteral("Choose a collection for %1 clipboard reference(s).")
                       .arg(m_pendingQuickUrls.size()));
    return true;
}

bool FileShelfManager::quickAddToCollection(const QString &collectionId)
{
    if (m_pendingQuickUrls.isEmpty()) {
        setMessage(QStringLiteral("There are no clipboard file references waiting to add."), true);
        return false;
    }
    QStringList paths;
    QStringList unsupported;
    for (const QUrl &url : m_pendingQuickUrls) {
        QString path, error;
        if (LocalFileUrl::toPath(QVariant::fromValue(url), &path, &error))
            paths.append(path);
        else
            unsupported.append(QStringLiteral("%1: %2").arg(url.toString(), error));
    }
    const bool pathsAdded = addPathsToCollection(paths, collectionId);
    if (!unsupported.isEmpty()) {
        QString message = m_message;
        if (!message.isEmpty()) message += QLatin1Char('\n');
        message += QStringLiteral("Unsupported clipboard item(s): %1").arg(unsupported.join(QStringLiteral("; ")));
        setMessage(message, true);
        return false;
    }
    if (pathsAdded) {
        m_pendingQuickUrls.clear();
        emit quickAddChanged();
    }
    return pathsAdded;
}

void FileShelfManager::cancelQuickAdd()
{
    if (m_pendingQuickUrls.isEmpty())
        return;
    m_pendingQuickUrls.clear();
    emit quickAddChanged();
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
    const int row = viewRowForId(id);
    if (row < 0)
        return;
    if (shift && !m_anchorId.isEmpty() && viewRowForId(m_anchorId) >= 0) {
        const int anchor = viewRowForId(m_anchorId);
        if (!control)
            m_selected.clear();
        for (int current = qMin(anchor, row); current <= qMax(anchor, row); ++current)
            m_selected.insert(m_records.at(m_visibleRows.at(current)).id);
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
    for (int row : m_visibleRows)
        m_selected.insert(m_records.at(row).id);
    if (!m_visibleRows.isEmpty())
        m_anchorId = m_records.at(m_visibleRows.first()).id;
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
    if (!m_database->removeFileShelfEntries(ids)) {
        setMessage(m_database->lastError(), true);
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
    if (!m_database->removeFileShelfEntries({id}, &removed)) {
        setMessage(m_database->lastError(), true);
        return false;
    }
    refresh();
    setMessage(QStringLiteral("Removed %1 shelf reference(s). Source files were left in place.").arg(removed));
    return true;
}

bool FileShelfManager::clearShelf()
{
    int removed = 0;
    if (!m_database->clearFileShelf(&removed)) {
        setMessage(m_database->lastError(), true);
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
    m_clipboard->writeText(m_records.at(row).path);
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
    m_clipboard->writeText(paths.join(QLatin1Char('\n')));
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
    m_transfer = std::make_shared<TransferControl>();
    m_transfer->itemCount = items.size();
    m_progressTimer.start();
    emit progressChanged();
    setBusy(true);
    setMessage(move ? QStringLiteral("Moving selected items…") : QStringLiteral("Copying selected items…"));
    m_transferWatcher.setFuture(QtConcurrent::run([items, destinationPath, move, control = m_transfer] {
        return executeTransfers(items, destinationPath, move, control);
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
