#include "pgp/MimeBodyReader.h"
#include "pgp/PgpMimeWriter.h"

#include <QTest>

class MimeBodyReaderTest : public QObject
{
    Q_OBJECT

private slots:
    void inlinePgpIsTheWholeMessage();
    void proseThatLooksLikeAHeaderIsNotEaten();
    void aBareEntityWithNoContentTypeIsStillPlain();
    void singlePlainPart();
    void singleHtmlPart();
    void alternativeKeepsBothForms();
    void nestedMultipartIsWalked();
    void base64IsDecoded();
    void quotedPrintableIsDecoded();
    void aNamedCharsetIsHonoured();
    void anUnknownCharsetFallsBackRatherThanFailing();
    void anUnknownTransferEncodingIsLeftAlone();
    void foldedHeadersAreUnfolded();
    void attachmentsOnlyLeaveNothingToRead();
    void aBoundaryInsideContentDoesNotSplit();
    void aBoundaryThatMerelyPrefixesAnotherIsNotADelimiter();
    void nestedBoundariesThatPrefixEachOtherDoNotTruncate();
    void aValuelessParameterBeforeTheBoundaryDoesNotLoseIt();
    void aValuelessParameterBeforeTheCharsetDoesNotLoseIt();
    void aTrailingValuelessParameterKeepsAnEarlierParameter();
    void theEpilogueAfterTheClosingDelimiterIsIgnored();
    void bareLineFeedsParseLikeCrLf();
    void aLaterNonBlankSiblingWins();
    void nestingIsBounded();
    void partCountIsBounded();
    void walkedBytesAreBounded();
    void emptyInputIsEmpty();
    void encryptedWriterAttachmentsRoundTrip();
    void extendedFilenamesAndNamedText();
    void malformedAttachmentsDoNotPartiallySucceed();
    void protectedSubjectRoundTrips();
    void legacySubjectAndNestedSubjects();
    void malformedAndLimitedMessagesAreExplicit();
};

void MimeBodyReaderTest::inlinePgpIsTheWholeMessage()
{
    // Still common, and it is not a MIME entity at all.
    const MimeBody body = readMimeBody("just some words\nover two lines\n");

    QCOMPARE(body.plain, QStringLiteral("just some words\nover two lines\n"));
    QVERIFY(body.html.isEmpty());
}

// The specific hazard in detecting "is this MIME" by looking at the first
// line: ordinary prose parses as a header field perfectly well. Eating the
// first paragraph of somebody's mail is a failure they cannot see.
void MimeBodyReaderTest::proseThatLooksLikeAHeaderIsNotEaten()
{
    const QByteArray message = "Note: bring the keys\n\nAnd the other thing.\n";
    const MimeBody body = readMimeBody(message);

    QCOMPARE(body.plain, QString::fromUtf8(message));
    QVERIFY2(body.plain.contains(QStringLiteral("bring the keys")),
             "the first paragraph was parsed away as a header");
}

// An entity naming no Content-Type defaults to text/plain per RFC 2045 --
// but with no MIME-Version either there is nothing to say it is an entity at
// all, so it is treated as inline text, headers and all.
void MimeBodyReaderTest::aBareEntityWithNoContentTypeIsStillPlain()
{
    const QByteArray message = "Subject: hi\n\nbody text\n";
    QCOMPARE(readMimeBody(message).plain, QString::fromUtf8(message));
}

void MimeBodyReaderTest::singlePlainPart()
{
    const QByteArray message = "MIME-Version: 1.0\r\n"
                                "Content-Type: text/plain; charset=utf-8\r\n"
                                "\r\n"
                                "the readable part\r\n";
    const MimeBody body = readMimeBody(message);

    QCOMPARE(body.plain, QStringLiteral("the readable part\r\n"));
    QVERIFY(body.html.isEmpty());
}

