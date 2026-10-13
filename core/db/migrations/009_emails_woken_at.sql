-- /api/inbox's `wokenAt`: when a snoozed message returned to INBOX. The list
-- sorts by COALESCE(woken_at, at_utc). NULL means not woken, which is every
-- row cached before this migration; the next full window or "updated" row
-- carries the current value.

ALTER TABLE emails ADD COLUMN woken_at TEXT;
