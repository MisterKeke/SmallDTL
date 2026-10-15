#include "WindowArrangementManager.h"

#include "../app/AppDataPaths.h"
#include "../app/JsonValidation.h"
#include "../profiles/ProfileManager.h"
#include "WindowArrangementMatching.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <cmath>
#include <utility>
#include <vector>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
QString makeId(){return QUuid::createUuid().toString(QUuid::WithoutBraces);}
QString normalizedExecutable(const QString &path){return WindowArrangementMatching::identity(path);}
#ifdef Q_OS_WIN
struct DisplayName { QString name; QString identity; };
QHash<QString,DisplayName> displayNames()
{
    for (int attempt=0;attempt<3;++attempt) {
        UINT32 pathCount=0,modeCount=0;
        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&pathCount,&modeCount)!=ERROR_SUCCESS)
            break;
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
        const LONG status=QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&pathCount,paths.data(),
                                             &modeCount,modes.data(),nullptr);
        if (status==ERROR_INSUFFICIENT_BUFFER) continue;
        if (status!=ERROR_SUCCESS) break;
        QHash<QString,DisplayName> names;
        for (UINT32 i=0;i<pathCount;++i) {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
            source.header={DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,sizeof(source),
                           paths[i].sourceInfo.adapterId,paths[i].sourceInfo.id};
            DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
            target.header={DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME,sizeof(target),
                           paths[i].targetInfo.adapterId,paths[i].targetInfo.id};
            if (DisplayConfigGetDeviceInfo(&source.header)!=ERROR_SUCCESS ||
                DisplayConfigGetDeviceInfo(&target.header)!=ERROR_SUCCESS) continue;
            const QString device=QString::fromWCharArray(source.viewGdiDeviceName).toCaseFolded();
            if (!names.contains(device))
                names.insert(device,{QString::fromWCharArray(target.monitorFriendlyDeviceName).trimmed(),
                                     QString::fromWCharArray(target.monitorDevicePath)});
        }
        return names;
    }
    return {};
}
#endif
QVariantMap slotToMap(const WindowLayoutSlot &slot,int index){
    return {{QStringLiteral("index"),index},{QStringLiteral("executable"),slot.executable},
            {QStringLiteral("titleRule"),slot.titleRule},{QStringLiteral("monitor"),slot.monitor},
            {QStringLiteral("maximized"),slot.maximized},
            {QStringLiteral("x"),slot.rect.x},{QStringLiteral("y"),slot.rect.y},
            {QStringLiteral("width"),slot.rect.width},{QStringLiteral("height"),slot.rect.height}};
}
}

WindowArrangementManager::WindowArrangementManager(ProfileManager *profiles,QObject *parent)
    : QAbstractListModel(parent),m_profiles(profiles)
{
    loadLayouts();
    connect(&m_windowWatcher,&QFutureWatcher<QVector<ArrangementWindow>>::finished,this,[this]{
        finishWindowJob(m_windowWatcher.result());
    });
    connect(m_profiles,&ProfileManager::activeProfileChanged,this,[this]{
        setSelectedLayoutId({});m_previewLayoutId.clear();m_resolutions.clear();m_preview.clear();
        emit previewChanged();emit layoutsChanged();
    });
    connect(m_profiles,&ProfileManager::profilesChanged,this,[this]{
        bool removed=false;for(int i=m_layouts.size()-1;i>=0;--i)if(!m_profiles->profileById(m_layouts.at(i).profileId)){m_layouts.removeAt(i);removed=true;}
        if(removed){saveLayouts();if(layoutIndex(m_selectedLayoutId)<0)setSelectedLayoutId({});if(layoutIndex(m_previewLayoutId)<0){m_previewLayoutId.clear();m_resolutions.clear();m_preview.clear();emit previewChanged();}}
        emit layoutsChanged();
    });
    refreshWindows();
}

int WindowArrangementManager::rowCount(const QModelIndex &parent)const{return parent.isValid()?0:m_windows.size();}
QVariant WindowArrangementManager::data(const QModelIndex &index,int role)const{
    if(!index.isValid()||index.row()<0||index.row()>=m_windows.size())return{};
    const ArrangementWindow &w=m_windows.at(index.row());
    switch(role){case WindowIdRole:return QString::number(w.handle);case TitleRole:return w.title;
    case ExecutableRole:return w.executable;case MonitorRole:return w.monitor;case SelectedRole:return w.selected;
    case MaximizeRole:return w.maximized;case MonitorLabelRole:return monitorLabel(w.monitor);default:return{};}
}
QHash<int,QByteArray> WindowArrangementManager::roleNames()const{return{{WindowIdRole,"windowId"},{TitleRole,"title"},{ExecutableRole,"executable"},{MonitorRole,"monitor"},{SelectedRole,"selected"},{MaximizeRole,"maximized"},{MonitorLabelRole,"monitorLabel"}};}
QVariantList WindowArrangementManager::layouts()const{
    QVariantList result;const QString active=m_profiles->activeProfileId();
    for(const WindowLayout &layout:m_layouts)if(layout.profileId==active)
        result.append(QVariantMap{{QStringLiteral("id"),layout.id},{QStringLiteral("layoutId"),layout.id},{QStringLiteral("name"),layout.name},{QStringLiteral("count"),layout.rules.size()}});
    return result;
}
void WindowArrangementManager::setSelectedLayoutId(const QString &id){if(m_selectedLayoutId==id)return;m_selectedLayoutId=id;emit selectedLayoutIdChanged();}
void WindowArrangementManager::setBusy(bool value){if(m_busy==value)return;m_busy=value;emit busyChanged();}
void WindowArrangementManager::setStatus(const QString &message,bool isError){if(m_status==message)return;m_status=message;emit statusChanged();if(isError)emit errorOccurred(message);}
int WindowArrangementManager::windowIndex(quintptr handle)const{for(int i=0;i<m_windows.size();++i)if(m_windows.at(i).handle==handle)return i;return-1;}
int WindowArrangementManager::layoutIndex(const QString &id)const{for(int i=0;i<m_layouts.size();++i)if(m_layouts.at(i).id==id)return i;return-1;}
QVector<int> WindowArrangementManager::selectedRows()const{QVector<int> rows;for(int i=0;i<m_windows.size();++i)if(m_windows.at(i).selected)rows.append(i);return rows;}
void WindowArrangementManager::updateMonitorList(){
    m_monitorRecords=enumerateMonitors();m_monitors.clear();
    for(const Monitor &monitor:m_monitorRecords)m_monitors.append(QVariantMap{{QStringLiteral("device"),monitor.device},{QStringLiteral("name"),monitorLabel(monitor.device)},{QStringLiteral("friendlyName"),monitor.name},{QStringLiteral("nickname"),monitorNickname(monitor.device)},{QStringLiteral("primary"),monitor.primary},{QStringLiteral("x"),monitor.workArea.x()},{QStringLiteral("y"),monitor.workArea.y()},{QStringLiteral("width"),monitor.workArea.width()},{QStringLiteral("height"),monitor.workArea.height()}});
    emit monitorsChanged();
    if(!m_windows.isEmpty())emit dataChanged(index(0),index(m_windows.size()-1),{MonitorLabelRole});
}