void MimeBodyReaderTest::singleHtmlPart()
{
    const QByteArray message = "Content-Type: text/html; charset=utf-8\r\n"
                                "\r\n"
                                "<p>hello</p>\r\n";
    const MimeBody body = readMimeBody(message);

    QCOMPARE(body.html, QStringLiteral("<p>hello</p>\r\n"));
    QVERIFY(body.plain.isEmpty());
}

// Both are kept: the caller decides which surface it has, and a message with
// only a plain part must not render as an empty page.
void MimeBodyReaderTest::alternativeKeepsBothForms()
{
    const QByteArray message = "Content-Type: multipart/alternative; boundary=\"sep\"\r\n"
                                "\r\n"
                                "preamble that no reader shows\r\n"
                                "--sep\r\n"
                                "Content-Type: text/plain; charset=utf-8\r\n"
                                "\r\n"
                                "plain form\r\n"
                                "--sep\r\n"
                                "Content-Type: text/html; charset=utf-8\r\n"
                                "\r\n"
                                "<b>html form</b>\r\n"
                                "--sep--\r\n";
    const MimeBody body = readMimeBody(message);

    QCOMPARE(body.plain, QStringLiteral("plain form"));
    QCOMPARE(body.html, QStringLiteral("<b>html form</b>"));
}

// The real shape of a signed-and-encrypted message with an alternative body.
void MimeBodyReaderTest::nestedMultipartIsWalked()
{
    const QByteArray message = "Content-Type: multipart/mixed; boundary=\"outer\"\r\n"
                                "\r\n"
                                "--outer\r\n"
                                "Content-Type: multipart/alternative; boundary=\"inner\"\r\n"
                                "\r\n"
                                "--inner\r\n"
                                "Content-Type: text/plain\r\n"
                                "\r\n"
                                "deep plain\r\n"
                                "--inner\r\n"
                                "Content-Type: text/html\r\n"
                                "\r\n"
                                "<i>deep html</i>\r\n"
                                "--inner--\r\n"
                                "--outer--\r\n";
    const MimeBody body = readMimeBody(message);

    QCOMPARE(body.plain, QStringLiteral("deep plain"));
    QCOMPARE(body.html, QStringLiteral("<i>deep html</i>"));
}

void MimeBodyReaderTest::base64IsDecoded()
{
    const QByteArray message = "Content-Type: text/plain; charset=utf-8\r\n"
                                "Content-Transfer-Encoding: base64\r\n"
                                "\r\n"
                                "aGlkZGVuIGluIGJhc2U2NA==\r\n";
    QCOMPARE(readMimeBody(message).plain, QStringLiteral("hidden in base64"));
}

void MimeBodyReaderTest::quotedPrintableIsDecoded()
{
    // A soft line break, an escaped byte, and a literal `=` that decodes to
    // nothing and must survive rather than vanish.
    const QByteArray message = "Content-Type: text/plain; charset=utf-8\r\n"
                                "Content-Transfer-Encoding: quoted-printable\r\n"
                                "\r\n"
                                "caf=C3=A9 and a very long line that was fol=\r\n"
                                "ded, plus 2 =+ 2\r\n";
    const MimeBody body = readMimeBody(message);

    QVERIFY2(body.plain.contains(QStringLiteral("café")), "=C3=A9 did not decode");
    QVERIFY2(body.plain.contains(QStringLiteral("folded")), "the soft line break was not joined");
    QVERIFY2(body.plain.contains(QStringLiteral("2 =+ 2")), "a literal equals sign was dropped");
}

void MimeBodyReaderTest::aNamedCharsetIsHonoured()
{
    QByteArray message = "Content-Type: text/plain; charset=\"iso-8859-1\"\r\n\r\n";
    message += "caf\xE9\r\n"; // 0xE9 is e-acute in Latin-1, invalid on its own in UTF-8
    QCOMPARE(readMimeBody(message).plain.trimmed(), QStringLiteral("café"));
}

// A charset this build has no codec for must lose formatting, not the
// message.
void MimeBodyReaderTest::anUnknownCharsetFallsBackRatherThanFailing()
{
    const QByteArray message = "Content-Type: text/plain; charset=x-not-a-real-charset\r\n"
                                "\r\n"
                                "still readable\r\n";
    QCOMPARE(readMimeBody(message).plain.trimmed(), QStringLiteral("still readable"));
}

