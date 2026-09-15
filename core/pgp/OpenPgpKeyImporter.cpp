#include "pgp/OpenPgpKeyImporter.h"
#include "pgp/PgpFingerprint.h"

#include "pgp/GpgmeInit.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QScopeGuard>
#include <QProcess>
#include <algorithm>
#include <cmath>
#include <vector>
#include <QTemporaryDir>

#include <gpgme.h>

namespace {

// Same RAII shape as OpenPgpDecryptor's, and for the same reason: gpgme's API
// is C and every early return here has to release two handles.
struct ContextHandle
{
    gpgme_ctx_t handle = nullptr;
    ~ContextHandle()
    {
        if (handle != nullptr)
            gpgme_release(handle);
    }
};

struct DataHandle
{
    gpgme_data_t handle = nullptr;
    ~DataHandle()
    {
        if (handle != nullptr)
            gpgme_data_release(handle);
    }
};

// Imports into `home` and reports what gpg made of it.
//
// Refuses anything carrying more than ONE key, and returns that key's
// fingerprint plus whether anything actually changed.
//
// The refusal is the security-relevant part. gpgme_op_import() imports EVERY
// key in the blob, and gpgme_op_import_result()->imports is a LINKED LIST
// with one entry per key. Reading only the head of that list -- which this
// function used to do, under a comment claiming it stopped bundle smuggling
// -- accepted a bundle whose first key was the expected one and whose
// second, third and fourth were the attacker's: the scratch check passed on
// the head fingerprint, and the whole bundle then went into the user's real
// keyring. Measured, not assumed: a two-key export reports considered=2 with
// two distinct fingerprints in the list.
//
// Compared by fingerprint rather than by counting list entries, because a
// secret-key import legitimately reports the SAME fingerprint twice (once
// public, once secret).
bool importInto(const QString& home, const QByteArray& armored, bool requireSecret,
                QString* fingerprint, bool* changed, QString* detail, bool allowPinentry = true)
{
    ensureGpgmeInitialised();

    ContextHandle context;
    if (gpgme_err_code(gpgme_new(&context.handle)) != GPG_ERR_NO_ERROR || context.handle == nullptr) {
        *detail = QStringLiteral("could not create a gpgme context");
        return false;
    }

    const QByteArray homeUtf8 = home.toUtf8();
    if (!homeUtf8.isEmpty()
        && gpgme_err_code(gpgme_ctx_set_engine_info(context.handle, GPGME_PROTOCOL_OpenPGP, nullptr,
                                                      homeUtf8.constData()))
            != GPG_ERR_NO_ERROR) {
        *detail = QStringLiteral("could not point gpgme at the keyring");
        return false;
    }

    if (!allowPinentry && gpgme_set_pinentry_mode(context.handle, GPGME_PINENTRY_MODE_ERROR) != GPG_ERR_NO_ERROR) {
        *detail = QStringLiteral("could not disable scratch keyring prompts");
        return false;
    }
    DataHandle keyData;
    if (gpgme_err_code(gpgme_data_new_from_mem(&keyData.handle, armored.constData(),
                                                 static_cast<size_t>(armored.size()), /*copy=*/0))
        != GPG_ERR_NO_ERROR) {
        *detail = QStringLiteral("could not read the key bytes");
        return false;
    }

    if (gpgme_err_code(gpgme_op_import(context.handle, keyData.handle)) != GPG_ERR_NO_ERROR) {
        *detail = QStringLiteral("gpg refused the key");
        return false;
    }

    const gpgme_import_result_t result = gpgme_op_import_result(context.handle);
    if (result == nullptr || result->considered == 0 || result->imports == nullptr) {
        *detail = QStringLiteral("the bytes carried no OpenPGP key");
        return false;
    }

    // Every status entry, not just the head. `considered` counts what gpg was
    // asked to import even when a key was rejected or already held, so it
    // catches a bundle whose extra keys did not make it in as well.
    QString only;
    for (gpgme_import_status_t status = result->imports; status != nullptr; status = status->next) {
        if (status->result != GPG_ERR_NO_ERROR || status->fpr == nullptr) {
            *detail = QStringLiteral("gpg reported an unsuccessful key import");
            return false;
        }
        const QString fpr = QString::fromLatin1(status->fpr);
        if (!only.isEmpty() && fpr.compare(only, Qt::CaseInsensitive) != 0) {
            *detail = QStringLiteral("the bytes carried more than one OpenPGP key");
            return false;
        }
        only = fpr;
    }
    if (only.isEmpty()) {
        *detail = QStringLiteral("the bytes carried no OpenPGP key");
        return false;
    }
    if (result->considered != 1) {
        *detail = QStringLiteral("the bytes carried more than one OpenPGP key");
        return false;
    }

    *fingerprint = only;
    if (requireSecret) {
        gpgme_key_t key = nullptr;
        const gpgme_error_t lookup = gpgme_get_key(context.handle, result->imports->fpr, &key, 1);
        const bool hasSecret = gpgme_err_code(lookup) == GPG_ERR_NO_ERROR && key != nullptr && key->secret;
        if (key != nullptr)
            gpgme_key_unref(key);
        if (!hasSecret) {
            *detail = QStringLiteral("the bytes carried no private OpenPGP key");
            return false;
        }
    }
    // `unchanged` counts keys gpg already had in full. Anything else means the
    // keyring gained material -- a new key, or new signatures or user IDs on
    // one it already held.
    *changed = result->unchanged == 0 || result->secret_imported > 0;
    return true;
}

// This schema contains ASCII names, fingerprints and ASCII armor only. Decode
// strings directly into owned, zeroised bytes: QJsonValue::toString() would put
// private keys in implicitly shared QString storage. No recursive JSON walker.
struct RingMember {
    QString fingerprint;
    SecureBytes privateKey;
};

struct RingPayload {
    PgpKeyringSnapshot snapshot;
    std::vector<RingMember> members;
    bool hasCertificate = false;
};


class RingReader {
public:
    explicit RingReader(const QByteArray& bytes) : m_bytes(bytes) {}

