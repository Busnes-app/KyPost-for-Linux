#include "mail/ProtectedImageHandler.h"

#include <QBuffer>
#include <QQuickWebEngineProfile>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlScheme>

ProtectedImageHandler::ProtectedImageHandler(std::function<Image(const QUrl&)> resolve, QObject* parent)
    : QWebEngineUrlSchemeHandler(parent), m_resolve(std::move(resolve))
{
}

void ProtectedImageHandler::registerScheme()
{
    QWebEngineUrlScheme scheme("kypost-cid");
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Host);
    // Only memory-backed images are served. CORS permits loadHtml's opaque
    // document origin; no local-file access, scripts or service workers.
    scheme.setFlags(QWebEngineUrlScheme::SecureScheme | QWebEngineUrlScheme::CorsEnabled);
    QWebEngineUrlScheme::registerScheme(scheme);
}

void ProtectedImageHandler::installOn(QQuickWebEngineProfile* profile)
{
    if (profile && profile->isOffTheRecord()) {
        profile->setHttpCacheType(QQuickWebEngineProfile::NoCache);
        profile->installUrlSchemeHandler("kypost-cid", this);
    }
}

void ProtectedImageHandler::requestStarted(QWebEngineUrlRequestJob* job)
{
    if (job->requestMethod() != "GET") {
        job->fail(QWebEngineUrlRequestJob::RequestDenied);
        return;
    }
    const Image image = m_resolve(job->requestUrl());
    if (image.first.isEmpty() || image.second.isEmpty()) {
        job->fail(QWebEngineUrlRequestJob::UrlNotFound);
        return;
    }
    auto* buffer = new QBuffer(job);
    buffer->setData(image.second);
    if (!buffer->open(QIODevice::ReadOnly)) {
        job->fail(QWebEngineUrlRequestJob::RequestFailed);
        return;
    }
    job->reply(image.first, buffer);
}
