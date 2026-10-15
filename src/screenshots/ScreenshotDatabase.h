#pragma once

#include <QObject>
#include <QDateTime>
#include <QSize>
#include <QStringList>
#include <QVector>

class QSqlDatabase;
class QThread;

struct ScreenshotRecord
{
    QString id;
    QString path;
    QString timestamp;
    int width = 0;
    int height = 0;
    qint64 sizeBytes = 0;
    QString source;
    QString sourceScreenshotId;
    QString captureProfileId;
    QStringList tags;
};

struct ScreenshotTag
{
    QString id;
    QString name;
};

class ScreenshotDatabase final : public QObject
{
public:
    explicit ScreenshotDatabase(const QString &databasePath = {}, QObject *parent = nullptr);
    ~ScreenshotDatabase() override;

    bool initialize(const QString &legacyIndexPath = {});
    bool isReady() const { return m_ready; }
    QString lastError() const { return m_lastError; }
    QString databasePath() const { return m_databasePath; }

    QVector<ScreenshotRecord> screenshots(const QString &directory = {}, const QString &tagId = {}) const;
    ScreenshotRecord screenshotById(const QString &id) const;
    QVector<ScreenshotTag> tags() const;
    QString screenshotIdForPath(const QString &path) const;
    bool addScreenshot(const QString &path, const QSize &dimensions, qint64 sizeBytes,
                       const QString &source, QString *id = nullptr,
                       const QString &sourceScreenshotId = {}, bool inheritTags = false,
                       const QString &captureProfileId = {}, const QString &tagId = {});
    bool updateScreenshotPath(const QString &id, const QString &path);
    bool updateScreenshotMetadata(const QString &id, const QString &timestamp,
                                  const QSize &dimensions, qint64 sizeBytes);
    bool removeScreenshot(const QString &id, ScreenshotRecord *removed = nullptr);
    bool restoreScreenshot(const ScreenshotRecord &record);

    QString createTag(const QString &name);
    bool renameTag(const QString &id, const QString &name);
    bool deleteTag(const QString &id);
    int tagAssignmentState(const QString &tagId, const QStringList &screenshotIds) const;
    bool setTagForScreenshots(const QStringList &screenshotIds, const QString &tagId, bool assigned);

private:
    bool checkThread() const;
    bool migrateSchema();
    bool importLegacyIndex(const QString &legacyIndexPath);
    bool transactionError(const QString &context, const QString &detail = {}) const;
    QString normalizedPath(const QString &path) const;

    QString m_databasePath;
    QString m_connectionName;
    mutable QString m_lastError;
    QThread *m_ownerThread = nullptr;
    QSqlDatabase *m_database = nullptr;
    bool m_ready = false;
};
