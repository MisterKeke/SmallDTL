#include "ScreenshotEditorImage.h"

#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QFont>
#include <QPolygonF>
#include <QVariantMap>
#include <QtMath>
#include <cmath>
#include <utility>

namespace {
constexpr quint64 kMaximumPixels = 64ULL * 1024 * 1024;
constexpr quint64 kMaximumBytes = 256ULL * 1024 * 1024;

bool withinImageLimits(const QSize &size)
{
    if (!size.isValid() || size.width() <= 0 || size.height() <= 0)
        return false;
    const quint64 pixels = quint64(size.width()) * quint64(size.height());
    return pixels <= kMaximumPixels && pixels * 4 <= kMaximumBytes;
}

qreal bounded(qreal value, qreal maximum)
{
    return qBound<qreal>(0.0, std::isfinite(value) ? value : 0.0, maximum);
}
}

QPointF ScreenshotEditorImage::displayToImagePoint(const QPointF &point,
                                                    const QSizeF &displaySize,
                                                    const QSize &imageSize)
{
    if (!displaySize.isValid() || displaySize.width() <= 0 || displaySize.height() <= 0 ||
        !imageSize.isValid() || imageSize.width() <= 0 || imageSize.height() <= 0)
        return {};

    const qreal scale = qMin(displaySize.width() / imageSize.width(),
                             displaySize.height() / imageSize.height());
    const QSizeF drawnSize(imageSize.width() * scale, imageSize.height() * scale);
    const QPointF offset((displaySize.width() - drawnSize.width()) / 2.0,
                         (displaySize.height() - drawnSize.height()) / 2.0);
    return QPointF(bounded((point.x() - offset.x()) / scale, imageSize.width()),
                   bounded((point.y() - offset.y()) / scale, imageSize.height()));
}

QRectF ScreenshotEditorImage::cropFromDrag(const QPointF &start, const QPointF &end,
                                           const QSize &imageSize, qreal lockedAspectRatio)
{
    if (!imageSize.isValid() || imageSize.width() <= 0 || imageSize.height() <= 0)
        return {};
    const QPointF first(bounded(start.x(), imageSize.width()), bounded(start.y(), imageSize.height()));
    const QPointF last(bounded(end.x(), imageSize.width()), bounded(end.y(), imageSize.height()));
    qreal width = qAbs(last.x() - first.x());
    qreal height = qAbs(last.y() - first.y());
    if (std::isfinite(lockedAspectRatio) && lockedAspectRatio > 0.0 && width > 0.0 && height > 0.0) {
        if (width / height > lockedAspectRatio)
            width = height * lockedAspectRatio;
        else
            height = width / lockedAspectRatio;
    }
    const qreal x = last.x() < first.x() ? first.x() - width : first.x();
    const qreal y = last.y() < first.y() ? first.y() - height : first.y();
    return QRectF(x, y, width, height);
}

QVector<ScreenshotEditorStroke> ScreenshotEditorImage::parseStrokes(const QVariantList &strokes)
{
    QVector<ScreenshotEditorStroke> parsed;
    parsed.reserve(strokes.size());
    for (const QVariant &strokeValue : strokes) {
        const QVariantMap strokeMap = strokeValue.toMap();
        ScreenshotEditorStroke stroke;
        stroke.type = strokeMap.value(QStringLiteral("type"), QStringLiteral("draw")).toString();
        if (!QStringList{QStringLiteral("draw"), QStringLiteral("arrow"), QStringLiteral("rectangle"),
                         QStringLiteral("ellipse"), QStringLiteral("highlight"), QStringLiteral("redact"),
                         QStringLiteral("text")}.contains(stroke.type)) continue;
        stroke.text = strokeMap.value(QStringLiteral("text")).toString().left(1000);
        const qreal fontSize = strokeMap.value(QStringLiteral("fontSize"), 32.0).toDouble();
        stroke.fontSize = qBound<qreal>(8, std::isfinite(fontSize) ? fontSize : 32, 512);
        stroke.color = QColor(strokeMap.value(QStringLiteral("color")).toString());
        if (stroke.type == QStringLiteral("redact")) stroke.color = QColor(Qt::black);
        if (stroke.type == QStringLiteral("highlight")) stroke.color.setAlphaF(0.30);
        const qreal width = strokeMap.value(QStringLiteral("width")).toDouble();
        stroke.width = qBound<qreal>(0.25, std::isfinite(width) ? width : 1.0, 512.0);
        const QVariantList points = strokeMap.value(QStringLiteral("points")).toList();
        stroke.points.reserve(points.size());
        for (const QVariant &pointValue : points) {
            const QVariantMap pointMap = pointValue.toMap();
            const qreal x = pointMap.value(QStringLiteral("x")).toDouble();
            const qreal y = pointMap.value(QStringLiteral("y")).toDouble();
            if (std::isfinite(x) && std::isfinite(y))
                stroke.points.append(QPointF(x, y));
        }
        const bool enoughPoints = stroke.points.size() >= ((stroke.type == QStringLiteral("draw") || stroke.type == QStringLiteral("text")) ? 1 : 2);
        if (stroke.color.isValid() && enoughPoints && (stroke.type != QStringLiteral("text") || !stroke.text.trimmed().isEmpty()))
            parsed.append(std::move(stroke));
    }
    return parsed;
}

