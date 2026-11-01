#include "theme/ThemeController.h"

#include "theme/SemanticColors.h"
#include "theme/Shape.h"
#include "theme/TypeScale.h"

#include <QEvent>
#include <QFont>
#include <QGuiApplication>
#include <QHash>
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

void ThemeController::changeSelection(const std::function<void()>& change)
{
    const AppTheme::Selection before = m_manager.selection();
    change();
    if (m_manager.selection() != before)
        emit themeChanged();
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


QString ThemeController::pairId() const
{
    return m_manager.selection().pairId;
}

QString ThemeController::mode() const
{
    switch (m_manager.selection().mode) {
    case AppTheme::Mode::Light:
        return QStringLiteral("light");
    case AppTheme::Mode::Dark:
        return QStringLiteral("dark");
    case AppTheme::Mode::System:
        break;
    }
    return QStringLiteral("system");
}

QString ThemeController::halfName() const
{
    return m_manager.half().name;
}

QVariantList ThemeController::pairs() const
{
    const auto preview = [](const QString& halfId) {
        const ThemePalette p = AppTheme::findHalf(halfId)->palette;
        return QVariantMap{{QStringLiteral("bg"), fromHex(p.bg)},
                           {QStringLiteral("panel"), fromHex(p.panel)},
                           {QStringLiteral("ink"), fromHex(p.ink)},
                           {QStringLiteral("inkStrong"), fromHex(p.inkStrong)},
                           {QStringLiteral("accent"), fromHex(p.accent)},
                           {QStringLiteral("line"), fromHex(p.line)}};
    };
    QVariantList list;
    for (const AppTheme::Pair& pair : AppTheme::pairs()) {
        list.append(QVariantMap{{QStringLiteral("id"), pair.id},
                                {QStringLiteral("name"), pair.name},
                                {QStringLiteral("light"), preview(pair.lightId)},
                                {QStringLiteral("dark"), preview(pair.darkId)}});
    }
    return list;
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
    return fromHex(m_manager.palette().onAccent);
}

QColor ThemeController::selection() const
{
    const ThemePalette p = m_manager.palette();
    return fromHex(SemanticColors::blend(p.panel, p.accent, 0.3));
}

bool ThemeController::isLight() const
{
    return m_manager.palette().isLight();
}

QColor ThemeController::danger() const
{
    return fromHex(m_manager.palette().danger);
}

QColor ThemeController::dangerSoft() const
{
    const ThemePalette p = m_manager.palette();
    return fromHex(p.statusSoft(p.danger));
}

QColor ThemeController::dangerBorder() const
{
    const ThemePalette p = m_manager.palette();
    return fromHex(p.statusBorder(p.danger));
}

QColor ThemeController::warning() const
{
    return fromHex(m_manager.palette().warning);
}

QColor ThemeController::warningSoft() const
{
    const ThemePalette p = m_manager.palette();
    return fromHex(p.statusSoft(p.warning));
}

QColor ThemeController::warningBorder() const
{
    const ThemePalette p = m_manager.palette();
    return fromHex(p.statusBorder(p.warning));
}

QColor ThemeController::success() const
{
    return fromHex(m_manager.palette().success);
}

QColor ThemeController::successSoft() const
{
    const ThemePalette p = m_manager.palette();
    return fromHex(p.statusSoft(p.success));
}

QColor ThemeController::successBorder() const
{
    const ThemePalette p = m_manager.palette();
    return fromHex(p.statusBorder(p.success));
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

int ThemeController::fontBody() const
{
    return scaled(TypeScale::body);
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

QColor ThemeController::readableOn(const QColor& background) const
{
    return fromHex(ThemePalette::readableOn(background.rgb() & 0xFFFFFF));
}

void ThemeController::setPair(const QString& pairId)
{
    changeSelection([&] { m_manager.setPair(pairId); });
}

void ThemeController::setMode(const QString& mode)
{
    static const QHash<QString, AppTheme::Mode> modes = {
        {QStringLiteral("system"), AppTheme::Mode::System},
        {QStringLiteral("light"), AppTheme::Mode::Light},
        {QStringLiteral("dark"), AppTheme::Mode::Dark},
    };
    const auto it = modes.constFind(mode);
    if (it == modes.constEnd())
        return;
    changeSelection([&] { m_manager.setMode(it.value()); });
}
