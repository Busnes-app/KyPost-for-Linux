#include "db/Database.h"
#include "db/FolderDao.h"

#include <QTest>

class FolderDaoTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void roundTripsInsertUpdateDelete();
    void findsByParentAndAll();
    void topLevelSnapshotKeepsTheSpecialUseRole();

private:
    Database m_db;
};

void FolderDaoTest::init()
{
    QVERIFY(m_db.open(QStringLiteral(":memory:")));
}

void FolderDaoTest::roundTripsInsertUpdateDelete()
{
    FolderDao dao(m_db.handle());

    QVERIFY(dao.insertOrReplace(QStringLiteral("Archive/2024"), QStringLiteral("Archive"), true,
                                 QStringLiteral("sync")));

    auto found = dao.findByPath(QStringLiteral("Archive/2024"));
    QVERIFY(found.has_value());
    QCOMPARE(found->path, QStringLiteral("Archive/2024"));
    QCOMPARE(found->parent, QStringLiteral("Archive"));
    QCOMPARE(found->deletable, true);
    QCOMPARE(found->sourceMode, QStringLiteral("sync"));

    QVERIFY(dao.insertOrReplace(QStringLiteral("Archive/2024"), QStringLiteral("Archive"), false,
                                 QStringLiteral("local")));
    auto updated = dao.findByPath(QStringLiteral("Archive/2024"));
    QVERIFY(updated.has_value());
    QCOMPARE(updated->deletable, false);
    QCOMPARE(updated->sourceMode, QStringLiteral("local"));

    QVERIFY(dao.deleteByPath(QStringLiteral("Archive/2024")));
    QVERIFY(!dao.findByPath(QStringLiteral("Archive/2024")).has_value());
}

void FolderDaoTest::findsByParentAndAll()
{
    FolderDao dao(m_db.handle());

    QVERIFY(dao.insertOrReplace(QStringLiteral("Archive"), QString(), false, QStringLiteral("sync")));
    QVERIFY(dao.insertOrReplace(QStringLiteral("Archive/2024"), QStringLiteral("Archive"), true,
                                 QStringLiteral("sync")));
    QVERIFY(dao.insertOrReplace(QStringLiteral("Archive/2025"), QStringLiteral("Archive"), true,
                                 QStringLiteral("sync")));

    QCOMPARE(dao.findByParent(QStringLiteral("Archive")).size(), 2);
    QCOMPARE(dao.findAll().size(), 3);

    QVERIFY(dao.deleteAll());
    QCOMPARE(dao.findAll().size(), 0);
}

// The top level is a null parent; it must still be found again, role intact.
void FolderDaoTest::topLevelSnapshotKeepsTheSpecialUseRole()
{
    FolderDao dao(m_db.handle());

    QVERIFY(dao.replaceParentSnapshot(
        QString(),
        { MailFolder{ QStringLiteral("INBOX.Gesendet"), QString(), false, QStringLiteral("sent") },
          MailFolder{ QStringLiteral("Projects"), QString(), false, QString() } },
        QStringLiteral("relay")));

    QCOMPARE(dao.findByParent(QString()).size(), 2);
    QCOMPARE(dao.findByPath(QStringLiteral("INBOX.Gesendet"))->special, QStringLiteral("sent"));
    QVERIFY(dao.findByPath(QStringLiteral("Projects"))->special.isEmpty());

    QVERIFY(dao.replaceParentSnapshot(QString(), {}, QStringLiteral("relay")));
    QVERIFY(dao.findByParent(QString()).isEmpty());
}

QTEST_GUILESS_MAIN(FolderDaoTest)
#include "FolderDaoTest.moc"
