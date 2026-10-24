#pragma once

#include <QSqlDatabase>
#include <QString>
#include <QVector>
#include <optional>

// Over the existing `pending_contact_changes` table from
// core/db/migrations/001_initial.sql (id, contact_uid, change_json,
// created_at) -- sitting unused since Phase 2; ContactSyncRepository is its
// first consumer.
struct PendingContactChangeRecord
{
    int id = 0;
    QString contactUid; // the LOCAL identity: a temp UUID for a queued
                         // create, or the real server uid for a queued
                         // update/delete -- see ContactSyncRepository
    QString changeJson; // ContactWire::contactToJson(...) result, serialized
    QString createdAt;

    bool operator==(const PendingContactChangeRecord&) const = default;
};

class PendingContactChangeDao
{
public:
    explicit PendingContactChangeDao(QSqlDatabase& db);

    // Returns the new row's id, or -1 on failure.
    int enqueue(const QString& contactUid, const QString& changeJson, const QString& createdAt);
    QVector<PendingContactChangeRecord> findAll() const;
    // nullopt when the table could not be read, which is not "empty".
    std::optional<int> count() const;
    bool deleteById(int id);
    bool deleteAll();

private:
    QSqlDatabase& m_db;
};
