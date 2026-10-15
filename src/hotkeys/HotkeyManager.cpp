#include "HotkeyManager.h"

#include "HotkeyUtils.h"
#include "../settings/SettingsManager.h"

#include <QCoreApplication>
#include <QKeySequence>
#include <QTimer>
#include <QWindow>
#include <QDebug>
#include <utility>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

HotkeyManager::HotkeyManager(ProfileManager *profiles, QObject *parent)
    : HotkeyManager(profiles, nullptr, parent)
{
}

HotkeyManager::HotkeyManager(ProfileManager *profiles, SettingsManager *settings, QObject *parent)
    : QAbstractListModel(parent), m_profiles(profiles), m_settings(settings)
{
    connect(m_profiles, &ProfileManager::activeProfileChanged, this, &HotkeyManager::refreshRegistrations);
    connect(m_profiles, &ProfileManager::profileDataChanged, this, &HotkeyManager::refreshRegistrations);
    if (m_settings)
        connect(m_settings, &SettingsManager::preferencesChanged, this, &HotkeyManager::refreshRegistrations);
    refreshRegistrations();
}

HotkeyManager::~HotkeyManager()
{
    unregisterAll();
    if (m_filterInstalled && QCoreApplication::instance())
        QCoreApplication::instance()->removeNativeEventFilter(this);
}

int HotkeyManager::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_assignments.size();
}

QVariant HotkeyManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_assignments.size())
        return {};
    const HotkeyAssignment &assignment = m_assignments.at(index.row());
    const QString scope=m_globalScope?QStringLiteral("global"):m_profiles->activeProfileId();
    const Status status = m_statusById.value(QStringLiteral("binding:")+scope+QLatin1Char(':')+assignment.id, {false, QStringLiteral("Not registered")});
    switch (role) {
    case IdRole: return assignment.id;
    case KeyRole: return assignment.key;
    case ModifiersRole: return assignment.modifiers;
    case ShortcutRole: return HotkeyUtils::formatShortcut(assignment.key, assignment.modifiers);
    case ActionTypeRole: return assignment.actionType;
    case TargetProfileIdRole: return assignment.targetProfileId;
    case SequenceIdRole: return assignment.sequenceId;
    case EnabledRole: return assignment.enabled;
    case RegisteredRole: return status.registered;
    case StatusRole: return status.text;
    default: return {};
    }
}

QHash<int, QByteArray> HotkeyManager::roleNames() const
{
    return {{IdRole, "assignmentId"}, {KeyRole, "key"}, {ModifiersRole, "modifiers"},
            {ShortcutRole, "shortcut"}, {ActionTypeRole, "actionType"},
            {TargetProfileIdRole, "targetProfileId"}, {SequenceIdRole, "sequenceId"},
            {EnabledRole, "assignmentEnabled"},
            {RegisteredRole, "registered"}, {StatusRole, "status"}};
}

void HotkeyManager::setOwnerWindow(QWindow *window)
{
    if (m_window == window && m_hasWindow)
        return;
    unregisterAll();
    m_window = window;
    m_hasWindow = !m_window.isNull();
    if (!m_filterInstalled && QCoreApplication::instance()) {
        QCoreApplication::instance()->installNativeEventFilter(this);
        m_filterInstalled = true;
    }
    refreshRegistrations();
}

void HotkeyManager::setRecording(bool recording)
{
    if(m_recording==recording)return;
    m_recording=recording;
    if(recording)refreshRegistrations();
    else QTimer::singleShot(0,this,[this]{if(!m_recording)refreshRegistrations();});
}

QString HotkeyManager::activationStatus(const QString &profileId) const
{
    const Status status = m_statusById.value(QStringLiteral("activation:") + profileId,
                                               {false, QStringLiteral("Not assigned")});
    return status.text;
}

QString HotkeyManager::clipboardHistoryStatus() const
{
    return m_statusById.value(QStringLiteral("clipboard-history"),
                              {false, QStringLiteral("Not assigned")}).text;
}

