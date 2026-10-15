#include "ActionManager.h"

#include "../app/AppDataPaths.h"
#include "../app/JsonValidation.h"
#include <QSet>
#include "../profiles/ProfileManager.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>

namespace {
const QString kLaunchApplication = QStringLiteral("launchApplication");
const QString kOpenFolder = QStringLiteral("openFolder");
const QString kOpenFile = QStringLiteral("openFile");
const QString kOpenUrl = QStringLiteral("openUrl");
const QString kSaveScreenshot = QStringLiteral("saveScreenshot");
const QString kSwitchProfile = QStringLiteral("switchProfile");
const QString kWait = QStringLiteral("wait");
constexpr int kMaximumSequences = 100;
constexpr int kMaximumSteps = 100;
constexpr int kMaximumWaitMs = 600000;

bool isKnownStepType(const QString &type)
{
    return type == kLaunchApplication || type == kOpenFolder || type == kOpenFile ||
           type == kOpenUrl || type == kSaveScreenshot || type == kSwitchProfile || type == kWait;
}
}

ActionManager::ActionManager(ProfileManager *profiles, QObject *parent)
    : QAbstractListModel(parent), m_profiles(profiles)
{
    load();
    connect(m_profiles,&ProfileManager::profilesChanged,this,&ActionManager::refreshModel);
    connect(m_profiles,&ProfileManager::activeProfileChanged,this,&ActionManager::refreshModel);
    refreshModel();
}

int ActionManager::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_sequences.size();
}

QVariant ActionManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_sequences.size())
        return {};
    const ActionSequence &sequence = m_sequences.at(index.row());
    switch (role) {
    case IdRole: return sequence.id;
    case NameRole: return sequence.name;
    case EnabledRole: return sequence.enabled;
    case StepCountRole: return sequence.steps.size();
    default: return {};
    }
}

QHash<int, QByteArray> ActionManager::roleNames() const
{
    return {{IdRole, "sequenceId"}, {NameRole, "name"},
            {EnabledRole, "sequenceEnabled"}, {StepCountRole, "stepCount"}};
}

bool ActionManager::contains(const QString &sequenceId) const
{
    return indexOfSequence(sequenceId)>=0;
}

const ActionSequence *ActionManager::sequenceById(const QString &sequenceId) const
{
    for (const auto &profile : m_profiles->profiles())
        for (const auto &sequence : profile.sequences) if (sequence.id==sequenceId) return &sequence;
    for (const auto &sequence : m_profiles->globalProfile().sequences) if(sequence.id==sequenceId)return &sequence;
    return nullptr;
}

QVariantList ActionManager::sequenceOptions() const
{
    QVariantList result;
    for (const ActionSequence &sequence : m_sequences) {
        result.append(QVariantMap{{QStringLiteral("id"), sequence.id},
                                  {QStringLiteral("name"), sequence.name},
                                  {QStringLiteral("enabled"), sequence.enabled}});
    }
    return result;
}

QVariantMap ActionManager::sequenceInfo(const QString &sequenceId) const
{
    const ActionSequence *sequence = sequenceById(sequenceId);
    if (!sequence)
        return {};
    return {{QStringLiteral("id"), sequence->id}, {QStringLiteral("name"), sequence->name},
            {QStringLiteral("enabled"), sequence->enabled},
            {QStringLiteral("stepCount"), sequence->steps.size()},
            {QStringLiteral("validationError"), validationError(sequenceId)}};
}

QVariantList ActionManager::steps(const QString &sequenceId) const
{
    QVariantList result;
    const ActionSequence *sequence = sequenceById(sequenceId);
    if (!sequence)
        return result;
    result.reserve(sequence->steps.size());
    for (const ActionStep &step : sequence->steps)
        result.append(stepToVariant(step));
    return result;
}

