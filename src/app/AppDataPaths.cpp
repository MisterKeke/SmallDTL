#include "AppDataPaths.h"

#include <QDir>
#include <QStandardPaths>

namespace {
QString g_testRoot;
}

QString AppDataPaths::root()
{
    return g_testRoot.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                                : g_testRoot;
}

void AppDataPaths::setTestRoot(const QString &path)
{
    g_testRoot = path.isEmpty() ? QString() : QDir::cleanPath(QDir::fromNativeSeparators(path));
    if (!g_testRoot.isEmpty())
        QDir().mkpath(g_testRoot);
}

bool AppDataPaths::hasTestRoot()
{
    return !g_testRoot.isEmpty();
}