void MimeBodyReaderTest::anUnknownTransferEncodingIsLeftAlone()
{
    const QByteArray message = "Content-Type: text/plain\r\n"
                                "Content-Transfer-Encoding: x-uuencode-ish\r\n"
                                "\r\n"
                                "verbatim bytes\r\n";
    QCOMPARE(readMimeBody(message).plain.trimmed(), QStringLiteral("verbatim bytes"));
}

void MimeBodyReaderTest::foldedHeadersAreUnfolded()
{
    const QByteArray message = "Content-Type: multipart/alternative;\r\n"
                                "\tboundary=\"folded-boundary\"\r\n"
                                "\r\n"
                                "--folded-boundary\r\n"
                                "Content-Type: text/plain\r\n"
                                "\r\n"
                                "found it\r\n"
                                "--folded-boundary--\r\n";
    QCOMPARE(readMimeBody(message).plain, QStringLiteral("found it"));
}

// Nothing readable is not the same as an empty message, and the caller has
// to be able to tell them apart -- so nothing is invented here.
void MimeBodyReaderTest::attachmentsOnlyLeaveNothingToRead()
{
    const QByteArray message = "Content-Type: multipart/mixed; boundary=\"sep\"\r\n"
                                "\r\n"
                                "--sep\r\n"
                                "Content-Type: image/png\r\n"
                                "Content-Transfer-Encoding: base64\r\n"
                                "\r\n"
                                "iVBORw0KGgo=\r\n"
                                "--sep--\r\n";
    QVERIFY(readMimeBody(message).plain.isEmpty());
    QCOMPARE(readMimeBody(message).attachments.size(), 1);
}

// A delimiter counts only at the start of a line. Otherwise a message that
// quotes its own boundary splits itself apart.
void MimeBodyReaderTest::aBoundaryInsideContentDoesNotSplit()
{
    const QByteArray message = "Content-Type: multipart/mixed; boundary=\"sep\"\r\n"
                                "\r\n"
                                "--sep\r\n"
                                "Content-Type: text/plain\r\n"
                                "\r\n"
                                "the string --sep appears mid-line here, and this\r\n"
                                "line ends with the boundary text: --sep\r\n"
                                "but the message continues\r\n"
                                "--sep--\r\n";
    const MimeBody body = readMimeBody(message);

    // The second line is the one that matters. A mid-line occurrence is
    // already rejected because what follows it is not a line break, so a
    // fixture with only that case cannot tell the line-start check from the
    // terminator check -- an occurrence that ENDS a line passes the
    // terminator check and is caught by nothing else.
    QVERIFY2(body.plain.contains(QStringLiteral("but the message continues")),
             "content was split at a boundary that did not start a line");
}

// The delimiter is the boundary followed by a line break or by "--", and
// nothing else counts. Without that check a boundary of "b1" also matches
// "--b11", so a SENDER -- who picks the boundaries -- decides where this
// parser splits. The symptom found in review was worse than a wrong part:
// mis-splitting produced fragments with no header block, which default to
// text/plain per RFC 2045, so raw MIME source was handed up as the message
// the sender wrote.
void MimeBodyReaderTest::aBoundaryThatMerelyPrefixesAnotherIsNotADelimiter()
{
    const QByteArray message = "Content-Type: multipart/mixed; boundary=\"b1\"\r\n"
                                "\r\n"
                                "--b11\r\n"
                                "Content-Type: text/plain\r\n"
                                "\r\n"
                                "decoy in the preamble\r\n"
                                "--b1\r\n"
                                "Content-Type: text/plain\r\n"
                                "\r\n"
                                "the real part\r\n"
                                "--b1--\r\n";
    const MimeBody body = readMimeBody(message);

    QCOMPARE(body.plain, QStringLiteral("the real part"));
    QVERIFY2(!body.plain.contains(QStringLiteral("Content-Type")),
             "MIME source was rendered as message text");
}

