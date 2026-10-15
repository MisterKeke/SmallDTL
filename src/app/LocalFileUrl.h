#pragma once

#include <QMetaType>
#include <QString>
#include <QUrl>
#include <QVariant>

namespace LocalFileUrl {

inline bool toPath(const QVariant &value, QString *path, QString *error = nullptr)
{
    QUrl url;
    if (value.metaType().id() == QMetaType::fromType<QUrl>().id()) {
        url = value.toUrl();
    } else if (value.metaType().id() == QMetaType::fromType<QString>().id()) {
        url = QUrl(value.toString(), QUrl::TolerantMode);
    } else {
        if (error) *error = QStringLiteral("The dropped item is not a file URL.");
        return false;
    }
    if (!url.isValid() || url.scheme().compare(QStringLiteral("file"), Qt::CaseInsensitive) != 0 ||
        !url.isLocalFile()) {
        if (error) *error = QStringLiteral("Only local file URLs can be used here.");
        return false;
    }
    const QString localPath = url.toLocalFile();
    if (localPath.isEmpty()) {
        if (error) *error = QStringLiteral("The dropped file URL does not contain a local path.");
        return false;
    }
    if (path) *path = localPath;
    return true;
}

} // namespace LocalFileUrl
