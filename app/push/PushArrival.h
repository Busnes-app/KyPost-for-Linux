#pragma once

#include <QByteArray>
#include <functional>

class PairingStore;
class PushRepository;
struct PushNotification;

// The distributor tier's handler for one UnifiedPush message: open it under
// this pairing's keys, parse it, record it in the history and hand it to
// `deliver`. Lifted out of main.cpp's messageReceived lambda so the ownership
// check below has a test.
//
// Opening reads the secret store, and the keychain backend does that on a
// nested event loop (SecureStoreKeychain::runBlocking), so the pairing can be
// replaced while openPushMessage() is still running. The keys it returns, and
// so the payload, then belong to the previous pairing. The identity is
// captured BEFORE the open and compared after it (AGENTS.md §6d, "captured,
// not re-read", and §6g, stillCurrent()), and nothing is recorded or
// delivered unless it still matches.
namespace PushArrival {

enum class Outcome
{
    Delivered,
    NotPaired,      // no pairing to file it under; dropped unopened
    Undecryptable,  // openPushMessage() refused it
    PairingChanged, // opened, but the pairing is no longer the one it was opened for
    Unparseable,    // PushPayloadParser::parse() refused it
};

// `deliver` is the desktop notification. Called at most once, and only for
// Outcome::Delivered.
Outcome handle(const QByteArray& message, const PairingStore& pairingStore, PushRepository& pushRepository,
               const std::function<void(const PushNotification&)>& deliver, qint64 receivedAtEpochMs);

} // namespace PushArrival
