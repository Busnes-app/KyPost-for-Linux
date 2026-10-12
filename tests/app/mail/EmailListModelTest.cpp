#include "mail/EmailListModel.h"

#include "models/Email.h"

#include <QSignalSpy>
#include <QTest>

class EmailListModelTest : public QObject
{
    Q_OBJECT

private slots:
    void rowCountAndEmailAtReflectSetEmails();
    void dataRoundTripsEveryRoleForAPopulatedRow();
    void pgpMarkerRolesReflectMessageState();
    void emailAtOutOfRangeReturnsDefaultConstructedEmail();
    void dataOutOfRangeReturnsInvalidVariant();
    void unreadRolesFollowTheExactWireStatus();
    void setStatusUpdatesTheRowInPlace();

private:
    static Email sampleEmail();
};

Email EmailListModelTest::sampleEmail()
{
    Email email;
    email.messageId = QStringLiteral("m1");
    email.folder = QStringLiteral("INBOX");
    email.sender = QStringLiteral("alice@example.com");
    email.sentTo = QStringLiteral("bob@example.com");
    email.cc = QStringLiteral("cc@example.com");
    email.bcc = QStringLiteral("bcc@example.com");
    email.subject = QStringLiteral("Hello");
    email.preview = QStringLiteral("Preview text");
    email.body = QStringLiteral("Body text");
    email.label = QStringLiteral("important");
    email.keywords = { QStringLiteral("Work"), QStringLiteral("Urgent") };
    email.status = QStringLiteral("unread");
    email.atUtc = QStringLiteral("2026-07-01T12:00:00Z");
    email.hasAttachments = true;
    email.sourceMode = QStringLiteral("plain");
    return email;
}

void EmailListModelTest::rowCountAndEmailAtReflectSetEmails()
{
    EmailListModel model;
    QCOMPARE(model.rowCount(), 0);

    const QVector<Email> emails = { sampleEmail(), sampleEmail() };
    model.setEmails(emails);

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.emailAt(0).messageId, QStringLiteral("m1"));
    QCOMPARE(model.emailAt(1).messageId, QStringLiteral("m1"));
}

void EmailListModelTest::dataRoundTripsEveryRoleForAPopulatedRow()
{
    EmailListModel model;
    model.setEmails({ sampleEmail() });

    const QModelIndex index = model.index(0, 0);
    QVERIFY(index.isValid());

    QCOMPARE(model.data(index, EmailListModel::MessageIdRole).toString(), QStringLiteral("m1"));
    QCOMPARE(model.data(index, EmailListModel::FolderRole).toString(), QStringLiteral("INBOX"));
    QCOMPARE(model.data(index, EmailListModel::SenderRole).toString(), QStringLiteral("alice@example.com"));
    QCOMPARE(model.data(index, EmailListModel::SentToRole).toString(), QStringLiteral("bob@example.com"));
    QCOMPARE(model.data(index, EmailListModel::CcRole).toString(), QStringLiteral("cc@example.com"));
    QCOMPARE(model.data(index, EmailListModel::BccRole).toString(), QStringLiteral("bcc@example.com"));
    QCOMPARE(model.data(index, EmailListModel::SubjectRole).toString(), QStringLiteral("Hello"));
    QCOMPARE(model.data(index, EmailListModel::PreviewRole).toString(), QStringLiteral("Preview text"));
    QCOMPARE(model.data(index, EmailListModel::BodyRole).toString(), QStringLiteral("Body text"));
    QCOMPARE(model.data(index, EmailListModel::LabelRole).toString(), QStringLiteral("important"));
    QCOMPARE(model.data(index, EmailListModel::KeywordsRole).toStringList(),
             QStringList({ QStringLiteral("Work"), QStringLiteral("Urgent") }));
    QCOMPARE(model.data(index, EmailListModel::StatusRole).toString(), QStringLiteral("unread"));
    QCOMPARE(model.data(index, EmailListModel::AtUtcRole).toString(), QStringLiteral("2026-07-01T12:00:00Z"));
    QCOMPARE(model.data(index, EmailListModel::HasAttachmentsRole).toBool(), true);
    QCOMPARE(model.data(index, EmailListModel::SourceModeRole).toString(), QStringLiteral("plain"));
    // sampleEmail() carries no PGP content, so both marker roles are empty.
    QVERIFY(model.data(index, EmailListModel::PgpMarkerRole).toString().isEmpty());
    QVERIFY(model.data(index, EmailListModel::PgpMarkerAccessibleNameRole).toString().isEmpty());

    // roleNames() must expose exactly these 19 role-name strings for QML.
    const QHash<int, QByteArray> roles = model.roleNames();
    QCOMPARE(roles.size(), 19);
    QCOMPARE(roles.value(EmailListModel::MessageIdRole), QByteArrayLiteral("messageId"));
    QCOMPARE(roles.value(EmailListModel::KeywordsRole), QByteArrayLiteral("keywords"));
    QCOMPARE(roles.value(EmailListModel::HasAttachmentsRole), QByteArrayLiteral("hasAttachments"));
    QCOMPARE(roles.value(EmailListModel::SourceModeRole), QByteArrayLiteral("sourceMode"));
    QCOMPARE(roles.value(EmailListModel::PgpMarkerRole), QByteArrayLiteral("pgpMarker"));
    QCOMPARE(roles.value(EmailListModel::PgpMarkerAccessibleNameRole),
             QByteArrayLiteral("pgpMarkerAccessibleName"));
    QCOMPARE(roles.value(EmailListModel::UnreadRole), QByteArrayLiteral("unread"));
    QCOMPARE(roles.value(EmailListModel::AccessibleNameRole), QByteArrayLiteral("accessibleName"));
}

