#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>

// Scheduled send (relay: scheduled_send.go). The relay takes a sendAt between
// one minute and 366 days ahead. The picker offers five minutes to 365 days,
// as Android does, so a pick survives a pinentry prompt and some clock skew.

// Inside the relay's window, measured from now.
bool sendAtAccepted(const QDateTime& at, const QDateTime& now);

// Inside the picker's window, measured from now.
bool sendLaterOffered(const QDateTime& at, const QDateTime& now);

enum class SendLaterPreset
{
    LaterToday,  // three hours on, on the hour, if still today
    ThisEvening, // 18:00 today, if more than an hour away
    Tomorrow,    // 08:00 tomorrow
    NextWeek,    // 08:00 next Monday
};

struct SendLaterChoice
{
    SendLaterPreset preset;
    QDateTime at;
};

// Android's presets (WhenPicker.kt), in now's time zone, each inside the picker
// window and each moment offered once.
QVector<SendLaterChoice> sendLaterChoices(const QDateTime& now);
