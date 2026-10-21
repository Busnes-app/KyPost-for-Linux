#pragma once

#include "theme/SemanticColors.h"
#include "theme/ThemeManager.h"

#include <QColor>
#include <QObject>
#include <QStringList>

class SettingsStore;

// QML-facing wrapper around core::ThemeManager: the resolved palette as
// live QColor properties, semantic colours, shape radii, font families and
// the system-scaled type scale. Follows the desktop colour scheme (for the
// Follow system theme) and the application font (for the type scale).
// Registered as the `Theme` QML singleton instance in main.cpp -- QML must
// never construct its own.
class ThemeController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString themeName READ themeName NOTIFY themeChanged)
    Q_PROPERTY(QStringList themeNames READ themeNames CONSTANT)
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
    // Danger/warning affordances depend on the palette's lightness.
    Q_PROPERTY(QColor dangerActionFill READ dangerActionFill NOTIFY themeChanged)
    Q_PROPERTY(QColor dangerActionBorder READ dangerActionBorder NOTIFY themeChanged)
    Q_PROPERTY(QColor dangerActionText READ dangerActionText NOTIFY themeChanged)
    Q_PROPERTY(QColor warningActionFill READ warningActionFill NOTIFY themeChanged)
    Q_PROPERTY(QColor warningActionBorder READ warningActionBorder NOTIFY themeChanged)
    Q_PROPERTY(QColor warningActionText READ warningActionText NOTIFY themeChanged)
    // Theme-invariant semantic colours and shapes.
    Q_PROPERTY(QColor dangerColor READ dangerColor CONSTANT)
    Q_PROPERTY(QColor dangerBorderColor READ dangerBorderColor CONSTANT)
    Q_PROPERTY(QColor dangerFillColor READ dangerFillColor CONSTANT)
    Q_PROPERTY(QColor warningColor READ warningColor CONSTANT)
    Q_PROPERTY(QColor successBorderColor READ successBorderColor CONSTANT)
    Q_PROPERTY(QColor successTextColor READ successTextColor CONSTANT)
    Q_PROPERTY(QColor successFillColor READ successFillColor CONSTANT)
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

    QString themeName() const;
    QStringList themeNames() const;

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

    QColor dangerActionFill() const;
    QColor dangerActionBorder() const;
    QColor dangerActionText() const;
    QColor warningActionFill() const;
    QColor warningActionBorder() const;
    QColor warningActionText() const;

    QColor dangerColor() const;
    QColor dangerBorderColor() const;
    QColor dangerFillColor() const;
    QColor warningColor() const;
    QColor successBorderColor() const;
    QColor successTextColor() const;
    QColor successFillColor() const;

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

    Q_INVOKABLE void setTheme(const QString& name);
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
    static QColor fromSemantic(const SemanticColor& color);
    static bool systemDarkNow();
    static double systemFontScale();
    int scaled(int sp) const;
    SemanticColors::Affordance affordance(SemanticColors::Hue hue) const;

    ThemeManager m_manager; // core::ThemeManager, owns the SettingsStore&
    double m_fontScale;
};
