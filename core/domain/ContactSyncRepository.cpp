#include "domain/ContactSyncRepository.h"

#include "db/ContactDao.h"
#include "db/PendingContactChangeDao.h"
#include "domain/ContactSyncReconciliation.h"
#include "domain/DevicePairing.h"
#include "domain/PairingStore.h"
#include "net/ContactSyncClient.h"
#include "net/NetworkError.h"
#include "net/RelayAuth.h"
#include "stores/CursorStore.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSqlDatabase>
#include <QStringList>
#include <QUrl>
#include <QUuid>

namespace {

QString serializeContact(const Contact& contact)
{
    return QString::fromUtf8(QJsonDocument(ContactWire::contactToJson(contact)).toJson(QJsonDocument::Compact));
}

Contact deserializeContact(const QString& json)
{
    return ContactWire::contactFromJson(QJsonDocument::fromJson(json.toUtf8()).object());
}

QString nowUtc()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

// One user action, one transaction. Every mutation below is at least two
// DAO calls -- a cached contact and its pending-queue row -- and both DAOs
// write through the same connection, so a transaction is what makes them one
// thing. Half of one of these has a name and a bug report: a contact that
// exists locally and can never sync, or a delete whose tombstone is gone and
// which therefore comes back on the next pull.
template <typename Fn>
bool inTransaction(QSqlDatabase& db, Fn&& body)
{
    if (!db.transaction())
        return false;
    if (!body()) {
        db.rollback();
        return false;
    }
    return db.commit();
}

ContactSyncStatus statusFromNetworkError(NetworkError error)
{
    switch (error) {
    case NetworkError::Unauthorized:
        return ContactSyncStatus::Unauthorized;
    case NetworkError::ServiceUnavailable:
        return ContactSyncStatus::ServiceUnavailable;
    default:
        return ContactSyncStatus::Retry;
    }
}

// Small, deliberately separate switch rather than sharing statusFromNetworkError
// above -- ContactDedupeStatus and ContactSyncStatus are distinct enums, not
// worth templatizing across for two five-line switches.
ContactDedupeStatus dedupeStatusFromNetworkError(NetworkError error)
{
    switch (error) {
    case NetworkError::Unauthorized:
        return ContactDedupeStatus::Unauthorized;
    case NetworkError::ServiceUnavailable:
        return ContactDedupeStatus::ServiceUnavailable;
    default:
        return ContactDedupeStatus::Retry;
    }
}

} // namespace

ContactSyncRepository::ContactSyncRepository(ContactSyncClient& client, ContactDao& contactDao,
                                               PendingContactChangeDao& pendingDao, CursorStore& cursorStore,
                                               PairingStore& pairingStore, QSqlDatabase& db)
    : m_client(client)
    , m_contactDao(contactDao)
    , m_pendingDao(pendingDao)
    , m_cursorStore(cursorStore)
    , m_pairingStore(pairingStore)
    , m_db(db)
{
}

QVector<Contact> ContactSyncRepository::contacts() const
{
    return m_contactDao.findAll();
}

std::optional<Contact> ContactSyncRepository::findByUid(const QString& uid) const
{
    return m_contactDao.findById(uid);
}

QSet<QString> ContactSyncRepository::pendingUids() const
{
    QSet<QString> uids;
    for (const PendingContactChangeRecord& record : m_pendingDao.findAll())
        uids.insert(record.contactUid);
    return uids;
}

bool ContactSyncRepository::isPending(const QString& uid) const
{
    return pendingUids().contains(uid);
}

namespace {

// The wire copy of a create carries the uid this device gave it, and the
// relay creates the contact under that uid (contacts.Store
// applyUpsertLocked: "UID was supplied but not found ... treat as a create
// under that UID"). An empty uid made the relay mint a new one on every
// push, so a create retried after a reply was lost, or edited before its
// reply came back, became a second contact account-wide.
QString createJson(const Contact& contact)
{
    Contact wire = contact;
    wire.deleted = false;
    return serializeContact(wire);
}

} // namespace

bool ContactSyncRepository::fitsOnePush(const Contact& contact)
{
    return serializeContact(contact).toUtf8().size() <= kMaxSyncBodyBytes - kSyncEnvelopeHeadroom;
}

