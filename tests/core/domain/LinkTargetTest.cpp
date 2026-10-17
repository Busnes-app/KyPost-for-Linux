#include "domain/LinkTarget.h"

#include <QTest>

// The host shown in the link confirm dialog must be the host the browser
// loads, and the URL handed to the browser must be the one shown.
class LinkTargetTest : public QObject
{
    Q_OBJECT

private slots:
    void lookalikeDomainShowsAsPunycode();
    void userinfoIsNotTheHostAndIsNotOpened();
    void backslashCannotSmuggleAnotherHost();
    void hostIsLowercasedAndKeepsItsTrailingDot();
    void ipLiteralsAreShownAsLoaded();
    void theOpenedUrlIsTheShownOne();
    void nonWebOrMalformedLinksAreRefused();
    void addressShapedTextNamingAnotherHostIsFlagged();
    void agreeingOrOrdinaryTextIsNotFlagged();
    void anyLinkWithTheClickedHrefCanFlagIt();
};

static QString host(const QString& raw)
{
    const auto target = linkTargetOf(QUrl(raw));
    return target ? target->host : QString();
}

void LinkTargetTest::lookalikeDomainShowsAsPunycode()
{
    // Cyrillic "а" in place of Latin "a" (same fixture as Android's).
    QCOMPARE(host(QStringLiteral("https://pаypаl.com/")), QStringLiteral("xn--pypl-53dc.com"));
    // Already-encoded input stays encoded rather than being prettified back.
    QCOMPARE(host(QStringLiteral("https://xn--pypl-53dc.com/")), QStringLiteral("xn--pypl-53dc.com"));
}

void LinkTargetTest::userinfoIsNotTheHostAndIsNotOpened()
{
    QCOMPARE(host(QStringLiteral("http://bank.com@evil.com/login")), QStringLiteral("evil.com"));
    QCOMPARE(host(QStringLiteral("https://bank.com:secret@evil.com/")), QStringLiteral("evil.com"));
    const auto target = linkTargetOf(QUrl(QStringLiteral("http://bank.com@evil.com/login")));
    QVERIFY(target);
    QCOMPARE(target->url.toString(QUrl::FullyEncoded), QStringLiteral("http://evil.com/login"));
}

// Browsers end the host at a backslash, QUrl takes it as userinfo and reads
// bank.example. WebEngine hands over its canonical URL, which has none; a raw
// one is refused rather than shown under the wrong host.
void LinkTargetTest::backslashCannotSmuggleAnotherHost()
{
    QVERIFY(!linkTargetOf(QUrl(QStringLiteral("https://evil.example\\@bank.example/"))));
    QVERIFY(!linkTargetOf(QUrl(QStringLiteral("https://evil.example%5C@bank.example/"))));
}

void LinkTargetTest::hostIsLowercasedAndKeepsItsTrailingDot()
{
    QCOMPARE(host(QStringLiteral("HTTPS://BANK.Example/Path")), QStringLiteral("bank.example"));
    // The trailing dot is what the browser resolves, so it is what is shown.
    QCOMPARE(host(QStringLiteral("https://bank.example./")), QStringLiteral("bank.example."));
}

void LinkTargetTest::ipLiteralsAreShownAsLoaded()
{
    QCOMPARE(host(QStringLiteral("http://192.0.2.7:8080/x")), QStringLiteral("192.0.2.7"));
    QCOMPARE(host(QStringLiteral("http://[2001:DB8::1]/")), QStringLiteral("[2001:db8::1]"));
}

void LinkTargetTest::theOpenedUrlIsTheShownOne()
{
    const auto target = linkTargetOf(QUrl(QStringLiteral("https://pаypаl.com/a b?q=1#f")));
    QVERIFY(target);
    QCOMPARE(target->url.toString(QUrl::FullyEncoded), QStringLiteral("https://xn--pypl-53dc.com/a%20b?q=1#f"));
    // The dialog opens its shown string; Qt.openUrlExternally parses it back
    // into the very URL that was checked.
    QCOMPARE(QUrl(target->url.toString(QUrl::FullyEncoded)), target->url);
}

