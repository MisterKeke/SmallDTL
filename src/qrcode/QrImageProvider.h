#pragma once

#include "QrCodeManager.h"

#include <QQuickImageProvider>

class QrImageProvider final : public QQuickImageProvider
{
public:
    explicit QrImageProvider(QrCodeManager *manager)
        : QQuickImageProvider(QQuickImageProvider::Image), m_manager(manager) {}

    QImage requestImage(const QString &, QSize *size, const QSize &requestedSize) override
    {
        QImage image = m_manager ? m_manager->generatedImageSnapshot() : QImage();
        if (size)
            *size = image.size();
        if (!image.isNull() && requestedSize.isValid())
            image = image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::FastTransformation);
        return image;
    }

private:
    QrCodeManager *m_manager = nullptr;
};