QString WindowArrangementManager::monitorNickname(const QString &device)const{
    for(const Monitor &monitor:m_monitorRecords)if(monitor.device==device)
        return m_monitorNicknames.value(monitor.identity);
    return {};
}
QString WindowArrangementManager::monitorLabel(const QString &device)const{
    for(int i=0;i<m_monitorRecords.size();++i){
        const Monitor &monitor=m_monitorRecords.at(i);if(monitor.device!=device)continue;
        QString name=m_monitorNicknames.value(monitor.identity,monitor.name);
        if(name.isEmpty())name=QStringLiteral("Display %1").arg(i+1);
        int duplicates=0;
        for(const Monitor &other:m_monitorRecords)
            if(m_monitorNicknames.value(other.identity,other.name)==name)++duplicates;
        if(duplicates>1)name+=QStringLiteral(" (Display %1)").arg(i+1);
        return QStringLiteral("%1 · %2 × %3%4").arg(name).arg(monitor.bounds.width())
            .arg(monitor.bounds.height()).arg(monitor.primary?QStringLiteral(" · Primary"):QString());
    }
    return QStringLiteral("Disconnected display");
}
bool WindowArrangementManager::setMonitorNickname(const QString &device,const QString &nickname){
    const QString cleaned=nickname.trimmed();
    if(cleaned.size()>120){setStatus(QStringLiteral("Monitor nicknames can contain at most 120 characters."),true);return false;}
    const auto found=std::find_if(m_monitorRecords.cbegin(),m_monitorRecords.cend(),
                                [&device](const Monitor &monitor){return monitor.device==device;});
    if(found==m_monitorRecords.cend()){setStatus(QStringLiteral("The selected display is no longer available."),true);return false;}
    const auto previous=m_monitorNicknames;
    if(cleaned.isEmpty())m_monitorNicknames.remove(found->identity);
    else m_monitorNicknames.insert(found->identity,cleaned);
    if(!saveLayouts()){m_monitorNicknames=previous;return false;}
    updateMonitorList();rebuildPreview();return true;
}

void WindowArrangementManager::refreshWindows(){
    enqueueWindowJob(WindowJob{});
}

bool WindowArrangementManager::enqueueWindowJob(const WindowJob &job)
{
    if (job.kind == WindowJobKind::Refresh) {
        if ((m_hasCurrentWindowJob && m_currentWindowJob.kind == WindowJobKind::Refresh) ||
            std::any_of(m_windowJobs.cbegin(), m_windowJobs.cend(), [](const WindowJob &queued) {
                return queued.kind == WindowJobKind::Refresh;
            }))
            return true;
    }
    if (m_windowJobs.size() >= 16) {
        const QString message = QStringLiteral("The window operation queue is full; try again after the current refresh finishes.");
        setStatus(message, true);
        if (job.kind == WindowJobKind::Apply)
            emit layoutApplyFinished(job.requestId, false, message);
        return false;
    }
    m_windowJobs.enqueue(job);
    setBusy(true);
    startNextWindowJob();
    return true;
}

void WindowArrangementManager::startNextWindowJob()
{
    if (m_windowWatcher.isRunning() || m_hasCurrentWindowJob || m_windowJobs.isEmpty()) {
        setBusy(m_windowWatcher.isRunning() || m_hasCurrentWindowJob || !m_windowJobs.isEmpty());
        return;
    }
    m_currentWindowJob = m_windowJobs.dequeue();
    m_hasCurrentWindowJob = true;
    updateMonitorList();
    if (m_currentWindowJob.kind == WindowJobKind::Refresh)
        setStatus(QStringLiteral("Refreshing open windows…"));
    else if (m_currentWindowJob.kind == WindowJobKind::Preview)
        setStatus(QStringLiteral("Refreshing windows for layout preview…"));
    else
        setStatus(QStringLiteral("Refreshing windows before applying the layout…"));
    setBusy(true);
    m_windowWatcher.setFuture(QtConcurrent::run(&WindowArrangementManager::enumerateWindows));
}

