#include "models/MfaChallenge.h"

#include <QTest>

namespace {

const QString kId = QStringLiteral("0123456789abcdef0123456789abcdef0123456789abcdef");

// push_mfa_handlers.go's dispatchPushChallenge data map.
QJsonObject validData()
{
    return QJsonObject{
        { QStringLiteral("type"), QStringLiteral("mfa_challenge") },
        { QStringLiteral("challengeId"), kId },
        { QStringLiteral("issuedAt"), QStringLiteral("1760000000000") },
        { QStringLiteral("matchDigits"), QStringLiteral("07") },
        { QStringLiteral("decoyDigits"), QStringLiteral("42,93") },
        { QStringLiteral("ipAddress"), QStringLiteral("203.0.113.9") },
        { QStringLiteral("userAgent"), QStringLiteral("Firefox on Linux") },
    };
}

} // namespace

class MfaChallengeTest : public QObject
{
    Q_OBJECT

private slots:
    void parsesTheServerShape();
    void contextIsOptional();
    void rejectsMalformedFields_data();
    void rejectsMalformedFields();
    void contextIsCappedAndStrippedOfControls();
};

void MfaChallengeTest::parsesTheServerShape()
{
    const std::optional<MfaChallenge> c = MfaChallenge::fromPushData(validData());
    QVERIFY(c.has_value());
    QCOMPARE(c->challengeId, kId);
    QCOMPARE(c->issuedAtMs, qint64(1760000000000));
    QCOMPARE(c->matchDigits, QStringLiteral("07"));
    QCOMPARE(c->decoyDigits, QStringList({ QStringLiteral("42"), QStringLiteral("93") }));
    QCOMPARE(c->ipAddress, QStringLiteral("203.0.113.9"));
    QCOMPARE(c->userAgent, QStringLiteral("Firefox on Linux"));
}

void MfaChallengeTest::contextIsOptional()
{
    QJsonObject data = validData();
    data.remove(QStringLiteral("ipAddress"));
    data.remove(QStringLiteral("userAgent"));
    const std::optional<MfaChallenge> c = MfaChallenge::fromPushData(data);
    QVERIFY(c.has_value());
    QVERIFY(c->ipAddress.isEmpty());
    QVERIFY(c->userAgent.isEmpty());
}

void MfaChallengeTest::rejectsMalformedFields_data()
{
    QTest::addColumn<QString>("key");
    QTest::addColumn<QJsonValue>("value");

    QTest::newRow("wrong type") << "type" << QJsonValue(QStringLiteral("mail"));
    QTest::newRow("short id") << "challengeId" << QJsonValue(kId.left(47));
    QTest::newRow("long id") << "challengeId" << QJsonValue(kId + QLatin1Char('0'));
    QTest::newRow("uppercase id") << "challengeId" << QJsonValue(kId.toUpper());
    QTest::newRow("non-hex id") << "challengeId" << QJsonValue(kId.left(47) + QLatin1Char('g'));
    QTest::newRow("missing id") << "challengeId" << QJsonValue();
    QTest::newRow("issuedAt text") << "issuedAt" << QJsonValue(QStringLiteral("soon"));
    QTest::newRow("issuedAt zero") << "issuedAt" << QJsonValue(QStringLiteral("0"));
    QTest::newRow("issuedAt negative") << "issuedAt" << QJsonValue(QStringLiteral("-5"));
    QTest::newRow("issuedAt huge") << "issuedAt" << QJsonValue(QStringLiteral("99999999999999999999"));
    QTest::newRow("issuedAt as number") << "issuedAt" << QJsonValue(1760000000000.0);
    QTest::newRow("match one digit") << "matchDigits" << QJsonValue(QStringLiteral("7"));
    QTest::newRow("match three digits") << "matchDigits" << QJsonValue(QStringLiteral("007"));
    QTest::newRow("match letters") << "matchDigits" << QJsonValue(QStringLiteral("ab"));
    QTest::newRow("match missing") << "matchDigits" << QJsonValue();
    QTest::newRow("match arabic-indic") << "matchDigits" << QJsonValue(QString::fromUtf8("١٢"));
    QTest::newRow("one decoy") << "decoyDigits" << QJsonValue(QStringLiteral("42"));
    QTest::newRow("three decoys") << "decoyDigits" << QJsonValue(QStringLiteral("42,93,11"));
    QTest::newRow("decoy equals match") << "decoyDigits" << QJsonValue(QStringLiteral("07,93"));
    QTest::newRow("duplicate decoys") << "decoyDigits" << QJsonValue(QStringLiteral("42,42"));
    QTest::newRow("decoy markup") << "decoyDigits" << QJsonValue(QStringLiteral("<b>4</b>,93"));
    QTest::newRow("decoys missing") << "decoyDigits" << QJsonValue();
}

void MfaChallengeTest::rejectsMalformedFields()
{
    QFETCH(QString, key);
    QFETCH(QJsonValue, value);
    QJsonObject data = validData();
    if (value.isUndefined() || value.isNull())
        data.remove(key);
    else
        data[key] = value;
    QVERIFY(!MfaChallenge::fromPushData(data).has_value());
}

void MfaChallengeTest::contextIsCappedAndStrippedOfControls()
{
    QJsonObject data = validData();
    // U+202E reverses what follows; a newline would fake a second field.
    data[QStringLiteral("ipAddress")] = QString::fromUtf8("1.2.3.4‮\n5");
    data[QStringLiteral("userAgent")] = QString(300, QLatin1Char('x'));
    const std::optional<MfaChallenge> c = MfaChallenge::fromPushData(data);
    QVERIFY(c.has_value());
    QCOMPARE(c->ipAddress, QStringLiteral("1.2.3.45"));
    QCOMPARE(c->userAgent.size(), MfaChallenge::kMaxContextLength);
}

QTEST_GUILESS_MAIN(MfaChallengeTest)
#include "MfaChallengeTest.moc"
