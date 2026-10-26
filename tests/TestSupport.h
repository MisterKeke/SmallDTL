#pragma once
#include "../src/actions/ActionManager.h"
#include "../src/actions/ActionSequenceRunner.h"
#include "../src/app/AppDataPaths.h"
#include "../src/app/LocalFileUrl.h"
#include "../src/app/ApplicationController.h"
#include "../src/app/CommandPaletteManager.h"
#include "../src/clipboard/ClipboardService.h"
#include "../src/fileshelf/FileShelfManager.h"
#include "../src/hotkeys/HotkeyUtils.h"
#include "../src/profiles/ProfileManager.h"
#include "../src/qrcode/QrCodeManager.h"
#include "../src/screenshots/ScreenshotManager.h"
#include "../src/storage/ApplicationDatabase.h"
#include "../src/screenshots/ScreenshotEditorImage.h"
#include "../src/screenshots/ScreenshotNaming.h"
#include "../src/settings/SettingsManager.h"
#include "../src/windows/WindowArrangementGeometry.h"
#include "../src/windows/WindowArrangementMatching.h"
#include "../src/windows/WindowArrangementManager.h"

#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QScopeGuard>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <QTimeZone>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QPainter>
#include <QPen>
#include <QQmlApplicationEngine>
#include <QTimer>
#include <QElapsedTimer>
#include <QThreadPool>
#include <QSettings>
#include <QCursor>
#include <QScreen>
#include "../src/screenshots/ThumbnailProvider.h"
#include "../src/qrcode/QrImageProvider.h"
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#include <winioctl.h>
#endif
#include <QtTest>
#include <functional>
#include <memory>
#include <algorithm>
#include <cstring>

extern std::unique_ptr<QTemporaryDir> g_testData;

inline int retroTestFontId()
{
    static const int id = QFontDatabase::addApplicationFont(
        QStringLiteral(":/qt/qml/ScreenshotHotkeyManagerTest/qml/assets/fonts/CascadiaCode.ttf"));
    return id;
}

inline void applyRetroTestFont()
{
    const QStringList families = QFontDatabase::applicationFontFamilies(retroTestFontId());
    if (families.isEmpty())
        return;
    QFont font = QApplication::font();
    font.setFamily(families.first());
    font.setStyleHint(QFont::Monospace);
    QApplication::setFont(font);
}

inline QQuickItem *findItem(QQuickItem *root, const QString &name)
{
    if (root->objectName() == name)
        return root;
    for (QQuickItem *child : root->childItems()) {
        if (QQuickItem *found = findItem(child, name))
            return found;
    }
    return nullptr;
}

