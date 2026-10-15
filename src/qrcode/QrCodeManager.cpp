#include "QrCodeManager.h"

#include "../app/LocalFileUrl.h"
#include "../clipboard/ClipboardService.h"
#include "../screenshots/ScreenshotManager.h"

#include <BarcodeFormat.h>
#include <BitMatrix.h>
#include <CharacterSet.h>
#include <MultiFormatWriter.h>
#include <ReadBarcode.h>
#include <ReaderOptions.h>

#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QImageReader>
#include <QImageWriter>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QtConcurrent>

#include <algorithm>
#include <array>
#include <exception>
#include <limits>

namespace {
constexpr qint64 MaximumImageBytes = 256LL * 1024 * 1024;
constexpr qint64 MaximumImagePixels = 64LL * 1024 * 1024;
constexpr qint64 MaximumInputBytes = 20LL * 1024;
constexpr int MaximumQrModulePixels = 20;
constexpr int MaximumQrMarginModules = 20;

bool validErrorCorrection(const QString &level)
{
    return level == QStringLiteral("L") || level == QStringLiteral("M") ||
           level == QStringLiteral("Q") || level == QStringLiteral("H");
}

QStringList decodeValidatedImage(const QImage &image, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return QStringList{};
    };
    if (image.isNull())
        return fail(QStringLiteral("The selected source does not contain a readable image."));
    const qint64 pixels = qint64(image.width()) * image.height();
    if (pixels <= 0 || pixels > MaximumImagePixels || image.sizeInBytes() <= 0 ||
        image.sizeInBytes() > MaximumImageBytes)
        return fail(QStringLiteral("Image exceeds the 64 megapixel / 256 MiB limit."));

    const QImage grayscale = image.convertToFormat(QImage::Format_Grayscale8);
    if (grayscale.isNull() || grayscale.sizeInBytes() > MaximumImageBytes)
        return fail(QStringLiteral("Could not prepare the image within the 256 MiB limit."));

    const ZXing::ImageView view(grayscale.constBits(), grayscale.width(), grayscale.height(),
                                ZXing::ImageFormat::Lum, grayscale.bytesPerLine(), 1);
    const ZXing::ReaderOptions options = ZXing::ReaderOptions()
                                             .formats(ZXing::BarcodeFormat::QRCode)
                                             .tryRotate(true);
    const ZXing::Barcodes barcodes = ZXing::ReadBarcodes(view, options);
    QStringList decoded;
    decoded.reserve(static_cast<qsizetype>(barcodes.size()));
    for (const ZXing::Barcode &barcode : barcodes) {
        if (!barcode.isValid())
            continue;
        const std::string text = barcode.text();
        decoded.append(QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size())));
    }
    if (decoded.isEmpty())
        return fail(QStringLiteral("No QR code was detected in this image."));
    if (error)
        error->clear();
    return decoded;
}
}

QrResultModel::QrResultModel(QObject *parent) : QAbstractListModel(parent) {}

int QrResultModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_results.size();
}

QVariant QrResultModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_results.size())
        return {};
    const QString &value = m_results.at(index.row());
    switch (role) {
    case ResultTextRole: return value;
    case ResultPreviewRole: return value.left(180).replace(QLatin1Char('\n'), QLatin1Char(' '));
    default: return {};
    }
}

QHash<int, QByteArray> QrResultModel::roleNames() const
{
    return {{ResultTextRole, "resultText"}, {ResultPreviewRole, "resultPreview"}};
}

void QrResultModel::setResults(const QStringList &results)
{
    beginResetModel();
    m_results = results;
    endResetModel();
    emit countChanged();
}

QString QrResultModel::resultAt(int row) const
{
    return row >= 0 && row < m_results.size() ? m_results.at(row) : QString();
}