QString ActionManager::validateSequence(const QString &sequenceId, QString *error) const
{
    const ActionSequence *sequence = sequenceById(sequenceId);
    QString reason;
    if (!sequence) {
        reason = QStringLiteral("That action sequence is unavailable.");
    } else if (!sequence->enabled) {
        reason = QStringLiteral("Enable this action sequence before running it.");
    } else {
        int enabledCount = 0;
        for (int i = 0; i < sequence->steps.size(); ++i) {
            const ActionStep &step = sequence->steps.at(i);
            if (!step.enabled)
                continue;
            ++enabledCount;
            const QString prefix = QStringLiteral("Step %1: ").arg(i + 1);
            if (step.type == kLaunchApplication) {
                const QFileInfo executable(step.path);
                if (step.path.trimmed().isEmpty() || !executable.exists() || !executable.isFile() || !executable.isExecutable())
                    reason = prefix + QStringLiteral("choose an existing application executable.");
            } else if (step.type == kOpenFolder) {
                const QFileInfo folder(step.path);
                if (step.path.trimmed().isEmpty() || !folder.exists() || !folder.isDir())
                    reason = prefix + QStringLiteral("choose an existing folder.");
            } else if (step.type == kOpenFile) {
                const QFileInfo file(step.path);
                if (step.path.trimmed().isEmpty() || !file.exists() || !file.isFile())
                    reason = prefix + QStringLiteral("choose an existing file.");
            } else if (step.type == kOpenUrl) {
                const QUrl url(step.url.trimmed());
                if (!url.isValid() || (url.scheme() != QStringLiteral("http") &&
                                       url.scheme() != QStringLiteral("https")) || url.host().isEmpty())
                    reason = prefix + QStringLiteral("enter a valid HTTP or HTTPS URL.");
            } else if (step.type == kSwitchProfile &&
                       (!m_profiles || step.targetProfileId==QStringLiteral("global") || !m_profiles->profileById(step.targetProfileId)))
                reason = prefix + QStringLiteral("choose an available profile.");
            else if (step.type == kWait && (step.delayMs < 0 || step.delayMs > kMaximumWaitMs))
                reason = prefix + QStringLiteral("wait must be between 0 and 600000 milliseconds.");
            else if (!isKnownStepType(step.type))
                reason = prefix + QStringLiteral("the action type is not supported.");
            if (!reason.isEmpty())
                break;
        }
        if (reason.isEmpty() && enabledCount == 0)
            reason = QStringLiteral("Add or enable at least one action step.");
    }
    if (error)
        *error = reason;
    return reason.isEmpty() ? sequenceId : QString();
}

QString ActionManager::createSequence(const QString &name)
{
    const QString normalized = name.trimmed();
    if (normalized.isEmpty() || m_sequences.size() >= kMaximumSequences) {
        emit errorOccurred(normalized.isEmpty() ? QStringLiteral("Enter a sequence name.")
                                               : QStringLiteral("The maximum of 100 sequences has been reached."));
        return {};
    }
    ActionSequence sequence;
    sequence.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    sequence.name = normalized;
    Profile profile=*m_profiles->profileById(scopeId());
    profile.sequences.append(sequence);
    return m_profiles->replaceProfile(profile)?sequence.id:QString();
}

bool ActionManager::renameSequence(const QString &sequenceId, const QString &name)
{
    const int row = indexOfSequence(sequenceId);
    const QString normalized = name.trimmed();
    if (row < 0 || normalized.isEmpty()) {
        emit errorOccurred(QStringLiteral("Enter a sequence name."));
        return false;
    }
    ActionSequence updated = m_sequences.at(row);
    updated.name = normalized;
    return commitChange(row, updated);
}

bool ActionManager::deleteSequence(const QString &sequenceId)
{
    const int row = indexOfSequence(sequenceId);
    if (row < 0)
        return false;
    QVector<Profile> scopes=m_profiles->profiles(); scopes.append(m_profiles->globalProfile());
    for(const auto &profile:scopes)for(const auto &binding:profile.hotkeys)
        if(binding.actionType==QStringLiteral("sequence") && binding.sequenceId==sequenceId) {
            emit errorOccurred(QStringLiteral("Remove the shortcut assignments to this sequence before deleting it."));return false;
        }
    Profile profile=*m_profiles->profileById(scopeId());profile.sequences.removeAt(row);
    return m_profiles->replaceProfile(profile);
}

