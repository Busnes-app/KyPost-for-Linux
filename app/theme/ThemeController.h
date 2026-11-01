#pragma once

#include "theme/ThemeManager.h"

#include <QColor>
#include <QObject>
#include <QVariantList>

#include <functional>

class SettingsStore;

// QML-facing wrapper around core::ThemeManager: the resolved palette as
// live QColor properties, semantic colours, shape radii, font families and
// the system-scaled type scale. Follows the desktop colour scheme (for the
// System mode) and the application font (for the type scale).
// Registered as the `Theme` QML singleton instance in main.cpp -- QML must
// never construct its own.
class ThemeController : public QObject
{
    Q_OBJECT
    // The selection: a pair ID (AppTheme::pairs()) and "system", "light" or "dark".
    Q_PROPERTY(QString pairId READ pairId NOTIFY themeChanged)
    Q_PROPERTY(QString mode READ mode NOTIFY themeChanged)
    // The half in effect, e.g. "Busnes Dark".
    Q_PROPERTY(QString halfName READ halfName NOTIFY themeChanged)
    // Every pair for the picker: {id, name, light, dark}, each half a map of
    // bg, panel, ink, inkStrong, accent and line.
    Q_PROPERTY(QVariantList pairs READ pairs CONSTANT)
    Q_PROPERTY(QColor bg READ bg NOTIFY themeChanged)
    Q_PROPERTY(QColor panel READ panel NOTIFY themeChanged)
    Q_PROPERTY(QColor ink READ ink NOTIFY themeChanged)
    Q_PROPERTY(QColor inkStrong READ inkStrong NOTIFY themeChanged)
    Q_PROPERTY(QColor accent READ accent NOTIFY themeChanged)
    Q_PROPERTY(QColor accentSoft READ accentSoft NOTIFY themeChanged)
    Q_PROPERTY(QColor line READ line NOTIFY themeChanged)
    Q_PROPERTY(QColor avatarStart READ avatarStart NOTIFY themeChanged)
    Q_PROPERTY(QColor avatarEnd READ avatarEnd NOTIFY themeChanged)
    Q_PROPERTY(QColor avatarBorder READ avatarBorder NOTIFY themeChanged)
    Q_PROPERTY(QColor readableOnAccent READ readableOnAccent NOTIFY themeChanged)
    // A selected row: panel blended 30 % toward accent (Android's selected card).
    Q_PROPERTY(QColor selection READ selection NOTIFY themeChanged)
    Q_PROPERTY(bool isLight READ isLight NOTIFY themeChanged)
    // Status colours, per half (ky-ui 0.8.0). The plain colour is for text,
    // dots and icons; *Soft is its pill/banner fill; *Border its outline.
    // Pair a status colour with a word and a dot or icon, never colour alone.
    Q_PROPERTY(QColor danger READ danger NOTIFY themeChanged)
    Q_PROPERTY(QColor dangerSoft READ dangerSoft NOTIFY themeChanged)
    Q_PROPERTY(QColor dangerBorder READ dangerBorder NOTIFY themeChanged)
    Q_PROPERTY(QColor warning READ warning NOTIFY themeChanged)
    Q_PROPERTY(QColor warningSoft READ warningSoft NOTIFY themeChanged)
    Q_PROPERTY(QColor warningBorder READ warningBorder NOTIFY themeChanged)
    Q_PROPERTY(QColor success READ success NOTIFY themeChanged)
    Q_PROPERTY(QColor successSoft READ successSoft NOTIFY themeChanged)
    Q_PROPERTY(QColor successBorder READ successBorder NOTIFY themeChanged)
    // Shapes.
    Q_PROPERTY(int shapeField READ shapeField CONSTANT)
    Q_PROPERTY(int shapeButton READ shapeButton CONSTANT)
    Q_PROPERTY(int shapePanel READ shapePanel CONSTANT)
    Q_PROPERTY(int shapeSheet READ shapeSheet CONSTANT)
    Q_PROPERTY(int shapeEmptyState READ shapeEmptyState CONSTANT)
    Q_PROPERTY(int fieldStroke READ fieldStroke CONSTANT)
    Q_PROPERTY(QString fontUi READ fontUi CONSTANT)
    Q_PROPERTY(QString fontMono READ fontMono CONSTANT)
    // Type scale in logical px: Android sp x the system font size / 10 pt.
    Q_PROPERTY(double fontScale READ fontScale NOTIFY fontScaleChanged)
    Q_PROPERTY(int fontWordmark READ fontWordmark NOTIFY fontScaleChanged)
    Q_PROPERTY(int fontSubject READ fontSubject NOTIFY fontScaleChanged)
    Q_PROPERTY(int fontBody READ fontBody NOTIFY fontScaleChanged)
    Q_PROPERTY(int fontSender READ fontSender NOTIFY fontScaleChanged)
    Q_PROPERTY(int fontPreview READ fontPreview NOTIFY fontScaleChanged)
    Q_PROPERTY(int fontDate READ fontDate NOTIFY fontScaleChanged)
    Q_PROPERTY(int fontEyebrow READ fontEyebrow NOTIFY fontScaleChanged)
    Q_PROPERTY(double eyebrowLetterSpacing READ eyebrowLetterSpacing NOTIFY fontScaleChanged)

public:
    explicit ThemeController(SettingsStore& settingsStore, QObject* parent = nullptr);

    QString pairId() const;
    QString mode() const;
    QString halfName() const;
    QVariantList pairs() const;

    QColor bg() const;
    QColor panel() const;
    QColor ink() const;
    QColor inkStrong() const;
    QColor accent() const;
    QColor accentSoft() const;
    QColor line() const;
    QColor avatarStart() const;
    QColor avatarEnd() const;
    QColor avatarBorder() const;
    QColor readableOnAccent() const;
    QColor selection() const;
    bool isLight() const;

    QColor danger() const;
    QColor dangerSoft() const;
    QColor dangerBorder() const;
    QColor warning() const;
    QColor warningSoft() const;
    QColor warningBorder() const;
    QColor success() const;
    QColor successSoft() const;
    QColor successBorder() const;

    int shapeField() const;
    int shapeButton() const;
    int shapePanel() const;
    int shapeSheet() const;
    int shapeEmptyState() const;
    int fieldStroke() const;

    QString fontUi() const;   // "Space Grotesk" -- app/resources/fonts.qrc
    QString fontMono() const; // "IBM Plex Mono" -- app/resources/fonts.qrc

    double fontScale() const;
    int fontWordmark() const;
    int fontSubject() const;
    int fontBody() const;
    int fontSender() const;
    int fontPreview() const;
    int fontDate() const;
    int fontEyebrow() const;
    double eyebrowLetterSpacing() const;

    // No-op for an unknown pair ID or mode. Both persist the selection.
    Q_INVOKABLE void setPair(const QString& pairId);
    Q_INVOKABLE void setMode(const QString& mode);
    // Black or white text for an opaque `background` (Android readableOn()).
    Q_INVOKABLE QColor readableOn(const QColor& background) const;
    // What the platform colour-scheme signal calls.
    void setSystemDark(bool systemDark);

signals:
    void themeChanged();
    void fontScaleChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    static QColor fromHex(quint32 rgbHex);
    static bool systemDarkNow();
    static double systemFontScale();
    int scaled(int sp) const;
    void changeSelection(const std::function<void()>& change);

    ThemeManager m_manager; // core::ThemeManager, owns the SettingsStore&
    double m_fontScale;
};
