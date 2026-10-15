#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QImage>
#include <QObject>
#include <QPointF>
#include <QSet>
#include <QStringList>
#include <QSize>
#include <QVector>
#include <QSortFilterProxyModel>
#include <QFuture>
#include <QVariantList>
#include <QVariantMap>

#include "../profiles/ProfileManager.h"
#include "ScreenshotDatabase.h"
#include "../clipboard/ClipboardService.h"

class SettingsManager;
class QClipboard;

class ScreenshotGalleryModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(QAbstractItemModel *recent READ recent CONSTANT)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)

public:
    enum Role {
        FileNameRole = Qt::UserRole + 1,
        PathRole,
        ImageUrlRole,
        ModifiedRole,
        WidthRole,
        HeightRole,
        SizeBytesRole,
        MissingRole,
        ThumbnailUrlRole,
        IdRole,
        TagsRole,
        SourceRole,
        SourceScreenshotIdRole
    };
    Q_ENUM(Role)

    explicit ScreenshotGalleryModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setDirectory(const QString &directory);
    void setManagedPaths(const QStringList &paths);
    void setRecords(const QVector<ScreenshotRecord> &records);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QString pathAt(int row) const;
    Q_INVOKABLE QString idAt(int row) const;
    QAbstractItemModel *recent() { return &m_recent; }
    bool loading() const { return m_loading; }

signals:
    void countChanged();
    void loadingChanged();

private:
    struct Item {
        QString id;
        QString path;
        QString fileName;
        QDateTime modified;
        qint64 sizeBytes = 0;
        QSize dimensions;
        bool missing = false;
        QStringList tags;
        QString source;
        QString sourceScreenshotId;
    };

    QString m_directory;
    QVector<ScreenshotRecord> m_records;
    QVector<Item> m_items;
    class RecentModel : public QSortFilterProxyModel {
    public: explicit RecentModel(QObject *parent):QSortFilterProxyModel(parent) {}
    protected: bool filterAcceptsRow(int row,const QModelIndex &) const override { return row<4; }
    } m_recent;
    quint64 m_generation = 0;
    bool m_loading = false;
};

class ScreenshotManager final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString destination READ destination NOTIFY destinationChanged)
    Q_PROPERTY(bool destinationOverridden READ destinationOverridden NOTIFY destinationChanged)
    Q_PROPERTY(bool clipboardHasImage READ clipboardHasImage NOTIFY clipboardHasImageChanged)
    Q_PROPERTY(ScreenshotGalleryModel *gallery READ gallery CONSTANT)
    Q_PROPERTY(QVariantList tags READ tags NOTIFY tagsChanged)
    Q_PROPERTY(QString galleryTagFilter READ galleryTagFilter NOTIFY galleryTagFilterChanged)
    Q_PROPERTY(bool automaticClipboardSavingEnabled READ automaticClipboardSavingEnabled NOTIFY automaticClipboardSavingChanged)
    Q_PROPERTY(bool automaticClipboardSavingPaused READ automaticClipboardSavingPaused NOTIFY automaticClipboardSavingChanged)

