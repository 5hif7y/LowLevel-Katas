-- notifyd: persistent notification history (SQLite).
--
-- Two layers:
--   events         append-only log. Nothing is ever updated or deleted here:
--                  every Notify, replace, close request, close, action and
--                  daemon start/stop is one row. This is the source of truth.
--   notifications  projection: the latest state of each notification id,
--                  rebuilt from `events` if it is ever lost (see REBUILD below).
--
-- Timestamps are Unix epoch milliseconds (UTC).

PRAGMA journal_mode = WAL;      -- readers (notifyd history) never block the daemon
PRAGMA synchronous = FULL;      -- every committed event survives a power cut
PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS events (
    seq             INTEGER PRIMARY KEY AUTOINCREMENT,
    ts              INTEGER NOT NULL,
    kind            TEXT    NOT NULL CHECK (kind IN (
                        'daemon_start', 'daemon_stop',
                        'notify', 'replace',
                        'close_request', 'closed', 'action')),
    notification_id INTEGER,
    client_pid      INTEGER,
    client_exe      TEXT,
    data            TEXT    NOT NULL DEFAULT '{}' CHECK (json_valid(data))
);
CREATE INDEX IF NOT EXISTS events_by_notification ON events(notification_id, seq);
CREATE INDEX IF NOT EXISTS events_by_ts ON events(ts);

CREATE TABLE IF NOT EXISTS notifications (
    id              INTEGER PRIMARY KEY,      -- id handed to the client; unique forever
    first_ts        INTEGER NOT NULL,
    last_ts         INTEGER NOT NULL,
    revision        INTEGER NOT NULL DEFAULT 1, -- bumps on every replace
    app_name        TEXT,
    app_icon        TEXT,
    summary         TEXT,
    body            TEXT,
    urgency         INTEGER,                  -- 0 low, 1 normal, 2 critical
    category        TEXT,
    desktop_entry   TEXT,
    expire_timeout  INTEGER,
    actions         TEXT NOT NULL DEFAULT '[]' CHECK (json_valid(actions)),
    hints           TEXT NOT NULL DEFAULT '{}' CHECK (json_valid(hints)),
    client_exe      TEXT,
    closed_ts       INTEGER,
    close_reason    INTEGER,                  -- 1 expired 2 dismissed 3 closed 4 undefined
    action_invoked  TEXT
);
CREATE INDEX IF NOT EXISTS notifications_by_app ON notifications(app_name, last_ts);
CREATE INDEX IF NOT EXISTS notifications_by_ts ON notifications(last_ts);

-- image-data / icon_data hints are stored once per distinct pixel buffer.
CREATE TABLE IF NOT EXISTS images (
    hash            TEXT PRIMARY KEY,         -- FNV-1a 64 of the encoded hint
    width           INTEGER, height INTEGER, rowstride INTEGER,
    has_alpha       INTEGER, bits_per_sample INTEGER, channels INTEGER,
    pixels          BLOB NOT NULL
);

-- Toasts imported from Windows' own notification store (notifyd --windows).
-- windows_id is Notification.Id in %LOCALAPPDATA%\Microsoft\Windows\Notifications\wpndatabase.db
CREATE TABLE IF NOT EXISTS windows_toasts (
    windows_id      INTEGER PRIMARY KEY,
    notification_id INTEGER NOT NULL,
    aumid           TEXT,
    arrival_ts      INTEGER,
    open            INTEGER NOT NULL DEFAULT 1  -- 0 once it left the Windows store
);

PRAGMA user_version = 2;

-- REBUILD (manual): the projection can be recomputed from the log, e.g.
--   SELECT notification_id, kind, json_extract(data,'$.summary') FROM events ORDER BY seq;