// The shallow reproduction of the same bug, and the one that matters: an
// outer boundary of "b2" wrapping an inner one of "b20". The sender picks
// both. Before the fix this rendered the inner part's Content-Type header to
// the reader as the body of the message -- at depth 2, in the shipping
// configuration, with no bound exceeded.
//
// Tested in both directions because the two are not symmetric: only the
// outer-is-a-prefix-of-inner order produces the truncation.
void MimeBodyReaderTest::nestedBoundariesThatPrefixEachOtherDoNotTruncate()
{
    const QByteArray outerIsPrefix = "Content-Type: multipart/mixed; boundary=\"b2\"\r\n\r\n"
                                      "--b2\r\n"
                                      "Content-Type: multipart/alternative; boundary=\"b20\"\r\n\r\n"
                                      "--b20\r\n"
                                      "Content-Type: text/plain\r\n\r\n"
                                      "inner text\r\n"
                                      "--b20--\r\n"
                                      "--b2--\r\n";
    const MimeBody outer = readMimeBody(outerIsPrefix);
    QCOMPARE(outer.plain, QStringLiteral("inner text"));
    QVERIFY2(!outer.plain.contains(QStringLiteral("Content-Type")),
             "MIME source was rendered as message text");

    const QByteArray innerIsPrefix = "Content-Type: multipart/mixed; boundary=\"b20\"\r\n\r\n"
                                      "--b20\r\n"
                                      "Content-Type: multipart/alternative; boundary=\"b2\"\r\n\r\n"
                                      "--b2\r\n"
                                      "Content-Type: text/plain\r\n\r\n"
                                      "inner text 2\r\n"
                                      "--b2--\r\n"
                                      "--b20--\r\n";
    QCOMPARE(readMimeBody(innerIsPrefix).plain, QStringLiteral("inner text 2"));
}

// A parameter with no value of its own is legal-ish in the wild, and the
// sender writes the Content-Type. Scanning for the `=` without bounding it to
// the segment took the NEXT parameter's `=`, so the key became
// "flowed; boundary", the scan ran off the end of the string, and the boundary
// was lost -- which splits into no parts, finds no text/plain, and renders the
// decrypted message as a blank page with no error to explain it.
void MimeBodyReaderTest::aValuelessParameterBeforeTheBoundaryDoesNotLoseIt()
{
    const QByteArray message = "Content-Type: multipart/mixed; flowed; boundary=\"b1\"\r\n"
                                "\r\n"
                                "--b1\r\n"
                                "Content-Type: text/plain\r\n"
                                "\r\n"
                                "still readable\r\n"
                                "--b1--\r\n";
    const MimeBody body = readMimeBody(message);

    QCOMPARE(body.plain, QStringLiteral("still readable"));
}

// The same scan serves charset, so the fix is checked at both call sites --
// here the loss would be silent mojibake rather than a blank page.
void MimeBodyReaderTest::aValuelessParameterBeforeTheCharsetDoesNotLoseIt()
{
    QByteArray message = "Content-Type: text/plain; flowed; charset=\"iso-8859-1\"\r\n\r\n";
    message += "caf\xE9\r\n"; // 0xE9 is e-acute in Latin-1, invalid on its own in UTF-8
    QCOMPARE(readMimeBody(message).plain.trimmed(), QStringLiteral("café"));
}

void MimeBodyReaderTest::aTrailingValuelessParameterKeepsAnEarlierParameter()
{
    const QByteArray message = "Content-Type: multipart/mixed; boundary=\"b1\"; flowed\r\n"
                                "\r\n"
                                "--b1\r\n"
                                "Content-Type: text/plain\r\n"
                                "\r\n"
                                "found anyway\r\n"
                                "--b1--\r\n";
    QCOMPARE(readMimeBody(message).plain, QStringLiteral("found anyway"));
}