public:
    ScreenshotManager(ProfileManager *profiles, SettingsManager *settings, QObject *parent = nullptr);

    QString destination() const;
    bool destinationOverridden() const;
    bool clipboardHasImage() const;
    ScreenshotGalleryModel *gallery();
    QVariantList tags() const;
    QString galleryTagFilter() const { return m_galleryTagFilter; }
    bool automaticClipboardSavingEnabled() const;
    bool automaticClipboardSavingPaused() const { return m_automaticClipboardSavingPaused; }
    bool databaseReady() const { return m_databaseReady; }
    QString databaseError() const { return m_database.lastError(); }
    ScreenshotDatabase *database() { return &m_database; }
    ClipboardService *clipboardService() { return &m_clipboardService; }
    QString profileDestination(const QString &profileId) const;
    QString generalDestination() const;

    void setDestinationOverride(const QString &directory);
    void clearDestinationOverride();
    Q_INVOKABLE void selectDestination(const QString &directory);

    Q_INVOKABLE QString saveClipboardImage();
    Q_INVOKABLE bool saveHistoryImage(const QString &payloadPath, const QString &tagId = {});
    Q_INVOKABLE bool openImage(const QString &path) const;
    Q_INVOKABLE bool openContainingFolder(const QString &path) const;
    Q_INVOKABLE bool copyImageToClipboard(const QString &path);
    Q_INVOKABLE void copyPath(const QString &path) const;
    Q_INVOKABLE QString renameScreenshot(const QString &path, const QString &newName);
    Q_INVOKABLE bool removeFromCollection(const QString &path);
    Q_INVOKABLE bool deleteScreenshotFile(const QString &path);
    Q_INVOKABLE QString createTag(const QString &name);
    Q_INVOKABLE bool renameTag(const QString &tagId, const QString &name);
    Q_INVOKABLE bool deleteTag(const QString &tagId);
    Q_INVOKABLE int tagAssignmentState(const QString &tagId, const QStringList &screenshotIds) const;
    Q_INVOKABLE bool setTagForScreenshots(const QStringList &screenshotIds, const QString &tagId, bool assigned);
    Q_INVOKABLE void setGalleryTagFilter(const QString &tagId);
    Q_INVOKABLE void setAutomaticClipboardSavingPaused(bool paused);
    Q_INVOKABLE QPointF editorPointToImage(qreal x, qreal y, qreal displayWidth, qreal displayHeight,
                                           int imageWidth, int imageHeight) const;
    Q_INVOKABLE QVariantMap editorCropFromDrag(qreal startX, qreal startY, qreal endX, qreal endY,
                                               int imageWidth, int imageHeight, qreal aspectRatio) const;
    Q_INVOKABLE QString saveEditedScreenshot(const QString &sourceScreenshotId, const QVariantMap &crop,
                                              const QVariantList &strokes, bool inheritTags);
    void finishPendingSaves();
    QString lastSaveError() const { return m_lastSaveError; }

signals:
    void destinationChanged();
    void clipboardHasImageChanged();
    void tagsChanged();
    void galleryTagFilterChanged();
    void automaticClipboardSavingChanged();
    void messageChanged(const QString &message, bool isError);
    void saveFinished(const QString &path, bool success, const QString &message);

private:
    friend class UnitTests;
    struct SaveResult {
        bool success = false;
        QString error;
    };
    struct SaveMetadata {
        QString sourceScreenshotId;
        QString captureProfileId;
        QString tagId;
        QString source;
        QByteArray automaticFingerprint;
        bool inheritTags = false;
        bool automatic = false;
    };
    struct AutomaticEvent {
        QImage image;
        QString destination;
        QString profileId;
        QString tagId;
        qint64 reservedBytes = 0;
    };

    QString resolvedDestination() const;
    QString nextFilePath(const QString &directory) const;
    static SaveResult writeImage(const QImage &image, const QString &path);
    static QByteArray imageFingerprint(const QImage &image);
    void completeSave(const QString &path, const SaveResult &result);
    void acceptAutomaticClipboardImage(const QImage &image);
    void finishAutomaticFingerprint(quint64 eventId, const QByteArray &fingerprint);
    void processAutomaticFingerprint(quint64 eventId, const QByteArray &fingerprint);
    void queueImageWrite(const QImage &image, const QString &path, const SaveMetadata &metadata);
    void refreshGallery();
    bool addToCollection(const QString &path, const QString &sourceScreenshotId = {}, bool inheritTags = false,
                         const QString &captureProfileId = {}, const QString &tagId = {}, const QString &source = {});

    ProfileManager *m_profiles = nullptr;
    SettingsManager *m_settings = nullptr;
    QClipboard *m_clipboard = nullptr;
    ClipboardService m_clipboardService;
    ScreenshotDatabase m_database;
    ScreenshotGalleryModel m_gallery;
    QString m_destinationOverride;
    QSet<QString> m_pendingPaths;
    QString m_galleryTagFilter;
    bool m_databaseReady = false;
    QHash<QString,qint64> m_pendingBytes;
    QHash<QString,QFuture<SaveResult>> m_jobs;
    QHash<QString, SaveMetadata> m_saveMetadata;
    QHash<quint64, AutomaticEvent> m_automaticEvents;
    QHash<quint64, QFuture<QByteArray>> m_fingerprintJobs;
    QHash<quint64, QByteArray> m_readyFingerprints;
    QSet<QByteArray> m_savedAutomaticFingerprints;
    QSet<QByteArray> m_inFlightAutomaticFingerprints;
    qint64 m_fingerprintBytes = 0;
    quint64 m_nextAutomaticEventId = 1;
    quint64 m_nextFingerprintToProcess = 1;
    bool m_automaticClipboardSavingPaused = false;
    QString m_lastSaveError;
};