QString HotkeyManager::fileShelfStatus() const
{
    return m_statusById.value(QStringLiteral("file-shelf"),
                              {false, QStringLiteral("Not assigned")}).text;
}

bool HotkeyManager::isRegistered(const QString &id) const
{
    return nativeIdFor(id)>0;
}

bool HotkeyManager::isSupportedKey(int key, int modifiers)
{
    const int supportedModifiers = int(Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier);
    if (key == 0 || (modifiers & ~supportedModifiers) != 0)
        return false;
    const bool hasModifier = (modifiers & supportedModifiers) != 0;
    if (!hasModifier)
        return false;
#ifdef Q_OS_WIN
    quint32 ignored = 0;
    return toWindowsKey(key, &ignored);
#else
    return true;
#endif
}

QString HotkeyManager::formatShortcut(int key, int modifiers)
{
    return HotkeyUtils::formatShortcut(key, modifiers);
}

bool HotkeyManager::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
#ifdef Q_OS_WIN
    if (eventType != QByteArrayLiteral("windows_generic_MSG") &&
        eventType != QByteArrayLiteral("windows_dispatcher_MSG"))
        return false;
    auto *nativeMessage = static_cast<MSG *>(message);
    if (!nativeMessage || nativeMessage->message != WM_HOTKEY)
        return false;
    const auto it = m_bindingByNativeId.constFind(static_cast<int>(nativeMessage->wParam));
    if (it == m_bindingByNativeId.cend())
        return false;
    const Binding binding = it.value();
    if (binding.actionType == QStringLiteral("clipboardHistory")) {
        emit hotkeyTriggered(binding.description);
        emit clipboardHistoryTriggered();
    } else if (binding.actionType == QStringLiteral("fileShelf")) {
        emit hotkeyTriggered(binding.description);
        emit fileShelfTriggered();
    } else if (binding.isActivation) {
        emit hotkeyTriggered(QStringLiteral("Activate %1").arg(binding.description));
        emit profileActivationRequested(binding.profileId);
    } else {
        emit hotkeyTriggered(binding.description);
        emit actionTriggered(binding.actionType, binding.targetProfileId, binding.sequenceId);
    }
    if (result)
        *result = 0;
    return true;
#else
    Q_UNUSED(eventType)
    Q_UNUSED(message)
    Q_UNUSED(result)
    return false;
#endif
}

