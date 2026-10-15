#pragma once
#include <QQuickImageProvider>
#include <QImageReader>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QImageWriter>
#include <QDateTime>
#include "../app/AppDataPaths.h"

// QML requests only visible thumbnails. Qt runs these requests on its image
// loading thread; full-size preview URLs remain separate.
class ThumbnailProvider final : public QQuickImageProvider {
public:
    ThumbnailProvider() : QQuickImageProvider(QQuickImageProvider::Image, QQuickImageProvider::ForceAsynchronousImageLoading), m_cacheRoot(QDir(AppDataPaths::root()).filePath(QStringLiteral("cache/thumbnails"))) {}
    QImage requestImage(const QString &id,QSize *size,const QSize &requested) override {
        const QString path=QString::fromUtf8(QByteArray::fromBase64(id.section(QLatin1Char('/'),0,0).toLatin1(),QByteArray::Base64UrlEncoding));
        const QFileInfo file(path);
        if(!file.exists() || !file.isFile())return {};
        const QSize bounds(requested.width()>0?qBound(16,requested.width(),512):240,requested.height()>0?qBound(16,requested.height(),512):160);
        const QByteArray signature=(path+QLatin1Char('\n')+QString::number(file.lastModified().toMSecsSinceEpoch())+QLatin1Char('\n')+QString::number(file.size())+QLatin1Char('\n')+QString::number(bounds.width())+QLatin1Char('x')+QString::number(bounds.height())).toUtf8();
        const QString directory=m_cacheRoot;
        QDir().mkpath(directory);
        const QString cached=QDir(directory).filePath(QString::fromLatin1(QCryptographicHash::hash(signature,QCryptographicHash::Sha256).toHex())+QStringLiteral(".png"));
        QImage image(cached);
        if(image.isNull()) {
            QImageReader reader(path);
            const QSize original=reader.size();
            if(!original.isValid() || qint64(original.width())*original.height()>64*1024*1024)return {};
            reader.setAutoTransform(true);reader.setScaledSize(original.scaled(bounds,Qt::KeepAspectRatio));image=reader.read();
            if(image.isNull())return {};
            image=image.scaled(bounds,Qt::KeepAspectRatio,Qt::SmoothTransformation);
            QSaveFile output(cached);
            if(output.open(QIODevice::WriteOnly)){QImageWriter writer(&output,"png");if(writer.write(image))output.commit();else output.cancelWriting();}
        }
        if(size)*size=image.size();return image;
    }
private:
    const QString m_cacheRoot;
};
