#pragma once

#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QHash>
#include <QJsonObject>
#include <QQueue>
#include <QVariantList>
#include <QVector>
#include <QSet>

#include "WindowArrangementGeometry.h"

class ProfileManager;

struct ArrangementWindow {
    quintptr handle=0;
    quint32 processId=0;
    QString title;
    QString executable;
    QString monitor;
    QRect rect;
    QRect workArea;
    bool maximized=false;
    bool selected=false;
};

struct WindowLayoutSlot {
    QString executable;
    QString titleRule;
    QString monitor;
    WindowArrangementGeometry::NormalizedRect rect;
    bool maximized=false;
};

struct WindowLayout {
    QString id;
    QString name;
    QString profileId;
    QVector<WindowLayoutSlot> rules;
};

class WindowArrangementManager final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY windowsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QVariantList layouts READ layouts NOTIFY layoutsChanged)
    Q_PROPERTY(QVariantList monitors READ monitors NOTIFY monitorsChanged)
    Q_PROPERTY(QVariantList preview READ preview NOTIFY previewChanged)
    Q_PROPERTY(QString selectedLayoutId READ selectedLayoutId WRITE setSelectedLayoutId NOTIFY selectedLayoutIdChanged)
public:
    enum Role { WindowIdRole=Qt::UserRole+1, TitleRole, ExecutableRole, MonitorRole, SelectedRole, MaximizeRole };
    Q_ENUM(Role)
    explicit WindowArrangementManager(ProfileManager *profiles, QObject *parent=nullptr);
    int rowCount(const QModelIndex &parent={}) const override;
    QVariant data(const QModelIndex &index,int role) const override;
    QHash<int,QByteArray> roleNames() const override;
    bool busy() const{return m_busy;}
    QString status() const{return m_status;}
    QVariantList layouts() const;
    QVariantList monitors() const{return m_monitors;}
    QVariantList preview() const{return m_preview;}
    QString selectedLayoutId() const{return m_selectedLayoutId;}
    void setSelectedLayoutId(const QString &id);
    Q_INVOKABLE void refreshWindows();
    Q_INVOKABLE void setWindowSelected(const QString &windowId,bool selected);
    Q_INVOKABLE void selectRange(int first,int last,bool additive,bool toggle);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE bool arrangeSelected(const QString &placement);
    Q_INVOKABLE bool moveSelectedToMonitor(const QString &monitor);
    Q_INVOKABLE bool restorePreviousArrangement();
    Q_INVOKABLE QString saveSelectedLayout(const QString &name);
    Q_INVOKABLE bool renameLayout(const QString &id,const QString &name);
    Q_INVOKABLE bool deleteLayout(const QString &id);
    Q_INVOKABLE QVariantList layoutSlots(const QString &id) const;
    Q_INVOKABLE bool setTitleRule(const QString &id,int slotIndex,const QString &rule);
    Q_INVOKABLE bool previewLayout(const QString &id);
    Q_INVOKABLE bool resolveAmbiguous(int slotIndex,const QString &windowId);
    Q_INVOKABLE bool applyLayout(const QString &id);
    quint64 requestApplyLayout(const QString &id);
    void cancelApplyLayout(quint64 requestId);
    Q_INVOKABLE QVariantList layoutOptions() const;
    Q_INVOKABLE bool containsLayout(const QString &id) const;
    Q_INVOKABLE QVariantMap layoutInfo(const QString &id) const;
    static QJsonObject serializeLayout(const WindowLayout &layout);
    static bool parseLayout(const QJsonObject &object,WindowLayout *layout);
signals:
    void windowsChanged();
    void busyChanged();
    void statusChanged();
    void layoutsChanged();
    void monitorsChanged();
    void previewChanged();
    void selectedLayoutIdChanged();
    void errorOccurred(const QString &message);
    void layoutApplyFinished(quint64 requestId,bool success,const QString &message);
private:
    enum class WindowJobKind { Refresh, Preview, Apply };
    struct WindowJob {
        WindowJobKind kind=WindowJobKind::Refresh;
        QString layoutId;
        quint64 requestId=0;
        QHash<int,quintptr> resolutions;
    };
    struct Monitor { QString device; QString name; QRect bounds; QRect workArea; bool primary=false; };
    struct Snapshot { quintptr handle=0; quint32 processId=0; QString executable; QRect rect; bool maximized=false; };
    static QVector<ArrangementWindow> enumerateWindows();
    static QVector<Monitor> enumerateMonitors();
    void setBusy(bool busy);
    void setStatus(const QString &status,bool isError=false);
    void loadLayouts();
    bool saveLayouts() const;
    void updateMonitorList();
    bool enqueueWindowJob(const WindowJob &job);
    void startNextWindowJob();
    void finishWindowJob(const QVector<ArrangementWindow> &windows);
    bool applyFreshLayout(const QString &id,QString *error);
    int layoutIndex(const QString &id) const;
    int windowIndex(quintptr handle) const;
    QVector<int> selectedRows() const;
    Monitor monitorFor(const QString &device) const;
    void capturePrevious(const QVector<quintptr> &handles);
    bool placeWindow(const ArrangementWindow &window,const QRect &rect,bool maximize=false,
                     const QString &titleRule={}) const;
    bool windowIdentityIsCurrent(const ArrangementWindow &window,const QString &titleRule={}) const;
    void rebuildPreview();
    ProfileManager *m_profiles=nullptr;
    QVector<ArrangementWindow> m_windows;
    QVector<Monitor> m_monitorRecords;
    QVector<WindowLayout> m_layouts;
    QVector<Snapshot> m_previous;
    QFutureWatcher<QVector<ArrangementWindow>> m_windowWatcher;
    QQueue<WindowJob> m_windowJobs;
    WindowJob m_currentWindowJob;
    QSet<quint64> m_cancelledApplyRequests;
    quint64 m_nextApplyRequestId=1;
    bool m_hasCurrentWindowJob=false;
    QVariantList m_monitors;
    QVariantList m_preview;
    QHash<int,quintptr> m_resolutions;
    QString m_selectedLayoutId;
    QString m_previewLayoutId;
    QString m_status;
    bool m_busy=false;
    bool m_storeInvalid=false;
};
