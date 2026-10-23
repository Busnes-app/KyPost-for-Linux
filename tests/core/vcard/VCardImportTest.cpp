#include "vcard/VCardImport.h"

#include "models/Contact.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

// The cases of kypost-android's VCardParserTest (#186): the file is hostile,
// so every refusal is whole-file, and only the writer's properties are read.
class VCardImportTest : public QObject
{
    Q_OBJECT

private slots:
    void readsEveryFieldTheWriterWrites();
    void readsCommonV3Shapes();
    void telUrisLoseTheirScheme();
    void neverImportsUidKeysPhotosOrGroups();
    void encodedTextPropertiesAreSkippedNotMisread();
    void aNamelessCardFallsBackToItsEmailAndAnEmptyOneIsSkipped();
    void anInvalidBirthdayIsDropped();
    void controlCharactersAreStripped();
    void refusals();
    void anyOtherBeginOrEndIsAStructuralError();
    void invalidUtf8IsRefused();
    void moreContactsThanTheCapAreRefusedWhole();
    void moreValuesThanTheServerKeepsAreRefused();
    void theReaderStopsAtTheSizeCap();
    void onlyRegularFilesAreRead();
};

namespace {

QByteArray card(const QStringList& lines, const QString& version = QStringLiteral("4.0"))
{
    QStringList all = {QStringLiteral("BEGIN:VCARD"), QStringLiteral("VERSION:") + version};
    all << lines << QStringLiteral("END:VCARD");
    return (all.join(QStringLiteral("\r\n")) + QStringLiteral("\r\n")).toUtf8();
}

QByteArray repeated(const QByteArray& bytes, int times)
{
    QByteArray out;
    for (int i = 0; i < times; ++i)
        out += bytes;
    return out;
}

VCardImport::Refusal refusalOf(const QByteArray& bytes)
{
    return VCardImport::parse(bytes).refusal;
}

Contact only(const QByteArray& bytes)
{
    const VCardImport::Result result = VCardImport::parse(bytes);
    if (result.refusal != VCardImport::Refusal::None || result.contacts.size() != 1)
        qFatal("expected exactly one parsed contact");
    return result.contacts.first();
}

Contact named(const QString& fn)
{
    Contact contact;
    contact.fn = fn;
    return contact;
}

} // namespace

void VCardImportTest::readsEveryFieldTheWriterWrites()
{
    for (const QString& version : {QStringLiteral("3.0"), QStringLiteral("4.0")}) {
        const Contact c = only(card({
            QStringLiteral("FN:Jane O'Doe\\; Jr\\, \\\\ esq"),
            QStringLiteral("N:O\\;Doe;Jane;Q;Dr;Jr"),
            QStringLiteral("NICKNAME:JJ"),
            QStringLiteral("ORG:Acme;R&D"),
            QStringLiteral("TITLE:Engineer"),
            QStringLiteral("BDAY:1990-01-15"),
            QStringLiteral("NOTE:line one\\nline two"),
            QStringLiteral("EMAIL;TYPE=work:jane@acme.example"),
            QStringLiteral("EMAIL:j@x.example"),
            QStringLiteral("TEL;TYPE=cell:+1 555 0100"),
            QStringLiteral("TEL;TYPE=work,fax:+1 555 0101"),
            QStringLiteral("ADR;TYPE=home:;;1 Main St;Town;ST;12345;US"),
            QStringLiteral("URL:https://jane.example"),
        }, version));
        QCOMPARE(c.fn, std::optional<QString>(QStringLiteral("Jane O'Doe; Jr, \\ esq")));
        QCOMPARE(c.familyName, std::optional<QString>(QStringLiteral("O;Doe")));
        QCOMPARE(c.givenName, std::optional<QString>(QStringLiteral("Jane")));
        QCOMPARE(c.middleName, std::optional<QString>(QStringLiteral("Q")));
        QCOMPARE(c.prefix, std::optional<QString>(QStringLiteral("Dr")));
        QCOMPARE(c.suffix, std::optional<QString>(QStringLiteral("Jr")));
        QCOMPARE(c.nickname, std::optional<QString>(QStringLiteral("JJ")));
        QCOMPARE(c.org, std::optional<QString>(QStringLiteral("Acme")));
        QCOMPARE(c.department, std::optional<QString>(QStringLiteral("R&D")));
        QCOMPARE(c.title, std::optional<QString>(QStringLiteral("Engineer")));
        QCOMPARE(c.birthday, std::optional<QString>(QStringLiteral("1990-01-15")));
        QCOMPARE(c.notes, std::optional<QString>(QStringLiteral("line one\nline two")));
        const QVector<ContactEmailEntry> emails = {{QStringLiteral("Work"), QStringLiteral("jane@acme.example")},
                                                   {std::nullopt, QStringLiteral("j@x.example")}};
        QCOMPARE(c.emails, emails);
        const QVector<ContactPhoneEntry> phones = {{QStringLiteral("Mobile"), QStringLiteral("+1 555 0100")},
                                                   {QStringLiteral("Work Fax"), QStringLiteral("+1 555 0101")}};
        QCOMPARE(c.phones, phones);
        const QVector<ContactAddressEntry> addresses = {{QStringLiteral("Home"), QStringLiteral("1 Main St"),
                                                         QStringLiteral("Town"), QStringLiteral("ST"),
                                                         QStringLiteral("12345"), QStringLiteral("US")}};
        QCOMPARE(c.addresses, addresses);
        const QVector<ContactUrlEntry> websites = {{std::nullopt, QStringLiteral("https://jane.example")}};
        QCOMPARE(c.websites, websites);
        QVERIFY(c.uid.isEmpty());
    }
}

