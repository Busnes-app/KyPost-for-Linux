#pragma once

#include "theme/AppTheme.h"
#include "theme/ThemePalette.h"

#include <QString>

class SettingsStore; // forward-declared, core/stores/SettingsStore.h

// Resolves the saved theme selection against the system colour scheme.
// themeName() is the selection (may be "Follow system"); palette() is what
// it paints. Writes back through SettingsStore only from setTheme().
class ThemeManager
{
public:
    ThemeManager(SettingsStore& settingsStore, bool systemDark);

    QString themeName() const;
    ThemePalette palette() const;

    // No-op if `name` is not one of AppTheme::themeNames(); on success,
    // persists via settingsStore.setThemeId(name).
    void setTheme(const QString& name);
    void setSystemDark(bool systemDark);

private:
    SettingsStore& m_settingsStore;
    QString m_themeName;
    bool m_systemDark;
};
