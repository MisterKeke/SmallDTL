#pragma once

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QVariantList>
#include <QVector>

struct ScreenshotEditorStroke
{
    QVector<QPointF> points;
    QColor color;
    qreal width = 1.0;
};

class ScreenshotEditorImage final
{
public:
    static QPointF displayToImagePoint(const QPointF &point, const QSizeF &displaySize,
                                       const QSize &imageSize);
    static QRectF cropFromDrag(const QPointF &start, const QPointF &end,
                               const QSize &imageSize, qreal lockedAspectRatio = 0.0);
    static bool render(const QString &path, const QRect &crop,
                       const QVariantList &strokes, QImage *result, QString *error = nullptr);

private:
    static QVector<ScreenshotEditorStroke> parseStrokes(const QVariantList &strokes);
};