    bool read(RingPayload& out)
    {
        if (m_bytes.size() > 128 * 1024)
            return false;
        QSet<QByteArray> fields;
        if (!take('{'))
            return false;
        do {
            QByteArray name;
            if (!field(name, fields))
                return false;
            if (name == "format") {
                SecureBytes value;
                if (!string(value, 32) || value.bytes() != "kypost-pgp-keyring-v1")
                    return false;
            } else if (name == "activeFingerprint") {
                if (!fingerprint(out.snapshot.activeFingerprint))
                    return false;
            } else if (name == "materialGeneration") {
                if (!generation(out.snapshot.materialGeneration))
                    return false;
            } else if (name == "keyFingerprints") {
                if (!take('['))
                    return false;
                do {
                    QString fpr;
                    if (out.snapshot.keyFingerprints.size() >= 256 || !fingerprint(fpr)
                        || out.snapshot.keyFingerprints.contains(fpr))
                        return false;
                    out.snapshot.keyFingerprints.append(fpr);
                } while (take(','));
                if (!take(']'))
                    return false;
            } else if (name == "keys") {
                if (!take('['))
                    return false;
                do {
                    if (out.members.size() >= 16)
                        return false;
                    RingMember member;
                    if (!key(member, out.hasCertificate))
                        return false;
                    for (const auto& prior : out.members) {
                        if (prior.fingerprint == member.fingerprint)
                            return false;
                    }
                    out.members.push_back(std::move(member));
                } while (take(','));
                if (!take(']'))
                    return false;
            } else {
                return false; // Unknown schema fields need an explicit compatibility decision.
            }
        } while (take(','));
        if (!take('}') || fields.size() != 5)
            return false;
        space();
        if (m_pos != m_bytes.size())
            return false;
        return std::count_if(out.members.begin(), out.members.end(), [&](const auto& member) {
            return member.fingerprint == out.snapshot.activeFingerprint;
        }) == 1;
    }

private:
    void space()
    {
        while (m_pos < m_bytes.size() && (m_bytes[m_pos] == ' ' || m_bytes[m_pos] == '\t'
               || m_bytes[m_pos] == '\r' || m_bytes[m_pos] == '\n'))
            ++m_pos;
    }
    bool take(char c)
    {
        space();
        if (m_pos == m_bytes.size() || m_bytes[m_pos] != c)
            return false;
        ++m_pos;
        return true;
    }
    bool string(SecureBytes& out, qsizetype limit = 128 * 1024)
    {
        if (!take('"'))
            return false;
        // Allocate once, so growth cannot leave an uncleansed private-key copy.
        out = SecureBytes(QByteArray(std::min(limit, m_bytes.size() - m_pos), '\0'));
        qsizetype used = 0;
        while (m_pos < m_bytes.size()) {
            unsigned char c = static_cast<unsigned char>(m_bytes[m_pos++]);
            if (c == '"') {
                out.bytes().resize(used);
                return true;
            }
            if (c < 0x20 || c > 0x7f)
                return false;
            if (c == '\\') {
                if (m_pos == m_bytes.size())
                    return false;
                c = static_cast<unsigned char>(m_bytes[m_pos++]);
                switch (c) {
                case '"': case '\\': case '/': break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                case 'u': {
                    unsigned value = 0;
                    for (int i = 0; i < 4; ++i) {
                        if (m_pos == m_bytes.size())
                            return false;
                        const char h = m_bytes[m_pos++];
                        const int digit = h >= '0' && h <= '9' ? h - '0'
                            : h >= 'a' && h <= 'f' ? h - 'a' + 10
                            : h >= 'A' && h <= 'F' ? h - 'A' + 10 : -1;
                        if (digit < 0)
                            return false;
                        value = value * 16 + static_cast<unsigned>(digit);
                    }
                    if (value > 0x7f)
                        return false;
                    c = static_cast<unsigned char>(value);
                    break;
                }
                default: return false;
                }
            }
            if (used == out.bytes().size())
                return false;
            out.bytes()[used++] = static_cast<char>(c);
        }
        return false;
    }
    bool fingerprint(QString& out)
    {
        SecureBytes text;
        if (!string(text, 64))
            return false;
        out = normalizedFingerprint(text.bytes());
        return !out.isEmpty();
    }
    bool field(QByteArray& out, QSet<QByteArray>& seen)
    {
        SecureBytes text;
        if (!string(text, 32) || !take(':') || seen.contains(text.bytes()))
            return false;
        out = QByteArray(text.bytes().constData(), text.bytes().size());
        seen.insert(out);
        return true;
    }
    bool generation(qint64& out)
    {
        space();
        const auto start = m_pos;
        while (m_pos < m_bytes.size() && QByteArrayView("0123456789.eE+-").contains(m_bytes[m_pos]))
            ++m_pos;
        // Only the public numeric token enters Qt's JSON parser. Accept JSON
        // integer-valued exponents/decimals too, matching Number.isSafeInteger.
        const auto doc = QJsonDocument::fromJson("[" + m_bytes.mid(start, m_pos - start) + "]");
        if (!doc.isArray() || doc.array().size() != 1 || !doc.array()[0].isDouble())
            return false;
        const double n = doc.array()[0].toDouble();
        if (n < 1 || n > 9007199254740991.0 || std::floor(n) != n)
            return false;
        out = static_cast<qint64>(n);
        return true;
    }
    bool key(RingMember& member, bool& hasCertificate)
    {
        if (!take('{'))
            return false;
        QSet<QByteArray> fields;
        do {
            QByteArray name;
            if (!field(name, fields))
                return false;
            if (name == "fingerprint") {
                if (!fingerprint(member.fingerprint))
                    return false;
            } else if (name == "privateKey") {
                if (!string(member.privateKey) || member.privateKey.isEmpty())
                    return false;
            } else if (name == "revocationCertificate") {
                SecureBytes certificate;
                if (!string(certificate))
                    return false;
                hasCertificate |= !certificate.isEmpty();
            } else {
                return false;
            }
        } while (take(','));
        return take('}') && fields.contains("fingerprint") && fields.contains("privateKey");
    }
    const QByteArray& m_bytes;
    qsizetype m_pos = 0;
};

// Key material has no reason to contain compressed/literal/encrypted packets.
// Inspect only framing before GnuPG parsing; reject compression/partial lengths
// so a 128 KiB JSON input cannot expand without bound inside the import engine.
// GnuPG still validates packet bodies, fingerprints, signatures and armor CRC.
// This stricter JSON-ring admission does not change legacy single-key imports.
bool boundedKeyPackets(const SecureBytes& armor)
{
    QByteArrayView remaining(armor.bytes());
    remaining = remaining.trimmed();
    auto line = [&]() {
        const qsizetype end = remaining.indexOf('\n');
        const auto out = remaining.first(end < 0 ? remaining.size() : end).trimmed();
        remaining = end < 0 ? QByteArrayView() : remaining.sliced(end + 1);
        return out;
    };
    if (line() != "-----BEGIN PGP PRIVATE KEY BLOCK-----")
        return false;
    bool separator = false;
    while (!remaining.empty()) {
        const auto header = line();
        if (header.empty()) { separator = true; break; }
        if (!header.contains(':'))
            return false;
    }
    if (!separator)
        return false;
    SecureBytes base64(QByteArray(armor.bytes().size(), '\0'));
    qsizetype used = 0;
    bool ended = false, crc = false;
    while (!remaining.empty()) {
        const auto part = line();
        if (part == "-----END PGP PRIVATE KEY BLOCK-----") { ended = true; break; }
        if (part.startsWith('=')) {
            if (crc || part.size() != 5)
                return false;
            crc = true;
            continue;
        }
        if (crc)
            return false;
        for (char c : part) {
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
                  || c == '+' || c == '/' || c == '='))
                return false;
            base64.bytes()[used++] = c;
        }
    }
    if (!ended || !remaining.trimmed().empty())
        return false;
    base64.bytes().resize(used);
    auto decoded = QByteArray::fromBase64Encoding(base64.bytes(), QByteArray::AbortOnBase64DecodingErrors);
    SecureBytes packets(std::move(decoded.decoded));
    if (decoded.decodingStatus != QByteArray::Base64DecodingStatus::Ok || packets.isEmpty())
        return false;
    const auto& bytes = packets.bytes();
    qsizetype pos = 0;
    int primaries = 0;
    auto octet = [&]() { return static_cast<unsigned char>(bytes[pos++]); };
    while (pos < bytes.size()) {
        const unsigned header = octet();
        if (!(header & 0x80))
            return false;
        const unsigned tag = header & 0x40 ? header & 0x3f : (header >> 2) & 0x0f;
        if (tag != 5 && tag != 7 && tag != 2 && tag != 13 && tag != 17)
            return false;
        if (tag == 5 && ++primaries != 1)
            return false;
        quint64 length = 0;
        if (header & 0x40) {
            if (pos == bytes.size()) return false;
            const unsigned first = octet();
            if (first < 192) length = first;
            else if (first < 224) {
                if (pos == bytes.size()) return false;
                length = (first - 192) * 256 + octet() + 192;
            } else if (first == 255) {
                if (bytes.size() - pos < 4) return false;
                for (int i = 0; i < 4; ++i) length = (length << 8) | octet();
            } else return false; // Partial lengths belong to streaming data, not keys.
        } else {
            const unsigned kind = header & 3;
            if (kind == 3) return false;
            const int count = 1 << kind;
            if (bytes.size() - pos < count) return false;
            for (int i = 0; i < count; ++i) length = (length << 8) | octet();
        }
        if (length == 0 || length > static_cast<quint64>(bytes.size() - pos))
            return false;
        pos += static_cast<qsizetype>(length);
    }
    return primaries == 1;
}

