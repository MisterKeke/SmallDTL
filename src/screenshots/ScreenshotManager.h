#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QSize>
#include <QVector>
#include <QSortFilterProxyModel>
#include <QFuture>
#include <QVariantList>

#include "../profiles/ProfileManager.h"
#include "ScreenshotDatabase.h"

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
        SourceRole
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

public:
    ScreenshotManager(ProfileManager *profiles, SettingsManager *settings, QObject *parent = nullptr);

    QString destination() const;
    bool destinationOverridden() const;
    bool clipboardHasImage() const;
    ScreenshotGalleryModel *gallery();
    QVariantList tags() const;
    QString galleryTagFilter() const { return m_galleryTagFilter; }
    bool databaseReady() const { return m_databaseReady; }
    QString databaseError() const { return m_database.lastError(); }
    QString profileDestination(const QString &profileId) const;
    QString generalDestination() const;

    void setDestinationOverride(const QString &directory);
    void clearDestinationOverride();
    Q_INVOKABLE void selectDestination(const QString &directory);

    Q_INVOKABLE QString saveClipboardImage();
    Q_INVOKABLE bool openImage(const QString &path) const;
    Q_INVOKABLE bool openContainingFolder(const QString &path) const;
    Q_INVOKABLE bool copyImageToClipboard(const QString &path) const;
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
    void finishPendingSaves();
    QString lastSaveError() const { return m_lastSaveError; }

signals:
    void destinationChanged();
    void clipboardHasImageChanged();
    void tagsChanged();
    void galleryTagFilterChanged();
    void messageChanged(const QString &message, bool isError);
    void saveFinished(const QString &path, bool success, const QString &message);

private slots:
    void updateClipboardState();

private:
    friend class UnitTests;
    struct SaveResult {
        bool success = false;
        QString error;
    };

    QString resolvedDestination() const;
    QString nextFilePath(const QString &directory) const;
    static SaveResult writeImage(const QImage &image, const QString &path);
    void completeSave(const QString &path, const SaveResult &result);
    void refreshGallery();
    bool addToCollection(const QString &path);

    ProfileManager *m_profiles = nullptr;
    SettingsManager *m_settings = nullptr;
    QClipboard *m_clipboard = nullptr;
    ScreenshotDatabase m_database;
    ScreenshotGalleryModel m_gallery;
    QString m_destinationOverride;
    QSet<QString> m_pendingPaths;
    QString m_galleryTagFilter;
    bool m_databaseReady = false;
    bool m_clipboardHasImage = false;
    QHash<QString,qint64> m_pendingBytes;
    QHash<QString,QFuture<SaveResult>> m_jobs;
    QString m_lastSaveError;
};
