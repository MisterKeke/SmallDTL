#include "SettingsManager.h"
#include "../app/AppDataPaths.h"
#include "../app/JsonValidation.h"
#include "../screenshots/ScreenshotNaming.h"
#include <QCoreApplication>
#include <QGuiApplication>
#include <QStyleHints>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QColor>
#include <QHash>
#include <QDebug>

SettingsManager::SettingsManager(QObject *parent) : QObject(parent) {
    load();
    m_loading = false;
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this]{m_cachedPalette.clear();emit preferencesChanged();});
}
QString SettingsManager::settingsFilePath() const { return QDir(AppDataPaths::root()).filePath(QStringLiteral("settings.json")); }
QString SettingsManager::generalScreenshotDirectory() const { return m_data.value(QStringLiteral("generalScreenshotDirectory")).toString(); }
QString SettingsManager::activeProfileId() const { return m_data.value(QStringLiteral("activeProfileId")).toString(); }
QStringList SettingsManager::knownScreenshotDirectories() const {
    QStringList result;
    for (const auto &v : m_data.value(QStringLiteral("knownScreenshotDirectories")).toArray()) result.append(v.toString());
    return result;
}
bool SettingsManager::darkTheme() const {
    if (retroArcade()) return true;
    return appearance() == QStringLiteral("dark") || (appearance() == QStringLiteral("system") && QGuiApplication::styleHints()->colorScheme() != Qt::ColorScheme::Light);
}
QString SettingsManager::visualStyle() const { return m_data.value(QStringLiteral("visualStyle")).toString(QStringLiteral("standard")); }
bool SettingsManager::retroArcade() const { return visualStyle() == QStringLiteral("retro-arcade"); }
void SettingsManager::report(const QString &message) {
    if (m_loading) m_startupMessages.append(message);
    emit errorOccurred(message);
    qWarning().noquote() << message;
}
void SettingsManager::load() {
    const QString dataRoot = AppDataPaths::root();
    QDir().mkpath(dataRoot);
    const QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString fallback = QDir(AppDataPaths::hasTestRoot() || pictures.isEmpty() ? dataRoot : pictures).filePath(AppDataPaths::hasTestRoot()?QStringLiteral("Screenshots"):QStringLiteral("SmallDTL"));
    m_data = {{QStringLiteral("schemaVersion"), 1}, {QStringLiteral("generalScreenshotDirectory"), fallback}, {QStringLiteral("activeProfileId"), QString()}, {QStringLiteral("knownScreenshotDirectories"), QJsonArray{fallback}}};
    m_data.insert(QStringLiteral("closeToTray"), true);
    m_data.insert(QStringLiteral("startMinimized"), false);
    m_data.insert(QStringLiteral("minimizeToTray"), false);
    m_data.insert(QStringLiteral("startWithWindows"), false);
    m_data.insert(QStringLiteral("notificationsEnabled"), true);
    m_data.insert(QStringLiteral("filenamePattern"), QStringLiteral("Screenshot_{timestamp}"));
    m_data.insert(QStringLiteral("appearance"), QStringLiteral("dark"));
    m_data.insert(QStringLiteral("accentColor"), QStringLiteral("#e96782"));
    m_data.insert(QStringLiteral("visualStyle"), QStringLiteral("standard"));
    m_data.insert(QStringLiteral("automaticClipboardSavingEnabled"), false);
    m_data.insert(QStringLiteral("automaticClipboardSaveTagId"), QString());
    m_data.insert(QStringLiteral("clipboardHistoryShortcutKey"), int(Qt::Key_V));
    m_data.insert(QStringLiteral("clipboardHistoryShortcutModifiers"), int(Qt::ControlModifier | Qt::ShiftModifier));
    m_data.insert(QStringLiteral("fileShelfShortcutKey"), int(Qt::Key_F));
    m_data.insert(QStringLiteral("fileShelfShortcutModifiers"), int(Qt::ControlModifier | Qt::ShiftModifier));
    m_data.insert(QStringLiteral("fileShelfQuickAddShortcutKey"), int(Qt::Key_5));
    m_data.insert(QStringLiteral("fileShelfQuickAddShortcutModifiers"), int(Qt::AltModifier | Qt::KeypadModifier));
    m_data.insert(QStringLiteral("commandPaletteShortcutKey"), int(Qt::Key_P));
    m_data.insert(QStringLiteral("commandPaletteShortcutModifiers"), int(Qt::ControlModifier | Qt::ShiftModifier));
    m_data.insert(QStringLiteral("clipboardHistoryRetentionDays"), 30);
    m_data.insert(QStringLiteral("clipboardHistoryRetentionCount"), 500);
    m_data.insert(QStringLiteral("clipboardHistoryStorageLimitMB"), 512);
    m_data.insert(QStringLiteral("clipboardHistoryEnabled"), true);
    m_data.insert(QStringLiteral("clipboardHistorySessionOnly"), false);
    m_data.insert(QStringLiteral("clipboardHistoryExcludedApplications"), QJsonArray{});
    m_data.insert(QStringLiteral("thumbnailCacheLimitMB"), 128);
    QFile file(settingsFilePath());
    if (!file.exists()) {
        QDir().mkpath(fallback);
        commit(m_data);
        return;
    }
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024) {
        m_invalid = true; report(QStringLiteral("Settings cannot be read; the original is preserved.")); return;
    }
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    const QJsonObject stored = doc.object();
    bool valid = error.error == QJsonParseError::NoError && doc.isObject() && stored.value(QStringLiteral("schemaVersion")).toInt() == 1;
    m_futureSchema = stored.value(QStringLiteral("schemaVersion")).toInt() > 1;
    for (auto it = m_data.begin(); it != m_data.end(); ++it) {
        if (stored.contains(it.key()) && stored.value(it.key()).type() != it.value().type()) valid = false;
    }
    if (stored.contains(QStringLiteral("knownScreenshotDirectories"))) {
        for (const auto &v : stored.value(QStringLiteral("knownScreenshotDirectories")).toArray())
            if (!v.isString() || v.toString().isEmpty() || !QDir::isAbsolutePath(v.toString())) valid = false;
    }
    if (stored.contains(QStringLiteral("generalScreenshotDirectory")) && !QDir::isAbsolutePath(stored.value(QStringLiteral("generalScreenshotDirectory")).toString())) valid = false;
    if (stored.contains(QStringLiteral("filenamePattern")) && !ScreenshotNaming::validPattern(stored.value(QStringLiteral("filenamePattern")).toString())) valid = false;
    if (stored.contains(QStringLiteral("appearance")) && !QStringList{QStringLiteral("dark"), QStringLiteral("light"), QStringLiteral("system")}.contains(stored.value(QStringLiteral("appearance")).toString())) valid = false;
    if (stored.contains(QStringLiteral("visualStyle")) && !QStringList{QStringLiteral("standard"), QStringLiteral("retro-arcade")}.contains(stored.value(QStringLiteral("visualStyle")).toString())) valid = false;
    if (stored.contains(QStringLiteral("accentColor")) && !QColor::isValidColorName(stored.value(QStringLiteral("accentColor")).toString())) valid = false;
    if (stored.contains(QStringLiteral("clipboardHistoryRetentionDays")) &&
        (stored.value(QStringLiteral("clipboardHistoryRetentionDays")).toInt() < 1 || stored.value(QStringLiteral("clipboardHistoryRetentionDays")).toInt() > 3650)) valid = false;
    if (stored.contains(QStringLiteral("clipboardHistoryRetentionCount")) &&
        (stored.value(QStringLiteral("clipboardHistoryRetentionCount")).toInt() < 1 || stored.value(QStringLiteral("clipboardHistoryRetentionCount")).toInt() > 100000)) valid = false;
    if (stored.contains(QStringLiteral("clipboardHistoryStorageLimitMB")) &&
        (stored.value(QStringLiteral("clipboardHistoryStorageLimitMB")).toInt() < 16 || stored.value(QStringLiteral("clipboardHistoryStorageLimitMB")).toInt() > 65536)) valid = false;
    if (stored.contains(QStringLiteral("clipboardHistoryShortcutKey")) && stored.value(QStringLiteral("clipboardHistoryShortcutKey")).toInt() < 0) valid = false;
    if (stored.contains(QStringLiteral("clipboardHistoryShortcutModifiers")) && stored.value(QStringLiteral("clipboardHistoryShortcutModifiers")).toInt() < 0) valid = false;
    if (stored.contains(QStringLiteral("fileShelfShortcutKey")) && stored.value(QStringLiteral("fileShelfShortcutKey")).toInt() < 0) valid = false;
    if (stored.contains(QStringLiteral("fileShelfShortcutModifiers")) && stored.value(QStringLiteral("fileShelfShortcutModifiers")).toInt() < 0) valid = false;
    if (stored.contains(QStringLiteral("fileShelfQuickAddShortcutKey")) && stored.value(QStringLiteral("fileShelfQuickAddShortcutKey")).toInt() < 0) valid = false;
    if (stored.contains(QStringLiteral("fileShelfQuickAddShortcutModifiers")) && stored.value(QStringLiteral("fileShelfQuickAddShortcutModifiers")).toInt() < 0) valid = false;
    if (stored.contains(QStringLiteral("commandPaletteShortcutKey")) && stored.value(QStringLiteral("commandPaletteShortcutKey")).toInt() < 0) valid = false;
    if (stored.contains(QStringLiteral("commandPaletteShortcutModifiers")) && stored.value(QStringLiteral("commandPaletteShortcutModifiers")).toInt() < 0) valid = false;
    if (stored.contains(QStringLiteral("thumbnailCacheLimitMB")) &&
        (stored.value(QStringLiteral("thumbnailCacheLimitMB")).toInt() < 16 || stored.value(QStringLiteral("thumbnailCacheLimitMB")).toInt() > 4096)) valid = false;
    if (stored.value(QStringLiteral("clipboardHistoryExcludedApplications")).toArray().size() > 256) valid = false;
    for (const auto &entry : stored.value(QStringLiteral("clipboardHistoryExcludedApplications")).toArray())
        if (!entry.isString() || entry.toString().size() > 32768) valid = false;
    if (!valid) {
        m_invalid = true; report(QStringLiteral("Settings are invalid or use an unsupported schema. The original is preserved; supported data is backed up before repair."));
        QDir().mkpath(fallback);
        return;
    }
    for (auto it = stored.begin(); it != stored.end(); ++it) m_data.insert(it.key(), it.value());
}
bool SettingsManager::commit(const QJsonObject &candidate) {
    if (m_futureSchema) { report(QStringLiteral("Settings were created by a newer version; saving is disabled to preserve them.")); return false; }
    if (m_invalid && !JsonValidation::backup(settingsFilePath(), QStringLiteral(".invalid-"))) { report(QStringLiteral("Invalid settings could not be backed up; changes were not saved.")); return false; }
    QString error;
    if (!JsonValidation::write(settingsFilePath(), candidate, &error)) { report(QStringLiteral("Could not save settings: %1").arg(error)); emit preferencesChanged(); return false; }
    const QJsonObject previous = m_data;
    m_data = candidate; m_invalid = false;m_cachedPalette.clear();
    if (previous.value(QStringLiteral("generalScreenshotDirectory")) != candidate.value(QStringLiteral("generalScreenshotDirectory"))) emit generalScreenshotDirectoryChanged();
    if (previous.value(QStringLiteral("knownScreenshotDirectories")) != candidate.value(QStringLiteral("knownScreenshotDirectories"))) emit knownScreenshotDirectoriesChanged();
    if (previous.value(QStringLiteral("activeProfileId")) != candidate.value(QStringLiteral("activeProfileId"))) emit activeProfileIdChanged();
    emit preferencesChanged(); emit closeToTrayChanged(); emit startMinimizedChanged();
    return true;
}
bool SettingsManager::update(const QString &key, const QJsonValue &value) {
    if (m_data.value(key) == value) return true;
    QJsonObject candidate = m_data; candidate.insert(key, value); return commit(candidate);
}
bool SettingsManager::setGeneralScreenshotDirectory(const QString &directory) {
    const QString path = QDir::cleanPath(QDir::fromNativeSeparators(directory.trimmed()));
    if (!QDir::isAbsolutePath(path) || !QDir(path).exists()) { report(QStringLiteral("Choose an existing absolute screenshot directory.")); return false; }
    QJsonObject candidate=m_data; candidate.insert(QStringLiteral("generalScreenshotDirectory"), path);
    QStringList dirs=knownScreenshotDirectories(); if (!dirs.contains(path, Qt::CaseInsensitive)) dirs.append(path);
    candidate.insert(QStringLiteral("knownScreenshotDirectories"), QJsonArray::fromStringList(dirs)); return commit(candidate);
}
bool SettingsManager::setActiveProfileId(const QString &id) { return update(QStringLiteral("activeProfileId"), id); }
bool SettingsManager::addKnownScreenshotDirectory(const QString &directory) {
    const QString path=QDir::cleanPath(QDir::fromNativeSeparators(directory.trimmed()));
    if (!QDir::isAbsolutePath(path)) return false;
    QStringList dirs=knownScreenshotDirectories(); if (dirs.contains(path, Qt::CaseInsensitive)) return true;
    dirs.append(path); return update(QStringLiteral("knownScreenshotDirectories"), QJsonArray::fromStringList(dirs));
}
bool SettingsManager::removeKnownScreenshotDirectory(const QString &directory) {
    if (directory.compare(generalScreenshotDirectory(), Qt::CaseInsensitive)==0) return false;
    QStringList dirs=knownScreenshotDirectories();
    for (int i=dirs.size()-1; i>=0; --i) if (dirs[i].compare(directory,Qt::CaseInsensitive)==0) dirs.removeAt(i);
    return update(QStringLiteral("knownScreenshotDirectories"), QJsonArray::fromStringList(dirs));
}
bool SettingsManager::startupRegistration(bool enabled) {
#ifdef Q_OS_WIN
    QSettings registry(AppDataPaths::hasTestRoot() ? QDir(AppDataPaths::root()).filePath(QStringLiteral("test-startup.ini")) : QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"), AppDataPaths::hasTestRoot() ? QSettings::IniFormat : QSettings::NativeFormat);
    const QString name=QStringLiteral("ScreenshotHotkeyManager");
    if (enabled) registry.setValue(name, QStringLiteral("\"%1\"").arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath())));
    else registry.remove(name);
    registry.sync();
    if (registry.status()!=QSettings::NoError) { report(QStringLiteral("Could not change Windows startup registration.")); return false; }
    return true;