void HotkeyManager::refreshRegistrations()
{
    QVector<Binding> desired;
    for(const auto &profile:m_profiles->profiles())if(profile.activationKey!=0)
        desired.append(Binding{QStringLiteral("activation:")+profile.id,profile.id,profile.name,profile.activationKey,profile.activationModifiers,true,{},{},{}});
    QVector<Profile> scopes{m_profiles->globalProfile()};
    if(m_profiles->activeProfile())scopes.append(*m_profiles->activeProfile());
    QHash<QString,Status> statuses;
    for(const auto &profile:scopes)for(const auto &assignment:profile.hotkeys) {
        const QString id=QStringLiteral("binding:")+profile.id+QLatin1Char(':')+assignment.id;
        if(!assignment.enabled || assignment.key==0){statuses.insert(id,{false,assignment.enabled?QStringLiteral("Not assigned"):QStringLiteral("Disabled")});continue;}
        desired.append(Binding{id,profile.id,HotkeyUtils::formatShortcut(assignment.key,assignment.modifiers),assignment.key,assignment.modifiers,false,assignment.actionType,assignment.targetProfileId,assignment.sequenceId});
    }
    if (m_settings) {
        const int key = m_settings->clipboardHistoryShortcutKey();
        const int modifiers = m_settings->clipboardHistoryShortcutModifiers();
        const QString id = QStringLiteral("clipboard-history");
        if (key == 0)
            statuses.insert(id, {false, QStringLiteral("Not assigned")});
        else
            desired.append(Binding{id, QStringLiteral("global"),
                                   QStringLiteral("Open clipboard history (%1)").arg(HotkeyUtils::formatShortcut(key, modifiers)),
                                   key, modifiers, false, QStringLiteral("clipboardHistory"), {}, {}});
        const int shelfKey = m_settings->fileShelfShortcutKey();
        const int shelfModifiers = m_settings->fileShelfShortcutModifiers();
        const QString shelfId = QStringLiteral("file-shelf");
        if (shelfKey == 0)
            statuses.insert(shelfId, {false, QStringLiteral("Not assigned")});
        else
            desired.append(Binding{shelfId, QStringLiteral("global"),
                                   QStringLiteral("Open file shelf (%1)").arg(HotkeyUtils::formatShortcut(shelfKey, shelfModifiers)),
                                   shelfKey, shelfModifiers, false, QStringLiteral("fileShelf"), {}, {}});
    }
    const bool canRegister=m_hasWindow && !m_window.isNull() && !m_recording;
    // Retain unchanged native registrations, especially application-wide bindings.
    for(auto it=m_bindingByNativeId.begin();it!=m_bindingByNativeId.end();) {
        bool keep=false;
        if(canRegister)for(const auto &binding:desired)if(binding.id==it->id && binding.key==it->key && binding.modifiers==it->modifiers){*it=binding;keep=true;break;}
        if(keep){++it;continue;}
#ifdef Q_OS_WIN
        UnregisterHotKey(nullptr,it.key());
#endif
        it=m_bindingByNativeId.erase(it);
    }
    QStringList failures;
    for(const auto &binding:desired) {
        if(!canRegister){statuses.insert(binding.id,{false,m_recording?QStringLiteral("Paused while recording"):QStringLiteral("Waiting for application window")});continue;}
        int existing=0;for(auto it=m_bindingByNativeId.begin();it!=m_bindingByNativeId.end();++it)if(it->id==binding.id){existing=it.key();break;}
        if(existing){statuses.insert(binding.id,{true,(binding.isActivation || binding.actionType==QStringLiteral("clipboardHistory") || binding.actionType==QStringLiteral("fileShelf"))?QStringLiteral("Registered globally"):QStringLiteral("Registered")});continue;}
        while(m_bindingByNativeId.contains(m_nextNativeId))m_nextNativeId=m_nextNativeId%0xbfff+1;
        const int nativeId=m_nextNativeId;m_nextNativeId=m_nextNativeId%0xbfff+1;
        if(registerBinding(binding,nativeId)){m_bindingByNativeId.insert(nativeId,binding);statuses.insert(binding.id,{true,(binding.isActivation || binding.actionType==QStringLiteral("clipboardHistory") || binding.actionType==QStringLiteral("fileShelf"))?QStringLiteral("Registered globally"):QStringLiteral("Registered")});}
        else {statuses.insert(binding.id,{false,QStringLiteral("Unavailable or conflicts with another hotkey")});if(m_statusById.value(binding.id).text!=statuses.value(binding.id).text)failures.append(binding.description);}
    }
    beginResetModel();
    const Profile *view=m_globalScope?&m_profiles->globalProfile():m_profiles->activeProfile();
    m_assignments=view?view->hotkeys:QVector<HotkeyAssignment>();m_statusById=statuses;
    endResetModel();emit assignmentsChanged();emit registrationStatusesChanged();
    if(!failures.isEmpty())emit registrationFailed(QStringLiteral("Windows could not register: %1").arg(failures.join(QStringLiteral(", "))));
}
void HotkeyManager::unregisterAll() {
#ifdef Q_OS_WIN
    for(auto it=m_bindingByNativeId.begin();it!=m_bindingByNativeId.end();++it)UnregisterHotKey(nullptr,it.key());
#endif
    m_bindingByNativeId.clear();
}
void HotkeyManager::setGlobalScope(bool global) {if(m_globalScope==global)return;m_globalScope=global;refreshRegistrations();}
int HotkeyManager::nativeIdFor(const QString &id) const {
    for(auto it=m_bindingByNativeId.begin();it!=m_bindingByNativeId.end();++it)
        if(it->id==id || it->id.endsWith(QLatin1Char(':')+id))return it.key();
    return 0;
}
int HotkeyManager::failureCount() const {
    int count=0;for(const auto &status:m_statusById)if(status.text==QStringLiteral("Unavailable or conflicts with another hotkey"))++count;
    return count;
}