// KEYINFO is agent metadata, never secret material. A fresh scratch agent has
// no cached passphrase: D/C means an on-disk, unprotected private key. Tokens,
// missing packets and protected keys are not a complete unprotected JSON ring.
// See GnuPG agent/command.c's KEYINFO protocol and GPGME's Assuan protocol API.
struct KeyInfo {
    QByteArray grip;
    bool clear = false;
    int replies = 0;
};
gpgme_error_t keyInfoStatus(void* opaque, const char* status, const char* args)
{
    auto& info = *static_cast<KeyInfo*>(opaque);
    if (QByteArrayView(status) == "KEYINFO") {
        ++info.replies;
        const auto fields = QByteArray(args).split(' ');
        info.clear = fields.size() >= 6 && fields[0] == info.grip
            && fields[1] == "D" && fields[5] == "C";
    }
    return GPG_ERR_NO_ERROR;
}

bool agentContext(ContextHandle& context, const QString& home)
{
    // Assuan's home_dir does NOT select an agent. Resolve the socket for the
    // disposable home explicitly, or KEYINFO would query the user's agent.
    QProcess locate;
    locate.start(QStringLiteral("gpgconf"), {QStringLiteral("--homedir"), home,
        QStringLiteral("--list-dirs"), QStringLiteral("agent-socket")});
    if (!locate.waitForStarted(5000) || !locate.waitForFinished(5000)
        || locate.exitStatus() != QProcess::NormalExit || locate.exitCode() != 0)
        return false;
    const QByteArray socket = QByteArray::fromPercentEncoding(locate.readAllStandardOutput().trimmed());
    if (socket.isEmpty() || socket.contains('\0'))
        return false;
    return gpgme_new(&context.handle) == GPG_ERR_NO_ERROR
        && gpgme_set_protocol(context.handle, GPGME_PROTOCOL_ASSUAN) == GPG_ERR_NO_ERROR
        && gpgme_ctx_set_engine_info(context.handle, GPGME_PROTOCOL_ASSUAN, socket.constData(),
                                     "") == GPG_ERR_NO_ERROR;
}

