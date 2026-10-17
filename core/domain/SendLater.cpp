#include "domain/SendLater.h"

#include <QTimeZone>

#include <algorithm>

namespace {

constexpr qint64 kDay = 24 * 3600;

bool within(const QDateTime& at, const QDateTime& now, qint64 minSecs, qint64 maxSecs)
{
    return at.isValid() && at >= now.addSecs(minSecs) && at <= now.addSecs(maxSecs);
}

QDateTime on(const QDate& date, int hour, const QDateTime& now)
{
    return QDateTime(date, QTime(hour, 0), now.timeZone());
}

} // namespace

bool sendAtAccepted(const QDateTime& at, const QDateTime& now)
{
    return within(at, now, 60, 366 * kDay);
}

bool sendLaterOffered(const QDateTime& at, const QDateTime& now)
{
    return within(at, now, 5 * 60, 365 * kDay);
}

QVector<SendLaterChoice> sendLaterChoices(const QDateTime& now)
{
    const QDate today = now.date();
    const QDateTime later = now.addSecs(3 * 3600);
    const QDateTime laterOnTheHour = on(later.date(), later.time().hour(), now);
    const QDateTime evening = on(today, 18, now);

    QVector<SendLaterChoice> candidates;
    if (laterOnTheHour.date() == today)
        candidates.append({ SendLaterPreset::LaterToday, laterOnTheHour });
    // Within the hour it is not "this evening", it is now.
    if (evening > now.addSecs(3600))
        candidates.append({ SendLaterPreset::ThisEvening, evening });
    candidates.append({ SendLaterPreset::Tomorrow, on(today.addDays(1), 8, now) });
    candidates.append({ SendLaterPreset::NextWeek, on(today.addDays(8 - today.dayOfWeek()), 8, now) });

    QVector<SendLaterChoice> out;
    for (const SendLaterChoice& choice : candidates) {
        const bool repeated = std::any_of(out.cbegin(), out.cend(),
                                          [&](const SendLaterChoice& c) { return c.at == choice.at; });
        if (!repeated && sendLaterOffered(choice.at, now))
            out.append(choice);
    }
    return out;
}