bool ContactSyncRepository::insertCreate(Contact& contact)
{
    contact.uid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    contact.deleted = false;
    if (!fitsOnePush(contact))
        return false;
    // The cached row without the queue row is a contact the user can see,
    // edit and rely on that will never reach the server or any other device.
    return m_contactDao.insertOrReplace(contact)
        && m_pendingDao.enqueue(contact.uid, createJson(contact), nowUtc()) > 0;
}

QString ContactSyncRepository::queueCreate(Contact contact)
{
    const bool queued = inTransaction(m_db, [&] { return insertCreate(contact); });
    return queued ? contact.uid : QString();
}

ContactImportOutcome ContactSyncRepository::queueImport(const QVector<Contact>& contacts)
{
    if (contacts.size() > kMaxSyncChanges)
        return ContactImportOutcome::TooLarge;
    qsizetype bytes = 0;
    for (Contact contact : contacts) {
        contact.uid = QUuid::createUuid().toString(QUuid::WithoutBraces); // the length insertCreate() gives it
        bytes += createJson(contact).toUtf8().size() + 1; // and its comma
    }
    if (bytes > kMaxSyncBodyBytes - kSyncEnvelopeHeadroom)
        return ContactImportOutcome::TooLarge;

    if (!m_db.transaction())
        return ContactImportOutcome::WriteFailed;
    // Read inside the transaction, so nothing can be queued between the check and the writes.
    const std::optional<int> pending = m_pendingDao.count();
    if (!pending || *pending > 0) {
        m_db.rollback();
        return pending ? ContactImportOutcome::OutboxNotEmpty : ContactImportOutcome::WriteFailed;
    }
    for (qsizetype i = 0; i < contacts.size(); ++i) {
        Contact contact = contacts.at(i);
        if (!insertCreate(contact)) {
            m_db.rollback();
            return ContactImportOutcome::WriteFailed;
        }
    }
    return m_db.commit() ? ContactImportOutcome::Queued : ContactImportOutcome::WriteFailed;
}

bool ContactSyncRepository::carryPending() const
{
    return m_cursorStore.contactCarryPending();
}

ContactImportOutcome ContactSyncRepository::carryInto(const QVector<Contact>& contacts)
{
    // Each becomes a new contact under a new uid, so nothing already on the
    // relay is ever overwritten -- not even when the "different" account is
    // in fact the same one (an unpair by an older build records no owner).
    // The sync pushes them in as many batches as the relay's caps need.
    const bool queued = inTransaction(m_db, [&] {
        if (!m_pendingDao.deleteAll() || !m_contactDao.deleteAll())
            return false;
        for (Contact contact : contacts) {
            if (!insertCreate(contact))
                return false;
        }
        return true;
    });
    if (!queued)
        return ContactImportOutcome::WriteFailed;
    return m_cursorStore.setContactBaseCursor(QString()) && m_cursorStore.setContactCarryPending(false)
        ? ContactImportOutcome::Queued
        : ContactImportOutcome::WriteFailed;
}

bool ContactSyncRepository::removeLocal()
{
    const bool removed = inTransaction(m_db, [&] { return m_pendingDao.deleteAll() && m_contactDao.deleteAll(); });
    return removed && m_cursorStore.setContactBaseCursor(QString()) && m_cursorStore.setContactCarryPending(false);
}

bool ContactSyncRepository::queueUpdate(const Contact& contact)
{
    // Same pair, same reason as queueCreate() -- with one extra way to be
    // wrong: the cache write can land while the queue write does not, and
    // then the local copy shows the user's edit while the server keeps the
    // old one, forever, with no pending badge to say so.
    if (!fitsOnePush(contact))
        return false;
    return inTransaction(m_db, [&] {
        return m_contactDao.insertOrReplace(contact)
            && m_pendingDao.enqueue(contact.uid, serializeContact(contact), nowUtc()) > 0;
    });
}

