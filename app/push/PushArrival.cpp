#include "push/PushArrival.h"

#include "domain/PairingStore.h"
#include "domain/PushRepository.h"
#include "models/PushNotification.h"
#include "push/PushPayloadParser.h"

#include <QDebug>
#include <optional>

namespace PushArrival {

Outcome handle(const QByteArray& message, const PairingStore& pairingStore, PushRepository& pushRepository,
               const std::function<void(const PushNotification&)>& deliver, qint64 receivedAtEpochMs)
{
    // Captured before the open, never re-read after it: see the header.
    const PairingIdentity owner = pairingStore.currentIdentity();
    if (owner.isEmpty()) {
        qWarning() << "PushArrival: dropped a push that arrived with no pairing," << message.size() << "bytes";
        return Outcome::NotPaired;
    }

    // Never falls back to parsing the raw bytes: see openPushMessage().
    const std::optional<PairingStore::OpenedPush> opened = pairingStore.openPushMessage(message);
    if (!opened.has_value()) {
        qWarning() << "PushArrival: dropped a push that did not decrypt under this pairing's keys," << message.size()
                   << "bytes";
        return Outcome::Undecryptable;
    }
    if (!pairingStore.stillCurrent(owner)) {
        qWarning() << "PushArrival: dropped a push opened for a pairing that has since changed," << message.size()
                   << "bytes";
        return Outcome::PairingChanged;
    }

    const std::optional<PushNotification> payload = PushPayloadParser::parse(opened->payload, opened->decrypted);
    if (!payload.has_value()) {
        qWarning() << "PushArrival: failed to parse push payload," << message.size() << "bytes";
        return Outcome::Unparseable;
    }
    // Shown either way -- the OS handed us this message and it cannot be
    // re-requested from the relay, so a lost history row is not a reason to
    // swallow the notification. Logged rather than ignored, because "it is
    // not in the history" is otherwise indistinguishable from "it never
    // arrived".
    if (!payload->messageId.isEmpty() && !pushRepository.recordPushArrival(*payload, receivedAtEpochMs))
        qWarning("PushArrival: a push arrived but could not be written to the notification history");
    deliver(*payload);
    return Outcome::Delivered;
}

} // namespace PushArrival
