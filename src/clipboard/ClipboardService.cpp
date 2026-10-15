#include "ClipboardService.h"

#include <QClipboard>
#include <QMimeData>
#include <QTimer>

ClipboardService::ClipboardService(QClipboard *clipboard, QObject *parent)
    : QObject(parent), m_clipboard(clipboard), m_ownWriteTimeout(new QTimer(this))
{
    m_ownWriteTimeout->setSingleShot(true);
    connect(m_ownWriteTimeout, &QTimer::timeout, this, [this] { m_ownWritePending = false; });
    if (m_clipboard)
        connect(m_clipboard, &QClipboard::dataChanged, this, &ClipboardService::readClipboardChange);
    readClipboardChange();
}

void ClipboardService::writeImage(const QImage &image)
{
    if (!m_clipboard || image.isNull())
        return;
    m_ownWritePending = true;
    // QClipboard normally reports our setImage change synchronously. Keep a
    // short fallback for backends that deliver it on the next UI turn, so an
    // unrelated external copy is not masked for seconds if no signal arrives.
    m_ownWriteTimeout->start(100);
    m_clipboard->setImage(image);
}

void ClipboardService::readClipboardChange()
{
    if (!m_clipboard || !m_clipboard->mimeData() || !m_clipboard->mimeData()->hasImage()) {
        processImageChange({}, m_ownWritePending);
        return;
    }
    const QImage snapshot = qvariant_cast<QImage>(m_clipboard->mimeData()->imageData());
    processImageChange(snapshot, m_ownWritePending || m_clipboard->ownsClipboard());
}

void ClipboardService::processImageChange(const QImage &image, bool appOwned)
{
    if (m_ownWritePending && appOwned) {
        m_ownWritePending = false;
        m_ownWriteTimeout->stop();
    }
    const bool nowHasImage = !image.isNull();
    if (m_hasImage != nowHasImage) {
        m_hasImage = nowHasImage;
        emit hasImageChanged();
    }
    if (!appOwned && nowHasImage)
        emit externalImageAvailable(image);
}