void WindowArrangementManager::finishWindowJob(const QVector<ArrangementWindow> &windows)
{
    const WindowJob job = m_currentWindowJob;
    QHash<quintptr, ArrangementWindow> previous;
    for (const ArrangementWindow &window : std::as_const(m_windows))
        if (window.selected)
            previous.insert(window.handle, window);
    QVector<ArrangementWindow> refreshed = windows;
    for (ArrangementWindow &window : refreshed) {
        const auto old = previous.constFind(window.handle);
        window.selected = old != previous.cend() && old->processId == window.processId &&
                          normalizedExecutable(old->executable) == normalizedExecutable(window.executable);
    }
    beginResetModel();
    m_windows = std::move(refreshed);
    endResetModel();
    emit windowsChanged();

    if (job.kind == WindowJobKind::Refresh) {
        rebuildPreview();
        setStatus(QStringLiteral("Found %1 eligible open window(s). Select windows to arrange.").arg(m_windows.size()));
    } else if (job.kind == WindowJobKind::Preview) {
        if (m_selectedLayoutId == job.layoutId) {
            m_previewLayoutId = job.layoutId;
            m_resolutions = job.resolutions;
            rebuildPreview();
            setStatus(QStringLiteral("Layout preview refreshed. Review matched, missing, and ambiguous windows."));
        }
    } else {
        m_previewLayoutId = job.layoutId;
        m_resolutions = job.resolutions;
        rebuildPreview();
        const bool cancelled = m_cancelledApplyRequests.remove(job.requestId) > 0;
        QString error;
        const bool success = !cancelled && applyFreshLayout(job.layoutId, &error);
        if (cancelled) {
            error = QStringLiteral("Window layout application was cancelled.");
            setStatus(error);
        } else if (!success) {
            if (error.isEmpty()) error = QStringLiteral("Could not apply the saved window layout.");
            setStatus(error, true);
        }
        emit layoutApplyFinished(job.requestId, success, error);
    }

    m_currentWindowJob = {};
    m_hasCurrentWindowJob = false;
    setBusy(!m_windowJobs.isEmpty());
    startNextWindowJob();
}
void WindowArrangementManager::setWindowSelected(const QString &windowId,bool selected){
    bool ok=false;const quintptr handle=windowId.toULongLong(&ok);const int row=ok?windowIndex(handle):-1;if(row<0)return;
    if(m_windows[row].selected==selected)return;m_windows[row].selected=selected;emit dataChanged(index(row),index(row),{SelectedRole});
}
void WindowArrangementManager::selectRange(int first,int last,bool additive,bool toggle){
    if(m_windows.isEmpty())return;first=qBound(0,first, m_windows.size()-1);last=qBound(0,last,m_windows.size()-1);
    if(!additive&&!toggle)for(int i=0;i<m_windows.size();++i)m_windows[i].selected=false;
    const int low=qMin(first,last),high=qMax(first,last);
    for(int i=low;i<=high;++i)m_windows[i].selected=toggle?!m_windows[i].selected:true;
    emit dataChanged(index(0),index(m_windows.size()-1),{SelectedRole});
}
void WindowArrangementManager::selectAll(){for(auto &window:m_windows)window.selected=true;if(!m_windows.isEmpty())emit dataChanged(index(0),index(m_windows.size()-1),{SelectedRole});}
void WindowArrangementManager::clearSelection(){for(auto &window:m_windows)window.selected=false;if(!m_windows.isEmpty())emit dataChanged(index(0),index(m_windows.size()-1),{SelectedRole});}

WindowArrangementManager::Monitor WindowArrangementManager::monitorFor(const QString &device)const{
    for(const Monitor &monitor:m_monitorRecords)if(monitor.device==device)return monitor;
    for(const Monitor &monitor:m_monitorRecords)if(monitor.primary)return monitor;
    return m_monitorRecords.isEmpty()?Monitor{}:m_monitorRecords.first();
}
void WindowArrangementManager::capturePrevious(const QVector<quintptr> &handles){
    QVector<Snapshot> snapshot;
#ifdef Q_OS_WIN
    for(quintptr value:handles){const int row=windowIndex(value);if(row<0)continue;const ArrangementWindow &window=m_windows.at(row);HWND hwnd=reinterpret_cast<HWND>(value);RECT rect{};if(windowIdentityIsCurrent(window)&&GetWindowRect(hwnd,&rect))snapshot.append({value,window.processId,window.executable,QRect(rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top),IsZoomed(hwnd)!=FALSE});}
#else
    Q_UNUSED(handles)
#endif
    if(!snapshot.isEmpty())m_previous=std::move(snapshot);
}
bool WindowArrangementManager::windowIdentityIsCurrent(const ArrangementWindow &window,const QString &titleRule)const{
#ifdef Q_OS_WIN
    HWND hwnd=reinterpret_cast<HWND>(window.handle);if(!IsWindow(hwnd))return false;
    DWORD processId=0;GetWindowThreadProcessId(hwnd,&processId);if(!processId||processId!=window.processId)return false;
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,processId);if(!process)return false;
    wchar_t executable[32768]{};DWORD size=DWORD(std::size(executable));
    const bool readExecutable=QueryFullProcessImageNameW(process,0,executable,&size)!=FALSE;
    CloseHandle(process);
    if(!readExecutable||normalizedExecutable(QString::fromWCharArray(executable,int(size)))!=normalizedExecutable(window.executable))return false;
    if(!titleRule.isEmpty()){
        wchar_t title[1024]{};const int length=GetWindowTextW(hwnd,title,int(std::size(title)));
        if(length<=0||!QString::fromWCharArray(title,length).contains(titleRule,Qt::CaseInsensitive))return false;
    }
    return true;
