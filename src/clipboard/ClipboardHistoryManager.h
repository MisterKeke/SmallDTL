#pragma once

#include <QAbstractListModel>
#include <QFuture>
#include <QFutureWatcher>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QTimer>

#include "ClipboardService.h"
#include "../storage/ApplicationDatabase.h"

class SettingsManager;
class ScreenshotManager;

class ClipboardHistoryModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    enum Role { EntryIdRole = Qt::UserRole + 1, TypeRole, CapturedAtRole, PreviewRole,
                ImageUrlRole, FileCountRole, PinnedRole, MissingRole, SizeBytesRole };
    Q_ENUM(Role)
    explicit ClipboardHistoryModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void setRecords(const QVector<ClipboardHistoryRecord> &records);
    ClipboardHistoryRecord recordAt(int row) const;
    ClipboardHistoryRecord recordById(const QString &id) const;
signals:
    void countChanged();
private:
    QVector<ClipboardHistoryRecord> m_records;
};

class ClipboardHistoryManager final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(ClipboardHistoryModel *model READ model CONSTANT)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY pausedChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY countChanged)
    Q_PROPERTY(QVariantMap storageUsage READ storageUsage NOTIFY storageUsageChanged)
    Q_PROPERTY(bool maintaining READ maintaining NOTIFY storageUsageChanged)
public:
    ClipboardHistoryManager(ApplicationDatabase *database, ClipboardService *clipboard, SettingsManager *settings, ScreenshotManager *gallery = nullptr, QObject *parent = nullptr);
    ~ClipboardHistoryManager() override;
    ClipboardHistoryModel *model() { return &m_model; }
    QString search() const { return m_search; }
    bool paused() const { return m_paused; }
    int count() const { return m_model.rowCount(); }
    int totalCount() const { return m_database ? m_database->clipboardHistoryCount() : 0; }
    bool loading() const { return m_refreshBusy; }
    bool hasMore() const { return m_hasMore; }
    QVariantMap storageUsage() const { return m_storageUsage; }
    bool maintaining() const { return m_maintenanceBusy; }

    Q_INVOKABLE void setSearch(const QString &search);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setPaused(bool paused);
    Q_INVOKABLE bool setPinned(const QString &id, bool pinned);
    Q_INVOKABLE bool deleteEntry(const QString &id);
    Q_INVOKABLE bool clearHistory(bool includePinned = true);
    Q_INVOKABLE int clearCount(bool includePinned) const;
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void maintainStorage(bool compact = false, bool clearThumbnails = false);
    Q_INVOKABLE bool restoreEntry(const QString &id);
    Q_INVOKABLE bool saveToGallery(const QString &id, const QString &tagId = {});
    void finishPendingSaves();
    void shutdown();

signals:
    void searchChanged();
    void pausedChanged();
    void countChanged();
    void messageChanged(const QString &message, bool isError);
    void loadingChanged();
    void storageUsageChanged();

private:
    struct PendingImage {
        QString id;
        QImage image;
        QString capturedAt;
        qint64 reservedBytes = 0;
        quint64 generation = 0;
    };
    struct ImageResult {
        ClipboardHistoryRecord record;
        QString error;
        quint64 generation = 0;
    };
    void acceptSnapshot(const ClipboardSnapshot &snapshot);
    void dispatchImageWrites();
    void completeImageWrite(const QString &id, const ImageResult &result);
    void cleanupRetention();
    void deleteOwnedPayloads(const QVector<ClipboardHistoryRecord> &records);
    bool ownsPayload(const ClipboardHistoryRecord &record) const;
    bool hasMissingFiles(const ClipboardHistoryRecord &record, QStringList *missing = nullptr) const;
    static ImageResult writeHistoryImage(const PendingImage &event, const QString &storageDirectory);
    void startRefresh();
    void invalidatePendingCaptures();
    void applyPreferences();
    struct RefreshResult {
        QVector<ClipboardHistoryRecord> records;
        QString error;
        quint64 revision = 0;
        bool hasMore = false;
    };
    struct MaintenanceResult { QVariantMap usage; QStringList failures; QString error; };
    QFutureWatcher<RefreshResult> m_refreshWatcher;
    QFutureWatcher<MaintenanceResult> m_maintenanceWatcher;
    QTimer m_retentionTimer;
    QTimer m_searchTimer;
    quint64 m_refreshRevision = 0;
    int m_pageLimit = 100;
    bool m_hasMore = false;
    bool m_refreshBusy = false;
    bool m_maintenanceBusy = false;
    bool m_sessionOnly = false;
    QVariantMap m_storageUsage;

    ScreenshotManager *m_screenshots = nullptr;
    SettingsManager *m_settings = nullptr;
    ApplicationDatabase *m_database = nullptr;
    ClipboardService *m_clipboard = nullptr;
    ClipboardHistoryModel m_model;
    QString m_storageDirectory;
    QString m_search;
    QQueue<PendingImage> m_imageQueue;
    QHash<QString, QFuture<ImageResult>> m_imageJobs;
    QHash<QString, QFutureWatcher<ImageResult> *> m_imageWatchers;
    QHash<QString, qint64> m_imageReservations;
    QHash<QString, quint64> m_imageGenerations;
    qint64 m_pendingImageBytes = 0;
    int m_activeImageJobs = 0;
    quint64 m_captureGeneration = 1;
    bool m_paused = false;
    bool m_restorePending = false;
};
