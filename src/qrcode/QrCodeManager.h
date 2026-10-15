#pragma once

#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QStringList>

class ClipboardService;
class ScreenshotManager;

struct QrOperationResult
{
    bool success = false;
    QImage image;
    QStringList decoded;
    QString error;
    QString source;
    quint64 actionToken = 0;
    bool copyToClipboard = false;
    bool isDecode = false;
};

class QrResultModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    enum Role { ResultTextRole = Qt::UserRole + 1, ResultPreviewRole };
    Q_ENUM(Role)

    explicit QrResultModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void setResults(const QStringList &results);
    QString resultAt(int row) const;

signals:
    void countChanged();

private:
    QStringList m_results;
};

class QrCodeManager final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool statusIsError READ statusIsError NOTIFY statusChanged)
    Q_PROPERTY(QString previewUrl READ previewUrl NOTIFY previewChanged)
    Q_PROPERTY(int previewRevision READ previewRevision NOTIFY previewChanged)
    Q_PROPERTY(QrResultModel *results READ results CONSTANT)
    Q_PROPERTY(int selectedResultIndex READ selectedResultIndex WRITE selectResult NOTIFY selectedResultChanged)
    Q_PROPERTY(QString selectedResult READ selectedResult NOTIFY selectedResultChanged)
    Q_PROPERTY(QString inputSource READ inputSource NOTIFY inputSourceChanged)

public:
    QrCodeManager(ScreenshotManager *screenshots, QObject *parent = nullptr);
    ~QrCodeManager() override;

    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    bool statusIsError() const { return m_statusIsError; }
    QString previewUrl() const;
    int previewRevision() const { return m_previewRevision; }
    QrResultModel *results() { return &m_results; }
    int selectedResultIndex() const { return m_selectedResultIndex; }
    QString selectedResult() const { return m_results.resultAt(m_selectedResultIndex); }
    QString inputSource() const { return m_inputSource; }
    QImage generatedImageSnapshot() const;

    Q_INVOKABLE bool generate(const QString &text, int pixelsPerModule = 8,
                              int marginModules = 4, const QString &errorCorrection = QStringLiteral("M"));
    Q_INVOKABLE bool decodeClipboardImage();
    Q_INVOKABLE bool chooseImageToDecode();
    Q_INVOKABLE bool decodeFile(const QString &path, const QString &source = QStringLiteral("Image file"));
    Q_INVOKABLE bool copyGeneratedImage();
    Q_INVOKABLE bool exportGeneratedImage();
    Q_INVOKABLE QString saveGeneratedToGallery(const QString &tagId = {});
    Q_INVOKABLE void selectResult(int row);

    bool runSequenceAction(quint64 token, const QString &type, const QString &value,
                           int pixelsPerModule, int marginModules,
                           const QString &errorCorrection, QString *error);
    void cancelSequenceAction(quint64 token);

    static QImage generateImage(const QString &text, int pixelsPerModule,
                                int marginModules, const QString &errorCorrection,
                                QString *error = nullptr);
    static QStringList decodeImage(const QImage &image, QString *error = nullptr);
    static QStringList decodeFileImage(const QString &path, QString *error = nullptr);

signals:
    void busyChanged();
    void statusChanged();
    void previewChanged();
    void selectedResultChanged();
    void inputSourceChanged();
    void sequenceActionFinished(quint64 token, bool success, const QString &message);

private:
    bool startGenerate(const QString &text, int pixelsPerModule, int marginModules,
                       const QString &errorCorrection, bool copyToClipboard,
                       quint64 actionToken = 0);
    bool startDecodeImage(const QImage &image, const QString &source, quint64 actionToken = 0);
    bool startDecodeFile(const QString &path, const QString &source, quint64 actionToken = 0);
    bool beginOperation(bool decode, const QString &source, quint64 actionToken,
                        bool copyToClipboard);
    void finishOperation();
    void setStatus(const QString &message, bool isError = false);

    ScreenshotManager *m_screenshots = nullptr;
    ClipboardService *m_clipboard = nullptr;
    mutable QMutex m_imageMutex;
    QrResultModel m_results;
    QFutureWatcher<QrOperationResult> *m_watcher = nullptr;
    QImage m_generatedImage;
    QString m_status;
    QString m_inputSource;
    QString m_pendingGalleryPath;
    int m_previewRevision = 0;
    int m_selectedResultIndex = -1;
    quint64 m_activeActionToken = 0;
    QSet<quint64> m_cancelledActions;
    bool m_busy = false;
    bool m_statusIsError = false;
    bool m_operationIsDecode = false;
};