QrCodeManager::QrCodeManager(ScreenshotManager *screenshots, QObject *parent)
    : QObject(parent), m_screenshots(screenshots),
      m_clipboard(screenshots ? screenshots->clipboardService() : nullptr), m_results(this),
      m_watcher(new QFutureWatcher<QrOperationResult>(this))
{
    connect(m_watcher, &QFutureWatcher<QrOperationResult>::finished,
            this, &QrCodeManager::finishOperation);
    if (m_screenshots) {
        connect(m_screenshots, &ScreenshotManager::saveFinished, this,
                [this](const QString &path, bool success, const QString &message) {
            if (path != m_pendingGalleryPath)
                return;
            m_pendingGalleryPath.clear();
            setStatus(success ? QStringLiteral("QR image saved to the screenshot gallery.") : message,
                      !success);
        });
    }
}

QrCodeManager::~QrCodeManager()
{
    if (m_watcher && m_watcher->isRunning()) {
        m_watcher->waitForFinished();
        m_watcher->result();
    }
}

QString QrCodeManager::previewUrl() const
{
    return m_generatedImage.isNull()
               ? QString()
               : QStringLiteral("image://qrpreview/generated?v=%1").arg(m_previewRevision);
}

QImage QrCodeManager::generatedImageSnapshot() const
{
    QMutexLocker lock(&m_imageMutex);
    return m_generatedImage;
}

bool QrCodeManager::generate(const QString &text, int pixelsPerModule,
                             int marginModules, const QString &errorCorrection)
{
    return startGenerate(text, pixelsPerModule, marginModules, errorCorrection, false);
}

bool QrCodeManager::decodeClipboardImage()
{
    if (!m_clipboard) {
        setStatus(QStringLiteral("Clipboard access is unavailable."), true);
        return false;
    }
    const QImage snapshot = m_clipboard->currentImage();
    if (snapshot.isNull()) {
        setStatus(QStringLiteral("The clipboard does not currently contain an image."), true);
        return false;
    }
    return startDecodeImage(snapshot, QStringLiteral("Clipboard image"));
}

bool QrCodeManager::chooseImageToDecode()
{
    const QString file = QFileDialog::getOpenFileName(
        nullptr, QStringLiteral("Decode QR code from image"), QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
        QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.tif *.tiff);;All files (*)"));
    return !file.isEmpty() && decodeFile(file, QStringLiteral("Image file"));
}

bool QrCodeManager::decodeFile(const QString &path, const QString &source)
{
    return startDecodeFile(path, source);
}

bool QrCodeManager::decodeFileUrl(const QVariant &url)
{
    QString path, error;
    if (!LocalFileUrl::toPath(url, &path, &error)) {
        setStatus(error, true);
        return false;
    }
    return decodeFile(path, QStringLiteral("Dropped image"));
}

bool QrCodeManager::copyGeneratedImage()
{
    const QImage image = generatedImageSnapshot();
    if (image.isNull() || !m_clipboard) {
        setStatus(QStringLiteral("Generate a QR image before copying it."), true);
        return false;
    }
    // Shared ownership tracking suppresses this write from automatic screenshot
    // saving and clipboard history while preserving ordinary external copies.
    m_clipboard->writeImage(image);
    setStatus(QStringLiteral("Generated QR image copied to the clipboard."));
    return true;
}

bool QrCodeManager::exportGeneratedImage()
{
    const QImage image = generatedImageSnapshot();
    if (image.isNull()) {
        setStatus(QStringLiteral("Generate a QR image before exporting it."), true);
        return false;
    }
    const QString path = QFileDialog::getSaveFileName(
        nullptr, QStringLiteral("Export QR code"),
        QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation))
            .filePath(QStringLiteral("SmallDTL-QR.png")),
        QStringLiteral("PNG image (*.png)"));
    if (path.isEmpty())
        return false;
    QString target = path;
    if (QFileInfo(target).suffix().isEmpty())
        target += QStringLiteral(".png");
    QSaveFile output(target);
    if (!output.open(QIODevice::WriteOnly)) {
        setStatus(QStringLiteral("Could not export QR image: %1").arg(output.errorString()), true);
        return false;
    }
    QImageWriter writer(&output, QByteArrayLiteral("png"));
    if (!writer.write(image) || !output.commit()) {
        const QString error = writer.errorString().isEmpty() ? output.errorString() : writer.errorString();
        output.cancelWriting();
        setStatus(QStringLiteral("Could not export QR image: %1").arg(error), true);
        return false;
    }
    setStatus(QStringLiteral("QR image exported to %1.").arg(QFileInfo(target).fileName()));
    return true;
}

