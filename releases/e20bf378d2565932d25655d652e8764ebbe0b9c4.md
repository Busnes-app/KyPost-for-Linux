# KyPost source e20bf378d256

Source: https://github.com/Busnes-app/KyPost-for-Linux/commit/e20bf378d2565932d25655d652e8764ebbe0b9c4

[Full release history](https://github.com/Busnes-app/KyPost-for-Linux/blob/e20bf378d2565932d25655d652e8764ebbe0b9c4/CHANGELOG.md)

### Changes

- Accept, Maybe and Decline on a calendar invite now answer through
  KyCalendar first, when your server has it on: your own calendar copy is
  updated and no email is sent. When KyCalendar is not in use, or has no
  event for that invite, the app says so and, after you confirm, emails the
  answer to the organizer as before. Any other refusal is shown and nothing
  is emailed.
- The invite card shows "Cancelled" for a cancellation, says what changed
  when an invite you already answered is updated and asks again, and offers
  "Join meeting" for an `https:` meeting link, naming its host and opening it
  in your browser. Before/after screenshots are in `docs/screenshots/`
  (`kypost-linux-131-*`).

### Upgrade impact

- No database, keyring or configuration migration. Answers given are
  remembered only while the app runs, so an update arriving after a restart
  shows the plain "This invite was updated" notice.
