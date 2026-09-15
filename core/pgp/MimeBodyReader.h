#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

struct MimeAttachment
{
    QString name;
    QString mimeType;
    QString contentId;
    QByteArray data;
    bool operator==(const MimeAttachment&) const = default;
};

// The readable text of a decrypted OpenPGP message.
//
// Both forms are kept rather than one "best" body. A message carrying only a
// plain part must not render as an empty page, and the caller -- not this
// parser -- decides which surface it has to render into.
struct MimeBody
{
    QString html;
    QString plain;
    QVector<MimeAttachment> attachments;
    QString to;
    QString cc;
    QString bcc;
    QString subject; // inner protected header only; transient, like the body
    enum class Status { Complete, Malformed, TooLarge };
    Status status = Status::Complete;

    bool isEmpty() const { return html.isEmpty() && plain.isEmpty() && attachments.isEmpty(); }
    bool operator==(const MimeBody&) const = default;
};

// Extracts the readable text from the decrypted body of an OpenPGP message.
//
// WHY THIS IS HAND-WRITTEN rather than KMime. `libkf6mime-dev` is not in
// Ubuntu noble, which is what CI builds on; taking it from the KDE neon
// archive layered on top is exactly the shape that broke the build on
// 2026-08-23 (neon's libgpgmepp-dev requires gpgme >= 2.0.0, noble carries
// 1.x). core/'s QtCore/QtNetwork/QtSql-only boundary points the same way.
// The scope here is genuinely small -- one MIME entity, two content types we
// care about -- so this is a bounded parser rather than a general one, and it
// does not try to be KMime.
//
// RFC 2231 filenames and ordinary MIME transfer encodings are supported.
// ponytail: message/rfc822 remains a downloadable attachment; recurse only if
// an attached-message viewer is added, never to select the parent's body.
//
// INPUT IS ATTACKER-CONTROLLED -- it is whatever the sender encrypted, and
// the relay never saw it, so nothing upstream has sanity-checked its shape.
// Nesting depth, part count, header size and cumulative walked bytes are all bounded; see the
// constants in the .cpp. Exceeding a bound returns TooLarge and no partial content. Malformed
// multipart structure returns Malformed; callers must check status before rendering.
//
// Bytes that are not a MIME entity at all -- inline PGP, which is still
// common -- are returned whole as `plain`. That case is detected by requiring
// a Content-Type or MIME-Version header, NOT by guessing from the shape of
// the first line: plain prose beginning "Note: ..." parses as a header field
// perfectly well, and treating it as one would silently eat the first
// paragraph of the message.
MimeBody readMimeBody(const QByteArray& entity);
