#pragma once

#include "theme/AppTheme.h"
#include "theme/ThemePalette.h"

#include <QString>

class SettingsStore; // forward-declared, core/stores/SettingsStore.h

// Resolves the saved theme selection (a pair plus System/Light/Dark) against
// the system colour scheme. The saved value stays one string under the same
// settings key: AppTheme::savedId(). Writes back through SettingsStore only
// from setPair()/setMode().
class ThemeManager
{
public:
    ThemeManager(SettingsStore& settingsStore, bool systemDark);

    AppTheme::Selection selection() const;
    ThemePalette palette() const;
    // The half palette() paints, e.g. "Busnes Dark".
    const AppTheme::Half& half() const;

    // No-op unless `pairId` is one of AppTheme::pairs(); keeps the mode.
    void setPair(const QString& pairId);
    void setMode(AppTheme::Mode mode);
    void setSystemDark(bool systemDark);

private:
    void save();

    SettingsStore& m_settingsStore;
    AppTheme::Selection m_selection;
    bool m_systemDark;
};
