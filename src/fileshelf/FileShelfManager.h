#pragma once

#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QSet>
#include <QUrl>
#include <QVariantList>
#include <QVector>
#include <QTimer>
#include <atomic>
#include <memory>
#ifdef SMALLDTL_TESTING
#include <functional>
#endif

#include "../storage/ApplicationDatabase.h"

class ClipboardService;

class FileShelfManager final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool messageIsError READ messageIsError NOTIFY messageChanged)
    Q_PROPERTY(QStringList selectedIds READ selectedIds NOTIFY selectionChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
    Q_PROPERTY(double bytesCopied READ bytesCopied NOTIFY progressChanged)
    Q_PROPERTY(double totalBytes READ totalBytes NOTIFY progressChanged)
    Q_PROPERTY(int completedItems READ completedItems NOTIFY progressChanged)
    Q_PROPERTY(int totalItems READ totalItems NOTIFY progressChanged)
    Q_PROPERTY(bool cancellationRequested READ cancellationRequested NOTIFY progressChanged)

public:
    struct TransferItem {
        QString id;
        QString source;
        QString targetName;
        QString resolution;
        bool directory = false;
    };
    struct TransferSummary {
        int succeeded = 0;
        int skipped = 0;
        int failed = 0;
        QStringList details;
        QVector<QPair<QString, QString>> movedPaths;
        bool cancelled = false;
    };
    struct TransferControl {
        std::atomic_bool cancel = false;
        std::atomic<qint64> bytes = 0;
        std::atomic<qint64> total = 0;
        std::atomic_int completed = 0;
        int itemCount = 0;
#ifdef SMALLDTL_TESTING
        std::function<void()> afterFirstCopyChunkForTesting;
#endif
    };

    enum Role {
        IdRole = Qt::UserRole + 1,
        PathRole,
        DisplayNameRole,
        SourceFolderRole,
        ItemTypeRole,
        FileTypeRole,
        SizeBytesRole,
        AvailableRole
    };
    Q_ENUM(Role)

    explicit FileShelfManager(ApplicationDatabase *database, ClipboardService *clipboard, QObject *parent = nullptr);
    ~FileShelfManager() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool busy() const { return m_busy; }
    QString message() const { return m_message; }
    bool messageIsError() const { return m_messageIsError; }
    QStringList selectedIds() const { return m_selected.values(); }
    int selectedCount() const { return m_selected.size(); }
    double bytesCopied() const { return m_transfer ? double(m_transfer->bytes.load()) : 0; }
    double totalBytes() const { return m_transfer ? double(m_transfer->total.load()) : 0; }
    int completedItems() const { return m_transfer ? m_transfer->completed.load() : 0; }
    int totalItems() const { return m_transfer ? m_transfer->itemCount : 0; }
    bool cancellationRequested() const { return m_transfer && m_transfer->cancel.load(); }
    Q_INVOKABLE void cancelTransfer();
    void finishTransfer();

    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool addPaths(const QStringList &paths);
    Q_INVOKABLE bool addFileUrls(const QVariantList &urls);
    Q_INVOKABLE void addFilesDialog();
    Q_INVOKABLE void addFolderDialog();
    Q_INVOKABLE void toggleSelection(const QString &id);
    Q_INVOKABLE void clickItem(const QString &id, bool control, bool shift);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE bool removeSelected();
    Q_INVOKABLE bool removeEntry(const QString &id);
    Q_INVOKABLE bool clearShelf();
    Q_INVOKABLE bool openEntry(const QString &id);
    Q_INVOKABLE bool openLocation(const QString &id);
    Q_INVOKABLE bool copyPath(const QString &id);
    Q_INVOKABLE bool copySelectedPaths();
    Q_INVOKABLE bool beginExternalDrag(const QStringList &ids);
    QList<QUrl> localFileUrlsForIds(const QStringList &ids) const;
    Q_INVOKABLE QString chooseTransferDestination();
    Q_INVOKABLE QVariantList previewTransfer(const QString &destination, bool move);
    Q_INVOKABLE bool startTransfer(const QVariantList &plan, const QString &destination, bool move);

signals:
    void countChanged();
    void busyChanged();
    void messageChanged();
    void selectionChanged();
    void operationFinished(int succeeded, int skipped, int failed, const QStringList &details);
    void progressChanged();

private:
#ifdef SMALLDTL_TESTING
    friend class UnitTests;
#endif
    int rowForId(const QString &id) const;
    void setMessage(const QString &message, bool isError = false);
    void setBusy(bool busy);
    ApplicationDatabase *m_database = nullptr;
    ClipboardService *m_clipboard = nullptr;
    QVector<FileShelfRecord> m_records;
    QSet<QString> m_selected;
    QString m_anchorId;
    QString m_message;
    bool m_messageIsError = false;
    bool m_busy = false;
    QFutureWatcher<TransferSummary> m_transferWatcher;
    std::shared_ptr<TransferControl> m_transfer;
    QTimer m_progressTimer;
    struct Availability { bool available = false; qint64 size = 0; };
    QHash<QString, Availability> m_availability;
    struct MetadataResult { QHash<QString, Availability> values; quint64 revision = 0; };
    QFutureWatcher<MetadataResult> m_metadataWatcher;
    quint64 m_metadataRevision = 0;
    bool m_metadataBusy = false;
    void refreshMetadata();
};