QString QrCodeManager::saveGeneratedToGallery(const QString &tagId)
{
    const QImage image = generatedImageSnapshot();
    if (image.isNull() || !m_screenshots) {
        setStatus(QStringLiteral("Generate a QR image before saving it to the gallery."), true);
        return {};
    }
    const QString path = m_screenshots->saveGeneratedImage(image, tagId);
    if (path.isEmpty()) {
        setStatus(m_screenshots->lastSaveError().isEmpty()
                      ? QStringLiteral("Could not queue the QR image for the screenshot gallery.")
                      : m_screenshots->lastSaveError(), true);
        return {};
    }
    m_pendingGalleryPath = path;
    setStatus(QStringLiteral("Saving QR image to the screenshot gallery…"));
    return path;
}

void QrCodeManager::selectResult(int row)
{
    if (row < -1 || row >= m_results.rowCount() || row == m_selectedResultIndex)
        return;
    m_selectedResultIndex = row;
    emit selectedResultChanged();
}

bool QrCodeManager::runSequenceAction(quint64 token, const QString &type, const QString &value,
                                      int pixelsPerModule, int marginModules,
                                      const QString &errorCorrection, QString *error)
{
    bool accepted = false;
    if (type == QStringLiteral("generateQr"))
        accepted = startGenerate(value, pixelsPerModule, marginModules, errorCorrection, true, token);
    else if (type == QStringLiteral("decodeQrImage"))
        accepted = startDecodeFile(value, QStringLiteral("Action sequence image"), token);
    if (!accepted && error)
        *error = m_status.isEmpty() ? QStringLiteral("Could not start the QR action.") : m_status;
    return accepted;
}

void QrCodeManager::cancelSequenceAction(quint64 token)
{
    if (token != 0 && token == m_activeActionToken)
        m_cancelledActions.insert(token);
}

QImage QrCodeManager::generateImage(const QString &text, int pixelsPerModule,
                                    int marginModules, const QString &errorCorrection,
                                    QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return QImage();
    };
    const QByteArray utf8 = text.toUtf8();
    if (text.trimmed().isEmpty())
        return fail(QStringLiteral("Enter text or a URL to generate a QR code."));
    if (utf8.size() > MaximumInputBytes)
        return fail(QStringLiteral("QR content is limited to 20 KiB of UTF-8 text."));
    if (pixelsPerModule < 1 || pixelsPerModule > MaximumQrModulePixels ||
        marginModules < 0 || marginModules > MaximumQrMarginModules ||
        !validErrorCorrection(errorCorrection))
        return fail(QStringLiteral("Choose a module size from 1–20, margin from 0–20, and error correction L/M/Q/H."));
    try {
        const int errorCorrectionValue = errorCorrection == QStringLiteral("L") ? 2 :
                                         errorCorrection == QStringLiteral("M") ? 4 :
                                         errorCorrection == QStringLiteral("Q") ? 6 : 8;
        ZXing::MultiFormatWriter writer(ZXing::BarcodeFormat::QRCode);
        writer.setEncoding(ZXing::CharacterSet::UTF8)
              .setEccLevel(errorCorrectionValue)
              .setMargin(marginModules);
        const std::string utf8Text(utf8.constData(), static_cast<size_t>(utf8.size()));
        const ZXing::BitMatrix matrix = writer.encode(utf8Text, 0, 0);
        if (matrix.width() <= 0 || matrix.height() <= 0)
            return fail(QStringLiteral("The QR library returned an empty matrix."));

        const qint64 width = qint64(matrix.width()) * pixelsPerModule;
        const qint64 height = qint64(matrix.height()) * pixelsPerModule;
        const qint64 pixels = width * height;
        if (width > std::numeric_limits<int>::max() || height > std::numeric_limits<int>::max() ||
            pixels <= 0 || pixels > MaximumImagePixels || pixels * 4 > MaximumImageBytes)
            return fail(QStringLiteral("Generated QR image exceeds the 64 megapixel / 256 MiB limit."));
        QImage output(static_cast<int>(width), static_cast<int>(height), QImage::Format_RGB32);
        if (output.isNull())
            return fail(QStringLiteral("Could not allocate the generated QR image."));
        output.fill(Qt::white);
        for (int y = 0; y < matrix.height(); ++y) {
            for (int repeatY = 0; repeatY < pixelsPerModule; ++repeatY) {
                QRgb *line = reinterpret_cast<QRgb *>(output.scanLine(y * pixelsPerModule + repeatY));
                for (int x = 0; x < matrix.width(); ++x) {
                    const QRgb color = matrix.get(x, y) ? qRgb(0, 0, 0) : qRgb(255, 255, 255);
                    std::fill_n(line + x * pixelsPerModule, pixelsPerModule, color);
                }
            }
        }
        if (error)
            error->clear();
        return output;
    } catch (const std::exception &exception) {
        return fail(QStringLiteral("QR generation failed: %1").arg(QString::fromUtf8(exception.what())));
    }
}