#else
    Q_UNUSED(enabled)
    report(QStringLiteral("Startup registration is supported on Windows only.")); return false;
#endif
}
bool SettingsManager::closeToTray() const { return m_data.value(QStringLiteral("closeToTray")).toBool(); }
bool SettingsManager::setCloseToTray(bool value) {
    return update(QStringLiteral("closeToTray"), value);
}
bool SettingsManager::startMinimized() const { return m_data.value(QStringLiteral("startMinimized")).toBool(); }
bool SettingsManager::setStartMinimized(bool value) {
    return update(QStringLiteral("startMinimized"), value);
}
bool SettingsManager::minimizeToTray() const { return m_data.value(QStringLiteral("minimizeToTray")).toBool(); }
bool SettingsManager::setMinimizeToTray(bool value) {
    return update(QStringLiteral("minimizeToTray"), value);
}
bool SettingsManager::startWithWindows() const { return m_data.value(QStringLiteral("startWithWindows")).toBool(); }
bool SettingsManager::setStartWithWindows(bool value) {
    if (value==startWithWindows()) return true;
    const bool previous=startWithWindows();
    if (!startupRegistration(value)) return false;
    if (!update(QStringLiteral("startWithWindows"), value)) { startupRegistration(previous); return false; }
    return true;
}
bool SettingsManager::notificationsEnabled() const { return m_data.value(QStringLiteral("notificationsEnabled")).toBool(); }
bool SettingsManager::setNotificationsEnabled(bool value) {
    return update(QStringLiteral("notificationsEnabled"), value);
}
QString SettingsManager::filenamePattern() const { return m_data.value(QStringLiteral("filenamePattern")).toString(); }
bool SettingsManager::setFilenamePattern(const QString &value) {
    if (!ScreenshotNaming::validPattern(value)) { report(QStringLiteral("Use a filename pattern with {timestamp}, {date}, or {time} and no Windows-reserved characters.")); emit preferencesChanged(); return false; }
    return update(QStringLiteral("filenamePattern"), value);
}
QString SettingsManager::appearance() const { return m_data.value(QStringLiteral("appearance")).toString(); }
bool SettingsManager::setAppearance(const QString &value) {
    if (!QStringList{QStringLiteral("dark"),QStringLiteral("light"),QStringLiteral("system")}.contains(value)) return false;
    return update(QStringLiteral("appearance"), value);
}
bool SettingsManager::setVisualStyle(const QString &value) {
    if (!QStringList{QStringLiteral("standard"), QStringLiteral("retro-arcade")}.contains(value)) return false;
    return update(QStringLiteral("visualStyle"), value);
}
QString SettingsManager::accentColor() const { return m_data.value(QStringLiteral("accentColor")).toString(); }
bool SettingsManager::setAccentColor(const QString &value) {
    if (!QColor::isValidColorName(value) || QColor(value).alpha()!=255) { report(QStringLiteral("Enter a valid opaque accent color.")); emit preferencesChanged(); return false; }
    return update(QStringLiteral("accentColor"), value);
}
bool SettingsManager::automaticClipboardSavingEnabled() const { return m_data.value(QStringLiteral("automaticClipboardSavingEnabled")).toBool(); }
bool SettingsManager::setAutomaticClipboardSavingEnabled(bool enabled) {
    return update(QStringLiteral("automaticClipboardSavingEnabled"), enabled);
}
QString SettingsManager::automaticClipboardSaveTagId() const { return m_data.value(QStringLiteral("automaticClipboardSaveTagId")).toString(); }
bool SettingsManager::setAutomaticClipboardSaveTagId(const QString &tagId) {
    return update(QStringLiteral("automaticClipboardSaveTagId"), tagId.trimmed());
}
int SettingsManager::clipboardHistoryShortcutKey() const { return m_data.value(QStringLiteral("clipboardHistoryShortcutKey")).toInt(); }
int SettingsManager::clipboardHistoryShortcutModifiers() const { return m_data.value(QStringLiteral("clipboardHistoryShortcutModifiers")).toInt(); }
bool SettingsManager::setClipboardHistoryShortcut(int key, int modifiers) {
    QJsonObject candidate = m_data;
    candidate.insert(QStringLiteral("clipboardHistoryShortcutKey"), key);
    candidate.insert(QStringLiteral("clipboardHistoryShortcutModifiers"), modifiers);
    return commit(candidate);
}
int SettingsManager::fileShelfShortcutKey() const { return m_data.value(QStringLiteral("fileShelfShortcutKey")).toInt(Qt::Key_F); }
int SettingsManager::fileShelfShortcutModifiers() const { return m_data.value(QStringLiteral("fileShelfShortcutModifiers")).toInt(int(Qt::ControlModifier | Qt::ShiftModifier)); }
bool SettingsManager::setFileShelfShortcut(int key, int modifiers) {
    QJsonObject candidate = m_data;
    candidate.insert(QStringLiteral("fileShelfShortcutKey"), key);
    candidate.insert(QStringLiteral("fileShelfShortcutModifiers"), modifiers);
    return commit(candidate);
}
int SettingsManager::fileShelfQuickAddShortcutKey() const { return m_data.value(QStringLiteral("fileShelfQuickAddShortcutKey")).toInt(Qt::Key_5); }
int SettingsManager::fileShelfQuickAddShortcutModifiers() const { return m_data.value(QStringLiteral("fileShelfQuickAddShortcutModifiers")).toInt(int(Qt::AltModifier | Qt::KeypadModifier)); }
bool SettingsManager::setFileShelfQuickAddShortcut(int key, int modifiers) {
    QJsonObject candidate = m_data;
    candidate.insert(QStringLiteral("fileShelfQuickAddShortcutKey"), key);
    candidate.insert(QStringLiteral("fileShelfQuickAddShortcutModifiers"), modifiers);
    return commit(candidate);
}
int SettingsManager::commandPaletteShortcutKey() const { return m_data.value(QStringLiteral("commandPaletteShortcutKey")).toInt(Qt::Key_P); }
int SettingsManager::commandPaletteShortcutModifiers() const { return m_data.value(QStringLiteral("commandPaletteShortcutModifiers")).toInt(int(Qt::ControlModifier | Qt::ShiftModifier)); }
bool SettingsManager::setCommandPaletteShortcut(int key, int modifiers) {
    QJsonObject candidate = m_data;
    candidate.insert(QStringLiteral("commandPaletteShortcutKey"), key);
    candidate.insert(QStringLiteral("commandPaletteShortcutModifiers"), modifiers);
    return commit(candidate);
}
int SettingsManager::clipboardHistoryRetentionDays() const { return m_data.value(QStringLiteral("clipboardHistoryRetentionDays")).toInt(30); }
bool SettingsManager::setClipboardHistoryRetentionDays(int days) { return days >= 1 && days <= 3650 && update(QStringLiteral("clipboardHistoryRetentionDays"), days); }
int SettingsManager::clipboardHistoryRetentionCount() const { return m_data.value(QStringLiteral("clipboardHistoryRetentionCount")).toInt(500); }
bool SettingsManager::setClipboardHistoryRetentionCount(int count) { return count >= 1 && count <= 100000 && update(QStringLiteral("clipboardHistoryRetentionCount"), count); }
int SettingsManager::clipboardHistoryStorageLimitMB() const { return m_data.value(QStringLiteral("clipboardHistoryStorageLimitMB")).toInt(512); }
bool SettingsManager::setClipboardHistoryStorageLimitMB(int megabytes) { return megabytes >= 16 && megabytes <= 65536 && update(QStringLiteral("clipboardHistoryStorageLimitMB"), megabytes); }

