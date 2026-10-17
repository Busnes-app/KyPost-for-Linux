#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>

#include <optional>

// The relay's snooze folder: adapters/imap/snooze.go's SnoozedFolder, a plain
// top-level folder created on the first snooze. It has no special-use role,
// so it is matched by exact name, as Android does.
inline const QString kSnoozedFolder = QStringLiteral("Snoozed");

// Android's WhenPicker.kt presets. `now` carries the zone the wall-clock
// times are read in.
struct SnoozePreset
{
    enum class Kind { LaterToday, ThisEvening, Tomorrow, NextWeek };
    Kind kind;
    QDateTime at;
};

// In display order, without the ones outside the window or repeating an
// earlier preset's time.
QVector<SnoozePreset> snoozePresets(const QDateTime& now);

// The server takes any time after now and at most 366 days ahead; the
// client keeps a minute and a day of margin for clock skew, like Android.
bool snoozeTimeInWindow(const QDateTime& at, const QDateTime& now);

// A typed date ("yyyy-MM-dd") and time ("HH:mm") in now's zone. Refused when
// unparseable, outside the window, or a wall-clock time the zone skips.
std::optional<QDateTime> snoozeCustomTime(const QString& date, const QString& time, const QDateTime& now);