QStringList QrCodeManager::decodeImage(const QImage &image, QString *error)
{
    return decodeValidatedImage(image, error);
}

QStringList QrCodeManager::decodeFileImage(const QString &path, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return QStringList{};
    };
    const QFileInfo file(path);
    if (!file.exists() || !file.isFile())
        return fail(QStringLiteral("The image file is unavailable: %1").arg(path));
    if (file.size() < 0 || file.size() > MaximumImageBytes)
        return fail(QStringLiteral("Image file exceeds the 256 MiB limit."));
    QImageReader reader(file.absoluteFilePath());
    reader.setAutoTransform(true);
    const QSize dimensions = reader.size();
    if (dimensions.isValid()) {
        const qint64 pixels = qint64(dimensions.width()) * dimensions.height();
        if (pixels <= 0 || pixels > MaximumImagePixels || pixels * 4 > MaximumImageBytes)
            return fail(QStringLiteral("Image exceeds the 64 megapixel / 256 MiB limit."));
    }
    const QImage image = reader.read();
    if (image.isNull())
        return fail(QStringLiteral("Could not read image: %1").arg(reader.errorString()));
    return decodeValidatedImage(image, error);
}

bool QrCodeManager::startGenerate(const QString &text, int pixelsPerModule,
                                  int marginModules, const QString &errorCorrection,
                                  bool copyToClipboard, quint64 actionToken)
{
    if (!beginOperation(false, QStringLiteral("Generated QR image"), actionToken, copyToClipboard))
        return false;
    const QString content = text;
    m_watcher->setFuture(QtConcurrent::run([content, pixelsPerModule, marginModules, errorCorrection,
                                           actionToken, copyToClipboard] {
        QrOperationResult result;
        result.actionToken = actionToken;
        result.copyToClipboard = copyToClipboard;
        result.image = generateImage(content, pixelsPerModule, marginModules, errorCorrection, &result.error);
        result.success = !result.image.isNull();
        return result;
    }));
    return true;
}

bool QrCodeManager::startDecodeImage(const QImage &image, const QString &source, quint64 actionToken)
{
    if (image.isNull()) {
        setStatus(QStringLiteral("The selected source does not contain an image."), true);
        return false;
    }
    if (image.width() <= 0 || image.height() <= 0 ||
        qint64(image.width()) * image.height() > MaximumImagePixels || image.sizeInBytes() > MaximumImageBytes) {
        setStatus(QStringLiteral("Image exceeds the 64 megapixel / 256 MiB limit."), true);
        return false;
    }
    if (!beginOperation(true, source, actionToken, false))
        return false;
    m_watcher->setFuture(QtConcurrent::run([image, source, actionToken] {
        QrOperationResult result;
        result.actionToken = actionToken;
        result.source = source;
        result.isDecode = true;
        result.decoded = decodeImage(image, &result.error);
        result.success = !result.decoded.isEmpty();
        return result;
    }));
    return true;
}

