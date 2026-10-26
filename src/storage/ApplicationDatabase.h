#pragma once

#include <QObject>
#include <QDateTime>
#include <QSize>
#include <QStringList>
#include <QVector>
#include <QVariantMap>

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

struct ClipboardHistoryRecord
{
    QString id;
    QString type;
    QString capturedAt;
    QString fingerprint;
    QString text;
    QStringList fileUrls;
    int fileCount = -1;
    QString payloadPath;
    qint64 sizeBytes = 0;
    int width = 0;
    int height = 0;
    bool pinned = false;
    bool missing = false;
};

struct FileShelfRecord
{
    QString id;
    QString path;
    QString addedAt;
    QString displayName;
    QString sourceFolder;
    QString itemType;
    qint64 sizeBytes = 0;
    bool favorite = false;
    QStringList collectionIds;
};

struct FileShelfCollection
{
    QString id;
    QString name;
    bool favorite = false;
    QString lastUsedAt;
};

class ApplicationDatabase final : public QObject
{
public:
    explicit ApplicationDatabase(const QString &databasePath = {}, QObject *parent = nullptr);
    ~ApplicationDatabase() override;

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

    QVector<ClipboardHistoryRecord> clipboardHistory(const QString &search = {}, int limit = -1,
                                                   bool previewsOnly = false, const QString &id = {}) const;
    QVector<ClipboardHistoryRecord> clipboardHistoryMetadata() const;
    int clipboardHistoryCount(bool includePinned = true) const;
    bool clipboardHistoryFilesMissing(const QString &id) const;
    bool compact();
    bool saveClipboardHistory(const ClipboardHistoryRecord &record, QString *id = nullptr,
                              bool *inserted = nullptr);
    bool setClipboardHistoryPinned(const QString &id, bool pinned);
    bool deleteClipboardHistory(const QString &id, ClipboardHistoryRecord *removed = nullptr);
    bool deleteClipboardHistory(const QStringList &ids, QVector<ClipboardHistoryRecord> *removed = nullptr);

    QVector<FileShelfRecord> fileShelfEntries() const;
    QVector<FileShelfCollection> fileShelfCollections() const;
    bool addFileShelfEntries(const QVector<FileShelfRecord> &records, int *inserted = nullptr,
                             int *duplicates = nullptr, const QString &collectionId = {});
    QString createFileShelfCollection(const QString &name);
    bool renameFileShelfCollection(const QString &id, const QString &name);
    bool deleteFileShelfCollection(const QString &id);
    bool setFileShelfCollectionFavorite(const QString &id, bool favorite);
    bool setFileShelfFavorite(const QString &id, bool favorite);
    bool setFileShelfCollectionMembership(const QStringList &entryIds, const QString &collectionId,
                                          bool assigned);
    bool touchFileShelfCollection(const QString &id);
    bool removeFileShelfEntries(const QStringList &ids, int *removed = nullptr);
    bool clearFileShelf(int *removed = nullptr);
    bool updateFileShelfPath(const QString &id, const QString &path);

private:
    bool checkThread() const;
    bool migrateSchema();
    bool importLegacyIndex(const QString &legacyIndexPath);
    bool transactionError(const QString &context, const QString &detail = {}) const;
    QString normalizedPath(const QString &path) const;
    QString normalizedPathKey(const QString &path) const;

    QString m_databasePath;
    QString m_connectionName;
    mutable QString m_lastError;
    QThread *m_ownerThread = nullptr;
    QSqlDatabase *m_database = nullptr;
    bool m_ready = false;
};
