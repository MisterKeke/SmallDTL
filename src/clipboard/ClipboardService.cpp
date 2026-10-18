#include "ClipboardService.h"

#include <QClipboard>
#include <QMimeData>
#include <QTimer>
#include <QFileInfo>
#include <QtEndian>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

bool ClipboardService::isSensitiveMimeData(const QMimeData *mime)
{
    if (!mime) return false;
    for (const QString &format : mime->formats()) {
        if (format.contains(QStringLiteral("ExcludeClipboardContentFromMonitorProcessing"), Qt::CaseInsensitive) ||
            format.contains(QStringLiteral("Clipboard Viewer Ignore"), Qt::CaseInsensitive) ||
            format == QStringLiteral("application/x-nspasteboard-concealed-type"))
            return true;
        if (format.contains(QStringLiteral("CanIncludeInClipboardHistory"), Qt::CaseInsensitive)) {
            const QByteArray value = mime->data(format);
            if (value.size() < 4 || qFromLittleEndian<quint32>(value.constData()) == 0) return true;
        }
        if (format == QStringLiteral("x-kde-passwordManagerHint") && mime->data(format) == QByteArrayLiteral("secret"))
            return true;
    }
    return false;
}

ClipboardService::ClipboardService(QClipboard *clipboard, QObject *parent)
    : QObject(parent), m_clipboard(clipboard), m_ownWriteTimeout(new QTimer(this))
{
    m_ownWriteTimeout->setSingleShot(true);
    connect(m_ownWriteTimeout, &QTimer::timeout, this, [this] {
        m_ownWritePending = false;
        m_pendingOwnSnapshot = {};
    });
    if (m_clipboard)
        connect(m_clipboard, &QClipboard::dataChanged, this, &ClipboardService::readClipboardChange);
    readClipboardChange();
}

void ClipboardService::writeImage(const QImage &image)
{
    ClipboardSnapshot snapshot;
    snapshot.type = QStringLiteral("image");
    snapshot.image = image;
    writeSnapshot(snapshot);
}

void ClipboardService::writeText(const QString &text)
{
    ClipboardSnapshot snapshot;
    snapshot.type = QStringLiteral("text");
    snapshot.text = text;
    writeSnapshot(snapshot);
}

void ClipboardService::writeUrls(const QList<QUrl> &urls)
{
    ClipboardSnapshot snapshot;
    snapshot.type = QStringLiteral("files");
    snapshot.urls = urls;
    writeSnapshot(snapshot);
}

void ClipboardService::writeSnapshot(const ClipboardSnapshot &snapshot)
{
    if (!m_clipboard || snapshot.isEmpty())
        return;
    auto *mime = new QMimeData;
    if (snapshot.type == QStringLiteral("image") && !snapshot.image.isNull())
        mime->setImageData(snapshot.image);
    else if (snapshot.type == QStringLiteral("files") && !snapshot.urls.isEmpty())
        mime->setUrls(snapshot.urls);
    else if (snapshot.type == QStringLiteral("text"))
        mime->setText(snapshot.text);
    else {
        delete mime;
        return;
    }
    m_pendingOwnSnapshot = snapshot;
    m_ownWritePending = true;
    // QClipboard normally reports our setMimeData change synchronously. Keep
    // a short fallback for backends that deliver it on the next UI turn.
    m_ownWriteTimeout->start(100);
    m_clipboard->setMimeData(mime);
}

QImage ClipboardService::currentImage() const
{
    return m_clipboard ? m_clipboard->image() : QImage();
}

void ClipboardService::readClipboardChange()
{
    if (!m_clipboard || !m_clipboard->mimeData()) {
        processContentChange({}, m_ownWritePending);
        return;
    }
    const QMimeData *mime = m_clipboard->mimeData();
    ClipboardSnapshot snapshot;
    // Check before decoding images or requesting text from a delayed owner.
    snapshot.sensitive = isSensitiveMimeData(mime);
#ifdef Q_OS_WIN
    DWORD processId = 0;
    GetWindowThreadProcessId(GetClipboardOwner(), &processId);
    if (HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId)) {
        wchar_t path[32768];
        DWORD length = 32768;
        if (QueryFullProcessImageNameW(process, 0, path, &length))
            snapshot.sourceApplication = QString::fromWCharArray(path, length);
        CloseHandle(process);
    }
#endif
    if (snapshot.sensitive) {
        processContentChange(snapshot, false);
        return;
    }
    if (mime->hasImage()) {
        snapshot.type = QStringLiteral("image");
        snapshot.image = qvariant_cast<QImage>(mime->imageData());
    } else if (mime->hasUrls()) {
        snapshot.type = QStringLiteral("files");
        snapshot.urls = mime->urls();
    } else if (mime->hasText()) {
        snapshot.type = QStringLiteral("text");
        snapshot.text = mime->text();
    }
    processContentChange(snapshot, m_clipboard->ownsClipboard() ||
                                   (m_ownWritePending && matchesPendingWrite(snapshot)));
}

bool ClipboardService::matchesPendingWrite(const ClipboardSnapshot &snapshot) const
{
    if (snapshot.type != m_pendingOwnSnapshot.type)
        return false;
    if (snapshot.type == QStringLiteral("text"))
        return snapshot.text == m_pendingOwnSnapshot.text;
    if (snapshot.type == QStringLiteral("files")) {
        if (snapshot.urls.size() != m_pendingOwnSnapshot.urls.size())
            return false;
        for (qsizetype i = 0; i < snapshot.urls.size(); ++i)
            if (snapshot.urls.at(i).toString(QUrl::FullyEncoded) != m_pendingOwnSnapshot.urls.at(i).toString(QUrl::FullyEncoded))
                return false;
        return true;
    }
    if (snapshot.type == QStringLiteral("image")) {
        if (snapshot.image.size() != m_pendingOwnSnapshot.image.size())
            return false;
        if (snapshot.image.cacheKey() == m_pendingOwnSnapshot.image.cacheKey())
            return true;
        // Pixel comparison can scan a large clipboard image. The normal owner
        // signal handles native delayed notifications; keep the fallback bounded.
        constexpr qint64 MaximumFallbackCompareBytes = 8LL * 1024 * 1024;
        return snapshot.image.sizeInBytes() <= MaximumFallbackCompareBytes &&
               snapshot.image == m_pendingOwnSnapshot.image;
    }
    return false;
}

void ClipboardService::processImageChange(const QImage &image, bool appOwned)
{
    ClipboardSnapshot snapshot;
    if (!image.isNull()) {
        snapshot.type = QStringLiteral("image");
        snapshot.image = image;
    }
    processContentChange(snapshot, appOwned);
}

void ClipboardService::processContentChange(const ClipboardSnapshot &snapshot, bool appOwned)
{
    if (m_ownWritePending && appOwned) {
        m_ownWritePending = false;
        m_ownWriteTimeout->stop();
        m_pendingOwnSnapshot = {};
    }
    const bool nowHasImage = snapshot.type == QStringLiteral("image") && !snapshot.image.isNull();
    if (m_hasImage != nowHasImage) {
        m_hasImage = nowHasImage;
        emit hasImageChanged();
    }
    if (appOwned || snapshot.sensitive || snapshot.isEmpty())
        return;
    if (nowHasImage)
        emit externalImageAvailable(snapshot.image);
    emit externalContentAvailable(snapshot);
}