void MimeBodyReaderTest::theEpilogueAfterTheClosingDelimiterIsIgnored()
{
    const QByteArray message = "Content-Type: multipart/mixed; boundary=\"sep\"\r\n"
                                "\r\n"
                                "--sep\r\n"
                                "Content-Type: text/plain\r\n"
                                "\r\n"
                                "real content\r\n"
                                "--sep--\r\n"
                                "--sep\r\n"
                                "Content-Type: text/plain\r\n"
                                "\r\n"
                                "smuggled after the close\r\n";
    const MimeBody body = readMimeBody(message);

    QCOMPARE(body.plain, QStringLiteral("real content"));
    QVERIFY2(!body.plain.contains(QStringLiteral("smuggled")),
             "content after the closing delimiter was read");
}

// Decrypted parts are whatever the sending client wrote, not something a
// server normalised, so both line endings turn up in practice.
void MimeBodyReaderTest::bareLineFeedsParseLikeCrLf()
{
    const QByteArray message = "Content-Type: multipart/alternative; boundary=sep\n"
                                "\n"
                                "--sep\n"
                                "Content-Type: text/plain\n"
                                "\n"
                                "lf only\n"
                                "--sep--\n";
    QCOMPARE(readMimeBody(message).plain, QStringLiteral("lf only"));
}

void MimeBodyReaderTest::aLaterNonBlankSiblingWins()
{
    const QByteArray message = "Content-Type: multipart/mixed; boundary=\"sep\"\r\n"
                                "\r\n"
                                "--sep\r\n"
                                "Content-Type: text/plain\r\n"
                                "\r\n"
                                "   \r\n"
                                "--sep\r\n"
                                "Content-Type: text/plain\r\n"
                                "\r\n"
                                "the actual message\r\n"
                                "--sep--\r\n";
    QCOMPARE(readMimeBody(message).plain, QStringLiteral("the actual message"));
}

// The input is whatever a stranger encrypted and the relay never saw it, so
// the structure is not trustworthy. Depth is bounded; exceeding the bound
// stops the walk rather than the process.
void MimeBodyReaderTest::nestingIsBounded()
{
    QByteArray message;
    const int depth = 40;
    for (int i = 0; i < depth; ++i) {
        message += "Content-Type: multipart/mixed; boundary=\"b" + QByteArray::number(i) + "\"\r\n\r\n";
        message += "--b" + QByteArray::number(i) + "\r\n";
    }
    message += "Content-Type: text/plain\r\n\r\ntoo deep to reach\r\n";
    for (int i = depth - 1; i >= 0; --i)
        message += "--b" + QByteArray::number(i) + "--\r\n";

    const MimeBody body = readMimeBody(message);
    QVERIFY2(!body.plain.contains(QStringLiteral("too deep to reach")),
             "the depth bound did not stop the walk");
}

void MimeBodyReaderTest::partCountIsBounded()
{
    QByteArray message = "Content-Type: multipart/mixed; boundary=\"sep\"\r\n\r\n";
    // Blank parts, so nothing claims the plain slot before the budget runs
    // out -- then one real part far past it.
    for (int i = 0; i < 200; ++i)
        message += "--sep\r\nContent-Type: text/plain\r\n\r\n \r\n";
    message += "--sep\r\nContent-Type: text/plain\r\n\r\npast the budget\r\n--sep--\r\n";

    const MimeBody body = readMimeBody(message);
    QVERIFY2(!body.plain.contains(QStringLiteral("past the budget")),
             "the part-count bound did not stop the walk");
}

void MimeBodyReaderTest::walkedBytesAreBounded()
{
    QByteArray message = "Content-Type: multipart/mixed; boundary=b\r\n\r\n--b\r\nContent-Type: text/plain\r\n\r\n";
    message.replace("--b\r\nContent-Type: text/plain", "--b\r\nContent-Type: multipart/mixed; boundary=c\r\n\r\n--c\r\nContent-Type: text/plain");
    message.append(25 * 1024 * 1024, 'x');
    message += "\r\n--c--";
    message += "\r\n--b--\r\n";

    const MimeBody body = readMimeBody(message);
    QVERIFY2(body.plain.isEmpty(), "the MIME walk exceeded its cumulative byte budget");
}

