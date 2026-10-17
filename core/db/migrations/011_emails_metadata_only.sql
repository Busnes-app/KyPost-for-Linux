-- Rows loaded from an older inbox page (`before=`): no body, attachment hint
-- or PGP flags. Every row cached before this migration came from a window.

ALTER TABLE emails ADD COLUMN metadata_only INTEGER NOT NULL DEFAULT 0;
