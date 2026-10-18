#include "ThumbnailCache.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <atomic>

namespace { std::atomic_int limitMB{128}; QMutex cacheMutex; }

void ThumbnailCache::setLimitMB(int megabytes) { limitMB.store(qBound(16, megabytes, 4096)); }

QVariantMap ThumbnailCache::trim(const QString &directory, bool clear)
{
    QMutexLocker lock(&cacheMutex);
    qint64 bytes = 0;
    QStringList failed;
    if (QFileInfo(directory).isSymLink() || QFileInfo(directory).isJunction())
        return {{QStringLiteral("bytes"), 0}, {QStringLiteral("failures"), QStringList{directory}}};
    static const QRegularExpression hash(QStringLiteral("^[0-9a-f]{64}\\.png$"));
    const auto files = QDir(directory).entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Time | QDir::Reversed);
    for (const auto &file : files)
        if (!file.isSymLink() && hash.match(file.fileName()).hasMatch()) bytes += file.size();
    const qint64 maximum = qint64(limitMB.load()) * 1024 * 1024;
    const auto expiry = QDateTime::currentDateTimeUtc().addDays(-30);
    for (const auto &file : files) {
        if (file.isSymLink() || !hash.match(file.fileName()).hasMatch()) continue;
        if (!clear && bytes <= maximum && file.lastModified().toUTC() >= expiry) continue;
        if (QFile::remove(file.absoluteFilePath())) bytes -= file.size();
        else failed.append(file.absoluteFilePath());
    }
    return {{QStringLiteral("bytes"), bytes}, {QStringLiteral("failures"), failed}};
}