void MimeBodyReaderTest::emptyInputIsEmpty()
{
    QVERIFY(readMimeBody(QByteArray()).isEmpty());
}

void MimeBodyReaderTest::protectedSubjectRoundTrips()
{
    OutgoingMessage message;
    message.subject = QStringLiteral("秘密 — café ").repeated(20);
    message.body = QStringLiteral("body");
    message.mode = QStringLiteral("plain");
    const MimeBody parsed = readMimeBody(protectedContent(message, QStringLiteral("test-boundary")));
    QCOMPARE(parsed.status, MimeBody::Status::Complete);
    QCOMPARE(parsed.subject, message.subject.trimmed());
    QCOMPARE(parsed.plain, message.body);
    QCOMPARE(readMimeBody("Subject: =?UTF-8?Q?caf=C3=A9_?=\r\n =?UTF-8?Q?menu?=\r\nContent-Type: text/plain\r\n\r\nbody").subject,
             QStringLiteral("café menu"));
    QCOMPARE(readMimeBody("Subject: =?UTF-8?B?YQ0KYg==?=\r\nContent-Type: text/plain\r\n\r\nbody").subject,
             QStringLiteral("a  b"));
}

void MimeBodyReaderTest::legacySubjectAndNestedSubjects()
{
    const QByteArray legacy = "Content-Type: multipart/mixed; boundary=b\r\n\r\n"
        "--b\r\nContent-Type: text/rfc822-headers; protected-headers=v1\r\n\r\nSubject: Legacy\r\n"
        "--b\r\nContent-Type: text/plain\r\nSubject: Nested forgery\r\n\r\nbody\r\n--b--\r\n";
    QCOMPARE(readMimeBody(legacy).subject, QStringLiteral("Legacy"));
    QCOMPARE(readMimeBody("Subject: Root\r\n" + legacy).subject, QStringLiteral("Root"));
    QByteArray attached = legacy;
    attached.replace("Content-Type: text/rfc822-headers;", "Content-Disposition: attachment\r\nContent-Type: text/rfc822-headers;");
    QVERIFY(readMimeBody(attached).subject.isEmpty());
    QVERIFY(readMimeBody("Content-Type: multipart/mixed; boundary=b\r\n\r\n"
        "--b\r\nContent-Type: message/rfc822\r\n\r\nSubject: Attached forgery\r\n"
        "Content-Type: text/plain\r\n\r\nattached\r\n"
        "--b\r\nContent-Type: text/plain\r\n\r\nbody\r\n--b--\r\n").subject.isEmpty());
}

void MimeBodyReaderTest::malformedAndLimitedMessagesAreExplicit()
{
    QCOMPARE(readMimeBody("Content-Type: multipart/mixed\r\n\r\nmissing boundary").status,
             MimeBody::Status::Malformed);
    QCOMPARE(readMimeBody("Content-Type: multipart/mixed; boundary=b\r\n\r\n"
        "--b\r\nContent-Type: text/plain\r\n\r\nbody\r\n--b\r\nunfinished").status,
             MimeBody::Status::Malformed);
    QByteArray hugeHeader = "Subject: ";
    hugeHeader.append(65536, 'x');
    hugeHeader += "\r\nContent-Type: text/plain\r\n\r\nbody";
    QCOMPARE(readMimeBody(hugeHeader).status, MimeBody::Status::TooLarge);
    QVERIFY(readMimeBody(hugeHeader).subject.isEmpty());
    QByteArray partial = "Subject: secret\r\nContent-Type: multipart/mixed; boundary=b\r\n\r\n";
    for (int i = 0; i < 65; ++i)
        partial += "--b\r\nContent-Type: text/plain\r\n\r\nbody\r\n";
    partial += "--b--\r\n";
    const MimeBody result = readMimeBody(partial);
    QCOMPARE(result.status, MimeBody::Status::TooLarge);
    QVERIFY(result.isEmpty());
    QVERIFY(result.subject.isEmpty());
}

