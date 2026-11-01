#include "theme/ThemeManager.h"

#include "stores/SettingsStore.h"

ThemeManager::ThemeManager(SettingsStore& settingsStore, bool systemDark)
    : m_settingsStore(settingsStore)
    // The saved id is untrusted: selectionFor() maps an old palette name to
    // its pair and anything unknown to Busnes, and a migrated value is never
    // written back on read -- only setPair()/setMode() persist.
    , m_selection(AppTheme::selectionFor(settingsStore.themeId()))
    , m_systemDark(systemDark)
{
}

AppTheme::Selection ThemeManager::selection() const
{
    return m_selection;
}

ThemePalette ThemeManager::palette() const
{
    return AppTheme::palette(m_selection, m_systemDark);
}

const AppTheme::Half& ThemeManager::half() const
{
    return AppTheme::resolve(m_selection, m_systemDark);
}

void ThemeManager::setPair(const QString& pairId)
{
    if (!AppTheme::findPair(pairId))
        return;
    m_selection.pairId = pairId;
    save();
}

void ThemeManager::setMode(AppTheme::Mode mode)
{
    m_selection.mode = mode;
    save();
}

void ThemeManager::setSystemDark(bool systemDark)
{
    m_systemDark = systemDark;
}

void ThemeManager::save()
{
    m_settingsStore.setThemeId(AppTheme::savedId(m_selection));
}