bool ContactSyncRepository::queueDelete(const QString& uid, qint64 rev)
{
    // If the only pending entry for this uid is a still-unflushed create,
    // the server never saw this contact -- cancel the create outright
    // rather than pushing a tombstone for something that doesn't exist
    // server-side.
    //
    // EVERY queued change for this uid is dropped, not just the create. An
    // edit saved after the create carries the temporary uid in its wire
    // payload, so it survived this loop and was pushed on the next sync as a
    // change naming a uid the server had never seen -- which the relay treats
    // as a create under that uid. The contact the user deleted was recreated
    // account-wide and echoed straight back into the local database.
    //
    // All of it in one transaction. Dropping the queued changes and then
    // failing to enqueue the tombstone is the resurrected-contact bug this
    // method's comment describes, arrived at from the other direction: the
    // row is gone locally, nothing tells the server, and the next pull hands
    // it straight back.
    return inTransaction(m_db, [&] {
        bool hadUnsyncedCreate = false;
        for (const PendingContactChangeRecord& record : m_pendingDao.findAll()) {
            if (record.contactUid != uid)
                continue;
            // Only a create queued by an older build has no uid. One that
            // carries its uid may already be on the relay (a push it
            // committed whose reply was lost), so it gets a tombstone; the
            // relay ignores one for a uid it never had.
            if (deserializeContact(record.changeJson).uid.isEmpty())
                hadUnsyncedCreate = true;
            if (!m_pendingDao.deleteById(record.id))
                return false;
        }

        if (!m_contactDao.deleteById(uid))
            return false;

        if (hadUnsyncedCreate)
            return true;

        Contact tombstone;
        tombstone.uid = uid;
        tombstone.rev = rev;
        tombstone.deleted = true;
        return m_pendingDao.enqueue(uid, serializeContact(tombstone), nowUtc()) > 0;
    });
}

std::optional<ContactSyncRepository::ContactSyncPlan> ContactSyncRepository::planSync() const
{
    const std::optional<DevicePairing> pairing = m_pairingStore.load();
    if (!pairing.has_value())
        return std::nullopt;

    const QString storedCursor = m_cursorStore.contactBaseCursor();

    ContactSyncPlan plan;
    plan.endpoint = RelayEndpoint{ QUrl(pairing->serverBaseUrl),
                                    RelayAuth{ pairing->deviceId, pairing->deviceSecret } };
    plan.cursor = storedCursor.isEmpty() ? 0 : storedCursor.toLongLong();
    // At most what one push may carry (the relay's change and body caps);
    // the rest goes on the next sync, which applySync() asks for. Never
    // nothing while something is queued.
    qsizetype bytes = 0;
    for (const PendingContactChangeRecord& record : m_pendingDao.findAll()) {
        bytes += record.changeJson.toUtf8().size() + 1;
        if (!plan.pending.isEmpty()
            && (plan.pending.size() >= kMaxSyncChanges || bytes > kMaxSyncBodyBytes - kSyncEnvelopeHeadroom))
            break;
        plan.pending.append(record);
    }
    plan.identity = identityOf(*pairing);
    return plan;
}

ContactSyncResult ContactSyncRepository::syncWith(HttpClient& httpClient, const ContactSyncPlan& plan)
{
    // Constructed here rather than reused from m_client: ContactSyncClient is
    // a stateless wrapper over an HttpClient reference, and on the async path
    // that HttpClient belongs to the executor thread.
    ContactSyncClient client(httpClient);
    if (plan.pending.isEmpty())
        return client.pull(plan.endpoint.serverBaseUrl, plan.endpoint.auth, plan.cursor);

    QVector<Contact> changes;
    changes.reserve(plan.pending.size());
    for (const PendingContactChangeRecord& record : plan.pending)
        changes.append(deserializeContact(record.changeJson));
    return client.push(plan.endpoint.serverBaseUrl, plan.endpoint.auth, plan.cursor, changes);
}

