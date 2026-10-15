#pragma once
#include <QString>
#include <QVector>
struct ActionStep {
    QString id;
    QString type;
    bool enabled = true;
    QString path;
    QString arguments;
    QString url;
    QString targetProfileId;
    int delayMs = 1000;
};
struct ActionSequence {
    QString id;
    QString name;
    bool enabled = true;
    QVector<ActionStep> steps;
};