struct ScratchKeyring {
    QTemporaryDir directory;
    ~ScratchKeyring()
    {
        if (!directory.isValid())
            return;
        // Close GPGME contexts before asking gpgconf to stop this agent. Sending
        // KILLAGENT through a live Assuan context makes its destructor write BYE
        // to a closed socket (SIGPIPE under QtTest's crash handler).
        QProcess stop;
        stop.start(QStringLiteral("gpgconf"), {QStringLiteral("--homedir"), directory.path(),
            QStringLiteral("--kill"), QStringLiteral("gpg-agent")});
        if (!stop.waitForStarted(5000) || !stop.waitForFinished(5000)
            || stop.exitStatus() != QProcess::NormalExit || stop.exitCode() != 0) {
            stop.kill();
            // Directory removal still proceeds; the agent also watches its home.
        }
    }
};

bool secretInventory(const QString& home, const QString& fingerprint,
                     bool requireUnprotected, QSet<QString>& inventory)
{
    ContextHandle context;
    const auto path = home.toUtf8();
    if (gpgme_new(&context.handle) != GPG_ERR_NO_ERROR
        || gpgme_ctx_set_engine_info(context.handle, GPGME_PROTOCOL_OpenPGP, nullptr,
            path.isEmpty() ? nullptr : path.constData()) != GPG_ERR_NO_ERROR
        || gpgme_set_keylist_mode(context.handle, GPGME_KEYLIST_MODE_LOCAL
            | GPGME_KEYLIST_MODE_WITH_SECRET | GPGME_KEYLIST_MODE_WITH_KEYGRIP) != GPG_ERR_NO_ERROR)
        return false;
    gpgme_key_t key = nullptr;
    const auto fpr = fingerprint.toLatin1();
    const auto lookup = gpgme_get_key(context.handle, fpr.constData(), &key, 0);
    const auto release = qScopeGuard([&] { if (key) gpgme_key_unref(key); });
    if (lookup != GPG_ERR_NO_ERROR || !key)
        return false;
    if (!key->subkeys || !key->subkeys->fpr || fingerprint != QString::fromLatin1(key->subkeys->fpr))
        return false;
    ContextHandle agent;
    if (requireUnprotected && !agentContext(agent, home))
        return false;
    for (auto sub = key->subkeys; sub; sub = sub->next) {
        if (!sub->secret) {
            if (requireUnprotected)
                return false;
            continue; // Existing destination public-only subkeys are not incoming material.
        }
        if (!sub->fpr || inventory.size() >= 256)
            return false;
        const auto actual = normalizedFingerprint(QByteArray(sub->fpr));
        if (actual.isEmpty() || inventory.contains(actual))
            return false;
        if (requireUnprotected) {
            if (!sub->keygrip || sub->is_cardkey)
                return false;
            KeyInfo info{QByteArray(sub->keygrip)};
            // A keygrip is a public 20-byte identifier, not arbitrary Assuan.
            if (info.grip.size() != 40 || normalizedFingerprint(info.grip).isEmpty())
                return false;
            const QByteArray command = "KEYINFO " + info.grip;
            gpgme_error_t operation = GPG_ERR_GENERAL;
            const auto transport = gpgme_op_assuan_transact_ext(agent.handle, command.constData(),
                nullptr, nullptr, nullptr, nullptr, keyInfoStatus, &info, &operation);
            if (transport != GPG_ERR_NO_ERROR || operation != GPG_ERR_NO_ERROR
                || info.replies != 1 || !info.clear)
                return false;
        }
        inventory.insert(actual);
    }
    return true;
}

} // namespace

