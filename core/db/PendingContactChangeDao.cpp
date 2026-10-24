#include "PendingContactChangeDao.h"

#include <QSqlQuery>
#include <QVariant>

namespace {

PendingContactChangeRecord recordFromQuery(const QSqlQuery& query)
{
    PendingContactChangeRecord record;
    record.id = query.value(QStringLiteral("id")).toInt();
    record.contactUid = query.value(QStringLiteral("contact_uid")).toString();
    record.changeJson = query.value(QStringLiteral("change_json")).toString();
    record.createdAt = query.value(QStringLiteral("created_at")).toString();
    return record;
}

} // namespace

PendingContactChangeDao::PendingContactChangeDao(QSqlDatabase& db) : m_db(db)
{
}

int PendingContactChangeDao::enqueue(const QString& contactUid, const QString& changeJson, const QString& createdAt)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT INTO pending_contact_changes (contact_uid, change_json, created_at) "
        "VALUES (:contact_uid, :change_json, :created_at)"));
    query.bindValue(QStringLiteral(":contact_uid"), contactUid);
    query.bindValue(QStringLiteral(":change_json"), changeJson);
    query.bindValue(QStringLiteral(":created_at"), createdAt);
    if (!query.exec())
        return -1;
    return query.lastInsertId().toInt();
}

QVector<PendingContactChangeRecord> PendingContactChangeDao::findAll() const
{
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT * FROM pending_contact_changes ORDER BY id")))
        return {};
    QVector<PendingContactChangeRecord> results;
    while (query.next())
        results.append(recordFromQuery(query));
    return results;
}

std::optional<int> PendingContactChangeDao::count() const
{
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM pending_contact_changes")) || !query.next())
        return std::nullopt;
    return query.value(0).toInt();
}

bool PendingContactChangeDao::deleteById(int id)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM pending_contact_changes WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), id);
    return query.exec();
}

bool PendingContactChangeDao::deleteAll()
{
    QSqlQuery query(m_db);
    return query.exec(QStringLiteral("DELETE FROM pending_contact_changes"));
}
