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
    QString qrText;
    QString qrErrorCorrection = QStringLiteral("M");
    int qrPixelsPerModule = 8;
    int qrMarginModules = 4;
    QString targetProfileId;
    QString layoutId;
    int delayMs = 1000;
};
struct ActionSequence {
    QString id;
    QString name;
    bool enabled = true;
    QVector<ActionStep> steps;
};
