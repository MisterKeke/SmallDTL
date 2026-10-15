#include "ActionSequenceRunner.h"

#include "ActionManager.h"
#include "../profiles/ProfileManager.h"
#include "../screenshots/ScreenshotManager.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QTimer>
#include <QUrl>

namespace {
QString actionLabel(const QString &type)
{
    if (type == QStringLiteral("launchApplication")) return QStringLiteral("Launch application");
    if (type == QStringLiteral("openFolder")) return QStringLiteral("Open folder");
    if (type == QStringLiteral("openFile")) return QStringLiteral("Open file");
    if (type == QStringLiteral("openUrl")) return QStringLiteral("Open URL");
    if (type == QStringLiteral("saveScreenshot")) return QStringLiteral("Save clipboard screenshot");
    if (type == QStringLiteral("switchProfile")) return QStringLiteral("Switch profile");
    if (type == QStringLiteral("wait")) return QStringLiteral("Wait");
    return QStringLiteral("Action");
}
}

ActionSequenceRunner::ActionSequenceRunner(ActionManager *actions, ProfileManager *profiles,
                                           ScreenshotManager *screenshots, QObject *parent)
    : QObject(parent), m_actions(actions), m_profiles(profiles), m_screenshots(screenshots),
      m_waitTimer(new QTimer(this))
{
    m_waitTimer->setSingleShot(true);
    connect(m_waitTimer, &QTimer::timeout, this, &ActionSequenceRunner::runNext);
    connect(m_screenshots, &ScreenshotManager::saveFinished, this,
            [this](const QString &path, bool success, const QString &message) {
        if (!m_running || path != m_pendingScreenshotPath)
            return;
        m_pendingScreenshotPath.clear();
        if (!success)
            finish(message, true);
        else
            scheduleNext();
    });
}

bool ActionSequenceRunner::running() const { return m_running; }
QString ActionSequenceRunner::status() const { return m_status; }

bool ActionSequenceRunner::start(const QString &sequenceId)
{
    if (m_running) {
        emit failed(QStringLiteral("An action sequence is already running."));
        return false;
    }
    QString validationError;
    if (m_actions->validateSequence(sequenceId, &validationError).isEmpty()) {
        setStatus(validationError);
        emit failed(validationError);
        return false;
    }
    const QVariantMap info = m_actions->sequenceInfo(sequenceId);
    m_sequenceName = info.value(QStringLiteral("name")).toString();
    m_steps = m_actions->steps(sequenceId);
    m_stepIndex = 0;
    ++m_runGeneration;
    m_running = true;
    emit runningChanged();
    setStatus(QStringLiteral("Starting %1…").arg(m_sequenceName));
    scheduleNext();
    return true;
}

void ActionSequenceRunner::cancel()
{
    if (!m_running)
        return;
    m_waitTimer->stop();
    finish(QStringLiteral("%1 was cancelled.").arg(m_sequenceName), false, true);
}

void ActionSequenceRunner::runNext()
{
    if (!m_running)
        return;
    while (m_stepIndex < m_steps.size()) {
        const QVariantMap step = m_steps.at(m_stepIndex++).toMap();
        if (!step.value(QStringLiteral("enabled")).toBool())
            continue;
        const QString type = step.value(QStringLiteral("stepType")).toString();
        setStatus(QStringLiteral("%1 · %2 of %3").arg(actionLabel(type))
                      .arg(m_stepIndex).arg(m_steps.size()));
        if (type == QStringLiteral("wait")) {
            m_waitTimer->start(step.value(QStringLiteral("delayMs")).toInt());
            return;
        }
        if (type == QStringLiteral("saveScreenshot")) {
            m_pendingScreenshotPath = m_screenshots->saveClipboardImage();
            if (m_pendingScreenshotPath.isEmpty())
                finish(m_screenshots->lastSaveError(), true);
            return;
        }
        QString error;
        if (!execute(step, &error)) {
            finish(error, true);
            return;
        }
        scheduleNext();
        return;
    }
    finish(QStringLiteral("%1 completed.").arg(m_sequenceName), false);
}

void ActionSequenceRunner::scheduleNext()
{
    const quint64 generation = m_runGeneration;
    QTimer::singleShot(0, this, [this, generation] {
        if (generation == m_runGeneration)
            runNext();
    });
}

bool ActionSequenceRunner::execute(const QVariantMap &step, QString *error)
{
    const QString type = step.value(QStringLiteral("stepType")).toString();
    const QString path = step.value(QStringLiteral("path")).toString();
    if (type == QStringLiteral("launchApplication")) {
        const QFileInfo executable(path);
        if (!executable.exists() || !executable.isFile() || !executable.isExecutable()) {
            *error = QStringLiteral("Cannot launch application. Check the executable path: %1").arg(path);
            return false;
        }
        const QStringList arguments = QProcess::splitCommand(step.value(QStringLiteral("arguments")).toString());
        if (!QProcess::startDetached(executable.absoluteFilePath(), arguments, executable.absolutePath())) {
            *error = QStringLiteral("Windows could not start %1.").arg(executable.fileName());
            return false;
        }
        return true;
    }
    if (type == QStringLiteral("openFolder")) {
        const QFileInfo folder(path);
        if (!folder.exists() || !folder.isDir() || !QDesktopServices::openUrl(QUrl::fromLocalFile(folder.absoluteFilePath()))) {
            *error = QStringLiteral("Could not open folder: %1").arg(path);
            return false;
        }
        return true;
    }
    if (type == QStringLiteral("openFile")) {
        const QFileInfo file(path);
        if (!file.exists() || !file.isFile() || !QDesktopServices::openUrl(QUrl::fromLocalFile(file.absoluteFilePath()))) {
            *error = QStringLiteral("Could not open file: %1").arg(path);
            return false;
        }
        return true;
    }
    if (type == QStringLiteral("openUrl")) {
        const QUrl url(step.value(QStringLiteral("url")).toString().trimmed());
        if (!url.isValid() || url.host().isEmpty() ||
            (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https")) ||
            !QDesktopServices::openUrl(url)) {
            *error = QStringLiteral("Could not open the configured HTTP or HTTPS URL.");
            return false;
        }
        return true;
    }
    if (type == QStringLiteral("switchProfile")) {
        const QString profileId = step.value(QStringLiteral("targetProfileId")).toString();
        if (!m_profiles->setActiveProfile(profileId)) {
            *error = QStringLiteral("The target profile is no longer available.");
            return false;
        }
        return true;
    }
    *error = QStringLiteral("This action type is not supported.");
    return false;
}

void ActionSequenceRunner::finish(const QString &message, bool isError, bool wasCancelled)
{
    if (!m_running)
        return;
    m_running = false;
    ++m_runGeneration;
    m_waitTimer->stop();
    m_pendingScreenshotPath.clear();
    m_steps.clear();
    m_stepIndex = 0;
    emit runningChanged();
    setStatus(message);
    if (isError)
        emit failed(message);
    else if (!wasCancelled)
        emit completed(m_sequenceName);
    m_sequenceName.clear();
}

void ActionSequenceRunner::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}
