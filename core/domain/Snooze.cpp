#include "domain/Snooze.h"

#include <QTimeZone>

#include <algorithm>

namespace {

constexpr qint64 kMinAheadSecs = 60;
constexpr qint64 kMaxAheadSecs = 365 * 24 * 3600;

QDateTime wallClock(const QDate& date, const QTime& time, const QDateTime& now)
{
    return QDateTime(date, time, now.timeZone());
}

// Rounds down to the hour in local wall time, keeping the instant's own UTC
// offset (java.time's truncatedTo(HOURS)), so a fall-back hour stays put.
QDateTime truncatedToHour(qint64 secs, const QTimeZone& zone)
{
    const int offset = QDateTime::fromSecsSinceEpoch(secs, zone).offsetFromUtc();
    qint64 local = secs + offset;
    local -= ((local % 3600) + 3600) % 3600;
    return QDateTime::fromSecsSinceEpoch(local - offset, zone);
}

} // namespace

bool snoozeTimeInWindow(const QDateTime& at, const QDateTime& now)
{
    if (!at.isValid() || !now.isValid())
        return false;
    const qint64 ahead = now.secsTo(at);
    return ahead >= kMinAheadSecs && ahead <= kMaxAheadSecs;
}

QVector<SnoozePreset> snoozePresets(const QDateTime& now)
{
    using Kind = SnoozePreset::Kind;
    const QDate today = now.date();
    const QDateTime later = truncatedToHour(now.toSecsSinceEpoch() + 3 * 3600, now.timeZone());
    const QDateTime evening = wallClock(today, QTime(18, 0), now);
    const QTime morning(8, 0);

    QVector<SnoozePreset> candidates;
    if (later.date() == today)
        candidates.append({ Kind::LaterToday, later });
    // Within the hour it is not "this evening", it is now.
    if (evening > now.addSecs(3600))
        candidates.append({ Kind::ThisEvening, evening });
    candidates.append({ Kind::Tomorrow, wallClock(today.addDays(1), morning, now) });
    candidates.append({ Kind::NextWeek, wallClock(today.addDays(8 - today.dayOfWeek()), morning, now) });

    QVector<SnoozePreset> presets;
    for (const SnoozePreset& preset : candidates) {
        const bool repeat = std::any_of(presets.cbegin(), presets.cend(),
                                        [&](const SnoozePreset& p) { return p.at == preset.at; });
        if (!repeat && snoozeTimeInWindow(preset.at, now))
            presets.append(preset);
    }
    return presets;
}

std::optional<QDateTime> snoozeCustomTime(const QString& date, const QString& time, const QDateTime& now)
{
    const QDate d = QDate::fromString(date.trimmed(), QStringLiteral("yyyy-MM-dd"));
    const QTime t = QTime::fromString(time.trimmed(), QStringLiteral("HH:mm"));
    if (!d.isValid() || !t.isValid())
        return std::nullopt;
    const QDateTime at = wallClock(d, t, now);
    // A time the zone skips (spring forward) comes back moved or invalid.
    if (!at.isValid() || at.date() != d || at.time() != t || !snoozeTimeInWindow(at, now))
        return std::nullopt;
    return at;
}
