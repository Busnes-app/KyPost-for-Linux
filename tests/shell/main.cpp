// Qt Quick Test runner for the AppShell root itself. QmlTests can only build
// leaf components; these tests load the whole shell against the screenshot
// fixtures (tests/screenshots/fakes) so root-level handlers actually run.
#include "Fixtures.h"
#include "stores/SettingsStore.h"

#include <KLocalizedQmlContext>
#include <KLocalizedString>

#include <QFile>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QtQuickTest>

#include <chrono>
#include <thread>

namespace {

long residentMb()
{
    QFile status(QStringLiteral("/proc/self/status"));
    if (!status.open(QIODevice::ReadOnly))
        return 0;
    for (const QByteArray& line : status.readAll().split('\n'))
        if (line.startsWith("VmRSS:"))
            return line.mid(6).trimmed().split(' ').value(0).toLong() / 1024;
    return 0;
}

} // namespace

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

        // KYPOST_RSS_LIMIT_MB: abort the moment resident memory passes it.
        // The shell once grew to 3 GB in two seconds on a 1.6x screen; failing
        // fast keeps a regression from taking the machine down with it.
        const long limit = qEnvironmentVariableIntValue("KYPOST_RSS_LIMIT_MB");
        // Its own thread: a layout loop never returns to the event loop.
        if (limit > 0) {
            if (residentMb() <= 0)
                qFatal("KYPOST_RSS_LIMIT_MB is set but VmRSS cannot be read; refusing to pass unchecked");
            std::thread([limit] {
                for (;;) {
                    const long rss = residentMb();
                    if (rss > limit)
                        qFatal("resident memory %ld MB passed the %ld MB limit", rss, limit);
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                }
            }).detach();
        }
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