#else
    Q_UNUSED(window) Q_UNUSED(titleRule) return false;
#endif
}
bool WindowArrangementManager::placeWindow(const ArrangementWindow &window,const QRect &rect,bool maximize,const QString &titleRule)const{
#ifdef Q_OS_WIN
    if(!windowIdentityIsCurrent(window,titleRule))return false;
    HWND hwnd=reinterpret_cast<HWND>(window.handle);
    ShowWindowAsync(hwnd,SW_RESTORE);
    const bool positioned=SetWindowPos(hwnd,nullptr,rect.x(),rect.y(),rect.width(),rect.height(),
                                       SWP_NOACTIVATE|SWP_NOZORDER|SWP_SHOWWINDOW|SWP_ASYNCWINDOWPOS)!=FALSE;
    if(maximize)ShowWindowAsync(hwnd,SW_MAXIMIZE);
    return positioned;
#else
    Q_UNUSED(window) Q_UNUSED(rect) Q_UNUSED(maximize) Q_UNUSED(titleRule) return false;
#endif
}
bool WindowArrangementManager::arrangeSelected(const QString &placement){
    updateMonitorList();
    const QVector<int> rows=selectedRows();if(rows.isEmpty()){setStatus(QStringLiteral("Select one or more windows first."),true);return false;}
    const auto normalized=WindowArrangementGeometry::placement(placement);QVector<quintptr> handles;for(int row:rows)handles.append(m_windows.at(row).handle);capturePrevious(handles);int placed=0,failed=0;
    for(int row:rows){const auto &window=m_windows.at(row);const Monitor monitor=monitorFor(window.monitor);
        const QRect target=WindowArrangementGeometry::denormalize(normalized,monitor.workArea);
        if(placeWindow(window,target,placement==QStringLiteral("maximize")))++placed;else ++failed;}
    setStatus(QStringLiteral("Arrangement requested for %1 window(s); %2 unavailable.").arg(placed).arg(failed),failed>0);return failed==0;
}
bool WindowArrangementManager::moveSelectedToMonitor(const QString &device){
    updateMonitorList();
    const QVector<int> rows=selectedRows();if(rows.isEmpty()){setStatus(QStringLiteral("Select one or more windows first."),true);return false;}
    if(std::none_of(m_monitorRecords.cbegin(),m_monitorRecords.cend(),[&device](const Monitor &monitor){return monitor.device==device;})){setStatus(QStringLiteral("The selected monitor is no longer connected. Refresh and choose an available monitor."),true);return false;}
    const Monitor targetMonitor=monitorFor(device);if(targetMonitor.workArea.isEmpty()){setStatus(QStringLiteral("No usable monitor work area is available."),true);return false;}
    QVector<quintptr> handles;for(int row:rows)handles.append(m_windows.at(row).handle);capturePrevious(handles);int placed=0,failed=0;
    for(int row:rows){const auto &window=m_windows.at(row);const auto source=window.workArea.isEmpty()?targetMonitor.workArea:window.workArea;
        const auto normalized=WindowArrangementGeometry::normalize(window.rect,source);
        if(placeWindow(window,WindowArrangementGeometry::denormalize(normalized,targetMonitor.workArea),false))++placed;else ++failed;}
    setStatus(QStringLiteral("Move requested for %1 window(s); %2 unavailable.").arg(placed).arg(failed),failed>0);return failed==0;
}
bool WindowArrangementManager::restorePreviousArrangement(){
    if(m_previous.isEmpty()){setStatus(QStringLiteral("There is no previous arrangement to restore."),true);return false;}
    QVector<Snapshot> snapshots=m_previous;m_previous.clear();int restored=0,failed=0;
    for(const auto &snapshot:snapshots){ArrangementWindow window;window.handle=snapshot.handle;window.processId=snapshot.processId;window.executable=snapshot.executable;if(placeWindow(window,snapshot.rect,snapshot.maximized))++restored;else ++failed;}
    setStatus(QStringLiteral("Restore requested for %1 window(s); %2 unavailable.").arg(restored).arg(failed),failed>0);return failed==0;
}

