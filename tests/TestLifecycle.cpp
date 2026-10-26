#include "UnitTests.h"
#include <QFontDatabase>

void UnitTests::init()
{
    static const QFont standardFont = QApplication::font();
    QApplication::setFont(standardFont);
    const int retroFontId = retroTestFontId();
    QVERIFY(retroFontId >= 0);
    QVERIFY(!QFontDatabase::applicationFontFamilies(retroFontId).isEmpty());
    QCoreApplication::setApplicationName(QString::fromLatin1(QTest::currentTestFunction()));
    const QString testName = QString::fromLatin1(QTest::currentTestFunction()) +
                             QLatin1Char('-') + QString::fromLatin1(QTest::currentDataTag());
    AppDataPaths::setTestRoot(QDir(g_testData->path()).filePath(testName));
}
