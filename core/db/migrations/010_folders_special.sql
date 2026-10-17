-- /api/inbox/folders' `special`: the folder's RFC 6154 role ("sent",
-- "drafts", ...), so a localized Sent or Drafts is found by role. NULL on rows
-- cached before this migration until the next folder listing replaces them.

ALTER TABLE folders ADD COLUMN special TEXT;
