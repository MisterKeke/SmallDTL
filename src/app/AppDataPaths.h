#pragma once

#include <QString>

namespace AppDataPaths {
QString root();
void setTestRoot(const QString &path);
bool hasTestRoot();
}
