#pragma once

#include <QWebEngineUrlSchemeHandler>
#include <QUrl>
#include <functional>

class QQuickWebEngineProfile;

// One memory-only route for every off-the-record mail profile. The resolver
// checks the current pairing, lock state and unguessable message token per read.
class ProtectedImageHandler : public QWebEngineUrlSchemeHandler
{
    Q_OBJECT
public:
    using Image = QPair<QByteArray, QByteArray>; // detected MIME type, bytes
    explicit ProtectedImageHandler(std::function<Image(const QUrl&)> resolve, QObject* parent = nullptr);
    static void registerScheme();
    Q_INVOKABLE void installOn(QQuickWebEngineProfile* profile);
    void requestStarted(QWebEngineUrlRequestJob* job) override;
private:
    std::function<Image(const QUrl&)> m_resolve;
};