QVariantMap SettingsManager::palette() const {
    if(!m_cachedPalette.isEmpty())return m_cachedPalette;
    const QStringList keys={QStringLiteral("#111216"),QStringLiteral("#17181d"),QStringLiteral("#191a20"),QStringLiteral("#1b1c22"),QStringLiteral("#1e1f25"),QStringLiteral("#1e332b"),QStringLiteral("#1f352c"),QStringLiteral("#201a20"),QStringLiteral("#202127"),QStringLiteral("#22232a"),QStringLiteral("#23242a"),QStringLiteral("#24252c"),QStringLiteral("#282930"),QStringLiteral("#282932"),QStringLiteral("#29232d"),QStringLiteral("#292a31"),QStringLiteral("#292a32"),QStringLiteral("#2a2229"),QStringLiteral("#2a2630"),QStringLiteral("#2b2c34"),QStringLiteral("#2c2d34"),QStringLiteral("#2d2e37"),QStringLiteral("#2e252d"),QStringLiteral("#30212a"),QStringLiteral("#30222a"),QStringLiteral("#30313a"),QStringLiteral("#315542"),QStringLiteral("#34353e"),QStringLiteral("#37313a"),QStringLiteral("#382830"),QStringLiteral("#383942"),QStringLiteral("#39222b"),QStringLiteral("#3b2229"),QStringLiteral("#4a2c37"),QStringLiteral("#50303a"),QStringLiteral("#555660"),QStringLiteral("#56303d"),QStringLiteral("#60616c"),QStringLiteral("#6a3645"),QStringLiteral("#6b3642"),QStringLiteral("#71727d"),QStringLiteral("#747580"),QStringLiteral("#74d3aa"),QStringLiteral("#75404e"),QStringLiteral("#777984"),QStringLiteral("#7b7c87"),QStringLiteral("#858692"),QStringLiteral("#888994"),QStringLiteral("#898a95"),QStringLiteral("#8b8c97"),QStringLiteral("#8e4359"),QStringLiteral("#8f909b"),QStringLiteral("#90919c"),QStringLiteral("#9697a3"),QStringLiteral("#999aa4"),QStringLiteral("#9b9ca7"),QStringLiteral("#a0a1ac"),QStringLiteral("#a1a2ac"),QStringLiteral("#a5a5ae"),QStringLiteral("#a5a6b0"),QStringLiteral("#aa111216"),QStringLiteral("#aa191a20"),QStringLiteral("#aa9da3"),QStringLiteral("#aaa0a7"),QStringLiteral("#aaaab4"),QStringLiteral("#aeb0ba"),QStringLiteral("#b0b1bb"),QStringLiteral("#b5b6c0"),QStringLiteral("#bd8b99"),QStringLiteral("#c3c2ca"),QStringLiteral("#c8c7cf"),QStringLiteral("#d45270"),QStringLiteral("#d7d6dc"),QStringLiteral("#de8197"),QStringLiteral("#dedde3"),QStringLiteral("#e5e4e8"),QStringLiteral("#e7a0b1"),QStringLiteral("#e7b879"),QStringLiteral("#e88ba0"),QStringLiteral("#e96782"),QStringLiteral("#eaa0b2"),QStringLiteral("#eeedf0"),QStringLiteral("#efedf1"),QStringLiteral("#f08a9b"),QStringLiteral("#f0eef2"),QStringLiteral("#f1be79"),QStringLiteral("#f1c1c8"),QStringLiteral("#f1eff2"),QStringLiteral("#f2f1f4"),QStringLiteral("#f3c6d0"),QStringLiteral("#f3f1f4"),QStringLiteral("#f6f1f3"),QStringLiteral("#ff4f68"),QStringLiteral("#ff8d9c"),QStringLiteral("#ffb8c7"),QStringLiteral("#ffd166"),QStringLiteral("#fff0f3"),QStringLiteral("#fff2f4"),QStringLiteral("#fff3f6")};
    QVariantMap result;const bool dark=darkTheme();const QColor accent(accentColor());
    const auto blend=[](QColor a,QColor b,double ratio){return QColor::fromRgbF(a.redF()*ratio+b.redF()*(1-ratio),a.greenF()*ratio+b.greenF()*(1-ratio),a.blueF()*ratio+b.blueF()*(1-ratio));};
    for(const auto &key:keys) {
        QColor color(key);
        if(key==QStringLiteral("#e96782"))color=accent;
        else if(QStringList{QStringLiteral("#39222b"),QStringLiteral("#30222a"),QStringLiteral("#2a2229"),QStringLiteral("#4a2c37"),QStringLiteral("#2e252d")}.contains(key))color=blend(accent,dark?QColor(QStringLiteral("#111216")):QColor(Qt::white),dark?0.24:0.12);
        else if(QStringList{QStringLiteral("#e7a0b1"),QStringLiteral("#eaa0b2"),QStringLiteral("#e88ba0"),QStringLiteral("#de8197"),QStringLiteral("#d45270")}.contains(key))color=dark?accent.lighter(120):accent.darker(135);
        else if(!dark && color.alpha()==255) {
            if(color.saturationF()<0.24) {
                if(color.valueF()<0.17)color=QColor(QStringLiteral("#f2f3f7"));
                else if(color.valueF()<0.25)color=QColor(QStringLiteral("#ffffff"));
                else if(color.valueF()<0.42)color=QColor(QStringLiteral("#c8ccd6"));
                else if(color.valueF()<0.73)color=QColor(QStringLiteral("#626777"));
                else color=QColor(QStringLiteral("#20232b"));
            } else if(color.valueF()<0.45)color=blend(color,QColor(Qt::white),0.15);
            else color=color.darker(175);
        }
        result.insert(key,color);
    }

    if (retroArcade()) {
        const QColor window(QStringLiteral("#030A19"));
        const QColor panel(QStringLiteral("#071329"));
        const QColor raised(QStringLiteral("#0B1C36"));
        const QColor inset(QStringLiteral("#020916"));
        const QColor hover(QStringLiteral("#10244F"));
        const QColor border(QStringLiteral("#294A78"));
        const QColor borderStrong(QStringLiteral("#5278B2"));
        const QColor muted(QStringLiteral("#9EBCEB"));
        const QColor disabled(QStringLiteral("#6F86A9"));
        const QColor primary(QStringLiteral("#EAF2FF"));
        const QColor cyan(QStringLiteral("#00D9FF"));
        const QColor cyanLight(QStringLiteral("#70E9FF"));
        const QColor blue(QStringLiteral("#0752E6"));
        const QColor green(QStringLiteral("#28EAA0"));
        const QColor warning(QStringLiteral("#FFD44A"));
        const QColor error(QStringLiteral("#FF537D"));
        const QColor errorSurface(QStringLiteral("#301725"));
        const QColor successSurface(QStringLiteral("#082C29"));
        QHash<QString, QColor> overrides;
        const auto assign = [&overrides](const QStringList &names, const QColor &color) {
            for (const QString &name : names) overrides.insert(name, color);
        };
        assign({QStringLiteral("#111216")}, window);
        assign({QStringLiteral("#17181d"),QStringLiteral("#191a20"),QStringLiteral("#1b1c22"),QStringLiteral("#1e1f25"),QStringLiteral("#23242a"),QStringLiteral("#24252c"),QStringLiteral("#282930"),QStringLiteral("#282932")}, panel);
        assign({QStringLiteral("#202127"),QStringLiteral("#2a2630"),QStringLiteral("#2b2c34"),QStringLiteral("#2c2d34")}, raised);
        assign({QStringLiteral("#22232a"),QStringLiteral("#292a31"),QStringLiteral("#292a32")}, hover);
        assign({QStringLiteral("#2d2e37"),QStringLiteral("#34353e"),QStringLiteral("#383942")}, border);
        assign({QStringLiteral("#30313a"),QStringLiteral("#37313a"),QStringLiteral("#29232d")}, borderStrong);
        assign({QStringLiteral("#39222b"),QStringLiteral("#30222a"),QStringLiteral("#30212a"),QStringLiteral("#2a2229"),QStringLiteral("#382830")}, blue);
        assign({QStringLiteral("#1e332b"),QStringLiteral("#1f352c"),QStringLiteral("#315542")}, successSurface);
        assign({QStringLiteral("#201a20"),QStringLiteral("#3b2229"),QStringLiteral("#4a2c37"),QStringLiteral("#50303a"),QStringLiteral("#56303d")}, errorSurface);
        assign({QStringLiteral("#6a3645"),QStringLiteral("#6b3642"),QStringLiteral("#75404e")}, QColor(QStringLiteral("#77334B")));
        assign({QStringLiteral("#e96782"),QStringLiteral("#e7a0b1"),QStringLiteral("#eaa0b2"),QStringLiteral("#e88ba0"),QStringLiteral("#de8197"),QStringLiteral("#d45270"),QStringLiteral("#bd8b99"),QStringLiteral("#8e4359")}, cyan);
        assign({QStringLiteral("#f1c1c8"),QStringLiteral("#f3c6d0"),QStringLiteral("#f6f1f3"),QStringLiteral("#ffb8c7")}, cyanLight);
        assign({QStringLiteral("#74d3aa")}, green);
        assign({QStringLiteral("#e7b879"),QStringLiteral("#f1be79"),QStringLiteral("#ffd166")}, warning);
        assign({QStringLiteral("#ff8d9c"),QStringLiteral("#f08a9b"),QStringLiteral("#ff4f68")}, error);
        assign({QStringLiteral("#60616c")}, disabled);
        assign({QStringLiteral("#71727d"),QStringLiteral("#747580"),QStringLiteral("#777984"),QStringLiteral("#7b7c87"),QStringLiteral("#858692"),QStringLiteral("#888994"),QStringLiteral("#898a95"),QStringLiteral("#8b8c97"),QStringLiteral("#8f909b"),QStringLiteral("#90919c"),QStringLiteral("#9697a3"),QStringLiteral("#999aa4"),QStringLiteral("#9b9ca7"),QStringLiteral("#a0a1ac"),QStringLiteral("#a1a2ac"),QStringLiteral("#a5a5ae"),QStringLiteral("#a5a6b0"),QStringLiteral("#aa9da3"),QStringLiteral("#aaa0a7"),QStringLiteral("#aaaab4"),QStringLiteral("#aeb0ba"),QStringLiteral("#b0b1bb"),QStringLiteral("#b5b6c0")}, muted);
        assign({QStringLiteral("#555660")}, disabled);
        assign({QStringLiteral("#c3c2ca"),QStringLiteral("#c8c7cf"),QStringLiteral("#d7d6dc"),QStringLiteral("#dedde3"),QStringLiteral("#e5e4e8"),QStringLiteral("#eeedf0"),QStringLiteral("#efedf1"),QStringLiteral("#f0eef2"),QStringLiteral("#f1eff2"),QStringLiteral("#f2f1f4"),QStringLiteral("#f3f1f4"),QStringLiteral("#fff0f3"),QStringLiteral("#fff2f4"),QStringLiteral("#fff3f6")}, primary);
        for (auto it = result.begin(); it != result.end(); ++it) {
            const QString key = it.key().toLower();
            QColor color = overrides.value(key, it.value().value<QColor>());
            if (!overrides.contains(key)) {
                const QColor source(key);
                const qreal luminance = 0.2126 * source.redF() + 0.7152 * source.greenF() + 0.0722 * source.blueF();
                if (luminance < 0.055) color = inset;
                else if (luminance < 0.12) color = panel;
                else if (luminance < 0.22) color = raised;
                else if (luminance < 0.38) color = border;
                else if (luminance < 0.58) color = disabled;
                else if (luminance < 0.78) color = muted;
                else color = primary;
            }
            const QColor source(key);
            if (source.alpha() < 255) color.setAlpha(source.alpha());
            it.value() = color;
        }
        result.insert(QStringLiteral("window"), window);
        result.insert(QStringLiteral("sidebar"), panel);
        result.insert(QStringLiteral("surface"), panel);
        result.insert(QStringLiteral("raisedSurface"), raised);
        result.insert(QStringLiteral("insetSurface"), inset);
        result.insert(QStringLiteral("hover"), hover);
        result.insert(QStringLiteral("selection"), blue);
        result.insert(QStringLiteral("text"), primary);
        result.insert(QStringLiteral("secondaryText"), muted);
        result.insert(QStringLiteral("disabledText"), disabled);
        result.insert(QStringLiteral("border"), border);
        result.insert(QStringLiteral("borderStrong"), borderStrong);
        result.insert(QStringLiteral("sidebarBorder"), border);
        result.insert(QStringLiteral("separator"), border);
        result.insert(QStringLiteral("highlightBorder"), QColor(QStringLiteral("#7DADFF")));
        result.insert(QStringLiteral("shadowBorder"), QColor(QStringLiteral("#10244F")));
        result.insert(QStringLiteral("focus"), cyan);
        result.insert(QStringLiteral("accent"), cyan);
        result.insert(QStringLiteral("accentSoft"), blue);
        result.insert(QStringLiteral("success"), green);
        result.insert(QStringLiteral("warning"), warning);
        result.insert(QStringLiteral("error"), error);
        result.insert(QStringLiteral("destructive"), error);
        result.insert(QStringLiteral("controlHeight"), 36);
        result.insert(QStringLiteral("panelRadius"), 2);
        result.insert(QStringLiteral("borderWidth"), 1.0);
        result.insert(QStringLiteral("frameWidth"), 2.0);
        result.insert(QStringLiteral("spacingUnit"), 4);
        result.insert(QStringLiteral("fontFamily"), QStringLiteral("Cascadia Code"));
    } else {
        result.insert(QStringLiteral("window"), result.value(QStringLiteral("#111216")));
        result.insert(QStringLiteral("sidebar"), result.value(QStringLiteral("#17181d")));
        result.insert(QStringLiteral("surface"), result.value(QStringLiteral("#191a20")));
        result.insert(QStringLiteral("raisedSurface"), result.value(QStringLiteral("#202127")));
        result.insert(QStringLiteral("insetSurface"), result.value(QStringLiteral("#17181d")));
        result.insert(QStringLiteral("hover"), result.value(QStringLiteral("#292a32")));
        result.insert(QStringLiteral("selection"), result.value(QStringLiteral("#39222b")));
        result.insert(QStringLiteral("text"), result.value(QStringLiteral("#f2f1f4")));
        result.insert(QStringLiteral("secondaryText"), result.value(QStringLiteral("#9697a3")));
        result.insert(QStringLiteral("disabledText"), result.value(QStringLiteral("#71727d")));
        result.insert(QStringLiteral("border"), result.value(QStringLiteral("#2d2e37")));
        result.insert(QStringLiteral("borderStrong"), result.value(QStringLiteral("#383942")));
        result.insert(QStringLiteral("sidebarBorder"), result.value(QStringLiteral("#24252c")));
        result.insert(QStringLiteral("separator"), result.value(QStringLiteral("#292a31")));
        result.insert(QStringLiteral("highlightBorder"), result.value(QStringLiteral("#555660")));
        result.insert(QStringLiteral("shadowBorder"), result.value(QStringLiteral("#111216")));
        result.insert(QStringLiteral("focus"), result.value(QStringLiteral("#e96782")));
        result.insert(QStringLiteral("accent"), result.value(QStringLiteral("#e96782")));
        result.insert(QStringLiteral("accentSoft"), result.value(QStringLiteral("#39222b")));
        result.insert(QStringLiteral("success"), result.value(QStringLiteral("#74d3aa")));
        result.insert(QStringLiteral("warning"), result.value(QStringLiteral("#e7b879")));
        result.insert(QStringLiteral("error"), result.value(QStringLiteral("#ff8d9c")));
        result.insert(QStringLiteral("destructive"), result.value(QStringLiteral("#ff8d9c")));
        result.insert(QStringLiteral("controlHeight"), 40);
        result.insert(QStringLiteral("panelRadius"), 12);
        result.insert(QStringLiteral("borderWidth"), 1.0);
        result.insert(QStringLiteral("frameWidth"), 0.0);
        result.insert(QStringLiteral("spacingUnit"), 4);
        result.insert(QStringLiteral("fontFamily"), QString());
    }
    m_cachedPalette=result;return result;
}