PgpImportResult importPublicKey(const QByteArray& armoredPublicKey, const QString& expectedFingerprint,
                                 const QString& homeDirectory)
{
    PgpImportResult out;

    if (armoredPublicKey.trimmed().isEmpty()) {
        out.status = PgpImportStatus::Rejected;
        out.detail = QStringLiteral("no key bytes");
        return out;
    }

    ensureGpgmeInitialised();
    if (gpgme_err_code(gpgme_engine_check_version(GPGME_PROTOCOL_OpenPGP)) != GPG_ERR_NO_ERROR) {
        out.status = PgpImportStatus::EngineUnavailable;
        return out;
    }

    // Step one, in a keyring nobody owns: find out what this actually IS
    // before the user's own keyring is touched. A mismatch discovered after
    // importing would leave this code deleting from a keyring it did not
    // create, and an interrupted run would leave the bad key behind.
    QTemporaryDir scratch;
    if (!scratch.isValid()) {
        out.status = PgpImportStatus::Rejected;
        out.detail = QStringLiteral("could not create a scratch keyring");
        return out;
    }
    // gpg refuses a home directory others can read.
    if (!QFile::setPermissions(scratch.path(),
            QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner)) {
        out.status = PgpImportStatus::Rejected;
        out.detail = QStringLiteral("could not protect the scratch keyring");
        return out;
    }

    QString observed;
    bool changedInScratch = false;
    QString detail;
    if (!importInto(scratch.path(), armoredPublicKey, false, &observed, &changedInScratch, &detail)) {
        out.status = PgpImportStatus::Rejected;
        out.detail = detail;
        return out;
    }
    out.fingerprint = observed;

    if (!expectedFingerprint.isEmpty()
        && observed.compare(expectedFingerprint, Qt::CaseInsensitive) != 0) {
        // The relay's key and the relay's claim about that key disagree. That
        // is not a reason to pick one of them.
        out.status = PgpImportStatus::Rejected;
        out.detail = QStringLiteral("fingerprint mismatch");
        return out;
    }

    // Step two: the real keyring, only now.
    QString importedFingerprint;
    bool changed = false;
    if (!importInto(homeDirectory, armoredPublicKey, false, &importedFingerprint, &changed, &detail)) {
        out.status = PgpImportStatus::Rejected;
        out.detail = detail;
        return out;
    }

    out.fingerprint = importedFingerprint;
    out.status = changed ? PgpImportStatus::Imported : PgpImportStatus::Unchanged;
    return out;
}