bool QrCodeManager::startDecodeFile(const QString &path, const QString &source, quint64 actionToken)
{
    const QFileInfo file(path);
    if (path.trimmed().isEmpty() || !file.exists() || !file.isFile()) {
        setStatus(QStringLiteral("Choose an available image file to decode."), true);
        return false;
    }
    if (!beginOperation(true, source, actionToken, false))
        return false;
    m_watcher->setFuture(QtConcurrent::run([path = file.absoluteFilePath(), source, actionToken] {
        QrOperationResult result;
        result.actionToken = actionToken;
        result.source = source;
        result.isDecode = true;
        result.decoded = decodeFileImage(path, &result.error);
        result.success = !result.decoded.isEmpty();
        return result;
    }));
    return true;
}

bool QrCodeManager::beginOperation(bool decode, const QString &source, quint64 actionToken,
                                   bool copyToClipboard)
{
    if (m_busy) {
        setStatus(QStringLiteral("Wait for the current QR operation to finish."), true);
        return false;
    }
    m_busy = true;
    m_operationIsDecode = decode;
    m_inputSource = source;
    m_activeActionToken = actionToken;
    if (actionToken)
        m_cancelledActions.remove(actionToken);
    emit busyChanged();
    emit inputSourceChanged();
    Q_UNUSED(copyToClipboard);
    setStatus(decode ? QStringLiteral("Decoding QR codes locally…") : QStringLiteral("Generating QR code locally…"));
    return true;
}

void QrCodeManager::finishOperation()
{
    QrOperationResult result;
    try {
        result = m_watcher->result();
    } catch (const std::exception &exception) {
        result.error = QStringLiteral("QR operation failed: %1").arg(QString::fromUtf8(exception.what()));
        result.isDecode = m_operationIsDecode;
        result.actionToken = m_activeActionToken;
    } catch (...) {
        result.error = QStringLiteral("QR operation failed unexpectedly.");
        result.isDecode = m_operationIsDecode;
        result.actionToken = m_activeActionToken;
    }
    const bool cancelled = result.actionToken && m_cancelledActions.remove(result.actionToken);
    m_busy = false;
    m_activeActionToken = 0;
    emit busyChanged();
    if (cancelled)
        return;

    if (!result.success) {
        if (result.isDecode)
            m_results.setResults({});
        m_selectedResultIndex = -1;
        emit selectedResultChanged();
        setStatus(result.error.isEmpty() ? QStringLiteral("QR operation failed.") : result.error, true);
        if (result.actionToken)
            emit sequenceActionFinished(result.actionToken, false, m_status);
        return;
    }

    QString output;
    if (result.isDecode) {
        m_results.setResults(result.decoded);
        m_selectedResultIndex = result.decoded.isEmpty() ? -1 : 0;
        emit selectedResultChanged();
        output = result.decoded.join(QLatin1Char('\n'));
        setStatus(QStringLiteral("Found %1 QR code(s) in %2. Review the decoded text before taking any action.")
                      .arg(result.decoded.size()).arg(result.source));
    } else {
        {
            QMutexLocker lock(&m_imageMutex);
            m_generatedImage = result.image;
        }
        ++m_previewRevision;
        emit previewChanged();
        if (result.copyToClipboard && m_clipboard)
            m_clipboard->writeImage(result.image);
        setStatus(result.copyToClipboard ? QStringLiteral("QR generated and copied to the clipboard.")
                                         : QStringLiteral("QR code generated."));
        output = QStringLiteral("QR image copied to the clipboard.");
    }
    if (result.actionToken)
        emit sequenceActionFinished(result.actionToken, true, output);
}

void QrCodeManager::setStatus(const QString &message, bool isError)
{
    if (m_status == message && m_statusIsError == isError)
        return;
    m_status = message;
    m_statusIsError = isError;
    emit statusChanged();
}