void MimeBodyReaderTest::encryptedWriterAttachmentsRoundTrip()
{
    OutgoingMessage message;
    message.subject = QStringLiteral("secret");
    message.mode = QStringLiteral("plain");
    message.body = QStringLiteral("the body");
    message.attachments = {{QStringLiteral("empty.txt"), QStringLiteral("text/plain"), {}},
        {QStringLiteral("café.bin"), QStringLiteral("application/octet-stream"), QByteArray::fromHex("00ff010203")}};
    const MimeBody parsed = readMimeBody(protectedContent(message, QStringLiteral("b")));
    QCOMPARE(parsed.status, MimeBody::Status::Complete);
    QCOMPARE(parsed.attachments.size(), 2);
    QCOMPARE(parsed.plain, message.body);
    for (qsizetype i = 0; i < parsed.attachments.size(); ++i) {
        QCOMPARE(parsed.attachments[i].name, message.attachments[i].name);
        QCOMPARE(parsed.attachments[i].data, message.attachments[i].data);
    }
}

void MimeBodyReaderTest::extendedFilenamesAndNamedText()
{
    const auto result = readMimeBody("Content-Type: multipart/mixed; boundary=b\r\n\r\n"
        "--b\r\nContent-Type: text/plain\r\nContent-Disposition: attachment; filename*0*=UTF-8''caf%C3; filename*1*=%A9%2520.txt\r\n"
        "Content-Transfer-Encoding: quoted-printable\r\n\r\nfile=00=FF\r\n"
        "--b\r\nContent-Type: image/png; name=\"a; b\\\".png\"\r\nContent-ID: <logo@example>\r\n\r\npng bytes\r\n"
        "--b\r\nContent-Type: text/plain\r\n\r\nbody\r\n--b--\r\n");
    QCOMPARE(result.status, MimeBody::Status::Complete);
    QCOMPARE(result.plain, QStringLiteral("body"));
    QCOMPARE(result.attachments.size(), 2);
    QCOMPARE(result.attachments[0].name, QStringLiteral("café%20.txt"));
    QCOMPARE(result.attachments[0].data, QByteArray::fromHex("66696c6500ff"));
    QCOMPARE(result.attachments[1].name, QStringLiteral("a; b\".png"));
    QCOMPARE(result.attachments[1].contentId, QStringLiteral("logo@example"));
    const auto attachedMessage = readMimeBody("Content-Type: message/rfc822\r\n\r\nSubject: another message\r\n\r\nbody");
    QCOMPARE(attachedMessage.attachments.size(), 1);
    QCOMPARE(attachedMessage.attachments[0].name, QStringLiteral("message.eml"));
    QVERIFY(attachedMessage.subject.isEmpty());
}

void MimeBodyReaderTest::malformedAttachmentsDoNotPartiallySucceed()
{
    for (const QByteArray& encoding : {QByteArray("base64"), QByteArray("quoted-printable"), QByteArray("unknown")}) {
        const auto result = readMimeBody("Content-Type: multipart/mixed; boundary=b\r\n\r\n"
            "--b\r\nContent-Type: text/plain\r\n\r\nbody\r\n"
            "--b\r\nContent-Type: application/octet-stream\r\nContent-Transfer-Encoding: " + encoding
            + "\r\n\r\n=XX!\r\n--b--\r\n");
        QCOMPARE(result.status, MimeBody::Status::Malformed);
        QVERIFY(result.isEmpty());
    }
    const auto truncated = readMimeBody("Content-Type: multipart/mixed; boundary=b\r\n\r\n"
        "--b\r\nContent-Type: text/plain\r\n\r\nbody\r\n--b--fake\r\n");
    QCOMPARE(truncated.status, MimeBody::Status::Malformed);
}

QTEST_GUILESS_MAIN(MimeBodyReaderTest)
#include "MimeBodyReaderTest.moc"