QString WindowArrangementManager::saveSelectedLayout(const QString &name){
    const QString normalized=name.trimmed();const auto rows=selectedRows();
    if(normalized.isEmpty()||normalized.size()>120||rows.isEmpty()){setStatus(rows.isEmpty()?QStringLiteral("Select windows before saving a layout."):QStringLiteral("Enter a layout name of 1–120 characters."),true);return{};}
    if(m_layouts.size()>=500||rows.size()>100){setStatus(m_layouts.size()>=500?QStringLiteral("The maximum of 500 saved layouts has been reached."):QStringLiteral("A layout can contain at most 100 window rules."),true);return{};}
    WindowLayout layout;layout.id=makeId();layout.name=normalized;layout.profileId=m_profiles->activeProfileId();
    for(int row:rows){const auto &window=m_windows.at(row);if(window.executable.isEmpty())continue;const Monitor monitor=monitorFor(window.monitor);if(monitor.workArea.isEmpty())continue;
        layout.rules.append({window.executable,QString(),monitor.device,WindowArrangementGeometry::normalize(window.rect,monitor.workArea),window.maximized});}
    if(layout.rules.isEmpty()){setStatus(QStringLiteral("Selected windows do not expose an executable identity."),true);return{};}
    m_layouts.append(layout);if(!saveLayouts()){m_layouts.removeLast();return{};}setSelectedLayoutId(layout.id);emit layoutsChanged();setStatus(QStringLiteral("Saved layout %1 with %2 window rule(s).").arg(layout.name).arg(layout.rules.size()));return layout.id;
}
bool WindowArrangementManager::renameLayout(const QString &id,const QString &name){const int row=layoutIndex(id);const QString normalized=name.trimmed();if(row<0||normalized.isEmpty()||normalized.size()>120)return false;const QString old=m_layouts[row].name;m_layouts[row].name=normalized;if(!saveLayouts()){m_layouts[row].name=old;return false;}emit layoutsChanged();return true;}
bool WindowArrangementManager::deleteLayout(const QString &id){const int row=layoutIndex(id);if(row<0)return false;const WindowLayout old=m_layouts.takeAt(row);if(!saveLayouts()){m_layouts.insert(row,old);return false;}if(m_selectedLayoutId==id)setSelectedLayoutId({});if(m_previewLayoutId==id){m_previewLayoutId.clear();m_preview.clear();emit previewChanged();}emit layoutsChanged();return true;}
QVariantList WindowArrangementManager::layoutSlots(const QString &id)const{QVariantList result;const int row=layoutIndex(id);if(row<0)return result;for(int i=0;i<m_layouts[row].rules.size();++i)result.append(slotToMap(m_layouts[row].rules.at(i),i));return result;}
bool WindowArrangementManager::setTitleRule(const QString &id,int slotIndex,const QString &rule){const int row=layoutIndex(id);if(row<0||slotIndex<0||slotIndex>=m_layouts[row].rules.size()||rule.size()>300)return false;const QString old=m_layouts[row].rules[slotIndex].titleRule;m_layouts[row].rules[slotIndex].titleRule=rule.trimmed();if(!saveLayouts()){m_layouts[row].rules[slotIndex].titleRule=old;return false;}if(m_previewLayoutId==id){m_resolutions.remove(slotIndex);rebuildPreview();}return true;}
bool WindowArrangementManager::previewLayout(const QString &id)
{
    if (layoutIndex(id) < 0) {
        setStatus(QStringLiteral("The saved layout is unavailable."), true);
        return false;
    }
    WindowJob job;
    job.kind = WindowJobKind::Preview;
    job.layoutId = id;
    if (m_previewLayoutId == id)
        job.resolutions = m_resolutions;
    setSelectedLayoutId(id);
    m_preview.clear();
    emit previewChanged();
    return enqueueWindowJob(job);
}

bool WindowArrangementManager::resolveAmbiguous(int slotIndex,const QString &windowId)
{
    bool ok = false;
    const quintptr handle = windowId.toULongLong(&ok);
    const int layout = layoutIndex(m_previewLayoutId);
    if (!ok || layout < 0 || slotIndex < 0 || slotIndex >= m_layouts.at(layout).rules.size())
        return false;
    const int row = windowIndex(handle);
    if (row < 0)
        return false;
    const auto &slot = m_layouts.at(layout).rules.at(slotIndex);
    const ArrangementWindow &window = m_windows.at(row);
    if (normalizedExecutable(window.executable) != normalizedExecutable(slot.executable) ||
        (!slot.titleRule.isEmpty() && !window.title.contains(slot.titleRule, Qt::CaseInsensitive)) ||
        !windowIdentityIsCurrent(window, slot.titleRule))
        return false;
    m_resolutions.insert(slotIndex, handle);
    rebuildPreview();
    return true;
}
void WindowArrangementManager::rebuildPreview(){
    QVariantList rows;const int li=layoutIndex(m_previewLayoutId);if(li<0){m_preview.clear();emit previewChanged();return;}
    QStringList executables,titles;for(const auto &window:m_windows){executables.append(window.executable);titles.append(window.title);}
    for(int slotIndex=0;slotIndex<m_layouts[li].rules.size();++slotIndex){const auto &slot=m_layouts[li].rules.at(slotIndex);QVariantList candidates;
        const QVector<int> matched=WindowArrangementMatching::matchingRows(slot.executable,slot.titleRule,executables,titles);
        for(int row:matched){const auto &window=m_windows.at(row);candidates.append(QVariantMap{{QStringLiteral("id"),QString::number(window.handle)},{QStringLiteral("title"),window.title}});}
        QString state=candidates.isEmpty()?QStringLiteral("Missing"):candidates.size()>1?QStringLiteral("Ambiguous"):QStringLiteral("Matched");
        const quintptr resolved=m_resolutions.value(slotIndex,0);bool stillCandidate=false;
        if(resolved){for(const auto &candidate:candidates)if(candidate.toMap().value(QStringLiteral("id")).toString()==QString::number(resolved)){stillCandidate=true;break;}if(!stillCandidate){m_resolutions.remove(slotIndex);}else state=QStringLiteral("Resolved");}
        const Monitor monitor=monitorFor(slot.monitor);const bool monitorMissing=monitor.device!=slot.monitor;
        rows.append(QVariantMap{{QStringLiteral("index"),slotIndex},{QStringLiteral("executable"),slot.executable},
            {QStringLiteral("titleRule"),slot.titleRule},{QStringLiteral("monitor"),slot.monitor},
            {QStringLiteral("monitorMissing"),monitorMissing},{QStringLiteral("monitorLabel"),monitorLabel(slot.monitor)},{QStringLiteral("status"),state},
            {QStringLiteral("candidates"),candidates},{QStringLiteral("resolvedId"),stillCandidate?QString::number(resolved):QString()}});
    }
    m_preview=rows;emit previewChanged();
}
bool WindowArrangementManager::applyLayout(const QString &id)
{
    return requestApplyLayout(id) != 0;
}

