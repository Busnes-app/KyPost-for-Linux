#pragma once

#include <QString>
#include <QUrl>
#include <QVariantList>
#include <optional>

// Where a web link in a message really goes. `url` is what gets opened, so the
// host the user confirmed and the page that loads come from one parse. `host`
// is lowercase ASCII: an IDN shows as xn--, so a lookalike cannot pass as the
// real name (AGENTS.md 6d), and an IPv6 literal is bracketed.
struct LinkTarget
{
    QString host;
    QUrl url;
};

// Null for anything but a valid http(s) URL with a host. Userinfo is dropped,
// so `https://bank.example@evil.example/` neither reads as bank.example in the
// dialog nor reaches the browser.
std::optional<LinkTarget> linkTargetOf(const QUrl& url);

// The host `href` really goes to, when the visible `text` reads as a web
// address on another site; empty when they agree or the text is not an
// address. Same rule as Android's linkTextMismatch: `www.` is ignored on both
// sides and a subdomain of the named host counts as that site, because its
// owner controls it; `bank.example.evil.example` does not.
// ponytail: no public-suffix list, so text naming `github.io` over a link to
// `evil.github.io` is not flagged. The dialog's host line still shows it.
QString linkTextMismatch(const QString& text, const QUrl& href);

// Same, over every link in the page that goes to `href`: `anchors` is what
// kLinkAnchorScript returns, [[href, text], ...]. Several links can share an
// href, and any one of them lying is enough to flag it.
QString linkTextMismatch(const QVariantList& anchors, const QUrl& href);

// Run in WebEngineScript::ApplicationWorld, which works with the page's own
// JavaScript disabled and is out of the sender's reach. Read-only; returns
// each link's resolved href and visible text.
inline constexpr char kLinkAnchorScript[] =
    "Array.from(document.links, function (a) { return [a.href, (a.innerText || '').slice(0, 256)] })";