PgpImportResult importPrivateKey(const SecureBytes& armoredPrivateKey, const QString& expectedFingerprint,
                                  const QString& homeDirectory)
{
    PgpImportResult out;
    if (armoredPrivateKey.isEmpty()) {
        out.status = PgpImportStatus::Rejected;
        out.detail = QStringLiteral("no key bytes");
        return out;
    }
    ensureGpgmeInitialised();
    if (gpgme_err_code(gpgme_engine_check_version(GPGME_PROTOCOL_OpenPGP)) != GPG_ERR_NO_ERROR) {
        out.status = PgpImportStatus::EngineUnavailable;
        return out;
    }
    QTemporaryDir scratch;
    if (!scratch.isValid()) {
        out.status = PgpImportStatus::Rejected;
        out.detail = QStringLiteral("could not create a scratch keyring");
        return out;
    }
    if (!QFile::setPermissions(scratch.path(),
            QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner)) {
        out.status = PgpImportStatus::Rejected;
        out.detail = QStringLiteral("could not protect the scratch keyring");
        return out;
    }
    QString observed;
    bool scratchChanged = false;
    QString detail;
    if (!importInto(scratch.path(), armoredPrivateKey.bytes(), true, &observed, &scratchChanged, &detail)) {
        out.status = PgpImportStatus::Rejected;
        out.detail = detail;
        return out;
    }
    out.fingerprint = observed;
    if (expectedFingerprint.isEmpty()
        || observed.compare(expectedFingerprint, Qt::CaseInsensitive) != 0) {
        out.status = PgpImportStatus::Rejected;
        out.detail = QStringLiteral("fingerprint mismatch");
        return out;
    }
    QString imported;
    bool changed = false;
    if (!importInto(homeDirectory, armoredPrivateKey.bytes(), true, &imported, &changed, &detail)) {
        out.status = PgpImportStatus::Rejected;
        out.detail = detail;
        return out;
    }
    out.fingerprint = imported;
    out.status = changed ? PgpImportStatus::Imported : PgpImportStatus::Unchanged;
    return out;
}