quint64 WindowArrangementManager::requestApplyLayout(const QString &id)
{
    if (layoutIndex(id) < 0) {
        setStatus(QStringLiteral("The saved layout is unavailable."), true);
        return 0;
    }
    WindowJob job;
    job.kind = WindowJobKind::Apply;
    job.layoutId = id;
    job.requestId = m_nextApplyRequestId++;
    if (job.requestId == 0)
        job.requestId = m_nextApplyRequestId++;
    if (m_previewLayoutId == id)
        job.resolutions = m_resolutions;
    setSelectedLayoutId(id);
    if (m_previewLayoutId != id) {
        m_preview.clear();
        emit previewChanged();
    }
    if (!enqueueWindowJob(job))
        return 0;
    return job.requestId;
}

void WindowArrangementManager::cancelApplyLayout(quint64 requestId)
{
    if (!requestId)
        return;
    QQueue<WindowJob> retained;
    while (!m_windowJobs.isEmpty()) {
        const WindowJob job = m_windowJobs.dequeue();
        if (job.kind != WindowJobKind::Apply || job.requestId != requestId)
            retained.enqueue(job);
    }
    m_windowJobs = std::move(retained);
    if (m_hasCurrentWindowJob && m_currentWindowJob.kind == WindowJobKind::Apply &&
        m_currentWindowJob.requestId == requestId)
        m_cancelledApplyRequests.insert(requestId);
    setBusy(m_windowWatcher.isRunning() || m_hasCurrentWindowJob || !m_windowJobs.isEmpty());
}

bool WindowArrangementManager::applyFreshLayout(const QString &id,QString *error)
{
    const int layoutRow = layoutIndex(id);
    if (layoutRow < 0) {
        if (error) *error = QStringLiteral("The saved window layout is no longer available.");
        return false;
    }
    const WindowLayout layout = m_layouts.at(layoutRow);
    struct Placement { ArrangementWindow window; QRect rect; bool maximize=false; QString titleRule; };
    QVector<Placement> placements;
    int skipped = 0;
    for (int slotIndex = 0; slotIndex < layout.rules.size(); ++slotIndex) {
        const WindowLayoutSlot &slot = layout.rules.at(slotIndex);
        QStringList executables, titles;
        for (const ArrangementWindow &window : m_windows) {
            executables.append(window.executable);
            titles.append(window.title);
        }
        const QVector<int> candidates = WindowArrangementMatching::matchingRows(
            slot.executable, slot.titleRule, executables, titles);
        const quintptr resolved = m_resolutions.value(slotIndex, 0);
        if (candidates.size() > 1 && !resolved) {
            if (error) *error = QStringLiteral("The refreshed layout preview has ambiguous matches. Resolve them in Window Arrangements before applying.");
            return false;
        }
        int matchedRow = -1;
        if (resolved) {
            matchedRow = windowIndex(resolved);
            if (matchedRow < 0 || !candidates.contains(matchedRow)) {
                if (error) *error = QStringLiteral("A previously resolved window is no longer a valid match. Review the refreshed preview.");
                return false;
            }
        } else if (candidates.size() == 1) {
            matchedRow = candidates.first();
        }
        if (matchedRow < 0) {
            ++skipped;
            continue;
        }
        const ArrangementWindow window = m_windows.at(matchedRow);
        if (!windowIdentityIsCurrent(window, slot.titleRule)) {
            ++skipped;
            continue;
        }
        const Monitor monitor = monitorFor(slot.monitor);
        if (monitor.workArea.isEmpty()) {
            ++skipped;
            continue;
        }
        placements.append({window, WindowArrangementGeometry::denormalize(slot.rect, monitor.workArea),
                           slot.maximized, slot.titleRule});
    }
    if (placements.isEmpty()) {
        const QString message = QStringLiteral("No current windows matched this layout; missing or inaccessible matches were skipped.");
        if (error) *error = message;
        setStatus(message, true);
        return false;
    }
    QVector<quintptr> handles;
    for (const Placement &placement : std::as_const(placements))
        handles.append(placement.window.handle);
    capturePrevious(handles);
    int placed = 0;
    for (const Placement &placement : std::as_const(placements)) {
        if (placeWindow(placement.window, placement.rect, placement.maximize, placement.titleRule))
            ++placed;
        else
            ++skipped;
    }
    const bool disconnected = std::any_of(layout.rules.cbegin(), layout.rules.cend(), [this](const WindowLayoutSlot &slot) {
        return monitorFor(slot.monitor).device != slot.monitor;
    });
    const QString message = QStringLiteral("Applied %1 window(s); skipped %2 missing or unavailable match(es).%3")
                                .arg(placed).arg(skipped)
                                .arg(disconnected ? QStringLiteral(" Disconnected monitors used the primary monitor.") : QString());
    setStatus(message, placed == 0 || skipped > 0);
    if (placed == 0 && error)
        *error = message;
    return placed > 0;
}
QVariantList WindowArrangementManager::layoutOptions()const{QVariantList result;for(const auto &layout:m_layouts)result.append(QVariantMap{{QStringLiteral("id"),layout.id},{QStringLiteral("name"),layout.name},{QStringLiteral("profileId"),layout.profileId},{QStringLiteral("profileName"),m_profiles->profileById(layout.profileId)?m_profiles->profileById(layout.profileId)->name:QStringLiteral("Unavailable profile")}});return result;}
bool WindowArrangementManager::containsLayout(const QString &id)const{return layoutIndex(id)>=0;}
QVariantMap WindowArrangementManager::layoutInfo(const QString &id)const{const int row=layoutIndex(id);if(row<0)return{};const auto &layout=m_layouts[row];return{{QStringLiteral("id"),layout.id},{QStringLiteral("name"),layout.name},{QStringLiteral("profileId"),layout.profileId},{QStringLiteral("count"),layout.rules.size()}};}

