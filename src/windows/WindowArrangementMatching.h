#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QDir>

namespace WindowArrangementMatching {
inline QString identity(const QString &path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path)).toCaseFolded();
}
inline QVector<int> matchingRows(const QString &executable,const QString &titleRule,
                                 const QStringList &executables,const QStringList &titles)
{
    QVector<int> matches;
    const QString wanted=identity(executable);
    const int count=qMin(executables.size(),titles.size());
    for(int i=0;i<count;++i)
        if(identity(executables.at(i))==wanted &&
           (titleRule.isEmpty()||titles.at(i).contains(titleRule,Qt::CaseInsensitive)))
            matches.append(i);
    return matches;
}
} // namespace WindowArrangementMatching
