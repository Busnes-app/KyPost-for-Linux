#pragma once
#include "ThemePalette.h"
#include <QList>
#include <QString>
#include <QStringList>

// The Ky suite theme catalog, ky-ui 0.11.0 src/themes.js and src/tokens.css:
// twelve halves in six light/dark pairs. IDs, names and aliases are what
// settings hold; never rename one.
namespace AppTheme
{
    struct Half
    {
        QString id;          // ky-ui catalog ID, e.g. "white-cliffs"
        QString name;        // "Ocean Light"
        bool dark;
        QString pairId;      // "ocean-pair"
        QStringList aliases; // earlier names, e.g. "White Cliffs"
        ThemePalette palette;
    };

    struct Pair
    {
        QString id;   // ends in "-pair", which no half ID does
        QString name; // "Ocean"
        QString lightId;
        QString darkId;
    };

    // ky-ui THEME_PAIRS order: Busnes, Matter, Ocean, Forest, Ky, Nord.
    const QList<Pair>& pairs();
    // ky-ui THEME_CATALOG order, light then dark per pair.
    const QList<Half>& halves();
    const Half* findHalf(const QString& idNameOrAlias);
    const Pair* findPair(const QString& pairId);

    QString defaultPairId(); // "busnes-pair"

    // System follows the desktop's colour scheme; Light and Dark force a half.
    enum class Mode { System, Light, Dark };

    struct Selection
    {
        QString pairId;
        Mode mode;

        bool operator==(const Selection&) const = default;
    };

    // A saved value as a selection, ky-ui's rules (briefs/pairs-spec.md):
    // a pair ID follows the system; a half's ID, name or alias forces that
    // half, so "Busnes Light" or the old "Ocean" keeps its look; anything
    // else ("Follow system", a removed palette such as "Sky", garbage) is
    // Busnes following the system.
    Selection selectionFor(const QString& saved);

    // What to save for a selection: the pair ID under System, else the
    // forced half's ID. selectionFor(savedId(s)) == s.
    QString savedId(const Selection& selection);

    // The half a selection paints.
    const Half& resolve(const Selection& selection, bool systemDark);
    ThemePalette palette(const Selection& selection, bool systemDark);
    ThemePalette palette(const QString& saved, bool systemDark);

    enum class SystemScheme { Unknown, Light, Dark };

    // A Light/Dark hint decides; Unknown (Qt 5, Qt < 6.5, or a desktop that
    // does not say) falls back to the platform window colour's lightness.
    bool systemPrefersDark(SystemScheme hint, quint32 windowRgb);
}