bool HotkeyManager::registerBinding(const Binding &binding, int nativeId)
{
#ifdef Q_OS_WIN
    if (m_window.isNull())
        return false;
    UINT virtualKey = 0;
    if (!toWindowsKey(binding.key, &virtualKey))
        return false;
    const UINT modifiers = toWindowsModifiers(binding.modifiers) | MOD_NOREPEAT;
    if (!RegisterHotKey(nullptr, nativeId, modifiers, virtualKey)) {
        qWarning().noquote() << "RegisterHotKey failed for" << HotkeyUtils::formatShortcut(binding.key, binding.modifiers)
                             << "with Win32 error" << GetLastError();
        return false;
    }
    return true;
#else
    Q_UNUSED(binding)
    Q_UNUSED(nativeId)
    return false;
#endif
}

bool HotkeyManager::toWindowsKey(int qtKey, UINT *virtualKey)
{
#ifdef Q_OS_WIN
    if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z) {
        *virtualKey = static_cast<quint32>(qtKey);
        return true;
    }
    if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9) {
        *virtualKey = static_cast<quint32>(qtKey);
        return true;
    }
    if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F24) {
        *virtualKey = VK_F1 + static_cast<quint32>(qtKey - Qt::Key_F1);
        return true;
    }
    switch (qtKey) {
    case Qt::Key_Space: *virtualKey = VK_SPACE; return true;
    case Qt::Key_Tab: *virtualKey = VK_TAB; return true;
    case Qt::Key_Return: *virtualKey = VK_RETURN; return true;
    case Qt::Key_Enter: *virtualKey = VK_RETURN; return true;
    case Qt::Key_Escape: *virtualKey = VK_ESCAPE; return true;
    case Qt::Key_Backspace: *virtualKey = VK_BACK; return true;
    case Qt::Key_Delete: *virtualKey = VK_DELETE; return true;
    case Qt::Key_Insert: *virtualKey = VK_INSERT; return true;
    case Qt::Key_Home: *virtualKey = VK_HOME; return true;
    case Qt::Key_End: *virtualKey = VK_END; return true;
    case Qt::Key_PageUp: *virtualKey = VK_PRIOR; return true;
    case Qt::Key_PageDown: *virtualKey = VK_NEXT; return true;
    case Qt::Key_Left: *virtualKey = VK_LEFT; return true;
    case Qt::Key_Right: *virtualKey = VK_RIGHT; return true;
    case Qt::Key_Up: *virtualKey = VK_UP; return true;
    case Qt::Key_Down: *virtualKey = VK_DOWN; return true;
    case Qt::Key_Comma: *virtualKey = VK_OEM_COMMA; return true;
    case Qt::Key_Period: *virtualKey = VK_OEM_PERIOD; return true;
    case Qt::Key_Minus: *virtualKey = VK_OEM_MINUS; return true;
    case Qt::Key_Plus: *virtualKey = VK_OEM_PLUS; return true;
    default: return false;
    }
#else
    Q_UNUSED(qtKey)
    Q_UNUSED(virtualKey)
    return false;
#endif
}

quint32 HotkeyManager::toWindowsModifiers(int qtModifiers)
{
#ifdef Q_OS_WIN
    quint32 modifiers = 0;
    if (qtModifiers & Qt::ControlModifier) modifiers |= MOD_CONTROL;
    if (qtModifiers & Qt::AltModifier) modifiers |= MOD_ALT;
    if (qtModifiers & Qt::ShiftModifier) modifiers |= MOD_SHIFT;
    if (qtModifiers & Qt::MetaModifier) modifiers |= MOD_WIN;
    return modifiers;
#else
    Q_UNUSED(qtModifiers)
    return 0;
#endif
}