bool SettingsManager::clipboardHistoryEnabled() const { return m_data.value(QStringLiteral("clipboardHistoryEnabled")).toBool(); }
bool SettingsManager::setClipboardHistoryEnabled(bool value) { return update(QStringLiteral("clipboardHistoryEnabled"), value); }

bool SettingsManager::clipboardHistorySessionOnly() const { return m_data.value(QStringLiteral("clipboardHistorySessionOnly")).toBool(); }
bool SettingsManager::setClipboardHistorySessionOnly(bool value) { return update(QStringLiteral("clipboardHistorySessionOnly"), value); }

QStringList SettingsManager::clipboardHistoryExcludedApplications() const {
    QStringList result;
    for (const auto &value : m_data.value(QStringLiteral("clipboardHistoryExcludedApplications")).toArray())
        result.append(QDir::fromNativeSeparators(value.toString().trimmed()).toCaseFolded());
    return result;
}
bool SettingsManager::setClipboardHistoryExcludedApplications(const QStringList &value) {
    QStringList normalized;
    qsizetype total = 0;
    for (const QString &entry : value) {
        const QString app = QDir::fromNativeSeparators(entry.trimmed()).toCaseFolded();
        total += app.size();
        if (app.size() > 32768 || total > 512 * 1024) return false;
        if (!app.isEmpty() && !normalized.contains(app)) normalized.append(app);
    }
    if (normalized.size() > 256) return false;
    return update(QStringLiteral("clipboardHistoryExcludedApplications"), QJsonArray::fromStringList(normalized));
}

int SettingsManager::thumbnailCacheLimitMB() const { return m_data.value(QStringLiteral("thumbnailCacheLimitMB")).toInt(); }
bool SettingsManager::setThumbnailCacheLimitMB(int value) { if (value < 16 || value > 4096) return false; return update(QStringLiteral("thumbnailCacheLimitMB"), value); }
