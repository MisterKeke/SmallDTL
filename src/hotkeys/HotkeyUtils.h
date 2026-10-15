#pragma once

#include <QKeySequence>
#include <QString>
#include <QVector>

#include "../profiles/ProfileManager.h"

namespace HotkeyUtils {
inline QString formatShortcut(int key, int modifiers)
{
    if (key == 0)
        return QStringLiteral("Not assigned");
    return QKeySequence(key | modifiers).toString(QKeySequence::NativeText);
}

inline bool sameShortcut(int firstKey, int firstModifiers, int secondKey, int secondModifiers)
{
    return firstKey != 0 && firstKey == secondKey && firstModifiers == secondModifiers;
}

inline bool assignmentConflict(const QVector<HotkeyAssignment> &assignments, int key, int modifiers,
                               const QString &exceptAssignmentId = {})
{
    for (const HotkeyAssignment &assignment : assignments) {
        if (assignment.id != exceptAssignmentId && assignment.enabled &&
            sameShortcut(key, modifiers, assignment.key, assignment.modifiers))
            return true;
    }
    return false;
}

inline bool profileActivationConflict(const QVector<Profile> &profiles, int key, int modifiers,
                                      const QString &exceptProfileId = {})
{
    for (const Profile &profile : profiles) {
        if (profile.id != exceptProfileId &&
            sameShortcut(key, modifiers, profile.activationKey, profile.activationModifiers))
            return true;
    }
    return false;
}

inline bool anyProfileAssignmentConflict(const QVector<Profile> &profiles, int key, int modifiers)
{
    for (const Profile &profile : profiles) {
        if (assignmentConflict(profile.hotkeys, key, modifiers))
            return true;
    }
    return false;
}
}