bool ActionManager::setSequenceEnabled(const QString &sequenceId, bool enabled)
{
    const int row = indexOfSequence(sequenceId);
    if (row < 0)
        return false;
    ActionSequence updated = m_sequences.at(row);
    updated.enabled = enabled;
    return commitChange(row, updated);
}

QString ActionManager::addStep(const QString &sequenceId, const QString &type)
{
    const int row = indexOfSequence(sequenceId);
    if (row < 0 || !isKnownStepType(type))
        return {};
    ActionSequence updated = m_sequences.at(row);
    if (updated.steps.size() >= kMaximumSteps) {
        emit errorOccurred(QStringLiteral("A sequence can contain at most 100 steps."));
        return {};
    }
    ActionStep step;
    step.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    step.type = type;
    updated.steps.append(step);
    return commitChange(row, updated) ? step.id : QString();
}

bool ActionManager::updateStep(const QString &sequenceId, const QString &stepId,
                               const QVariantMap &changes)
{
    const int row = indexOfSequence(sequenceId);
    if (row < 0)
        return false;
    ActionSequence updated = m_sequences.at(row);
    const int stepIndex = indexOfStep(updated, stepId);
    if (stepIndex < 0)
        return false;
    ActionStep &step = updated.steps[stepIndex];
    if (changes.contains(QStringLiteral("path")))
        step.path = changes.value(QStringLiteral("path")).toString().trimmed();
    if (changes.contains(QStringLiteral("arguments")))
        step.arguments = changes.value(QStringLiteral("arguments")).toString();
    if (changes.contains(QStringLiteral("url")))
        step.url = changes.value(QStringLiteral("url")).toString().trimmed();
    if (changes.contains(QStringLiteral("targetProfileId")))
        step.targetProfileId = changes.value(QStringLiteral("targetProfileId")).toString();
    if (changes.contains(QStringLiteral("delayMs")))
        step.delayMs = changes.value(QStringLiteral("delayMs")).toInt();
    return commitChange(row, updated);
}

bool ActionManager::setStepEnabled(const QString &sequenceId, const QString &stepId, bool enabled)
{
    const int row = indexOfSequence(sequenceId);
    if (row < 0)
        return false;
    ActionSequence updated = m_sequences.at(row);
    const int stepIndex = indexOfStep(updated, stepId);
    if (stepIndex < 0)
        return false;
    updated.steps[stepIndex].enabled = enabled;
    return commitChange(row, updated);
}

bool ActionManager::moveStep(const QString &sequenceId, const QString &stepId, int direction)
{
    const int row = indexOfSequence(sequenceId);
    if (row < 0 || (direction != -1 && direction != 1))
        return false;
    ActionSequence updated = m_sequences.at(row);
    const int stepIndex = indexOfStep(updated, stepId);
    const int targetIndex = stepIndex + direction;
    if (stepIndex < 0 || targetIndex < 0 || targetIndex >= updated.steps.size())
        return false;
    updated.steps.swapItemsAt(stepIndex, targetIndex);
    return commitChange(row, updated);
}

bool ActionManager::removeStep(const QString &sequenceId, const QString &stepId)
{
    const int row = indexOfSequence(sequenceId);
    if (row < 0)
        return false;
    ActionSequence updated = m_sequences.at(row);
    const int stepIndex = indexOfStep(updated, stepId);
    if (stepIndex < 0)
        return false;
    updated.steps.removeAt(stepIndex);
    return commitChange(row, updated);
}

QString ActionManager::validationError(const QString &sequenceId) const
{
    QString error;
    validateSequence(sequenceId, &error);
    return error;
}

int ActionManager::indexOfSequence(const QString &sequenceId) const
{
    for (int i = 0; i < m_sequences.size(); ++i) {
        if (m_sequences.at(i).id == sequenceId)
            return i;
    }
    return -1;
}

