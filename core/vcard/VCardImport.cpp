#include "vcard/VCardImport.h"

#include <QDate>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QStringList>
#include <optional>

namespace VCardImport {
namespace {

struct Property
{
    QString name;      // upper case, any "group." prefix dropped
    QStringList types; // lower case, in file order, no duplicates
    bool encoded = false;
    QString value;     // still escaped
};

// Android's IGNORED_TYPES: qualifiers, not labels.
bool isIgnoredType(const QString& type)
{
    static const QStringList ignored = {QStringLiteral("pref"), QStringLiteral("voice"), QStringLiteral("internet"),
                                        QStringLiteral("text"), QStringLiteral("x400"), QStringLiteral("msg")};
    return ignored.contains(type);
}

bool isPropertyName(const QString& name)
{
    if (name.isEmpty())
        return false;
    for (const QChar ch : name) {
        if (!(ch.unicode() < 128 && (ch.isLetterOrNumber() || ch == QLatin1Char('-'))))
            return false;
    }
    return true;
}

// `group.NAME;PARAM=a,"b;c";BARE:value`; nullopt when there is no unquoted colon.
std::optional<Property> parseProperty(const QString& line)
{
    QStringList parts;
    QString current;
    bool quoted = false;
    for (qsizetype i = 0; i < line.size(); ++i) {
        const QChar ch = line.at(i);
        if (ch == QLatin1Char('"')) {
            quoted = !quoted;
        } else if (ch == QLatin1Char(';') && !quoted) {
            parts << current;
            current.clear();
        } else if (ch == QLatin1Char(':') && !quoted) {
            parts << current;
            QString name = parts.first();
            const qsizetype dot = name.indexOf(QLatin1Char('.'));
            if (dot >= 0)
                name = name.mid(dot + 1);
            if (!isPropertyName(name))
                return std::nullopt;
            Property property;
            property.name = name.toUpper();
            for (qsizetype p = 1; p < parts.size(); ++p) {
                const QString& param = parts.at(p);
                const qsizetype eq = param.indexOf(QLatin1Char('='));
                const QString key = eq < 0 ? QString() : param.left(eq).toUpper();
                const QString value = eq < 0 ? param : param.mid(eq + 1);
                if (key == QLatin1String("ENCODING")) {
                    property.encoded = true;
                } else if (key == QLatin1String("TYPE") || key.isEmpty()) {
                    for (const QString& type : value.split(QLatin1Char(','))) {
                        const QString t = type.trimmed().toLower();
                        if (!t.isEmpty() && !property.types.contains(t))
                            property.types << t;
                    }
                }
            }
            property.value = line.mid(i + 1);
            return property;
        } else {
            current += ch;
        }
    }
    return std::nullopt;
}

// Undoes the writer's escaping; drops control characters other than newline,
// so nothing from the file can break a line in the UI or a header.
QString unescapeText(const QString& s)
{
    QString out;
    out.reserve(s.size());
    for (qsizetype i = 0; i < s.size();) {
        QChar ch = s.at(i++);
        if (ch == QLatin1Char('\\')) {
            if (i == s.size())
                break;
            ch = s.at(i++);
            if (ch == QLatin1Char('n') || ch == QLatin1Char('N'))
                ch = QLatin1Char('\n');
        }
        if ((ch >= QLatin1Char(' ') && ch.unicode() != 0x7f) || ch == QLatin1Char('\n'))
            out += ch;
    }
    return out;
}

// Splits on unescaped `;`, then unescapes each component.
QStringList splitComponents(const QString& value)
{
    QStringList parts;
    QString current;
    for (qsizetype i = 0; i < value.size(); ++i) {
        const QChar ch = value.at(i);
        if (ch == QLatin1Char('\\') && i + 1 < value.size()) {
            current += ch;
            current += value.at(++i);
        } else if (ch == QLatin1Char(';')) {
            parts << unescapeText(current);
            current.clear();
        } else {
            current += ch;
        }
    }
    parts << unescapeText(current);
    return parts;
}

std::optional<QString> nonBlank(const QString& s)
{
    return s.trimmed().isEmpty() ? std::nullopt : std::optional<QString>(s);
}

std::optional<QString> at(const QStringList& parts, int i)
{
    return i < parts.size() ? nonBlank(parts.at(i)) : std::nullopt;
}

// Android's label(): the names its writer gives, others keep their own.
std::optional<QString> label(const QStringList& types)
{
    QStringList t;
    for (const QString& type : types) {
        if (!isIgnoredType(type))
            t << type;
    }
    if (t.contains(QLatin1String("cell")))
        return QStringLiteral("Mobile");
    if (t.contains(QLatin1String("fax")) && t.contains(QLatin1String("work")))
        return QStringLiteral("Work Fax");
    if (t.contains(QLatin1String("fax")) && t.contains(QLatin1String("home")))
        return QStringLiteral("Home Fax");
    if (t.contains(QLatin1String("home")))
        return QStringLiteral("Home");
    if (t.contains(QLatin1String("work")))
        return QStringLiteral("Work");
    if (t.isEmpty())
        return std::nullopt;
    QString first = t.first();
    first[0] = first.at(0).toUpper();
    return first;
}

// yyyymmdd or yyyy-mm-dd[T...], and a real calendar day; anything else is dropped.
std::optional<QString> birthday(const std::optional<QString>& raw)
{
    if (!raw)
        return std::nullopt;
    const QString v = raw->trimmed();
    static const QRegularExpression basic(QStringLiteral("^\\d{8}$"));
    static const QRegularExpression extended(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}(T.*)?$"));
    QString iso;
    if (basic.match(v).hasMatch())
        iso = v.left(4) + QLatin1Char('-') + v.mid(4, 2) + QLatin1Char('-') + v.mid(6, 2);
    else if (extended.match(v).hasMatch())
        iso = v.left(10);
    else
        return std::nullopt;
    if (!QDate(iso.left(4).toInt(), iso.mid(5, 2).toInt(), iso.mid(8, 2).toInt()).isValid())
        return std::nullopt;
    return iso;
}

// One card. Sets `refusal` and returns nullopt to refuse the file; returns
// nullopt with `refusal` untouched for a card with nothing to call it by.
std::optional<Contact> toContact(const QVector<Property>& properties, Refusal& refusal)
{
    std::optional<QString> version;
    for (const Property& p : properties) {
        if (p.name == QLatin1String("VERSION")) {
            version = p.value.trimmed();
            break;
        }
    }
    if (!version) {
        refusal = Refusal::Malformed;
        return std::nullopt;
    }
    if (*version != QLatin1String("3.0") && *version != QLatin1String("4.0")) {
        refusal = Refusal::UnsupportedVersion;
        return std::nullopt;
    }

    QHash<QString, QVector<Property>> byName;
    for (const Property& p : properties) {
        if (!p.encoded)
            byName[p.name] << p;
    }
    auto all = [&](const char* name) -> std::optional<QVector<Property>> {
        const QVector<Property> values = byName.value(QLatin1String(name));
        if (values.size() > kMaxValuesPerField) {
            refusal = Refusal::TooManyValues;
            return std::nullopt;
        }
        return values;
    };
    auto text = [&](const char* name) -> std::optional<QString> {
        const QVector<Property> values = byName.value(QLatin1String(name));
        return values.isEmpty() ? std::nullopt : nonBlank(unescapeText(values.first().value));
    };
    auto parts = [&](const char* name) -> QStringList {
        const QVector<Property> values = byName.value(QLatin1String(name));
        return values.isEmpty() ? QStringList() : splitComponents(values.first().value);
    };

    const auto emails = all("EMAIL");
    const auto phones = all("TEL");
    const auto addresses = all("ADR");
    const auto urls = all("URL");
    if (!emails || !phones || !addresses || !urls)
        return std::nullopt;

    Contact contact;
    for (const Property& p : *emails) {
        const QString value = unescapeText(p.value).trimmed();
        if (!value.isEmpty())
            contact.emails.append({label(p.types), value});
    }
    for (const Property& p : *phones) {
        QString value = unescapeText(p.value).trimmed();
        if (value.startsWith(QLatin1String("tel:"), Qt::CaseInsensitive))
            value = value.mid(4);
        if (!value.isEmpty())
            contact.phones.append({label(p.types), value});
    }
    for (const Property& p : *addresses) {
        const QStringList a = splitComponents(p.value);
        ContactAddressEntry entry{label(p.types), at(a, 2), at(a, 3), at(a, 4), at(a, 5), at(a, 6)};
        if (entry.street || entry.city || entry.region || entry.postalCode || entry.country)
            contact.addresses.append(entry);
    }
    for (const Property& p : *urls) {
        const QString value = unescapeText(p.value).trimmed();
        if (!value.isEmpty())
            contact.websites.append({label(p.types), value});
    }

    const QStringList n = parts("N");
    const QStringList org = parts("ORG");
    contact.familyName = at(n, 0);
    contact.givenName = at(n, 1);
    contact.middleName = at(n, 2);
    contact.prefix = at(n, 3);
    contact.suffix = at(n, 4);
    contact.nickname = text("NICKNAME");
    contact.org = at(org, 0);
    contact.department = at(org, 1);
    contact.title = text("TITLE");
    contact.birthday = birthday(text("BDAY"));
    contact.notes = text("NOTE");

    contact.fn = text("FN");
    if (!contact.fn) {
        QStringList name;
        for (const auto& part : {contact.prefix, contact.givenName, contact.middleName, contact.familyName,
                                 contact.suffix}) {
            if (part)
                name << *part;
        }
        contact.fn = nonBlank(name.join(QLatin1Char(' ')));
    }
    if (!contact.fn)
        contact.fn = contact.org;
    if (!contact.fn && !contact.emails.isEmpty())
        contact.fn = contact.emails.first().value;
    if (!contact.fn)
        return std::nullopt; // skipped, not refused
    return contact;
}

Result refused(Refusal refusal)
{
    Result result;
    result.refusal = refusal;
    return result;
}

} // namespace

Result parse(const QByteArray& raw)
{
    if (raw.size() > kMaxBytes)
        return refused(Refusal::TooLarge);
    const QByteArray bytes = raw.startsWith("\xEF\xBB\xBF") ? raw.mid(3) : raw;
    // Strict UTF-8: anything fromUtf8 had to replace does not survive the trip back.
    const QString decoded = QString::fromUtf8(bytes);
    if (decoded.toUtf8() != bytes)
        return refused(Refusal::NotUtf8);

    QString text = decoded;
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    static const QRegularExpression fold(QStringLiteral("\n[ \t]"));
    text.remove(fold);

    Result result;
    std::optional<QVector<Property>> card;
    for (const QString& line : text.split(QLatin1Char('\n'))) {
        if (line.trimmed().isEmpty())
            continue;
        const std::optional<Property> property = parseProperty(line);
        if (!property)
            return refused(Refusal::Malformed);
        const bool begin = property->name == QLatin1String("BEGIN");
        const bool end = property->name == QLatin1String("END");
        if ((begin || end) && property->value.compare(QLatin1String("VCARD"), Qt::CaseInsensitive) != 0)
            return refused(Refusal::Malformed);
        if (begin) {
            if (card)
                return refused(Refusal::Malformed);
            if (result.contacts.size() + result.skipped >= kMaxContacts)
                return refused(Refusal::TooManyContacts);
            card.emplace();
        } else if (end) {
            if (!card)
                return refused(Refusal::Malformed);
            Refusal refusal = Refusal::None;
            const std::optional<Contact> contact = toContact(*card, refusal);
            if (refusal != Refusal::None)
                return refused(refusal);
            if (contact)
                result.contacts << *contact;
            else
                ++result.skipped;
            card.reset();
        } else if (!card) {
            return refused(Refusal::Malformed);
        } else {
            card->append(*property);
        }
    }
    if (card)
        return refused(Refusal::Malformed);
    if (result.contacts.isEmpty())
        return refused(Refusal::Empty);
    return result;
}

Result readFile(const QString& path)
{
    const QFileInfo info(path);
    if (!info.isFile())
        return refused(Refusal::Unreadable);
    if (info.size() > kMaxBytes)
        return refused(Refusal::TooLarge);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return refused(Refusal::Unreadable);
    // One byte past the cap tells "exactly at the limit" from "over it" even
    // when the file grew after the size check.
    const QByteArray bytes = file.read(kMaxBytes + 1);
    if (file.error() != QFileDevice::NoError)
        return refused(Refusal::Unreadable);
    return parse(bytes);
}

} // namespace VCardImport
