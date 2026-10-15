#pragma once

#include <QImage>
#include <QObject>

class QClipboard;
class QTimer;

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

    // Kept as a small input boundary so event processing can be tested without
    // relying on platform-specific clipboard ownership semantics.
    void processImageChange(const QImage &image, bool appOwned);

signals:
    void hasImageChanged();
    void externalImageAvailable(const QImage &snapshot);

private slots:
    void readClipboardChange();

private:
    QClipboard *m_clipboard = nullptr;
    QTimer *m_ownWriteTimeout = nullptr;
    bool m_hasImage = false;
    bool m_ownWritePending = false;
};
