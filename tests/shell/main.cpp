// Qt Quick Test runner for the AppShell root itself. QmlTests can only build
// leaf components; these tests load the whole shell against the screenshot
// fixtures (tests/screenshots/fakes) so root-level handlers actually run.
#include "Fixtures.h"
#include "stores/SettingsStore.h"

#include <KLocalizedQmlContext>
#include <KLocalizedString>

#include <QQmlEngine>
#include <QTemporaryDir>
#include <QtQuickTest>

class Setup : public QObject
{
    Q_OBJECT

public:
    Setup()
        : m_settings(m_dir.filePath(QStringLiteral("settings.ini")))
    {
        m_settings.setThemeId(QStringLiteral("Busnes Light"));
    }

public slots:
    void applicationAvailable()
    {
        KLocalizedString::setApplicationDomain("kypost");
        Fixtures::pinFont();
        m_theme = new ThemeController(m_settings, this);
        m_run.qrDataUrl = Fixtures::fakeQr();
    }

    void qmlEngineAvailable(QQmlEngine* engine)
    {
        KLocalization::setupLocalizedContext(engine);
        Fixtures::registerAll(*engine, m_run, *m_theme);
    }

private:
    QTemporaryDir m_dir;
    SettingsStore m_settings;
    ThemeController* m_theme = nullptr;
    ScreenRun m_run;
};

QUICK_TEST_MAIN_WITH_SETUP(kypost_shell, Setup)

#include "main.moc"