QJsonObject WindowArrangementManager::serializeLayout(const WindowLayout &layout){
    QJsonArray entries;for(const auto &slot:layout.rules)entries.append(QJsonObject{{QStringLiteral("executable"),slot.executable},{QStringLiteral("titleRule"),slot.titleRule},{QStringLiteral("monitor"),slot.monitor},{QStringLiteral("x"),slot.rect.x},{QStringLiteral("y"),slot.rect.y},{QStringLiteral("width"),slot.rect.width},{QStringLiteral("height"),slot.rect.height},{QStringLiteral("maximized"),slot.maximized}});
    return {{QStringLiteral("id"),layout.id},{QStringLiteral("name"),layout.name},{QStringLiteral("profileId"),layout.profileId},{QStringLiteral("slots"),entries}};
}
bool WindowArrangementManager::parseLayout(const QJsonObject &object,WindowLayout *layout){
    if(!layout||!JsonValidation::string(object,QStringLiteral("id"))||!JsonValidation::string(object,QStringLiteral("name"))||!JsonValidation::string(object,QStringLiteral("profileId"))||!object.value(QStringLiteral("slots")).isArray())return false;
    WindowLayout parsed;parsed.id=object.value(QStringLiteral("id")).toString();parsed.name=object.value(QStringLiteral("name")).toString().trimmed();parsed.profileId=object.value(QStringLiteral("profileId")).toString();
    if(!JsonValidation::safeId(parsed.id)||parsed.name.isEmpty()||parsed.name.size()>120||!JsonValidation::safeId(parsed.profileId))return false;
    const auto entries=object.value(QStringLiteral("slots")).toArray();if(entries.isEmpty()||entries.size()>100)return false;
    for(const auto &value:entries){if(!value.isObject())return false;const auto item=value.toObject();
        for(const auto &key:{QStringLiteral("executable"),QStringLiteral("titleRule"),QStringLiteral("monitor")})if(!JsonValidation::string(item,key))return false;
        if(!JsonValidation::boolean(item,QStringLiteral("maximized"))||!item.value(QStringLiteral("x")).isDouble()||!item.value(QStringLiteral("y")).isDouble()||!item.value(QStringLiteral("width")).isDouble()||!item.value(QStringLiteral("height")).isDouble())return false;
        WindowLayoutSlot slot;slot.executable=item.value(QStringLiteral("executable")).toString();slot.titleRule=item.value(QStringLiteral("titleRule")).toString();slot.monitor=item.value(QStringLiteral("monitor")).toString();slot.maximized=item.value(QStringLiteral("maximized")).toBool();slot.rect=WindowArrangementGeometry::NormalizedRect{item.value(QStringLiteral("x")).toDouble(),item.value(QStringLiteral("y")).toDouble(),item.value(QStringLiteral("width")).toDouble(),item.value(QStringLiteral("height")).toDouble()};
        if(slot.executable.isEmpty()||slot.executable.size()>32768||slot.titleRule.size()>300||slot.monitor.size()>100||!std::isfinite(slot.rect.x)||!std::isfinite(slot.rect.y)||!std::isfinite(slot.rect.width)||!std::isfinite(slot.rect.height)||slot.rect.x < -4||slot.rect.x>4||slot.rect.y < -4||slot.rect.y>4||slot.rect.width<=0||slot.rect.height<=0||slot.rect.width>4||slot.rect.height>4)return false;
        parsed.rules.append(slot);
    }
    *layout=std::move(parsed);return true;
}

