#pragma once

#include "models/Contact.h"

#include <QByteArray>
#include <QString>
#include <QVector>

// Reads a .vcf the user picked into contacts to queue as ordinary creates.
// A port of kypost-android's parseVCards (contacts/VCard.kt, #186), which is
// the spec: same caps, same refusals, same labels.
//
// The file is HOSTILE INPUT -- it came from anywhere -- so this is not
// VCardContact::contactFromVCard, which converts one card this app wrote and
// trusts it. Here the whole file is refused on any structural error or cap,
// and only the properties Android's writer writes are read. UID, KEY, PHOTO,
// CATEGORIES and every encoded value are ignored, so a file can never pin a
// PGP key, choose a server uid, put a contact in a group or smuggle bytes.
namespace VCardImport {

constexpr qsizetype kMaxBytes = 256 * 1024;
constexpr int kMaxContacts = 200;
// KyPost-Server's maxValuesPerField. It truncates past this, so refuse rather
// than store something other than what the user saw in the preview.
constexpr int kMaxValuesPerField = 64;

enum class Refusal {
    None,
    Unreadable,
    TooLarge,
    NotUtf8,
    Malformed,
    UnsupportedVersion,
    TooManyContacts,
    TooManyValues,
    Empty,
};

struct Result
{
    Refusal refusal = Refusal::None;
    QVector<Contact> contacts; // empty unless refusal == None
    int skipped = 0;           // cards with no name, email or organisation
};

// vCard 3.0/4.0, strict UTF-8, balanced BEGIN/END, at most kMaxContacts.
Result parse(const QByteArray& bytes);

// Reads at most kMaxBytes of a regular file, then parse(). Anything else (a
// directory, a FIFO that would block, a device) is Unreadable.
Result readFile(const QString& path);

} // namespace VCardImport