ContactSyncOutcome ContactSyncRepository::applySync(const ContactSyncPlan& plan, const ContactSyncResult& result)
{
    const QVector<PendingContactChangeRecord>& pending = plan.pending;

    // First, before either branch below writes anything. The tooOld branch in
    // particular deletes every contact and clears the cursor: run for a reply
    // that belongs to a pairing this device no longer has, it would wipe the
    // NEW account's contacts on the previous account's say-so.
    //
    // The pending queue is deliberately left alone here too. Those are local
    // edits that have not been accepted anywhere yet; dropping them because
    // the account changed would silently discard the user's own work.
    if (!m_pairingStore.stillCurrent(plan.identity))
        return { ContactSyncStatus::PairingChanged, {}, QString(), {} };

    // An unpushed queue must survive to the next sync() call -- pending is
    // deliberately left untouched here.
    if (result.error.has_value())
        return { statusFromNetworkError(*result.error), {}, result.detail, {} };

    if (result.tooOld) {
        // setContactBaseCursor(QString()) directly rather than
        // CursorStore::reset(), which also clears the unrelated mail
        // cursor -- a contacts-only tooOld response has nothing to do with
        // mail sync.
        //
        // Cursor first here; database first on the success path below. Both
        // orders serve one invariant: whatever fails, the cursor that
        // survives must never be AHEAD of the contacts that survive. Ahead
        // means the next pull asks for a delta against rows this device does
        // not have, and the relay never mentions the missing ones again --
        // silent, permanent, and indistinguishable from an empty address
        // book. Behind costs one redundant pull that re-applies rows which
        // are already there.
        //
        // So: clear the cursor, THEN wipe. A failed wipe after a cleared
        // cursor just means the next sync pulls the whole book again and
        // upserts over what is still here.
        if (!m_cursorStore.setContactBaseCursor(QString()))
            return { ContactSyncStatus::CacheWriteFailed, {}, QString(), {} };

        const bool wiped = inTransaction(m_db, [&] {
            if (!m_contactDao.deleteAll())
                return false;
            // Same reasoning as the success path below: only the snapshot
            // that was pushed may be dropped.
            for (const PendingContactChangeRecord& record : pending) {
                if (!m_pendingDao.deleteById(record.id))
                    return false;
            }
            return true;
        });
        if (!wiped)
            return { ContactSyncStatus::CacheWriteFailed, {}, QString(), {} };

        return { ContactSyncStatus::Success,
                 ContactSyncSummary{ static_cast<int>(pending.size()), 0, 0 },
                 QString(),
                 {},
                 // Only from a real cursor: a relay that answered since=0 with
                 // tooOld would otherwise be asked again forever.
                 /*fullPullNeeded=*/plan.cursor > 0 };
    }

    // A create the relay filed under the uid it carried needs no matching.
    // One queued by an older build (no uid), or one a relay filed under a uid
    // of its own, is matched to the record it became by name and address.
    QSet<QString> answered;
    for (const Contact& c : result.changed)
        answered.insert(c.uid);
    QSet<QString> pushedUids;
    for (const PendingContactChangeRecord& record : pending)
        pushedUids.insert(record.contactUid);
    QVector<Contact> pendingCreates;
    for (const PendingContactChangeRecord& record : pending) {
        const Contact wire = deserializeContact(record.changeJson);
        if (wire.deleted || !(wire.uid.isEmpty() || (wire.rev == 0 && !answered.contains(wire.uid))))
            continue;
        if (!wire.uid.isEmpty()) {
            if (answered.contains(wire.uid))
                continue; // its edit, queued behind the create: one match per contact
            answered.insert(wire.uid);
        }
        Contact create;
        create.uid = record.contactUid;
        create.fn = wire.fn;
        create.emails = wire.emails;
        pendingCreates.append(create);
    }
    QVector<Contact> unclaimed;
    for (const Contact& c : result.changed)
        if (!pushedUids.contains(c.uid))
            unclaimed.append(c);

    const QVector<ContactReconciliationAssignment> assignments =
        ContactSyncReconciliation::reconcile(pendingCreates, unclaimed);

    // The response and the queue it answers are applied as ONE database
    // transaction, for the same reason the cursor exists at all: every row
    // in it is described exactly once, and a half-applied response is a
    // state no cursor value can describe. Dropping the pushed queue while
    // the changed rows failed to land is the specific version of that which
    // loses the user's own edits -- the relay considers them delivered, this
    // device no longer has them queued, and nothing will ever ask again.
    int applied = 0;
    const bool persisted = inTransaction(m_db, [&] {
        for (const ContactReconciliationAssignment& assignment : assignments) {
            if (!m_contactDao.deleteById(assignment.localUid))
                return false;
        }

        // Contacts the user edited while this request was out: their queued
        // change is newer than this reply and goes up on the next sync, so the
        // cached row keeps the user's version until then.
        QSet<QString> editedMeanwhile;
        QSet<int> pushedIds;
        for (const PendingContactChangeRecord& record : pending)
            pushedIds.insert(record.id);
        for (const PendingContactChangeRecord& record : m_pendingDao.findAll()) {
            if (!pushedIds.contains(record.id))
                editedMeanwhile.insert(record.contactUid);
        }

        for (const Contact& c : result.changed) {
            if (c.uid.isEmpty() || editedMeanwhile.contains(c.uid))
                continue;
            // The relay sends every changed contact WHOLE (contacts.Store::
            // ChangedSince), and its omitempty drops a cleared field from the
            // JSON. So a missing field means "empty now", and the record
            // replaces the cached one, as Android and Mac apply it. Merging
            // kept every field anyone had ever cleared elsewhere -- a removed
            // phone number, or a removed PGP key this device kept encrypting to.
            if (!m_contactDao.insertOrReplace(c))
                return false;
            ++applied;
        }

        for (const Contact& c : result.deletedContacts) {
            if (!m_contactDao.deleteById(c.uid))
                return false;
            ++applied;
        }

        // Delete only what was actually pushed -- by record id, never
        // deleteAll(). A Save or Delete in the contact pane can land while
        // the request is out, and truncating the whole queue discarded it:
        // the user saw "Synced" while their edit -- a replaced PGP key, say
        // -- never reached the server.
        //
        // That used to happen because HttpClient blocked on a nested
        // QEventLoop and QML kept delivering clicks. The request runs on the
        // executor thread now, so the GUI thread is simply free for the whole
        // round trip and a mid-sync edit is the ORDINARY case rather than a
        // race. Same rule, more load-bearing than before.
        for (const PendingContactChangeRecord& record : pending) {
            if (!m_pendingDao.deleteById(record.id))
                return false;
        }
        return true;
    });

    if (!persisted)
        return { ContactSyncStatus::CacheWriteFailed, {}, QString(), {} };

    // Database first, cursor second -- the opposite order to the tooOld
    // branch above, and the same invariant (see there). A committed batch
    // with a cursor that did not persist costs one redundant pull whose rows
    // upsert over themselves; a persisted cursor over a batch that did not
    // commit costs the batch, permanently.
    //
    // The reassignments ride along even on this failure path, unlike every
    // other one: the transaction above committed, so those temp uids really
    // are dead in the local cache, and the pending creates that produced them
    // are gone -- no later sync can derive this mapping again.
    if (!m_cursorStore.setContactBaseCursor(QString::number(result.cursor)))
        return { ContactSyncStatus::CacheWriteFailed, {}, QString(), assignments };

    ContactSyncOutcome done{ ContactSyncStatus::Success,
                             ContactSyncSummary{ static_cast<int>(pending.size()), applied, result.cursor },
                             QString(),
                             assignments };
    done.morePending = m_pendingDao.count().value_or(0) > 0;
    return done;
}