PgpKeyringImportStatus importPrivateKeyring(const SecureBytes& keyringJson,
    const PgpKeyringSnapshot& expected, const QString& homeDirectory,
    const std::function<bool()>& stillCurrent)
{
    RingPayload ring;
    if (!RingReader(keyringJson.bytes()).read(ring))
        return PgpKeyringImportStatus::Rejected;
    if (ring.hasCertificate)
        return PgpKeyringImportStatus::UnsupportedRevocationCertificate;
    if (expected.keyFingerprints.size() > 256)
        return PgpKeyringImportStatus::Rejected;
    QSet<QString> expectedInventory;
    for (const auto& fpr : expected.keyFingerprints) {
        const auto normalized = normalizedFingerprint(fpr.toLatin1());
        if (normalized.isEmpty() || expectedInventory.contains(normalized))
            return PgpKeyringImportStatus::Rejected;
        expectedInventory.insert(normalized);
    }
    const QSet<QString> claimed(ring.snapshot.keyFingerprints.begin(), ring.snapshot.keyFingerprints.end());
    if (ring.snapshot.activeFingerprint != normalizedFingerprint(expected.activeFingerprint.toLatin1())
        || ring.snapshot.materialGeneration != expected.materialGeneration || claimed != expectedInventory)
        return PgpKeyringImportStatus::Rejected;
    ensureGpgmeInitialised();
    if (gpgme_engine_check_version(GPGME_PROTOCOL_OpenPGP) != GPG_ERR_NO_ERROR)
        return PgpKeyringImportStatus::EngineUnavailable;
    ScratchKeyring scratch;
    if (!scratch.directory.isValid() || !QFile::setPermissions(scratch.directory.path(),
            QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner))
        return PgpKeyringImportStatus::Rejected;
    QSet<QString> actual;
    for (const auto& member : ring.members) {
        if (stillCurrent && !stillCurrent())
            return PgpKeyringImportStatus::Cancelled;
        QString observed, detail;
        bool changed = false;
        if (!boundedKeyPackets(member.privateKey)
            || !importInto(scratch.directory.path(), member.privateKey.bytes(), true, &observed, &changed, &detail, false)
            || observed != member.fingerprint
            || !secretInventory(scratch.directory.path(), member.fingerprint, true, actual))
            return PgpKeyringImportStatus::Rejected;
    }
    if (actual != claimed)
        return PgpKeyringImportStatus::Rejected;
    bool anyChanged = false;
    for (const auto& member : ring.members) {
        if (stillCurrent && !stillCurrent())
            return PgpKeyringImportStatus::Cancelled;
        QString observed, detail;
        bool changed = false;
        if (!importInto(homeDirectory, member.privateKey.bytes(), true, &observed, &changed, &detail)
            || observed != member.fingerprint)
            return PgpKeyringImportStatus::ImportFailed;
        QSet<QString> persisted;
        if (!secretInventory(homeDirectory, member.fingerprint, false, persisted))
            return PgpKeyringImportStatus::ImportFailed;
        // Existing keys may have additional subkeys; merging must not remove them.
        // All incoming subkeys for this member must still be durably available.
        QSet<QString> incoming;
        if (!secretInventory(scratch.directory.path(), member.fingerprint, false, incoming)
            || !persisted.contains(incoming))
            return PgpKeyringImportStatus::ImportFailed;
        anyChanged |= changed;
    }
    if (stillCurrent && !stillCurrent())
        return PgpKeyringImportStatus::Cancelled;
    return anyChanged ? PgpKeyringImportStatus::Imported : PgpKeyringImportStatus::Unchanged;
}