void VCardImportTest::readsCommonV3Shapes()
{
    const QByteArray text = QStringList({
        QStringLiteral("BEGIN:VCARD"),
        QStringLiteral("VERSION:3.0"),
        QStringLiteral("N:Doe;John;;;"),
        QStringLiteral("item1.EMAIL;TYPE=INTERNET,HOME:john@home.example"),
        QStringLiteral("TEL;CELL:+1 555"),
        QStringLiteral("TEL;TYPE=WORK,VOICE:+1 666"),
        QStringLiteral("BDAY:1980-02-29T00:00:00Z"),
        QStringLiteral("NOTE:folded"),
        QStringLiteral("\tacross lines"),
        QStringLiteral("END:VCARD"),
    }).join(QLatin1Char('\n')).toUtf8();
    const Contact c = only(text);
    QCOMPARE(c.fn, std::optional<QString>(QStringLiteral("John Doe")));
    const QVector<ContactEmailEntry> emails = {{QStringLiteral("Home"), QStringLiteral("john@home.example")}};
    QCOMPARE(c.emails, emails);
    const QVector<ContactPhoneEntry> phones = {{QStringLiteral("Mobile"), QStringLiteral("+1 555")},
                                               {QStringLiteral("Work"), QStringLiteral("+1 666")}};
    QCOMPARE(c.phones, phones);
    QCOMPARE(c.birthday, std::optional<QString>(QStringLiteral("1980-02-29")));
    QCOMPARE(c.notes, std::optional<QString>(QStringLiteral("foldedacross lines")));
}

void VCardImportTest::telUrisLoseTheirScheme()
{
    QCOMPARE(only(card({QStringLiteral("FN:A"), QStringLiteral("TEL;VALUE=uri:tel:+15550100")})).phones.first().value,
             QStringLiteral("+15550100"));
}

// A file must never be able to pin a PGP key, choose a server uid, put the
// contact in a group or mark it as the user's own card.
void VCardImportTest::neverImportsUidKeysPhotosOrGroups()
{
    const Contact c = only(card({
        QStringLiteral("UID:urn:uuid:attacker-chosen"),
        QStringLiteral("FN:A"),
        QStringLiteral("KEY;MEDIATYPE=application/pgp-keys:data:application/pgp-keys;base64,AAAA"),
        QStringLiteral("PHOTO;ENCODING=b;TYPE=JPEG:AAAA"),
        QStringLiteral("CATEGORIES:friends"),
        QStringLiteral("X-KYPOST-ISSELF:true"),
        QStringLiteral("REV:20260101T000000Z"),
    }));
    QCOMPARE(c, named(QStringLiteral("A")));
}