void EmailListModelTest::pgpMarkerRolesReflectMessageState()
{
    Email clientProtected = sampleEmail();
    clientProtected.pgpEncrypted = true;
    clientProtected.body = std::nullopt;

    Email decryptFailed = sampleEmail();
    decryptFailed.pgpEncrypted = true;
    decryptFailed.body = std::nullopt;
    decryptFailed.pgpDecryptError = QStringLiteral("no secret key");

    Email serverDecrypted = sampleEmail();
    serverDecrypted.pgpEncrypted = true;
    serverDecrypted.body = QStringLiteral("readable");

    EmailListModel model;
    model.setEmails({ clientProtected, decryptFailed, serverDecrypted });

    const auto marker = [&model](int row) {
        return model.data(model.index(row, 0), EmailListModel::PgpMarkerRole).toString();
    };
    const auto spoken = [&model](int row) {
        return model.data(model.index(row, 0), EmailListModel::PgpMarkerAccessibleNameRole).toString();
    };

    QCOMPARE(marker(0), QStringLiteral("\U0001F512"));
    QCOMPARE(marker(1), QStringLiteral("\u26A0"));
    // Server-decrypted rows read normally, so they are deliberately unmarked.
    QVERIFY(marker(2).isEmpty());

    QVERIFY(!spoken(0).isEmpty());
    QVERIFY(!spoken(1).isEmpty());
    QVERIFY(spoken(2).isEmpty());
}

void EmailListModelTest::emailAtOutOfRangeReturnsDefaultConstructedEmail()
{
    EmailListModel model;
    model.setEmails({ sampleEmail() });

    QCOMPARE(model.emailAt(-1), Email());
    QCOMPARE(model.emailAt(1), Email());
}

void EmailListModelTest::dataOutOfRangeReturnsInvalidVariant()
{
    EmailListModel model;
    model.setEmails({ sampleEmail() });

    QVERIFY(!model.data(model.index(1, 0), EmailListModel::MessageIdRole).isValid());
    QVERIFY(!model.data(QModelIndex(), EmailListModel::MessageIdRole).isValid());
}

void EmailListModelTest::unreadRolesFollowTheExactWireStatus()
{
    Email unread = sampleEmail();
    Email read = sampleEmail();
    read.status = QStringLiteral("read");
    Email unknown = sampleEmail();
    unknown.status = QString();

    EmailListModel model;
    model.setEmails({ unread, read, unknown });

    QCOMPARE(model.data(model.index(0, 0), EmailListModel::UnreadRole).toBool(), true);
    QCOMPARE(model.data(model.index(1, 0), EmailListModel::UnreadRole).toBool(), false);
    QCOMPARE(model.data(model.index(2, 0), EmailListModel::UnreadRole).toBool(), false);

    const QString unreadName = model.data(model.index(0, 0), EmailListModel::AccessibleNameRole).toString();
    const QString readName = model.data(model.index(1, 0), EmailListModel::AccessibleNameRole).toString();
    QVERIFY2(unreadName.contains(QStringLiteral("Unread")), qPrintable(unreadName));
    QVERIFY2(!readName.contains(QStringLiteral("nread")), qPrintable(readName));
    QVERIFY(unreadName.contains(QStringLiteral("alice@example.com")) && unreadName.contains(QStringLiteral("Hello")));
}

// A reset would throw away the list's scroll position on every message opened.
void EmailListModelTest::setStatusUpdatesTheRowInPlace()
{
    Email other = sampleEmail();
    other.messageId = QStringLiteral("m2");
    Email archived = sampleEmail();
    archived.folder = QStringLiteral("Archive");

    EmailListModel model;
    model.setEmails({ sampleEmail(), other, archived });

    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);
    model.setStatus(QStringLiteral("INBOX"), QStringLiteral("m1"), QStringLiteral("read"));

    QCOMPARE(resetSpy.count(), 0);
    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(changedSpy.at(0).at(0).toModelIndex().row(), 0);
    QCOMPARE(model.emailAt(0).status, QStringLiteral("read"));
    QCOMPARE(model.emailAt(1).status, QStringLiteral("unread"));
    QCOMPARE(model.emailAt(2).status, QStringLiteral("unread")); // same id, other folder

    // Already in that state: nothing to announce.
    model.setStatus(QStringLiteral("INBOX"), QStringLiteral("m1"), QStringLiteral("read"));
    QCOMPARE(changedSpy.count(), 1);
}

QTEST_GUILESS_MAIN(EmailListModelTest)
#include "EmailListModelTest.moc"