int ActionManager::indexOfStep(const ActionSequence &sequence, const QString &stepId)
{
    for (int i = 0; i < sequence.steps.size(); ++i) {
        if (sequence.steps.at(i).id == stepId)
            return i;
    }
    return -1;
}

bool ActionManager::commitChange(int sequenceIndex, const ActionSequence &updated)
{
    if (sequenceIndex < 0 || sequenceIndex >= m_sequences.size())
        return false;
    Profile profile=*m_profiles->profileById(scopeId());profile.sequences[sequenceIndex]=updated;
    return m_profiles->replaceProfile(profile);
}

void ActionManager::refreshModel() {
    beginResetModel();
    const Profile *profile=m_profiles->profileById(scopeId());
    m_sequences=profile?profile->sequences:QVector<ActionSequence>();
    endResetModel();emit sequencesChanged();
}
QString ActionManager::scopeId() const {return m_globalScope?QStringLiteral("global"):m_profiles->activeProfileId();}
void ActionManager::setGlobalScope(bool global) {if(m_globalScope==global)return;m_globalScope=global;refreshModel();}
QString ActionManager::sequenceOwner(const QString &id) const {
    for(const auto &profile:m_profiles->profiles())for(const auto &sequence:profile.sequences)if(sequence.id==id)return profile.id;
    for(const auto &sequence:m_profiles->globalProfile().sequences)if(sequence.id==id)return QStringLiteral("global");
    return {};
}
void ActionManager::load() {
    // Version 1 used a shared actions file. Migrate each legacy profile atomically,
    // retaining both the source actions and a profile backup until all data is safe.
    QFile file(configPath());if(!file.exists())return;
    auto report=[this](const QString &message){m_startupMessages.append(message);emit errorOccurred(message);};
    if(!file.open(QIODevice::ReadOnly) || file.size()>4*1024*1024){report(QStringLiteral("Legacy actions cannot be read; the file is preserved."));return;}
    QJsonParseError error;const auto doc=QJsonDocument::fromJson(file.readAll(),&error);
    const auto root=doc.object();
    if(error.error!=QJsonParseError::NoError || root.value(QStringLiteral("schemaVersion")).toInt()!=1 || !root.value(QStringLiteral("sequences")).isArray()) {
        if(root.value(QStringLiteral("schemaVersion")).toInt()<=1)JsonValidation::backup(configPath(),QStringLiteral(".corrupt-"));
        report(QStringLiteral("Legacy action data is invalid or newer than supported. The original is preserved."));return;
    }
    QVector<ActionSequence> recovered;QSet<QString> ids;bool invalid=false;
    for(const auto &value:root.value(QStringLiteral("sequences")).toArray()) {
        ActionSequence sequence;
        if(!value.isObject() || !parseSequence(value.toObject(),&sequence) || ids.contains(sequence.id) || recovered.size()>=100){invalid=true;continue;}
        ids.insert(sequence.id);recovered.append(sequence);
    }
    if(invalid){JsonValidation::backup(configPath(),QStringLiteral(".corrupt-"));report(QStringLiteral("Invalid legacy action records were skipped; the original is preserved."));}
    const QVector<Profile> profiles=m_profiles->profiles();
    for(const Profile &stored:profiles) {
        if(!stored.legacyActions)continue;
        Profile profile=stored;profile.sequences=recovered;profile.legacyActions=false;
        QHash<QString,QString> remapped;
        for(auto &sequence:profile.sequences){const QString old=sequence.id;sequence.id=QUuid::createUuid().toString(QUuid::WithoutBraces);remapped.insert(old,sequence.id);for(auto &step:sequence.steps)step.id=QUuid::createUuid().toString(QUuid::WithoutBraces);}
        for(auto &binding:profile.hotkeys)if(binding.actionType==QStringLiteral("sequence")) {
            if(remapped.contains(binding.sequenceId))binding.sequenceId=remapped.value(binding.sequenceId);
            else {binding.enabled=false;binding.sequenceId.clear();report(QStringLiteral("A legacy shortcut referenced an unavailable sequence and was disabled. Assign a sequence or remove the shortcut."));}
        }
        const QString path=QDir(AppDataPaths::root()).filePath(QStringLiteral("profiles/%1.json").arg(profile.id));
        if(!JsonValidation::backup(path,QStringLiteral(".pre-sequences-")) || !m_profiles->replaceProfile(profile))report(QStringLiteral("A profile's sequence migration could not be saved. Its original was preserved."));
    }
}