void VCardImportTest::encodedTextPropertiesAreSkippedNotMisread()
{
    QVERIFY(!only(card({QStringLiteral("FN:A"), QStringLiteral("NOTE;ENCODING=QUOTED-PRINTABLE:=41")})).notes);
}

void VCardImportTest::aNamelessCardFallsBackToItsEmailAndAnEmptyOneIsSkipped()
{
    const VCardImport::Result result = VCardImport::parse(
        card({QStringLiteral("EMAIL:a@x.example")}) + card({QStringLiteral("NOTE:nothing to call it")}));
    QCOMPARE(result.refusal, VCardImport::Refusal::None);
    QCOMPARE(result.contacts.size(), 1);
    QCOMPARE(result.contacts.first().fn, std::optional<QString>(QStringLiteral("a@x.example")));
    QCOMPARE(result.skipped, 1);
    QCOMPARE(only(card({QStringLiteral("ORG:Acme;Sales")})).fn, std::optional<QString>(QStringLiteral("Acme")));
}

void VCardImportTest::anInvalidBirthdayIsDropped()
{
    QVERIFY(!only(card({QStringLiteral("FN:A"), QStringLiteral("BDAY:19901345")})).birthday);
    QVERIFY(!only(card({QStringLiteral("FN:A"), QStringLiteral("BDAY:1981-02-29")})).birthday);
    QVERIFY(!only(card({QStringLiteral("FN:A"), QStringLiteral("BDAY:--0415")})).birthday);
    QCOMPARE(only(card({QStringLiteral("FN:A"), QStringLiteral("BDAY:19900115")})).birthday,
             std::optional<QString>(QStringLiteral("1990-01-15")));
}

void VCardImportTest::controlCharactersAreStripped()
{
    QCOMPARE(only(card({QStringLiteral("FN:A") + QChar(0x07) + QStringLiteral("B")})).fn,
             std::optional<QString>(QStringLiteral("AB")));
    QCOMPARE(only(card({QStringLiteral("FN:A\\") + QChar(0x07) + QStringLiteral("B")})).fn,
             std::optional<QString>(QStringLiteral("AB")));
    QCOMPARE(only("\xEF\xBB\xBF" + card({QStringLiteral("FN:A\\")})).fn, std::optional<QString>(QStringLiteral("A")));
}

void VCardImportTest::refusals()
{
    using R = VCardImport::Refusal;
    QCOMPARE(refusalOf(card({QStringLiteral("FN:A")}, QStringLiteral("2.1"))), R::UnsupportedVersion);
    QCOMPARE(refusalOf("BEGIN:VCARD\r\nVERSION:4.0\r\nFN:A\r\n"), R::Malformed);
    QCOMPARE(refusalOf("BEGIN:VCARD\r\nVERSION:4.0\r\n" + card({QStringLiteral("FN:A")}) + "END:VCARD\r\n"),
             R::Malformed);
    QCOMPARE(refusalOf("hello\r\n" + card({QStringLiteral("FN:A")})), R::Malformed);
    QCOMPARE(refusalOf(card({QStringLiteral("FN:A"), QStringLiteral("no colon here")})), R::Malformed);
    QCOMPARE(refusalOf("BEGIN:VCARD\r\nFN:A\r\nEND:VCARD\r\n"), R::Malformed);
    QCOMPARE(refusalOf(""), R::Empty);
    QCOMPARE(refusalOf(card({QStringLiteral("NOTE:x")})), R::Empty);
    // Refused whole: a good card before a bad one is not imported on its own.
    QCOMPARE(refusalOf(card({QStringLiteral("FN:A")}) + card({QStringLiteral("FN:B")}, QStringLiteral("2.1"))),
             R::UnsupportedVersion);
}

void VCardImportTest::anyOtherBeginOrEndIsAStructuralError()
{
    using R = VCardImport::Refusal;
    QCOMPARE(refusalOf(card({QStringLiteral("FN:A")}) + card({QStringLiteral("FN:B"), QStringLiteral("END:OTHER")})),
             R::Malformed);
    QCOMPARE(refusalOf(card({QStringLiteral("FN:A"), QStringLiteral("BEGIN:VEVENT")})), R::Malformed);
    QCOMPARE(refusalOf("BEGIN:VCALENDAR\r\n" + card({QStringLiteral("FN:A")})), R::Malformed);
}

