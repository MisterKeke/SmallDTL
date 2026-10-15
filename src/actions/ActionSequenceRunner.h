#pragma once

#include <QObject>
#include <QVariantList>
#include <QStringList>

class ActionManager;
class ProfileManager;
class ScreenshotManager;
class QrCodeManager;
class QTimer;

class ActionSequenceRunner final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    explicit ActionSequenceRunner(ActionManager *actions, ProfileManager *profiles,
                                  ScreenshotManager *screenshots, QrCodeManager *qrCode = nullptr,
                                  QObject *parent = nullptr);
    bool running() const;
    QString status() const;
    Q_INVOKABLE bool start(const QString &sequenceId);
    Q_INVOKABLE void cancel();

signals:
    void runningChanged();
    void statusChanged();
    void failed(const QString &message);
    void completed(const QString &sequenceName);

private:
    void runNext();
    void scheduleNext();
    bool execute(const QVariantMap &step, QString *error);
    void finish(const QString &message, bool isError, bool wasCancelled = false);
    void setStatus(const QString &status);

    ActionManager *m_actions = nullptr;
    ProfileManager *m_profiles = nullptr;
    ScreenshotManager *m_screenshots = nullptr;
    QrCodeManager *m_qrCode = nullptr;
    QTimer *m_waitTimer = nullptr;
    QVariantList m_steps;
    QString m_sequenceName;
    QString m_status;
    QString m_pendingScreenshotPath;
    QStringList m_qrOutputs;
    quint64 m_pendingQrActionToken = 0;
    quint64 m_nextQrActionToken = 1;
    bool m_pendingQrIsDecode = false;
    quint64 m_runGeneration = 0;
    int m_stepIndex = 0;
    bool m_running = false;
};
