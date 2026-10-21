#include "models/StandardFolder.h"

#include <QTest>

class StandardFolderTest : public QObject
{
    Q_OBJECT

private slots:
    void wireNameCoversAllValues();
    void displayNameSplitsOnSlash();
    void displayNameSplitsOnDot();
    void displayNameHandlesPlainPath();
    void specialUseRoleWinsOverName();
    void nameIsTheFallbackWithoutARole();
    void aLocalizedFolderResolvesByRole();
    void twoClaimantsResolveTheSameInEitherOrder();
};

namespace {
MailFolder folder(const QString& path, const QString& special = QString())
{
    return MailFolder{ path, QString(), false, special };
}
} // namespace

void StandardFolderTest::wireNameCoversAllValues()
{
    const QVector<QPair<StandardFolder, QString>> cases = {
        {StandardFolder::Inbox, QStringLiteral("INBOX")},
        {StandardFolder::Drafts, QStringLiteral("Drafts")},
        {StandardFolder::Junk, QStringLiteral("Junk")},
        {StandardFolder::Sent, QStringLiteral("Sent")},
        {StandardFolder::Trash, QStringLiteral("Trash")},
        {StandardFolder::Archive, QStringLiteral("Archive")},
    };

    for (const auto& [folder, wireName] : cases)
        QCOMPARE(standardFolderWireName(folder), wireName);
}

void StandardFolderTest::displayNameSplitsOnSlash()
{
    QCOMPARE(standardFolderDisplayName(QStringLiteral("Archive/2024")), QStringLiteral("2024"));
}

void StandardFolderTest::displayNameSplitsOnDot()
{
    QCOMPARE(standardFolderDisplayName(QStringLiteral("INBOX.Old")), QStringLiteral("Old"));
}

void StandardFolderTest::displayNameHandlesPlainPath()
{
    QCOMPARE(standardFolderDisplayName(QStringLiteral("INBOX")), QStringLiteral("Inbox"));
    QCOMPARE(standardFolderDisplayName(QStringLiteral("inbox")), QStringLiteral("Inbox"));
    QCOMPARE(standardFolderDisplayName(QStringLiteral("Sent")), QStringLiteral("Sent"));
    QCOMPARE(standardFolderDisplayName(QStringLiteral("INBOX/Receipts")), QStringLiteral("Receipts"));
}

// A server can carry both the conventional "Sent" and the real, flagged one.
void StandardFolderTest::specialUseRoleWinsOverName()
{
    const QVector<MailFolder> topLevel = { folder(QStringLiteral("Sent")),
                                           folder(QStringLiteral("Sent Items"), QStringLiteral("sent")) };
    QCOMPARE(standardFolderPath(StandardFolder::Sent, topLevel), QStringLiteral("Sent Items"));
}

void StandardFolderTest::nameIsTheFallbackWithoutARole()
{
    const QVector<MailFolder> topLevel = { folder(QStringLiteral("Gesendet")),
                                           folder(QStringLiteral("Spam"), QStringLiteral("junk")) };
    QCOMPARE(standardFolderPath(StandardFolder::Sent, topLevel), QStringLiteral("Sent"));
    QCOMPARE(standardFolderPath(StandardFolder::Drafts, {}), QStringLiteral("Drafts"));
    // INBOX has no RFC 6154 role, whatever a server claims.
    QCOMPARE(standardFolderPath(StandardFolder::Inbox, { folder(QStringLiteral("X"), QStringLiteral("inbox")) }),
             QStringLiteral("INBOX"));
}

void StandardFolderTest::aLocalizedFolderResolvesByRole()
{
    const QVector<MailFolder> topLevel = { folder(QStringLiteral("INBOX.Gesendet"), QStringLiteral("sent")),
                                           folder(QStringLiteral("INBOX.Entw&APw-rfe"), QStringLiteral("drafts")),
                                           folder(QStringLiteral("Papierkorb"), QStringLiteral("trash")),
                                           folder(QStringLiteral("Archiv"), QStringLiteral("archive")),
                                           folder(QStringLiteral("Spam"), QStringLiteral("junk")) };
    QCOMPARE(standardFolderPath(StandardFolder::Sent, topLevel), QStringLiteral("INBOX.Gesendet"));
    QCOMPARE(standardFolderPath(StandardFolder::Drafts, topLevel), QStringLiteral("INBOX.Entw&APw-rfe"));
    QCOMPARE(standardFolderPath(StandardFolder::Trash, topLevel), QStringLiteral("Papierkorb"));
    QCOMPARE(standardFolderPath(StandardFolder::Archive, topLevel), QStringLiteral("Archiv"));
    QCOMPARE(standardFolderPath(StandardFolder::Junk, topLevel), QStringLiteral("Spam"));
}

void StandardFolderTest::twoClaimantsResolveTheSameInEitherOrder()
{
    const MailFolder a = folder(QStringLiteral("Enviados"), QStringLiteral("sent"));
    const MailFolder b = folder(QStringLiteral("Gesendet"), QStringLiteral("sent"));
    QCOMPARE(standardFolderPath(StandardFolder::Sent, { a, b }), QStringLiteral("Enviados"));
    QCOMPARE(standardFolderPath(StandardFolder::Sent, { b, a }), QStringLiteral("Enviados"));
}

QTEST_APPLESS_MAIN(StandardFolderTest)
#include "StandardFolderTest.moc"
