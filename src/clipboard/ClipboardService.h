#pragma once

#include <QImage>
#include <QObject>
#include <QUrl>

class QClipboard;
class QTimer;
class QMimeData;

struct ClipboardSnapshot
{
    QString type;
    QImage image;
    QString text;
    QList<QUrl> urls;
    QString sourceApplication;
    bool sensitive = false;
    bool isEmpty() const {
        return type.isEmpty() || (type == QStringLiteral("image") && image.isNull()) ||
               (type == QStringLiteral("files") && urls.isEmpty());
    }
};

// Shared clipboard event boundary. It exposes immutable image snapshots and
// filters writes made by SmallDTL itself before downstream consumers see them.
class ClipboardService final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool hasImage READ hasImage NOTIFY hasImageChanged)

public:
    explicit ClipboardService(QClipboard *clipboard, QObject *parent = nullptr);

    bool hasImage() const { return m_hasImage; }
    void writeImage(const QImage &image);
    void writeText(const QString &text);
    void writeUrls(const QList<QUrl> &urls);
    void writeSnapshot(const ClipboardSnapshot &snapshot);
    QImage currentImage() const;

    // Kept as a small input boundary so event processing can be tested without
    // relying on platform-specific clipboard ownership semantics.
    void processImageChange(const QImage &image, bool appOwned);
    void processContentChange(const ClipboardSnapshot &snapshot, bool appOwned);
    static bool isSensitiveMimeData(const QMimeData *mime);

signals:
    void hasImageChanged();
    void externalImageAvailable(const QImage &snapshot);
    void externalContentAvailable(const ClipboardSnapshot &snapshot);

private slots:
    void readClipboardChange();

private:
    bool matchesPendingWrite(const ClipboardSnapshot &snapshot) const;
    QClipboard *m_clipboard = nullptr;
    QTimer *m_ownWriteTimeout = nullptr;
    bool m_hasImage = false;
    bool m_ownWritePending = false;
    ClipboardSnapshot m_pendingOwnSnapshot;
};

Q_DECLARE_METATYPE(ClipboardSnapshot)