void VCardImportTest::invalidUtf8IsRefused()
{
    QByteArray bytes = card({QStringLiteral("FN:A")});
    bytes.insert(bytes.indexOf("FN:A") + 4, "\xC3\x28"); // a lead byte with no continuation
    QCOMPARE(refusalOf(bytes), VCardImport::Refusal::NotUtf8);
    QByteArray overlong = card({QStringLiteral("FN:A")});
    overlong.insert(overlong.indexOf("FN:A") + 4, "\xC0\xAF");
    QCOMPARE(refusalOf(overlong), VCardImport::Refusal::NotUtf8);
}

void VCardImportTest::moreContactsThanTheCapAreRefusedWhole()
{
    const QByteArray one = card({QStringLiteral("FN:A")});
    QCOMPARE(VCardImport::parse(repeated(one, VCardImport::kMaxContacts)).contacts.size(), VCardImport::kMaxContacts);
    QCOMPARE(refusalOf(repeated(one, VCardImport::kMaxContacts + 1)), VCardImport::Refusal::TooManyContacts);
    // Skipped cards count toward the cap too.
    QCOMPARE(refusalOf(repeated(card({QStringLiteral("NOTE:x")}), VCardImport::kMaxContacts) + one),
             VCardImport::Refusal::TooManyContacts);
}

void VCardImportTest::moreValuesThanTheServerKeepsAreRefused()
{
    QStringList lines = {QStringLiteral("FN:A")};
    for (int i = 0; i < VCardImport::kMaxValuesPerField; ++i)
        lines << QStringLiteral("EMAIL:a%1@x.example").arg(i);
    QCOMPARE(only(card(lines)).emails.size(), VCardImport::kMaxValuesPerField);
    lines << QStringLiteral("EMAIL:one-too-many@x.example");
    QCOMPARE(refusalOf(card(lines)), VCardImport::Refusal::TooManyValues);
}

void VCardImportTest::theReaderStopsAtTheSizeCap()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString big = dir.filePath(QStringLiteral("big.vcf"));
    {
        QFile file(big);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray bytes = card({QStringLiteral("FN:A"), QStringLiteral("NOTE:") + QString(int(VCardImport::kMaxBytes), QLatin1Char('x'))});
        QCOMPARE(file.write(bytes), qint64(bytes.size()));
    }
    QCOMPARE(VCardImport::readFile(big).refusal, VCardImport::Refusal::TooLarge);
    QCOMPARE(VCardImport::parse(QByteArray(int(VCardImport::kMaxBytes) + 1, 'x')).refusal,
             VCardImport::Refusal::TooLarge);

    const QString small = dir.filePath(QStringLiteral("small.vcf"));
    {
        QFile file(small);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray bytes = card({QStringLiteral("FN:A")});
        QCOMPARE(file.write(bytes), qint64(bytes.size()));
    }
    const VCardImport::Result result = VCardImport::readFile(small);
    QCOMPARE(result.refusal, VCardImport::Refusal::None);
    QCOMPARE(result.contacts.size(), 1);
}

// A directory, a missing path or a device is refused before anything is read:
// a FIFO or /dev/zero would otherwise block or feed the reader forever.
void VCardImportTest::onlyRegularFilesAreRead()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QCOMPARE(VCardImport::readFile(dir.path()).refusal, VCardImport::Refusal::Unreadable);
    QCOMPARE(VCardImport::readFile(dir.filePath(QStringLiteral("missing.vcf"))).refusal,
             VCardImport::Refusal::Unreadable);
    if (QFile::exists(QStringLiteral("/dev/zero")))
        QCOMPARE(VCardImport::readFile(QStringLiteral("/dev/zero")).refusal, VCardImport::Refusal::Unreadable);
}

QTEST_GUILESS_MAIN(VCardImportTest)
#include "VCardImportTest.moc"
