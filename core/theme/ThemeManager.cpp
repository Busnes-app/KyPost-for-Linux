#include "theme/ThemeManager.h"

#include "stores/SettingsStore.h"

ThemeManager::ThemeManager(SettingsStore& settingsStore, bool systemDark)
    : m_settingsStore(settingsStore)
    , m_systemDark(systemDark)
{
    // The saved id is untrusted: allowlist it, and never write a fallback
    // back on read -- only setTheme() persists.
    const QString name = m_settingsStore.themeId();
    m_themeName = AppTheme::themeNames().contains(name) ? name : AppTheme::defaultThemeName();
}

QString ThemeManager::themeName() const
{
    return m_themeName;
}

ThemePalette ThemeManager::palette() const
{
    return AppTheme::palette(m_themeName, m_systemDark);
}

void ThemeManager::setTheme(const QString& name)
{
    if (!AppTheme::themeNames().contains(name))
        return;

    m_themeName = name;
    m_settingsStore.setThemeId(m_themeName);
}

void ThemeManager::setSystemDark(bool systemDark)
{
    m_systemDark = systemDark;
}