void LinkTargetTest::nonWebOrMalformedLinksAreRefused()
{
    QVERIFY(!linkTargetOf(QUrl(QStringLiteral("javascript:alert(1)"))));
    QVERIFY(!linkTargetOf(QUrl(QStringLiteral("mailto:a@example.com"))));
    QVERIFY(!linkTargetOf(QUrl(QStringLiteral("kypost://native-pair?srv=evil.example"))));
    QVERIFY(!linkTargetOf(QUrl(QStringLiteral("file:///etc/passwd"))));
    QVERIFY(!linkTargetOf(QUrl(QStringLiteral("https://"))));
    QVERIFY(!linkTargetOf(QUrl(QStringLiteral("not a url"))));
    QVERIFY(!linkTargetOf(QUrl()));
}

void LinkTargetTest::addressShapedTextNamingAnotherHostIsFlagged()
{
    const auto mismatch = [](const char* text, const char* href) {
        return linkTextMismatch(QString::fromUtf8(text), QUrl(QString::fromUtf8(href)));
    };
    QCOMPARE(mismatch("https://one.example/", "https://two.example/"), QStringLiteral("two.example"));
    QCOMPARE(mismatch("bank.example", "https://bank.example.evil.example/"),
             QStringLiteral("bank.example.evil.example"));
    QCOMPARE(mismatch("www.bank.example/login", "http://bank.example@evil.example/"),
             QStringLiteral("evil.example"));
    QCOMPARE(mismatch("BANK.EXAMPLE", "https://evil.example/"), QStringLiteral("evil.example"));
    QCOMPARE(mismatch("http://192.0.2.7/", "https://evil.example/"), QStringLiteral("evil.example"));
    // The lookalike text over the real name, and the real name over a lookalike.
    QCOMPARE(mismatch("pаypаl.com", "https://paypal.com/"), QStringLiteral("paypal.com"));
    QCOMPARE(mismatch("paypal.com", "https://xn--pypl-53dc.com/"), QStringLiteral("xn--pypl-53dc.com"));
    // A host-shaped text naming a parent of the real site is not the real site.
    QCOMPARE(mismatch("login.bank.example", "https://bank.example/"), QStringLiteral("bank.example"));
}

void LinkTargetTest::agreeingOrOrdinaryTextIsNotFlagged()
{
    const auto mismatch = [](const char* text, const char* href) {
        return linkTextMismatch(QString::fromUtf8(text), QUrl(QString::fromUtf8(href)));
    };
    QCOMPARE(mismatch("https://bank.example/login", "https://bank.example/other"), QString());
    QCOMPARE(mismatch("bank.example", "https://www.bank.example/"), QString());
    QCOMPARE(mismatch("www.bank.example", "https://bank.example/"), QString());
    QCOMPARE(mismatch("bank.example", "https://login.bank.example/"), QString());
    QCOMPARE(mismatch("Bank.Example.", "https://bank.example/"), QString());
    QCOMPARE(mismatch("pаypаl.com", "https://xn--pypl-53dc.com/"), QString());
    QCOMPARE(mismatch("Click here", "https://evil.example/"), QString());
    QCOMPARE(mismatch("Read the report.", "https://evil.example/"), QString());
    QCOMPARE(mismatch("version 1.2.3", "https://evil.example/"), QString());
    QCOMPARE(mismatch("support@bank.example", "https://evil.example/"), QString());
    QCOMPARE(mismatch("", "https://evil.example/"), QString());
    // Not a web link at all, so there is no host to disagree with.
    QCOMPARE(mismatch("https://one.example/", "mailto:a@two.example"), QString());
}

void LinkTargetTest::anyLinkWithTheClickedHrefCanFlagIt()
{
    const QUrl href(QStringLiteral("https://evil.example/"));
    const QVariantList anchors{
        QVariantList{QStringLiteral("https://evil.example/"), QStringLiteral("Read more")},
        QVariantList{QStringLiteral("https://other.example/"), QStringLiteral("https://bank.example/")},
        QVariantList{QStringLiteral("https://evil.example/"), QStringLiteral("bank.example")},
    };
    QCOMPARE(linkTextMismatch(anchors, href), QStringLiteral("evil.example"));
    // A lying link elsewhere in the message does not flag an honest one.
    QCOMPARE(linkTextMismatch(anchors, QUrl(QStringLiteral("https://other.example/x"))), QString());
    QCOMPARE(linkTextMismatch(anchors.mid(0, 2), href), QString());
    // Malformed script output is ignored rather than trusted.
    QCOMPARE(linkTextMismatch(QVariantList{QStringLiteral("https://evil.example/"), 42}, href), QString());
}

QTEST_APPLESS_MAIN(LinkTargetTest)
#include "LinkTargetTest.moc"
