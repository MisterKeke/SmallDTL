#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSaveFile>
#include <QJsonDocument>
#include <QFileInfo>
#include <QDateTime>
#include <cmath>

namespace JsonValidation {
inline bool safeId(const QString &id) {
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_-]{0,127}$"));
    static const QRegularExpression device(QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$"), QRegularExpression::CaseInsensitiveOption);
    return pattern.match(id).hasMatch() && !device.match(id).hasMatch();
}
inline bool string(const QJsonObject &o, const QString &key) { return !o.contains(key) || o.value(key).isString(); }
inline bool boolean(const QJsonObject &o, const QString &key) { return !o.contains(key) || o.value(key).isBool(); }
inline bool integer(const QJsonObject &o, const QString &key, double min, double max) {
    if (!o.contains(key)) return true;
    const QJsonValue v = o.value(key);
    return v.isDouble() && std::isfinite(v.toDouble()) && std::floor(v.toDouble()) == v.toDouble() && v.toDouble() >= min && v.toDouble() <= max;
}
inline bool write(const QString &path, const QJsonObject &object, QString *error = nullptr,
                  qint64 maximumBytes = -1) {
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (maximumBytes >= 0 && bytes.size() > maximumBytes) {
        if (error) *error = QStringLiteral("Configuration exceeds the %1 MiB limit; the previous file was preserved.")
                                .arg(maximumBytes / (1024 * 1024));
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}
inline bool backup(const QString &path, const QString &suffix) {
    if (!QFileInfo::exists(path)) return true;
    return QFile::copy(path, path + suffix + QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")));
}
}