void WindowArrangementManager::loadLayouts(){
    QDir().mkpath(AppDataPaths::root());
    const QString path=QDir(AppDataPaths::root()).filePath(QStringLiteral("window-layouts.json"));QFile file(path);if(!file.exists())return;
    if(!file.open(QIODevice::ReadOnly)||file.size()>8*1024*1024){m_storeInvalid=true;setStatus(QStringLiteral("Window layouts could not be read; the original file was preserved."),true);return;}
    QJsonParseError parse;const auto doc=QJsonDocument::fromJson(file.readAll(),&parse);const auto root=doc.object();if(parse.error!=QJsonParseError::NoError||!doc.isObject()||root.value(QStringLiteral("schemaVersion")).toInt()!=1||!root.value(QStringLiteral("layouts")).isArray()){m_storeInvalid=true;setStatus(QStringLiteral("Window layout storage is invalid; the original file was preserved."),true);return;}
    const auto nicknames=root.value(QStringLiteral("monitorNicknames"));
    if(!nicknames.isUndefined()){
        if(!nicknames.isObject()){m_storeInvalid=true;setStatus(QStringLiteral("Saved monitor names are invalid; the original file was preserved."),true);return;}
        const auto names=nicknames.toObject();
        for(auto it=names.begin();it!=names.end();++it){
            if(it.key().isEmpty()||it.key().size()>1024||!it.value().isString()||it.value().toString().trimmed().isEmpty()||it.value().toString().size()>120){m_storeInvalid=true;m_monitorNicknames.clear();setStatus(QStringLiteral("Saved monitor names are invalid; the original file was preserved."),true);return;}
            m_monitorNicknames.insert(it.key(),it.value().toString());
        }
    }
    QSet<QString> ids;const auto array=root.value(QStringLiteral("layouts")).toArray();if(array.size()>500){m_storeInvalid=true;setStatus(QStringLiteral("Window layout file exceeds the supported limit."),true);return;}
    for(const auto &value:array){WindowLayout layout;if(!value.isObject()||!parseLayout(value.toObject(),&layout)||ids.contains(layout.id)){m_storeInvalid=true;m_layouts.clear();setStatus(QStringLiteral("Some saved window layouts are invalid; the original file was preserved."),true);return;}ids.insert(layout.id);m_layouts.append(layout);
    }
}
bool WindowArrangementManager::saveLayouts()const{
    if(m_storeInvalid){const_cast<WindowArrangementManager*>(this)->setStatus(QStringLiteral("Window layouts cannot be changed until the invalid saved file is repaired; the original was preserved."),true);return false;}
    QJsonArray layouts;for(const auto &layout:m_layouts)layouts.append(serializeLayout(layout));
    QJsonObject names;for(auto it=m_monitorNicknames.cbegin();it!=m_monitorNicknames.cend();++it)names.insert(it.key(),it.value());
    const QByteArray data=QJsonDocument(QJsonObject{{QStringLiteral("schemaVersion"),1},{QStringLiteral("layouts"),layouts},{QStringLiteral("monitorNicknames"),names}}).toJson(QJsonDocument::Indented);
    if(data.size()>8*1024*1024){const_cast<WindowArrangementManager*>(this)->setStatus(QStringLiteral("Saved window layouts exceed the 8 MiB storage limit."),true);return false;}
    QSaveFile file(QDir(AppDataPaths::root()).filePath(QStringLiteral("window-layouts.json")));if(!file.open(QIODevice::WriteOnly)||file.write(data)!=data.size()||!file.commit()){const_cast<WindowArrangementManager*>(this)->setStatus(QStringLiteral("Could not save window layouts: %1").arg(file.errorString()),true);return false;}return true;
}

QVector<WindowArrangementManager::Monitor> WindowArrangementManager::enumerateMonitors(){
    QVector<Monitor> result;
#ifdef Q_OS_WIN
    const auto names=displayNames();
    struct Context{QVector<Monitor>*items;const QHash<QString,DisplayName>*names;};Context context{&result,&names};
    EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR handle,HDC,LPRECT,LPARAM data)->BOOL{auto *ctx=reinterpret_cast<Context*>(data);MONITORINFOEXW info{};info.cbSize=sizeof(info);if(!GetMonitorInfoW(handle,&info))return TRUE;Monitor m;m.device=QString::fromWCharArray(info.szDevice);const auto name=ctx->names->value(m.device.toCaseFolded());m.name=name.name;m.identity=name.identity.isEmpty()?m.device:name.identity;m.bounds=QRect(info.rcMonitor.left,info.rcMonitor.top,info.rcMonitor.right-info.rcMonitor.left,info.rcMonitor.bottom-info.rcMonitor.top);m.workArea=QRect(info.rcWork.left,info.rcWork.top,info.rcWork.right-info.rcWork.left,info.rcWork.bottom-info.rcWork.top);m.primary=(info.dwFlags&MONITORINFOF_PRIMARY)!=0;ctx->items->append(m);return TRUE;},reinterpret_cast<LPARAM>(&context));
#endif
    return result;
}
QVector<ArrangementWindow> WindowArrangementManager::enumerateWindows(){
    QVector<ArrangementWindow> result;
#ifdef Q_OS_WIN
    const DWORD ownPid=GetCurrentProcessId();
    struct Context{QVector<ArrangementWindow>*items;DWORD ownPid;};Context context{&result,ownPid};
    EnumWindows([](HWND hwnd,LPARAM data)->BOOL{auto *ctx=reinterpret_cast<Context*>(data);if(!IsWindowVisible(hwnd)||(GetWindowLongPtrW(hwnd,GWL_EXSTYLE)&WS_EX_TOOLWINDOW)!=0)return TRUE;DWORD pid=0;GetWindowThreadProcessId(hwnd,&pid);if(!pid||pid==ctx->ownPid)return TRUE;
        wchar_t title[1024]{};const int len=GetWindowTextW(hwnd,title,1024);if(len<=0)return TRUE;
        HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);QString executable;if(process){wchar_t path[32768]{};DWORD size=DWORD(std::size(path));if(QueryFullProcessImageNameW(process,0,path,&size))executable=QString::fromWCharArray(path,int(size));CloseHandle(process);}if(executable.isEmpty())return TRUE;
        RECT rect{};if(!GetWindowRect(hwnd,&rect))return TRUE;ArrangementWindow w;w.handle=reinterpret_cast<quintptr>(hwnd);w.processId=pid;w.title=QString::fromWCharArray(title,len);w.executable=executable;w.rect=QRect(rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top);w.maximized=IsZoomed(hwnd)!=FALSE;
        HMONITOR monitor=MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST);MONITORINFOEXW info{};info.cbSize=sizeof(info);if(GetMonitorInfoW(monitor,&info)){w.monitor=QString::fromWCharArray(info.szDevice);w.workArea=QRect(info.rcWork.left,info.rcWork.top,info.rcWork.right-info.rcWork.left,info.rcWork.bottom-info.rcWork.top);}ctx->items->append(w);return TRUE;
    },reinterpret_cast<LPARAM>(&context));
#endif
    return result;
}
