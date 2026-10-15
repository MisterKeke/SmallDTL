#include "ProfileManager.h"
#include "../app/AppDataPaths.h"
#include "../app/JsonValidation.h"
#include "../actions/ActionManager.h"
#include "../hotkeys/HotkeyUtils.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QUuid>
#include <QSet>
#include <algorithm>

namespace {
QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
void cloneIdentifiers(Profile &profile, const QString &originalId) {
    QHash<QString,QString> ids;
    for (auto &sequence : profile.sequences) {
        const QString original=sequence.id; sequence.id=newId(); ids.insert(original,sequence.id);
        for (auto &step : sequence.steps) { step.id=newId(); if (step.targetProfileId==originalId) step.targetProfileId=profile.id; }
    }
    for (auto &assignment : profile.hotkeys) {
        assignment.id=newId();
        if (assignment.actionType==QStringLiteral("sequence")) assignment.sequenceId=ids.value(assignment.sequenceId,assignment.sequenceId);
        if (assignment.targetProfileId==originalId) assignment.targetProfileId=profile.id;
    }
}
}
ProfileManager::ProfileManager(SettingsManager *settings,QObject *parent) : QAbstractListModel(parent),m_settings(settings) {
    connect(this,&ProfileManager::activeProfileChanged,this,&ProfileManager::activeProfileNameChanged);
    connect(this,&ProfileManager::profilesChanged,this,&ProfileManager::activeProfileNameChanged);
    QDir().mkpath(profilesDirectory());
    m_globalProfile.id=QStringLiteral("global"); m_globalProfile.name=QStringLiteral("Application-wide");
    m_globalProfile.screenshotDirectory=settings->generalScreenshotDirectory();
    loadProfiles();
    if (std::none_of(m_profiles.begin(),m_profiles.end(),[](const Profile &profile){return profile.isDefault;})) ensureDefaultProfile();
    m_activeId=settings->activeProfileId();
    if (!profileById(m_activeId) || m_activeId==QStringLiteral("global")) {
        m_activeId=m_profiles.first().id;
        if(!settings->setActiveProfileId(m_activeId))m_startupMessages.append(QStringLiteral("The fallback profile could not be saved; the previous settings were preserved."));
    }
}
int ProfileManager::rowCount(const QModelIndex &parent) const { return parent.isValid()?0:m_profiles.size(); }
QVariant ProfileManager::data(const QModelIndex &index,int role) const {
    if (!index.isValid() || index.row()<0 || index.row()>=m_profiles.size()) return {};
    const Profile &p=m_profiles.at(index.row());
    switch(role) {
    case IdRole:return p.id; case NameRole:return p.name; case DescriptionRole:return p.description;
    case ScreenshotDirectoryRole:return p.screenshotDirectory; case ActivationKeyRole:return p.activationKey;
    case ActivationModifiersRole:return p.activationModifiers; case ActivationShortcutRole:return HotkeyUtils::formatShortcut(p.activationKey,p.activationModifiers);
    case IsDefaultRole:return p.isDefault; case IsActiveRole:return p.id==m_activeId; case HotkeyCountRole:return p.hotkeys.size(); case IconRole:return p.icon; default:return {};
    }
}
QHash<int,QByteArray> ProfileManager::roleNames() const { return {{IdRole,"profileId"},{NameRole,"name"},{DescriptionRole,"description"},{ScreenshotDirectoryRole,"screenshotDirectory"},{ActivationKeyRole,"activationKey"},{ActivationModifiersRole,"activationModifiers"},{ActivationShortcutRole,"activationShortcut"},{IsDefaultRole,"isDefault"},{IsActiveRole,"isActive"},{HotkeyCountRole,"hotkeyCount"},{IconRole,"icon"}}; }
QString ProfileManager::activeProfileId() const { return m_activeId; }
QString ProfileManager::activeProfileName() const { const auto *p=activeProfile();return p?p->name:QString(); }
QString ProfileManager::activeScreenshotDirectory() const { const auto *p=activeProfile();return p?p->screenshotDirectory:QString(); }
const QVector<Profile> &ProfileManager::profiles() const { return m_profiles; }
const Profile *ProfileManager::profileById(const QString &id) const {
    if (id==QStringLiteral("global")) return &m_globalProfile;
    for (const auto &p:m_profiles) if(p.id==id) return &p;
    return nullptr;
}
Profile *ProfileManager::profileById(const QString &id) { return const_cast<Profile *>(std::as_const(*this).profileById(id)); }
const Profile *ProfileManager::activeProfile() const { return profileById(m_activeId); }
QVariantMap ProfileManager::profileInfo(const QString &id) const {
    const auto *p=profileById(id); if(!p)return {};
    return {{QStringLiteral("id"),p->id},{QStringLiteral("name"),p->name},{QStringLiteral("description"),p->description},{QStringLiteral("icon"),p->icon},{QStringLiteral("screenshotDirectory"),p->screenshotDirectory},{QStringLiteral("activationKey"),p->activationKey},{QStringLiteral("activationModifiers"),p->activationModifiers},{QStringLiteral("activationShortcut"),HotkeyUtils::formatShortcut(p->activationKey,p->activationModifiers)},{QStringLiteral("isDefault"),p->isDefault},{QStringLiteral("isActive"),p->id==m_activeId},{QStringLiteral("hotkeyCount"),p->hotkeys.size()}};
}
QVariantList ProfileManager::profileOptions() const { QVariantList result;for(const auto &p:m_profiles)result.append(QVariantMap{{QStringLiteral("id"),p.id},{QStringLiteral("name"),p.name}});return result; }
bool ProfileManager::setActiveProfile(const QString &id) {
    if(id==QStringLiteral("global") || !profileById(id)) {emit errorOccurred(QStringLiteral("That profile is unavailable."));return false;}
    if(id==m_activeId)return true;
    if(!m_settings->setActiveProfileId(id))return false;
    m_activeId=id;emit activeProfileChanged();
    emit dataChanged(index(0),index(m_profiles.size()-1),{IsActiveRole});return true;
}
QString ProfileManager::createProfile(const QString &name) {
    if(name.trimmed().isEmpty() || name.size()>200) {emit errorOccurred(QStringLiteral("Enter a profile name of 1–200 characters."));return {};}
    Profile p;p.id=newId();p.name=name.trimmed();p.screenshotDirectory=m_settings->generalScreenshotDirectory();
    if(!saveProfile(p))return {};
    beginInsertRows({},m_profiles.size(),m_profiles.size());m_profiles.append(p);endInsertRows();notifyProfileChanged(p.id);return p.id;
}
QString ProfileManager::duplicateProfile(const QString &id) {
    const auto *source=profileById(id);if(!source || id==QStringLiteral("global"))return {};
    Profile p=*source;p.id=newId();p.name=source->name.left(190)+QStringLiteral(" Copy");p.isDefault=false;p.activationKey=0;p.activationModifiers=0;p.legacyActions=false;
    cloneIdentifiers(p,id);
    if(!saveProfile(p))return {};
    beginInsertRows({},m_profiles.size(),m_profiles.size());m_profiles.append(p);endInsertRows();notifyProfileChanged(p.id);return p.id;
}
bool ProfileManager::replaceProfile(const Profile &profile) {
    auto *stored=profileById(profile.id);if(!stored || !saveProfile(profile))return false;
    *stored=profile;stored->legacyActions=false;if(profile.id==QStringLiteral("global"))m_globalInvalid=false;notifyProfileChanged(profile.id);return true;
}
bool ProfileManager::renameProfile(const QString &id,const QString &name) {
    const auto *p=profileById(id);if(!p || name.trimmed().isEmpty())return false;
    Profile updated=*p;updated.name=name.trimmed();return replaceProfile(updated);
}
bool ProfileManager::deleteProfile(const QString &id) {
    const auto *p=profileById(id);if(!p || id==QStringLiteral("global"))return false;
    if(m_profiles.size()<=1 || (p->isDefault && std::count_if(m_profiles.begin(),m_profiles.end(),[](const Profile &v){return v.isDefault;})<=1)) {emit errorOccurred(QStringLiteral("Keep the last default profile and at least one profile."));return false;}
    const bool active=id==m_activeId;
    const QString fallback=m_profiles.first().id==id?m_profiles.at(1).id:m_profiles.first().id;
    if(active && !setActiveProfile(fallback))return false;
    const QString path=profileFilePath(id);
    if(QFileInfo::exists(path) && !QFile::remove(path)) {if(active)setActiveProfile(id);emit errorOccurred(QStringLiteral("Could not remove the profile file; the profile was kept."));return false;}
    for(int row=0;row<m_profiles.size();++row)if(m_profiles[row].id==id){beginRemoveRows({},row,row);m_profiles.removeAt(row);endRemoveRows();break;}
    emit profilesChanged();emit profileDataChanged(id);return true;
}
bool ProfileManager::setScreenshotDirectory(const QString &id,const QString &directory) {
    const auto *p=profileById(id);const QString path=QDir::cleanPath(QDir::fromNativeSeparators(directory.trimmed()));
    if(!p || !QDir::isAbsolutePath(path) || !QDir(path).exists()) {emit errorOccurred(QStringLiteral("Choose an existing absolute screenshot directory."));return false;}
    Profile updated=*p;updated.screenshotDirectory=path;
    if(!replaceProfile(updated))return false;
    m_settings->addKnownScreenshotDirectory(path);if(id==m_activeId)emit activeProfileChanged();return true;
}
bool ProfileManager::setActivationShortcut(const QString &id,int key,int modifiers) {
    const auto *p=profileById(id);if(!p || id==QStringLiteral("global"))return false;
    Profile updated=*p;updated.activationKey=key;updated.activationModifiers=modifiers;return replaceProfile(updated);
}
bool ProfileManager::addHotkey(const QString &id,const HotkeyAssignment &assignment) {
    const auto *p=profileById(id);if(!p)return false;
    for(const auto &a:p->hotkeys)if(a.id==assignment.id)return false;
    Profile updated=*p;updated.hotkeys.append(assignment);return replaceProfile(updated);
}
bool ProfileManager::updateHotkey(const QString &id,const HotkeyAssignment &assignment) {
    const auto *p=profileById(id);if(!p)return false;
    Profile updated=*p;for(auto &a:updated.hotkeys)if(a.id==assignment.id){a=assignment;return replaceProfile(updated);}return false;
}
bool ProfileManager::removeHotkey(const QString &id,const QString &assignmentId) {
    const auto *p=profileById(id);if(!p)return false;
    Profile updated=*p;for(int i=0;i<updated.hotkeys.size();++i)if(updated.hotkeys[i].id==assignmentId){updated.hotkeys.removeAt(i);return replaceProfile(updated);}return false;
}
bool ProfileManager::setDetails(const QString &id,const QString &description,const QString &icon) {
    const auto *p=profileById(id);if(!p)return false;
    Profile updated=*p;updated.description=description.trimmed();updated.icon=icon.trimmed().isEmpty()?QStringLiteral("◉"):icon.trimmed();return replaceProfile(updated);
}
void ProfileManager::loadProfiles() {
    QSet<QString> sequenceIds;
    for(const QFileInfo &info:QDir(profilesDirectory()).entryInfoList({QStringLiteral("*.json")},QDir::Files,QDir::Name)) {
        QFile file(info.absoluteFilePath());bool valid=false;Profile p;int schema=0;
        if(!info.isSymLink() && file.open(QIODevice::ReadOnly) && file.size()<=4*1024*1024) {
            QJsonParseError error;const auto doc=QJsonDocument::fromJson(file.readAll(),&error);
            schema=doc.object().value(QStringLiteral("schemaVersion")).toInt();
            if(error.error==QJsonParseError::NoError && doc.isObject())p=parseProfile(doc.object(),&valid);
        }
        if(!valid || info.completeBaseName()!=p.id) {if(info.completeBaseName()==QStringLiteral("global")){m_globalInvalid=true;m_globalFutureSchema=schema>2 || !file.isOpen();}m_startupMessages.append(QStringLiteral("Invalid profile %1 was skipped and preserved.").arg(info.fileName()));continue;}
        bool duplicateSequence=false;for(const auto &sequence:p.sequences)if(sequenceIds.contains(sequence.id))duplicateSequence=true;
        if(duplicateSequence){if(p.id==QStringLiteral("global"))m_globalInvalid=true;m_startupMessages.append(QStringLiteral("Duplicate sequence identifiers in %1; the profile was skipped and preserved.").arg(info.fileName()));continue;}
        for(const auto &sequence:p.sequences)sequenceIds.insert(sequence.id);
        if(p.id==QStringLiteral("global")){m_globalProfile=p;continue;}
        if(profileById(p.id)){m_startupMessages.append(QStringLiteral("Duplicate profile %1 was skipped.").arg(info.fileName()));continue;}
        m_profiles.append(p);
    }
}
bool ProfileManager::saveProfile(const Profile &profile) const {
    if(profile.id==QStringLiteral("global") && m_globalInvalid) {
        if(m_globalFutureSchema || !JsonValidation::backup(profileFilePath(profile.id),QStringLiteral(".invalid-"))) {
            emit const_cast<ProfileManager *>(this)->errorOccurred(QStringLiteral("Global configuration could not be safely repaired; its original is preserved."));return false;
        }
    }
    bool valid=false;const auto object=serializeProfile(profile);parseProfile(object,&valid);
    if(!valid){emit const_cast<ProfileManager *>(this)->errorOccurred(QStringLiteral("Invalid profile data was not saved."));return false;}
    QString error;if(!JsonValidation::write(profileFilePath(profile.id),object,&error)){emit const_cast<ProfileManager *>(this)->errorOccurred(QStringLiteral("Could not save profile: %1").arg(error));return false;}return true;
}
QString ProfileManager::profilesDirectory() const {return QDir(AppDataPaths::root()).filePath(QStringLiteral("profiles"));}
QString ProfileManager::profileFilePath(const QString &id) const {return JsonValidation::safeId(id)?QDir(profilesDirectory()).filePath(id+QStringLiteral(".json")):QString();}
void ProfileManager::ensureDefaultProfile() {
    Profile p;p.id=newId();p.name=QStringLiteral("Default");p.description=QStringLiteral("Your everyday shortcuts and screenshots.");p.isDefault=true;p.screenshotDirectory=m_settings->generalScreenshotDirectory();
    m_profiles.append(p);if(!saveProfile(p))m_startupMessages.append(QStringLiteral("The default profile could not be persisted; check the app-data directory."));
}
void ProfileManager::notifyProfileChanged(const QString &id) {
    for(int row=0;row<m_profiles.size();++row)if(m_profiles[row].id==id){emit dataChanged(index(row),index(row));break;}
    emit profilesChanged();emit profileDataChanged(id);
}
Profile ProfileManager::parseProfile(const QJsonObject &o,bool *valid) {
    Profile p;*valid=false;const int schema=o.value(QStringLiteral("schemaVersion")).toInt();
    if(!JsonValidation::integer(o,QStringLiteral("schemaVersion"),1,2) || (schema!=1 && schema!=2) || !o.value(QStringLiteral("hotkeys")).isArray())return p;
    for(const QString &key:{QStringLiteral("id"),QStringLiteral("name"),QStringLiteral("description"),QStringLiteral("icon"),QStringLiteral("screenshotDirectory")})if(!JsonValidation::string(o,key))return p;
    if(!JsonValidation::boolean(o,QStringLiteral("isDefault")) || !JsonValidation::integer(o,QStringLiteral("activationKey"),0,0x01ffffff) || !JsonValidation::integer(o,QStringLiteral("activationModifiers"),0,0x1fffffff))return p;
    p.id=o.value(QStringLiteral("id")).toString();p.name=o.value(QStringLiteral("name")).toString().trimmed();p.description=o.value(QStringLiteral("description")).toString();p.icon=o.value(QStringLiteral("icon")).toString(QStringLiteral("◉"));p.screenshotDirectory=o.value(QStringLiteral("screenshotDirectory")).toString();
    p.activationKey=o.value(QStringLiteral("activationKey")).toInt();p.activationModifiers=o.value(QStringLiteral("activationModifiers")).toInt();p.isDefault=o.value(QStringLiteral("isDefault")).toBool();p.legacyActions=schema==1;
    if(!JsonValidation::safeId(p.id) || p.name.isEmpty() || p.name.size()>200 || p.description.size()>2000 || p.icon.size()>8 || !QDir::isAbsolutePath(p.screenshotDirectory))return p;
    const int mask=int(Qt::ControlModifier|Qt::AltModifier|Qt::ShiftModifier|Qt::MetaModifier);
    if((p.activationModifiers & ~mask)!=0 || (p.activationKey!=0 && p.activationModifiers==0))return p;
    const QJsonArray assignments=o.value(QStringLiteral("hotkeys")).toArray();if(assignments.size()>1000)return p;QSet<QString> ids;
    for(const auto &v:assignments) {
        if(!v.isObject())return p;const auto a=v.toObject();
        for(const QString &key:{QStringLiteral("id"),QStringLiteral("actionType"),QStringLiteral("targetProfileId"),QStringLiteral("sequenceId")})if(!JsonValidation::string(a,key))return p;
        if(!JsonValidation::boolean(a,QStringLiteral("enabled")) || !JsonValidation::integer(a,QStringLiteral("key"),0,0x01ffffff) || !JsonValidation::integer(a,QStringLiteral("modifiers"),0,0x1fffffff))return p;
        HotkeyAssignment item;item.id=a.value(QStringLiteral("id")).toString();item.key=a.value(QStringLiteral("key")).toInt();item.modifiers=a.value(QStringLiteral("modifiers")).toInt();item.actionType=a.value(QStringLiteral("actionType")).toString(QStringLiteral("saveScreenshot"));item.targetProfileId=a.value(QStringLiteral("targetProfileId")).toString();item.sequenceId=a.value(QStringLiteral("sequenceId")).toString();item.enabled=a.value(QStringLiteral("enabled")).toBool(true);
        if(!JsonValidation::safeId(item.id) || ids.contains(item.id) || !QStringList{QStringLiteral("saveScreenshot"),QStringLiteral("switchProfile"),QStringLiteral("sequence")}.contains(item.actionType) || (item.modifiers & ~mask)!=0 || (item.key!=0 && item.modifiers==0))return p;
        ids.insert(item.id);p.hotkeys.append(item);
    }
    if(schema==2) {
        if(!o.value(QStringLiteral("sequences")).isArray())return p;
        const auto sequences=o.value(QStringLiteral("sequences")).toArray();if(sequences.size()>100)return p;QSet<QString> sequenceIds;
        for(const auto &v:sequences){ActionSequence sequence;if(!v.isObject() || !ActionManager::parseSequence(v.toObject(),&sequence) || sequenceIds.contains(sequence.id))return p;sequenceIds.insert(sequence.id);p.sequences.append(sequence);}
        for(const auto &binding:p.hotkeys)if(binding.actionType==QStringLiteral("sequence") && !sequenceIds.contains(binding.sequenceId) && (binding.enabled || !binding.sequenceId.isEmpty()))return p;
    }
    *valid=true;return p;
}
QJsonObject ProfileManager::serializeProfile(const Profile &p) {
    QJsonArray assignments,sequences;
    for(const auto &a:p.hotkeys)assignments.append(QJsonObject{{QStringLiteral("id"),a.id},{QStringLiteral("key"),a.key},{QStringLiteral("modifiers"),a.modifiers},{QStringLiteral("actionType"),a.actionType},{QStringLiteral("targetProfileId"),a.targetProfileId},{QStringLiteral("sequenceId"),a.sequenceId},{QStringLiteral("enabled"),a.enabled}});
    for(const auto &s:p.sequences)sequences.append(ActionManager::serializeSequence(s));
    return {{QStringLiteral("schemaVersion"),2},{QStringLiteral("id"),p.id},{QStringLiteral("name"),p.name},{QStringLiteral("description"),p.description},{QStringLiteral("icon"),p.icon},{QStringLiteral("screenshotDirectory"),p.screenshotDirectory},{QStringLiteral("activationKey"),p.activationKey},{QStringLiteral("activationModifiers"),p.activationModifiers},{QStringLiteral("isDefault"),p.isDefault},{QStringLiteral("hotkeys"),assignments},{QStringLiteral("sequences"),sequences}};
}
bool ProfileManager::exportProfile(const QString &id,const QString &path) {
    const auto *p=profileById(id);if(!p)return false;QString error;
    if(!JsonValidation::write(path,serializeProfile(*p),&error)){emit errorOccurred(QStringLiteral("Export failed: %1").arg(error));return false;}return true;
}
QString ProfileManager::importProfile(const QString &path) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly) || file.size()>4*1024*1024){emit errorOccurred(QStringLiteral("Cannot read the profile import."));return {};}
    QJsonParseError error;const auto doc=QJsonDocument::fromJson(file.readAll(),&error);bool valid=false;Profile p=parseProfile(doc.object(),&valid);
    if(error.error!=QJsonParseError::NoError || !valid || p.legacyActions){emit errorOccurred(QStringLiteral("Choose a valid schema 2 export containing its action sequences."));return {};}
    QSet<QString> ids;for(const auto &s:p.sequences)ids.insert(s.id);
    for(const auto &a:p.hotkeys)if(a.actionType==QStringLiteral("sequence") && !ids.contains(a.sequenceId) && (a.enabled || !a.sequenceId.isEmpty())){emit errorOccurred(QStringLiteral("Imported shortcuts reference a missing sequence."));return {};}
    const QString original=p.id;p.id=newId();p.isDefault=false;p.activationKey=0;p.activationModifiers=0;cloneIdentifiers(p,original);
    for(auto &a:p.hotkeys)a.enabled=false;
    for(auto &s:p.sequences)for(auto &step:s.steps)if(step.type==QStringLiteral("switchProfile") && step.targetProfileId!=p.id && !profileById(step.targetProfileId))step.enabled=false;
    if(!saveProfile(p))return {};
    beginInsertRows({},m_profiles.size(),m_profiles.size());m_profiles.append(p);endInsertRows();notifyProfileChanged(p.id);return p.id;
}
