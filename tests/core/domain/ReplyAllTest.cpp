#include "domain/ReplyAll.h"

#include <QTest>

class ReplyAllTest : public QObject
{
    Q_OBJECT

private slots:
    void dropsEveryOwnAddressCaseInsensitively();
    void displayNameFormsCompareOnTheBareAddress();
    void ownSentMailKeepsTheOtherRecipients();
    void onlyOwnAddressesAreKept();
    void noKnownOwnAddressesKeepsEveryone();
    void addressIsTheLastAngleAddr();
    void aCommaInADisplayNameIsNotARecipient();
};

// The old QML split on every comma, so a sender-chosen display name could add
// "evil@attacker.tld as a Reply All recipient.
void ReplyAllTest::aCommaInADisplayNameIsNotARecipient()
{
    const QStringList recipients = replyAllRecipients(
        QStringLiteral("bob@corp.com"),
        QStringLiteral("\"evil@attacker.tld, x\" <carol@corp.com>; \"Doe; \\\"J\\\"\" <jane@corp.com>"), QString(),
        {});

    QCOMPARE(recipients, QStringList({ QStringLiteral("bob@corp.com"), QStringLiteral("carol@corp.com"),
                                       QStringLiteral("jane@corp.com") }));
}

// Android #151's own fixture, with To/Cc as the wire strings Linux holds.
void ReplyAllTest::dropsEveryOwnAddressCaseInsensitively()
{
    const QStringList recipients = replyAllRecipients(
        QStringLiteral("Bob <bob@example.com>"),
        QStringLiteral("Me <ME@example.com>, carol@example.com"),
        QStringLiteral("alias@example.com; Bob <BOB@example.com>"),
        { QStringLiteral("me@example.com"), QStringLiteral("Alias@Example.com") });

    QCOMPARE(recipients, QStringList({ QStringLiteral("bob@example.com"), QStringLiteral("carol@example.com") }));
}

// An own address that arrives as "Name <addr>" is still the account's, and a
// display name that merely mentions it is not.
void ReplyAllTest::displayNameFormsCompareOnTheBareAddress()
{
    const QStringList recipients = replyAllRecipients(
        QStringLiteral("\"me@example.com\" <eve@example.com>"),
        QStringLiteral("Me Myself <Me@Example.COM>, me@example.com"),
        QString(),
        { QStringLiteral("Me <me@example.com>") });

    QCOMPARE(recipients, QStringList({ QStringLiteral("eve@example.com") }));
}

void ReplyAllTest::ownSentMailKeepsTheOtherRecipients()
{
    const QStringList recipients = replyAllRecipients(QStringLiteral("me@example.com"),
                                                      QStringLiteral("carol@example.com"), QString(),
                                                      { QStringLiteral("me@example.com") });

    QCOMPARE(recipients, QStringList({ QStringLiteral("carol@example.com") }));
}

// A note to self has nobody else to answer; an empty To would be worse.
void ReplyAllTest::onlyOwnAddressesAreKept()
{
    const QStringList recipients = replyAllRecipients(QStringLiteral("me@example.com"),
                                                      QStringLiteral("ME@example.com"), QString(),
                                                      { QStringLiteral("me@example.com") });

    QCOMPARE(recipients, QStringList({ QStringLiteral("me@example.com") }));
}

// Bootstrap not answered yet: the previous behaviour, sender + To + Cc.
void ReplyAllTest::noKnownOwnAddressesKeepsEveryone()
{
    const QStringList recipients = replyAllRecipients(QStringLiteral("bob@example.com"),
                                                      QStringLiteral("me@example.com,, Unknown"),
                                                      QStringLiteral("carol@example.com"), {});

    QCOMPARE(recipients, QStringList({ QStringLiteral("bob@example.com"), QStringLiteral("me@example.com"),
                                       QStringLiteral("carol@example.com") }));
}

void ReplyAllTest::addressIsTheLastAngleAddr()
{
    QCOMPARE(addressFromHeader(QStringLiteral("\"Bob <bob@corp.com>\" <evil@attacker.tld>")),
             QStringLiteral("evil@attacker.tld"));
    QCOMPARE(addressFromHeader(QStringLiteral(" bob@corp.com ")), QStringLiteral("bob@corp.com"));
    QCOMPARE(addressFromHeader(QStringLiteral("Unknown sender")), QString());
}

QTEST_APPLESS_MAIN(ReplyAllTest)
#include "ReplyAllTest.moc"