bool ScreenshotEditorImage::render(const QString &path, const QRect &requestedCrop,
                                   const QVariantList &strokes, QImage *result, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (!result)
        return fail(QStringLiteral("No image output was provided."));
    *result = QImage();
    if (!QFileInfo(path).isFile())
        return fail(QStringLiteral("The source screenshot is no longer available."));

    QImageReader reader(path);
    reader.setAutoTransform(false);
    const QSize reportedSize = reader.size();
    if (!withinImageLimits(reportedSize))
        return fail(QStringLiteral("The source image exceeds the 64 megapixel / 256 MiB editing limit."));
    QImage source = reader.read();
    if (source.isNull())
        return fail(QStringLiteral("Could not decode the source screenshot: %1").arg(reader.errorString()));
    if (!withinImageLimits(source.size()) || source.sizeInBytes() > qint64(kMaximumBytes))
        return fail(QStringLiteral("The decoded source image exceeds the 64 megapixel / 256 MiB editing limit."));

    QRect crop = requestedCrop;
    if (crop.isEmpty())
        crop = source.rect();
    crop = crop.intersected(source.rect());
    if (crop.width() <= 0 || crop.height() <= 0)
        return fail(QStringLiteral("The crop area is empty."));
    const QSize outputSize = crop.size();
    if (!withinImageLimits(outputSize))
        return fail(QStringLiteral("The edited image exceeds the 64 megapixel / 256 MiB limit."));

    const int dotsPerMeterX = source.dotsPerMeterX();
    const int dotsPerMeterY = source.dotsPerMeterY();
    QImage output;
    if (crop == source.rect())
        output = std::move(source);
    else {
        output = source.copy(crop);
        source = QImage();
    }
    if (output.format() == QImage::Format_Indexed8 || output.format() == QImage::Format_Mono ||
        output.format() == QImage::Format_MonoLSB)
        output = output.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (output.isNull() || output.sizeInBytes() > qint64(kMaximumBytes))
        return fail(QStringLiteral("Could not allocate the edited image within the image resource limit."));
    output.setDotsPerMeterX(dotsPerMeterX);
    output.setDotsPerMeterY(dotsPerMeterY);

    const QVector<ScreenshotEditorStroke> parsedStrokes = parseStrokes(strokes);
    if (!parsedStrokes.isEmpty()) {
        QPainter painter(&output);
        if (!painter.isActive())
            return fail(QStringLiteral("Could not start drawing on the edited image."));
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setClipRect(QRectF(0, 0, output.width(), output.height()));
        painter.translate(-crop.x(), -crop.y());
        for (const ScreenshotEditorStroke &stroke : parsedStrokes) {
            QPen pen(stroke.color, stroke.width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            painter.setPen(pen);
            const QPointF start = stroke.points.first();
            const QPointF end = stroke.points.last();
            const QRectF bounds = QRectF(start, end).normalized();
            if (stroke.type == QStringLiteral("text")) {
                QFont font(QStringLiteral("Segoe UI"));
                font.setPixelSize(qRound(stroke.fontSize));
                painter.setFont(font);
                painter.drawText(start + QPointF(0, stroke.fontSize), stroke.text);
                continue;
            }
            if (stroke.type == QStringLiteral("highlight") || stroke.type == QStringLiteral("redact")) {
                painter.fillRect(bounds.toAlignedRect(), stroke.color);
                continue;
            }
            if (stroke.type == QStringLiteral("rectangle")) { painter.drawRect(bounds); continue; }
            if (stroke.type == QStringLiteral("ellipse")) { painter.drawEllipse(bounds); continue; }
            if (stroke.type == QStringLiteral("arrow")) {
                const qreal dx = end.x() - start.x(), dy = end.y() - start.y();
                const qreal length = std::hypot(dx, dy);
                if (length <= 0) continue;
                const QPointF direction(dx / length, dy / length);
                const QPointF normal(-direction.y(), direction.x());
                const qreal head = qMin(length / 2, qMax<qreal>(12, stroke.width * 3));
                painter.drawLine(start, end);
                painter.setPen(Qt::NoPen);
                painter.setBrush(stroke.color);
                painter.drawPolygon(QPolygonF{end, end - direction * head + normal * head * 0.5,
                                                   end - direction * head - normal * head * 0.5});
                painter.setBrush(Qt::NoBrush);
                continue;
            }
            if (stroke.points.size() == 1) {
                painter.drawPoint(stroke.points.first());
                continue;
            }
            QPainterPath path;
            path.moveTo(stroke.points.first());
            for (int i = 1; i < stroke.points.size(); ++i)
                path.lineTo(stroke.points.at(i));
            painter.drawPath(path);
        }
    }
    *result = std::move(output);
    if (error)
        error->clear();
    return true;
}
