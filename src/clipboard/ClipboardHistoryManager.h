#pragma once

#include <QAbstractListModel>
#include <QFuture>
#include <QFutureWatcher>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QQueue>
#include <QSet>

#include "ClipboardService.h"
#include "../screenshots/ScreenshotDatabase.h"

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
public:
    ClipboardHistoryManager(ScreenshotManager *screenshots, SettingsManager *settings, QObject *parent = nullptr);
    ~ClipboardHistoryManager() override;
    ClipboardHistoryModel *model() { return &m_model; }
    QString search() const { return m_search; }
    bool paused() const { return m_paused; }
    int count() const { return m_model.rowCount(); }

    Q_INVOKABLE void setSearch(const QString &search);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setPaused(bool paused);
    Q_INVOKABLE bool setPinned(const QString &id, bool pinned);
    Q_INVOKABLE bool deleteEntry(const QString &id);
    Q_INVOKABLE bool clearHistory();
    Q_INVOKABLE bool restoreEntry(const QString &id);
    Q_INVOKABLE bool saveToGallery(const QString &id, const QString &tagId = {});
    void finishPendingSaves();

signals:
    void searchChanged();
    void pausedChanged();
    void countChanged();
    void messageChanged(const QString &message, bool isError);

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

    ScreenshotManager *m_screenshots = nullptr;
    SettingsManager *m_settings = nullptr;
    ScreenshotDatabase *m_database = nullptr;
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
