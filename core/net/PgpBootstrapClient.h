#pragma once

#include "net/HttpClient.h"
#include "net/NetworkError.h"

#include <QString>
#include <QUrl>
#include <optional>

struct RelayAuth;

// Account identity and custody from GET /api/pgp/bootstrap. Required custody
// fields are type-checked: an unavailable or malformed answer must never be
// mistaken for permission to upload a plaintext draft. Browser key-wrapping
// fields are deliberately not retained here; durable key custody is GnuPG's.
struct PgpBootstrapResult
{
    std::optional<NetworkError> error;
    QString detail; // human-readable detail on error; empty otherwise
    bool ok = false;
    bool hasIdentity = false;
    QString protection;
    QString fingerprint;

    // The account's own mail address -- suggestedUserIDs[0], which the server
    // takes from the IMAP username.
    //
    // Parsed because a client-encrypted send has to write the From header
    // itself, and the relay binds every delivery's From to the address it
    // authorised for the caller (resolveMailFrom, from the same IMAP
    // username). Nothing else in this app knows the user's own address: the
    // pairing carries a subscriber id, not a mailbox.
    //
    // Empty when the account has no mail configured, which the caller must
    // treat as "cannot send" rather than guessing an address.
    QString primaryAddress;
};

// Talks to the account's PGP bootstrap endpoint. Follows PgpQrClient's
// template (constructor takes HttpClient&, one method per endpoint, a small
// *Result struct per call) rather than anything new.
class PgpBootstrapClient
{
public:
    explicit PgpBootstrapClient(HttpClient& httpClient);

    // GET {serverBaseUrl}/api/pgp/bootstrap -- same pairing-header auth shape
    // as PgpQrClient::fetchToken.
    PgpBootstrapResult fetch(const QUrl& serverBaseUrl, const RelayAuth& auth) const;

private:
    HttpClient& m_httpClient;
};
