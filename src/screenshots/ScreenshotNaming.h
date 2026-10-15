#pragma once

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QString>
#include <QRegularExpression>

namespace ScreenshotNaming {
inline QString baseName(QString pattern, const QDateTime &timestamp) {
    pattern.replace(QStringLiteral("{timestamp}"), timestamp.toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss")));
    pattern.replace(QStringLiteral("{date}"), timestamp.toString(QStringLiteral("yyyy-MM-dd")));
    pattern.replace(QStringLiteral("{time}"), timestamp.toString(QStringLiteral("HH-mm-ss")));
    return pattern;
}
inline bool validPattern(const QString &pattern) {
    static const QRegularExpression invalid(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1F{}]"));
    static const QRegularExpression device(QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])($|\\.)"),QRegularExpression::CaseInsensitiveOption);
    const QString rendered = baseName(pattern, QDateTime::currentDateTime());
    return pattern.size() <= 120 && !pattern.trimmed().isEmpty() &&
           (pattern.contains(QStringLiteral("{timestamp}")) || pattern.contains(QStringLiteral("{date}")) || pattern.contains(QStringLiteral("{time}"))) &&
           !rendered.contains(invalid) && !device.match(rendered).hasMatch() && !rendered.endsWith(QLatin1Char('.')) && !rendered.endsWith(QLatin1Char(' '));
}
inline QString uniqueFilePath(const QString &directory, const QDateTime &timestamp,
                              const QSet<QString> &reservedPaths = {},
                              const QString &pattern = QStringLiteral("Screenshot_{timestamp}"))
{
    const QString name = baseName(pattern, timestamp);
    QString candidate = QDir(directory).filePath(name + QStringLiteral(".png"));
    int suffix = 2;
    while (QFileInfo::exists(candidate) || reservedPaths.contains(candidate))
        candidate = QDir(directory).filePath(name + QStringLiteral("_%1.png").arg(suffix++));
    return candidate;
}
}
