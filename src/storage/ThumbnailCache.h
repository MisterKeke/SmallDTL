#pragma once
#include <QString>
#include <QVariantMap>

// Only hash-named regular files in the application's thumbnail directory are
// eligible. Maintenance is serialized. Providers commit atomically and can
// regenerate a thumbnail if it is evicted before the next read.
class ThumbnailCache {
public:
    static void setLimitMB(int megabytes);
    static QVariantMap trim(const QString &directory, bool clear = false);
};
