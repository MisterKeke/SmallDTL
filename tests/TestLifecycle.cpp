#include "UnitTests.h"

void UnitTests::init()
{
    QCoreApplication::setApplicationName(QString::fromLatin1(QTest::currentTestFunction()));
    const QString testName = QString::fromLatin1(QTest::currentTestFunction()) +
                             QLatin1Char('-') + QString::fromLatin1(QTest::currentDataTag());
    AppDataPaths::setTestRoot(QDir(g_testData->path()).filePath(testName));
}
