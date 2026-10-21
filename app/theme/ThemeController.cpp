#include "theme/ThemeController.h"

#include "theme/Shape.h"
#include "theme/TypeScale.h"

#include <QEvent>
#include <QFont>
#include <QGuiApplication>
#include <QPalette>
#include <QStyleHints>
#include <QtMath>

ThemeController::ThemeController(SettingsStore& settingsStore, QObject* parent)
    : QObject(parent)
    , m_manager(settingsStore, systemDarkNow())
    , m_fontScale(systemFontScale())
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            [this] { setSystemDark(systemDarkNow()); });
#endif
    // Font changes, and palette changes for the window-colour fallback.
    qApp->installEventFilter(this);
}

bool ThemeController::systemDarkNow()
{
    AppTheme::SystemScheme hint = AppTheme::SystemScheme::Unknown;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    switch (QGuiApplication::styleHints()->colorScheme()) {
    case Qt::ColorScheme::Dark:
        hint = AppTheme::SystemScheme::Dark;
        break;
    case Qt::ColorScheme::Light:
        hint = AppTheme::SystemScheme::Light;
        break;
    default:
        break;
    }
#endif
    const QRgb window = QGuiApplication::palette().color(QPalette::Window).rgb();
    return AppTheme::systemPrefersDark(hint, window & 0xFFFFFF);
}

double ThemeController::systemFontScale()
{
    const QFont font = QGuiApplication::font();
    return TypeScale::scaleFactor(font.pointSizeF(), font.pixelSize());
}

bool ThemeController::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == qApp) {
        if (event->type() == QEvent::ApplicationPaletteChange) {
            setSystemDark(systemDarkNow());
        } else if (event->type() == QEvent::ApplicationFontChange) {
            const double scale = systemFontScale();
            if (!qFuzzyCompare(scale, m_fontScale)) {
                m_fontScale = scale;
                emit fontScaleChanged();
            }
        }
    }
    return QObject::eventFilter(watched, event);
}

void ThemeController::setSystemDark(bool systemDark)
{
    const ThemePalette before = m_manager.palette();
    m_manager.setSystemDark(systemDark);
    if (m_manager.palette() != before)
        emit themeChanged();
}

SemanticColors::Affordance ThemeController::affordance(SemanticColors::Hue hue) const
{
    return SemanticColors::affordance(hue, m_manager.palette());
}

int ThemeController::scaled(int sp) const
{
    return qRound(sp * m_fontScale);
}

QColor ThemeController::fromHex(quint32 rgbHex)
{
    return QColor(static_cast<int>((rgbHex >> 16) & 0xFF),
                  static_cast<int>((rgbHex >> 8) & 0xFF),
                  static_cast<int>(rgbHex & 0xFF));
}

QColor ThemeController::fromSemantic(const SemanticColor& color)
{
    QColor qcolor = fromHex(color.rgb);
    qcolor.setAlphaF(static_cast<float>(color.alpha));
    return qcolor;
}

QString ThemeController::themeName() const
{
    return m_manager.themeName();
}

QStringList ThemeController::themeNames() const
{
    return AppTheme::themeNames();
}

QColor ThemeController::bg() const
{
    return fromHex(m_manager.palette().bg);
}

QColor ThemeController::panel() const
{
    return fromHex(m_manager.palette().panel);
}

QColor ThemeController::ink() const
{
    return fromHex(m_manager.palette().ink);
}

QColor ThemeController::inkStrong() const
{
    return fromHex(m_manager.palette().inkStrong);
}

QColor ThemeController::accent() const
{
    return fromHex(m_manager.palette().accent);
}

QColor ThemeController::accentSoft() const
{
    return fromHex(m_manager.palette().accentSoft);
}

QColor ThemeController::line() const
{
    return fromHex(m_manager.palette().line);
}

QColor ThemeController::avatarStart() const
{
    return fromHex(m_manager.palette().avatarStart);
}

QColor ThemeController::avatarEnd() const
{
    return fromHex(m_manager.palette().avatarEnd);
}

QColor ThemeController::avatarBorder() const
{
    return fromHex(m_manager.palette().avatarBorder);
}

QColor ThemeController::readableOnAccent() const
{
    return fromHex(m_manager.palette().readableOnAccent());
}

bool ThemeController::isLight() const
{
    return m_manager.palette().isLight();
}

QColor ThemeController::dangerActionFill() const
{
    return fromSemantic(affordance(SemanticColors::Hue::Danger).fill);
}

QColor ThemeController::dangerActionBorder() const
{
    return fromSemantic(affordance(SemanticColors::Hue::Danger).border);
}

QColor ThemeController::dangerActionText() const
{
    return fromSemantic(affordance(SemanticColors::Hue::Danger).text);
}

QColor ThemeController::warningActionFill() const
{
    return fromSemantic(affordance(SemanticColors::Hue::Warning).fill);
}

QColor ThemeController::warningActionBorder() const
{
    return fromSemantic(affordance(SemanticColors::Hue::Warning).border);
}

QColor ThemeController::warningActionText() const
{
    return fromSemantic(affordance(SemanticColors::Hue::Warning).text);
}

QColor ThemeController::dangerColor() const
{
    return fromSemantic(SemanticColors::danger);
}

QColor ThemeController::dangerBorderColor() const
{
    return fromSemantic(SemanticColors::dangerBorder);
}

QColor ThemeController::dangerFillColor() const
{
    return fromSemantic(SemanticColors::dangerFill);
}

QColor ThemeController::warningColor() const
{
    return fromSemantic(SemanticColors::warning);
}

QColor ThemeController::successBorderColor() const
{
    return fromSemantic(SemanticColors::successBorder);
}

QColor ThemeController::successTextColor() const
{
    return fromSemantic(SemanticColors::successText);
}

QColor ThemeController::successFillColor() const
{
    return fromSemantic(SemanticColors::successFill);
}

int ThemeController::shapeField() const
{
    return Shape::field;
}

int ThemeController::shapeButton() const
{
    return Shape::button;
}

int ThemeController::shapePanel() const
{
    return Shape::panel;
}

int ThemeController::shapeSheet() const
{
    return Shape::sheet;
}

int ThemeController::shapeEmptyState() const
{
    return Shape::emptyState;
}

int ThemeController::fieldStroke() const
{
    return Shape::fieldStroke;
}

QString ThemeController::fontUi() const
{
    return QStringLiteral("Space Grotesk");
}

QString ThemeController::fontMono() const
{
    return QStringLiteral("IBM Plex Mono");
}

double ThemeController::fontScale() const
{
    return m_fontScale;
}

int ThemeController::fontWordmark() const
{
    return scaled(TypeScale::wordmark);
}

int ThemeController::fontSubject() const
{
    return scaled(TypeScale::subject);
}

int ThemeController::fontSender() const
{
    return scaled(TypeScale::sender);
}

int ThemeController::fontPreview() const
{
    return scaled(TypeScale::preview);
}

int ThemeController::fontDate() const
{
    return scaled(TypeScale::date);
}

int ThemeController::fontEyebrow() const
{
    return scaled(TypeScale::eyebrow);
}

double ThemeController::eyebrowLetterSpacing() const
{
    return TypeScale::eyebrowLetterSpacingEm * fontEyebrow();
}

void ThemeController::setTheme(const QString& name)
{
    const QString before = m_manager.themeName();
    m_manager.setTheme(name);
    if (m_manager.themeName() != before)
        emit themeChanged();
}
