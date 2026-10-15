#include "app/ApplicationController.h"
#include "app/AppDataPaths.h"
#include "screenshots/ThumbnailProvider.h"
#include <QImageReader>

#include <QApplication>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QLocalServer>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QWindow>
#include <QTemporaryDir>
#include <QTimer>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFile>
#include <QDir>
#include <memory>
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    const bool smokeTest=application.arguments().contains(QStringLiteral("--smoke-test"));
    std::unique_ptr<QTemporaryDir> smokeData;
    if(smokeTest) {smokeData=std::make_unique<QTemporaryDir>(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("smoke-data-XXXXXX")));if(!smokeData->isValid())return 2;AppDataPaths::setTestRoot(smokeData->path());}
    QCoreApplication::setOrganizationName(QStringLiteral("SmallDTL"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("local.small-dtl"));
    // Keep the storage/instance identity stable for existing installations.
    QCoreApplication::setApplicationName(QStringLiteral("ScreenshotHotkeyManager"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("SmallDTL"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    application.setQuitOnLastWindowClosed(true);

    const QByteArray identity = QCryptographicHash::hash(
        AppDataPaths::root().toUtf8(),
        QCryptographicHash::Sha256).toHex().left(16);
    const QString serverName = QStringLiteral("ScreenshotHotkeyManager-") + QString::fromLatin1(identity);

    QLocalSocket existingInstance;
    existingInstance.connectToServer(serverName, QIODevice::WriteOnly);
    if (existingInstance.waitForConnected(350)) {
        existingInstance.write(QByteArrayLiteral("activate"));
        existingInstance.waitForBytesWritten(500);
        return 0;
    }

    QLocalServer::removeServer(serverName);
    QLocalServer server;
    if (!server.listen(serverName))
        return 1;

    QQuickStyle::setStyle(QStringLiteral("Material"));
    QImageReader::setAllocationLimit(256);
    ApplicationController controller;
    QObject::connect(&server, &QLocalServer::newConnection, &controller, [&server, &controller] {
        while (QLocalSocket *socket = server.nextPendingConnection()) {
            QObject::connect(socket, &QLocalSocket::readyRead, &controller, [socket, &controller] {
                socket->readAll();
                controller.showWindow();
                socket->disconnectFromServer();
            });
            QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        }
    });

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("thumbnail"),new ThumbnailProvider);
    int smokeWarnings=0;
    if(smokeTest)QObject::connect(&engine,&QQmlEngine::warnings,&application,[&smokeWarnings](const QList<QQmlError> &warnings){smokeWarnings+=warnings.size();});
    engine.rootContext()->setContextProperty(QStringLiteral("app"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("settings"), controller.settings());
    engine.rootContext()->setContextProperty(QStringLiteral("profiles"), controller.profiles());
    engine.rootContext()->setContextProperty(QStringLiteral("screenshots"), controller.screenshots());
    engine.rootContext()->setContextProperty(QStringLiteral("hotkeys"), controller.hotkeys());
    engine.rootContext()->setContextProperty(QStringLiteral("actions"), controller.actions());
    engine.rootContext()->setContextProperty(QStringLiteral("actionRunner"), controller.actionRunner());

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &application, [] { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("ScreenshotHotkeyManager"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty())
        return 1;

    auto *window = qobject_cast<QWindow *>(engine.rootObjects().constFirst());
    controller.initializeWindow(window);
    controller.showAtStartup();
    if(smokeTest)QTimer::singleShot(1000,&application,[&]{
        for(const QString &page:{QStringLiteral("Dashboard"),QStringLiteral("Screenshots"),QStringLiteral("Hotkeys"),QStringLiteral("Profiles"),QStringLiteral("Actions"),QStringLiteral("Settings")})controller.setCurrentPage(page);
        auto metrics=std::make_shared<QJsonObject>(QJsonObject{{QStringLiteral("qmlWarnings"),smokeWarnings},{QStringLiteral("loaded"),!engine.rootObjects().isEmpty()},
            {QStringLiteral("applicationDisplayName"),QGuiApplication::applicationDisplayName()},
            {QStringLiteral("windowTitle"),window ? window->title() : QString()}});
#ifdef Q_OS_WIN
        FILETIME created,exitTime,kernel,user;
        GetProcessTimes(GetCurrentProcess(),&created,&exitTime,&kernel,&user);
        auto cpuTime=[](FILETIME value){return (quint64(value.dwHighDateTime)<<32)|value.dwLowDateTime;};
        const quint64 cpuBefore=cpuTime(kernel)+cpuTime(user);
#else
        const quint64 cpuBefore=0;
#endif
        QTimer::singleShot(2000,&application,[&,metrics,cpuBefore]{
            metrics->insert(QStringLiteral("qmlWarnings"),smokeWarnings);
#ifdef Q_OS_WIN
            FILETIME created,exitTime,kernel,user;GetProcessTimes(GetCurrentProcess(),&created,&exitTime,&kernel,&user);
            auto cpuTime=[](FILETIME value){return (quint64(value.dwHighDateTime)<<32)|value.dwLowDateTime;};
            metrics->insert(QStringLiteral("idleCpuMilliseconds"),double(cpuTime(kernel)+cpuTime(user)-cpuBefore)/10000.0);
            PROCESS_MEMORY_COUNTERS memory{};memory.cb=sizeof(memory);GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory));
            metrics->insert(QStringLiteral("workingSetMiB"),double(memory.WorkingSetSize)/1048576.0);metrics->insert(QStringLiteral("idleSampleMilliseconds"),2000);
#endif
            QFile result(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("smoke-result.json")));
            const bool written=result.open(QIODevice::WriteOnly) && result.write(QJsonDocument(*metrics).toJson())>0;
            application.exit(written && smokeWarnings==0?0:3);
        });
    });
    return application.exec();
}
