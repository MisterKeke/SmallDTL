#include "UnitTests.h"

std::unique_ptr<QTemporaryDir> g_testData;

int main(int argc, char **argv)
{
#ifdef Q_OS_WIN
    const QString fonts=QDir(qEnvironmentVariable("WINDIR")).filePath(QStringLiteral("Fonts"));
    if(QDir(fonts).exists())qputenv("QT_QPA_FONTDIR",QFile::encodeName(fonts));
#endif
    QApplication app(argc, argv);
    // Offscreen QPA does not automatically load Windows system fonts.
    for (const QString &directory : QStandardPaths::standardLocations(QStandardPaths::FontsLocation)) {
        const QString font = QDir(directory).filePath(QStringLiteral("segoeui.ttf"));
        if (QFileInfo::exists(font)) {
            QFontDatabase::addApplicationFont(font);
            app.setFont(QFont(QStringLiteral("Segoe UI")));
            break;
        }
    }
    QQuickStyle::setStyle(QStringLiteral("Material"));
    QImageReader::setAllocationLimit(256);
    g_testData = std::make_unique<QTemporaryDir>(
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("test-data-XXXXXX")));
    if (!g_testData->isValid())
        return 2;
    QCoreApplication::setOrganizationName(QStringLiteral("SmallDTL"));
    QCoreApplication::setApplicationName(QStringLiteral("UnitTests"));
    UnitTests tests;
    return QTest::qExec(&tests, argc, argv);
}