QString ActionManager::configPath() const
{
    return QDir(AppDataPaths::root())
        .filePath(QStringLiteral("actions.json"));
}

QVariantMap ActionManager::stepToVariant(const ActionStep &step) const
{
    return {{QStringLiteral("stepId"), step.id}, {QStringLiteral("stepType"), step.type},
            {QStringLiteral("enabled"), step.enabled}, {QStringLiteral("path"), step.path},
            {QStringLiteral("arguments"), step.arguments}, {QStringLiteral("url"), step.url},
            {QStringLiteral("targetProfileId"), step.targetProfileId},
            {QStringLiteral("delayMs"), step.delayMs}};
}

QJsonObject ActionManager::serializeSequence(const ActionSequence &sequence)
{
    QJsonArray steps;
    for (const ActionStep &step : sequence.steps) {
        steps.append(QJsonObject{{QStringLiteral("id"), step.id}, {QStringLiteral("type"), step.type},
                                 {QStringLiteral("enabled"), step.enabled}, {QStringLiteral("path"), step.path},
                                 {QStringLiteral("arguments"), step.arguments}, {QStringLiteral("url"), step.url},
                                 {QStringLiteral("targetProfileId"), step.targetProfileId},
                                 {QStringLiteral("delayMs"), step.delayMs}});
    }
    return {{QStringLiteral("id"), sequence.id}, {QStringLiteral("name"), sequence.name},
            {QStringLiteral("enabled"), sequence.enabled}, {QStringLiteral("steps"), steps}};
}

bool ActionManager::parseSequence(const QJsonObject &object, ActionSequence *sequence)
{
    sequence->id = object.value(QStringLiteral("id")).toString();
    sequence->name = object.value(QStringLiteral("name")).toString().trimmed();
    sequence->enabled = object.value(QStringLiteral("enabled")).toBool(true);
    if (!JsonValidation::safeId(sequence->id) || sequence->name.isEmpty() || sequence->name.size()>200 || !JsonValidation::boolean(object,QStringLiteral("enabled")) ||
        !object.value(QStringLiteral("steps")).isArray())
        return false;
    const QJsonArray steps = object.value(QStringLiteral("steps")).toArray();
    if (steps.size() > kMaximumSteps)
        return false;
    QSet<QString> stepIds;
    for (const QJsonValue &value : steps) {
        if (!value.isObject())
            return false;
        const QJsonObject item = value.toObject();
        for(const QString &key:{QStringLiteral("id"),QStringLiteral("type"),QStringLiteral("path"),QStringLiteral("arguments"),QStringLiteral("url"),QStringLiteral("targetProfileId")})if(!JsonValidation::string(item,key))return false;
        if(!JsonValidation::boolean(item,QStringLiteral("enabled")) || !JsonValidation::integer(item,QStringLiteral("delayMs"),0,2147483647))return false;
        ActionStep step;
        step.id = item.value(QStringLiteral("id")).toString();
        step.type = item.value(QStringLiteral("type")).toString();
        step.enabled = item.value(QStringLiteral("enabled")).toBool(true);
        step.path = item.value(QStringLiteral("path")).toString();
        step.arguments = item.value(QStringLiteral("arguments")).toString();
        step.url = item.value(QStringLiteral("url")).toString();
        step.targetProfileId = item.value(QStringLiteral("targetProfileId")).toString();
        step.delayMs = item.value(QStringLiteral("delayMs")).toInt(1000);
        if (!JsonValidation::safeId(step.id) || stepIds.contains(step.id) || !isKnownStepType(step.type))
            return false;
        stepIds.insert(step.id);
        sequence->steps.append(step);
    }
    return true;
}