ContactDedupeResult ContactSyncRepository::dedupeWith(HttpClient& httpClient, const RelayEndpoint& endpoint)
{
    ContactSyncClient client(httpClient);
    return client.dedupe(endpoint.serverBaseUrl, endpoint.auth);
}

ContactDedupeOutcome ContactSyncRepository::dedupeOutcomeOf(const ContactDedupeResult& result)
{
    if (result.error.has_value())
        return { dedupeStatusFromNetworkError(*result.error), 0, {}, result.detail };
    return { ContactDedupeStatus::Success, result.mergedCount, result.groups, QString() };
}

// The synchronous forms, kept as compositions of the phases above so the
// async paths cannot drift from the behaviour their tests pin.

ContactSyncOutcome ContactSyncRepository::sync()
{
    const std::optional<ContactSyncPlan> plan = planSync();
    if (!plan.has_value())
        return { ContactSyncStatus::NotPaired, {}, QStringLiteral("Not paired"), {} };

    ContactSyncResult result;
    if (plan->pending.isEmpty()) {
        result = m_client.pull(plan->endpoint.serverBaseUrl, plan->endpoint.auth, plan->cursor);
    } else {
        QVector<Contact> changes;
        changes.reserve(plan->pending.size());
        for (const PendingContactChangeRecord& record : plan->pending)
            changes.append(deserializeContact(record.changeJson));
        result = m_client.push(plan->endpoint.serverBaseUrl, plan->endpoint.auth, plan->cursor, changes);
    }
    return applySync(*plan, result);
}

ContactDedupeOutcome ContactSyncRepository::dedupe()
{
    const std::optional<DevicePairing> pairing = m_pairingStore.load();
    if (!pairing.has_value())
        return { ContactDedupeStatus::NotPaired, 0, {}, QStringLiteral("Not paired") };

    const RelayAuth auth{ pairing->deviceId, pairing->deviceSecret };
    const QUrl serverUrl(pairing->serverBaseUrl);
    return dedupeOutcomeOf(m_client.dedupe(serverUrl, auth));
}
