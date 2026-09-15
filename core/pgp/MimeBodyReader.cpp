#include "pgp/MimeBodyReader.h"

#include <QList>
#include <QPair>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QUrl>
#include <optional>

namespace {

// Bounds on attacker-controlled structure. Exceeding any of them stops the
// walk and refuses the result, so a partial message cannot look complete.
//
// The numbers are generous against real mail: a signed, encrypted message
// with an alternative body and inline images nests about four deep, and a
// part count in the low hundreds means a mailing-list digest, not a message
// anybody is reading here.
constexpr int kMaxDepth = 8;
constexpr int kMaxParts = 64;
constexpr qsizetype kMaxHeaderBytes = 64 * 1024;
constexpr qsizetype kMaxEntityBytes = 32 * 1024 * 1024;
constexpr qsizetype kMaxWalkBytes = 64 * 1024 * 1024;

struct Entity
{
    QList<QPair<QByteArray, QByteArray>> headers; // field name lowercased, value unfolded
    QByteArray body;
    bool hasMimeHeaders = false; // Content-Type or MIME-Version was present
};

// Finds the blank line that ends a header block, tolerating both CRLF and
// bare LF. Mail arrives with both, and a decrypted part is whatever the
// sending client wrote rather than anything a server normalised.
qsizetype headerBlockEnd(const QByteArray& entity, qsizetype* bodyStart)
{
    const qsizetype crlf = entity.indexOf("\r\n\r\n");
    const qsizetype lf = entity.indexOf("\n\n");
    if (crlf >= 0 && (lf < 0 || crlf <= lf)) {
        *bodyStart = crlf + 4;
        return crlf;
    }
    if (lf >= 0) {
        *bodyStart = lf + 2;
        return lf;
    }
    return -1;
}

Entity parseEntity(const QByteArray& raw)
{
    Entity entity;

    qsizetype bodyStart = 0;
    const qsizetype headerEnd = headerBlockEnd(raw, &bodyStart);
    if (headerEnd < 0 || headerEnd > kMaxHeaderBytes) {
        // No header block, or one too large to be a real one. Either way the
        // bytes are not something to read structure out of.
        entity.body = raw;
        return entity;
    }

    entity.body = raw.mid(bodyStart);

    const QByteArray block = raw.left(headerEnd);
    QByteArray pending;
    const QList<QByteArray> lines = block.split('\n');
    auto flush = [&entity, &pending]() {
        if (pending.isEmpty())
            return;
        const qsizetype colon = pending.indexOf(':');
        if (colon > 0) {
            const QByteArray name = pending.left(colon).trimmed().toLower();
            const QByteArray value = pending.mid(colon + 1).trimmed();
            entity.headers.append({ name, value });
            if (name == "content-type" || name == "mime-version")
                entity.hasMimeHeaders = true;
        }
        pending.clear();
    };

    for (const QByteArray& rawLine : lines) {
        QByteArray line = rawLine;
        if (line.endsWith('\r'))
            line.chop(1);
        // A line beginning with whitespace continues the one before it
        // (RFC 5322 folding). Folded into a single space, which is what the
        // fold represented.
        if (!line.isEmpty() && (line.startsWith(' ') || line.startsWith('\t'))) {
            pending += ' ';
            pending += line.trimmed();
            continue;
        }
        flush();
        pending = line;
    }
    flush();

    return entity;
}

QByteArray headerOf(const Entity& entity, const char* name)
{
    for (const auto& header : entity.headers) {
        if (header.first == name)
            return header.second;
    }
    return {};
}

// The bare `type/subtype`, lowercased, with any parameters dropped.
QByteArray mimeTypeOf(const Entity& entity)
{
    const QByteArray value = headerOf(entity, "content-type");
    if (value.isEmpty())
        return "text/plain"; // RFC 2045's default for an entity that names none
    const qsizetype semicolon = value.indexOf(';');
    return (semicolon < 0 ? value : value.left(semicolon)).trimmed().toLower();
}

// One parameter out of a header value: `; name=value` or `; name="value"`.
// Case-insensitive on the name, as RFC 2045 requires.
QByteArray parameterOf(const QByteArray& headerValue, const char* name)
{
    const QByteArray needle = QByteArray(name).toLower();
    qsizetype at = 0;
    while ((at = headerValue.indexOf(';', at)) >= 0) {
        ++at;
        qsizetype segmentEnd = headerValue.indexOf(';', at);
        if (segmentEnd < 0)
            segmentEnd = headerValue.size();
        const qsizetype equals = headerValue.indexOf('=', at);
        // A parameter carrying no `=` of its own is skipped, never fatal.
        // Searching unbounded took the NEXT parameter's `=` instead and
        // swallowed both segments, so `multipart/mixed; flowed; boundary="b1"`
        // yielded no boundary at all -- and a sender writes that header.
        if (equals < 0 || equals > segmentEnd) {
            at = segmentEnd;
            continue;
        }
        const QByteArray key = headerValue.mid(at, equals - at).trimmed().toLower();
        qsizetype valueStart = equals + 1;
        while (valueStart < headerValue.size() && (headerValue[valueStart] == ' ' || headerValue[valueStart] == '\t'))
            ++valueStart;

        QByteArray value;
        if (valueStart < headerValue.size() && headerValue[valueStart] == '"') {
            qsizetype closing = valueStart + 1;
            for (; closing < headerValue.size() && headerValue[closing] != '"'; ++closing) {
                if (headerValue[closing] == '\\' && closing + 1 < headerValue.size())
                    ++closing;
                value += headerValue[closing];
            }
            if (closing == headerValue.size())
                return {};
            at = closing;
        } else {
            qsizetype end = headerValue.indexOf(';', valueStart);
            if (end < 0)
                end = headerValue.size();
            value = headerValue.mid(valueStart, end - valueStart).trimmed();
            at = end;
        }
        if (key == needle)
            return value;
    }
    return {};
}

QByteArray decodeQuotedPrintable(const QByteArray& input)
{
    QByteArray out;
    out.reserve(input.size());
    for (qsizetype i = 0; i < input.size(); ++i) {
        const char c = input.at(i);
        if (c != '=') {
            out.append(c);
            continue;
        }
        // Soft line break: `=` at end of line means the line was folded and
        // neither the `=` nor the break is part of the content.
        if (i + 1 < input.size() && input.at(i + 1) == '\n') {
            ++i;
            continue;
        }
        if (i + 2 < input.size() && input.at(i + 1) == '\r' && input.at(i + 2) == '\n') {
            i += 2;
            continue;
        }
        if (i + 2 < input.size()) {
            bool ok = false;
            const int byte = input.mid(i + 1, 2).toInt(&ok, 16);
            if (ok) {
                out.append(static_cast<char>(byte));
                i += 2;
                continue;
            }
        }
        // A stray `=` that decodes to nothing is kept verbatim rather than
        // dropped: it is far more likely to be a literal equals sign in prose
        // than a malformed escape, and silently deleting characters from a
        // message is worse than leaving one in.
        out.append(c);
    }
    return out;
}

QByteArray decodeTransfer(const QByteArray& body, const QByteArray& encoding)
{
    const QByteArray normalized = encoding.trimmed().toLower();
    if (normalized == "base64")
        return QByteArray::fromBase64(body);
    if (normalized == "quoted-printable")
        return decodeQuotedPrintable(body);
    // 7bit, 8bit, binary, absent, or something we do not know: the bytes are
    // the bytes. An unknown encoding is NOT an error worth losing the message
    // over, and guessing would be worse.
    return body;
}

QString decodeText(const QByteArray& bytes, const QByteArray& charset)
{
    if (!charset.isEmpty()) {
        QStringDecoder decoder(charset.constData());
        if (decoder.isValid()) {
            QString decoded = decoder(bytes);
            // hasError() catches a byte sequence that is not valid in the
            // charset the sender named. Falling through to UTF-8 would just
            // produce different mojibake, so the decoder's own replacement
            // characters are kept -- they at least came from the charset the
            // sender claimed.
            return decoded;
        }
    }
    // No charset, or one this build has no codec for. UTF-8 is the only
    // defensible default: it is what modern mail uses, and it degrades to
    // ASCII exactly.
    return QString::fromUtf8(bytes);
}

// Decode only complete RFC 2047 words. Adjacent encoded words discard their
// folding whitespace; ordinary text retains it. Header size is bounded before
// this runs, and the expression contains no nested repetition.
QString decodeSubject(const QByteArray& value)
{
    static const QRegularExpression word(QStringLiteral("=\\?([^?\\s]{1,40})\\?([bBqQ])\\?([^?\\s]{1,75})\\?="));
    const QString raw = QString::fromUtf8(value);
    QString result;
    qsizetype end = 0;
    bool previousEncoded = false;
    auto matches = word.globalMatch(raw);
    while (matches.hasNext()) {
        const auto match = matches.next();
        const QString between = raw.mid(end, match.capturedStart() - end);
        QByteArray bytes = match.captured(3).toLatin1();
        bool valid = true;
        if (match.captured(2).compare(QStringLiteral("b"), Qt::CaseInsensitive) == 0) {
            const auto decoded = QByteArray::fromBase64Encoding(bytes, QByteArray::AbortOnBase64DecodingErrors);
            valid = bool(decoded);
            bytes = decoded.decoded;
        } else {
            bytes.replace('_', ' ');
            bytes = decodeQuotedPrintable(bytes);
        }
        if (!previousEncoded || !valid || !between.trimmed().isEmpty())
            result += between;
        result += valid ? decodeText(bytes, match.captured(1).toLatin1()) : match.captured();
        previousEncoded = valid;
        end = match.capturedEnd();
    }
    result += raw.mid(end);
    // A header rendered in a title or reply must remain a single line, even
    // when a sender hid controls inside an encoded word.
    for (QChar& c : result) {
        if (c.unicode() < 0x20 || c.unicode() == 0x7f || c == QChar(0x2028) || c == QChar(0x2029))
            c = QLatin1Char(' ');
    }
    return result.trimmed();
}

// RFC 2231: unescape each encoded segment once, then decode the combined
// bytes so UTF-8 can span segments. Segment count and header bytes are bounded.
QString filenameOf(const QByteArray& header, const char* parameter)
{
    const QByteArray name(parameter);
    QByteArray joined;
    QByteArray charset;
    bool continued = false;
    const auto append = [&joined, &charset](QByteArray part, bool encoded, bool first) {
        if (encoded && first) {
            const qsizetype quote = part.indexOf('\'');
            const qsizetype languageEnd = part.indexOf('\'', quote + 1);
            if (quote < 0 || languageEnd < 0)
                return false;
            charset = part.left(quote);
            part = part.mid(languageEnd + 1);
        }
        joined += encoded ? QByteArray::fromPercentEncoding(part) : part;
        return true;
    };
    for (int i = 0; i < kMaxParts; ++i) {
        const QByteArray key = name + '*' + QByteArray::number(i);
        QByteArray part = parameterOf(header, (key + '*').constData());
        const bool encoded = !part.isEmpty();
        if (!encoded)
            part = parameterOf(header, key.constData());
        if (part.isEmpty())
            break;
        continued = true;
        if (!append(part, encoded, i == 0))
            return {};
    }
    if (continued)
        return decodeText(joined, charset);
    const QByteArray extended = parameterOf(header, (name + '*').constData());
    if (!extended.isEmpty()) {
        if (!append(extended, true, true))
            return {};
        return decodeText(joined, charset);
    }
    return decodeSubject(parameterOf(header, parameter));
}

std::optional<QByteArray> attachmentBytes(const QByteArray& body, const QByteArray& encoding)
{
    const QByteArray enc = encoding.trimmed().toLower();
    if (enc == "base64") {
        QByteArray compact = body;
        compact.replace("\r", ""); compact.replace("\n", "");
        compact.replace("\t", ""); compact.replace(" ", "");
        auto decoded = QByteArray::fromBase64Encoding(compact, QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded)
            return std::nullopt;
        return std::move(decoded.decoded);
    }
    if (enc == "quoted-printable") {
        // For files, preserve-or-guess would silently corrupt bytes. Prose
        // retains the legacy tolerant decoder, but attachments fail closed.
        for (qsizetype i = 0; i < body.size(); ++i) {
            if (body[i] != '=')
                continue;
            if (body.mid(i + 1, 2) == "\r\n") { i += 2; continue; }
            if (body.mid(i + 1, 1) == "\n") { ++i; continue; }
            const auto hex = [](char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'); };
            if (i + 2 >= body.size() || !hex(body[i + 1]) || !hex(body[i + 2]))
                return std::nullopt;
            i += 2;
        }
        return decodeQuotedPrintable(body);
    }
    if (enc.isEmpty() || enc == "7bit" || enc == "8bit" || enc == "binary")
        return body;
    return std::nullopt;
}

// Splits a multipart body on its boundary. Returns the parts between the
// delimiters, dropping the preamble before the first and anything after the
// closing `--boundary--`.
QList<QByteArray> splitOnBoundary(const QByteArray& body, const QByteArray& boundary, MimeBody::Status& status)
{
    QList<QByteArray> parts;
    if (boundary.isEmpty()) {
        status = MimeBody::Status::Malformed;
        return parts;
    }

    const QByteArray delimiter = "--" + boundary;
    qsizetype searchFrom = 0;
    qsizetype partStart = -1;

    while (searchFrom <= body.size()) {
        const qsizetype at = body.indexOf(delimiter, searchFrom);
        if (at < 0)
            break;

        // A delimiter counts only at the start of a line, or the sequence is
        // just bytes that happen to appear inside a part's content.
        if (at != 0 && body.at(at - 1) != '\n') {
            searchFrom = at + delimiter.size();
            continue;
        }

        // RFC 2046: the delimiter is the boundary followed by optional linear
        // whitespace and then a line break, or by "--" to close. Anything
        // else is a LONGER boundary that merely starts with this one --
        // "--b20" when we are splitting on "b2".
        //
        // Both checks happen before the pending part is closed off, and that
        // ordering is the whole point. An earlier version rejected the
        // candidate correctly but had already appended the part ending at
        // it, so the real part was truncated at the impostor and the
        // remainder -- headers and all -- was walked as a fresh entity. An
        // entity naming no Content-Type defaults to text/plain (RFC 2045),
        // so raw MIME source was handed up as the message the sender wrote.
        // Reproduced at depth 2 with boundaries "b2" and "b20"; the sender
        // chooses both.
        qsizetype cursor = at + delimiter.size();
        while (cursor < body.size() && (body.at(cursor) == ' ' || body.at(cursor) == '\t'))
            ++cursor;

        const bool closing = body.mid(cursor, 2) == "--";
        if (closing) {
            cursor += 2;
            while (cursor < body.size() && (body[cursor] == ' ' || body[cursor] == '\t'))
                ++cursor;
        }
        const bool terminated = cursor >= body.size() || body.at(cursor) == '\n'
            || body.mid(cursor, 2) == "\r\n";
        if (!terminated) {
            searchFrom = at + delimiter.size();
            continue;
        }

        if (partStart >= 0) {
            qsizetype end = at;
            // The line break before a delimiter belongs to the delimiter, not
            // to the part -- RFC 2046 is explicit, and keeping it appends a
            // phantom blank line to every part.
            if (end > partStart && body.at(end - 1) == '\n')
                --end;
            if (end > partStart && body.at(end - 1) == '\r')
                --end;
            if (parts.size() >= kMaxParts) {
                status = MimeBody::Status::TooLarge;
                return {};
            }
            parts.append(body.mid(partStart, end - partStart));
        }

        if (closing)
            return parts; // anything after the closing delimiter is epilogue

        const qsizetype nextLine = body.indexOf('\n', cursor);
        if (nextLine < 0)
            break;
        partStart = nextLine + 1;
        searchFrom = partStart;
    }

    status = MimeBody::Status::Malformed; // no closing delimiter
    return {};
}

void walk(const QByteArray& raw, int depth, int& partBudget, qsizetype& byteBudget, MimeBody& out)
{
    if (depth > kMaxDepth || partBudget <= 0 || raw.size() > byteBudget) {
        out.status = MimeBody::Status::TooLarge;
        return;
    }
    --partBudget;
    byteBudget -= raw.size();

    qsizetype bodyStart = 0;
    if (headerBlockEnd(raw, &bodyStart) > kMaxHeaderBytes) {
        out.status = MimeBody::Status::TooLarge;
        return;
    }
    const Entity entity = parseEntity(raw);
    const QByteArray contentType = headerOf(entity, "content-type");
    const QByteArray type = mimeTypeOf(entity);

    const QByteArray disposition = headerOf(entity, "content-disposition");
    QString filename = filenameOf(disposition, "filename");
    if (filename.isEmpty())
        filename = filenameOf(contentType, "name");
    const bool isAttachment = !filename.isEmpty() || disposition.trimmed().toLower().startsWith("attachment");

    if (type.startsWith("multipart/") && !isAttachment) {
        const QList<QByteArray> parts = splitOnBoundary(entity.body, parameterOf(contentType, "boundary"), out.status);
        if (out.status != MimeBody::Status::Complete)
            return;
        if (parts.isEmpty()) {
            out.status = MimeBody::Status::Malformed;
            return;
        }
        for (const QByteArray& part : parts) {
            walk(part, depth + 1, partBudget, byteBudget, out);
            if (out.status != MimeBody::Status::Complete)
                return;
        }
        return;
    }

    if (type == "text/rfc822-headers" && !isAttachment)
        return; // protected legacy header, extracted separately
    // ponytail: attached messages stay downloadable .eml files; a future
    // nested-message viewer can parse them without replacing the parent body.
    if (isAttachment || (type != "text/html" && type != "text/plain")) {
        const auto bytes = attachmentBytes(entity.body, headerOf(entity, "content-transfer-encoding"));
        if (!bytes) {
            out.status = MimeBody::Status::Malformed;
            return;
        }
        MimeAttachment file;
        file.name = filename.isEmpty() ? (type == "message/rfc822" ? QStringLiteral("message.eml") : QStringLiteral("attachment")) : filename;
        file.mimeType = QString::fromLatin1(type);
        file.contentId = QString::fromUtf8(headerOf(entity, "content-id")).trimmed();
        if (file.contentId.startsWith(QLatin1Char('<')) && file.contentId.endsWith(QLatin1Char('>')))
            file.contentId = file.contentId.mid(1, file.contentId.size() - 2);
        file.data = type.startsWith("multipart/") ? raw : *bytes;
        if (type.startsWith("multipart/"))
            file.mimeType = QStringLiteral("message/rfc822");
        out.attachments.append(std::move(file));
        return;
    }

    const QByteArray decoded =
        decodeTransfer(entity.body, headerOf(entity, "content-transfer-encoding"));
    const QString text = decodeText(decoded, parameterOf(contentType, "charset"));

    // A blank part is real content and is taken when nothing else is there,
    // but a later non-blank sibling wins -- mirroring kypost-android's
    // PgpMimeReader, so a message does not render as an empty page on one
    // client and as text on the other.
    QString& slot = (type == "text/html") ? out.html : out.plain;
    if (slot.isEmpty() || slot.trimmed().isEmpty())
        slot = text;
}

} // namespace

MimeBody readMimeBody(const QByteArray& entity)
{
    MimeBody out;
    if (entity.isEmpty())
        return out;

    qsizetype bodyStart = 0;
    if (entity.size() > kMaxEntityBytes || headerBlockEnd(entity, &bodyStart) > kMaxHeaderBytes) {
        out.status = MimeBody::Status::TooLarge;
        return out;
    }

    // Inline PGP: no MIME entity at all, just the message. Detected by the
    // ABSENCE of Content-Type/MIME-Version rather than by the shape of the
    // first line, because prose beginning "Note: something" parses as a
    // header field perfectly well and eating the first paragraph of somebody's
    // mail is not a failure they can see or recover from.
    const Entity parsed = parseEntity(entity);
    if (!parsed.hasMimeHeaders) {
        out.plain = QString::fromUtf8(entity);
        return out;
    }

    int partBudget = kMaxParts;
    qsizetype byteBudget = kMaxWalkBytes;
    walk(entity, 0, partBudget, byteBudget, out);

    if (out.status != MimeBody::Status::Complete) {
        out.html.clear();
        out.plain.clear();
        out.attachments.clear();
        return out;
    }

    // Only the decrypted root and its leading legacy display part can name
    // this message. In particular, never descend into an attached message or
    // let a nested part replace the subject of the enclosing mail.
    // Preserve mailbox syntax: decoding a display-name comma before splitting
    // recipients would turn it into a new recipient separator.
    out.to = QString::fromUtf8(headerOf(parsed, "to"));
    out.cc = QString::fromUtf8(headerOf(parsed, "cc"));
    out.bcc = QString::fromUtf8(headerOf(parsed, "bcc"));
    out.subject = decodeSubject(headerOf(parsed, "subject"));
    if (out.subject.isEmpty() && mimeTypeOf(parsed).startsWith("multipart/")) {
        const auto parts = splitOnBoundary(parsed.body, parameterOf(headerOf(parsed, "content-type"), "boundary"), out.status);
        if (!parts.isEmpty()) {
            const Entity first = parseEntity(parts.first());
            if (mimeTypeOf(first) == "text/rfc822-headers"
                && parameterOf(headerOf(first, "content-type"), "protected-headers") == "v1"
                && !headerOf(first, "content-disposition").toLower().startsWith("attachment")) {
                const QByteArray headers = decodeTransfer(first.body, headerOf(first, "content-transfer-encoding"));
                if (headers.size() <= kMaxHeaderBytes)
                    out.subject = decodeSubject(headerOf(parseEntity(headers + "\r\n\r\n"), "subject"));
            }
        }
    }

    return out;
}
