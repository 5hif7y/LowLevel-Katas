/* notifyd - a notification daemon that never forgets.
 *
 * Owns the wnotify pipe (org.freedesktop.Notifications semantics, see
 * ../wnotify/PROTOCOL.md) and writes every event into an append-only SQLite
 * log before anything else happens. Display is delegated:
 *
 *   notifyd                          headless: record only (like tiramisu)
 *   notifyd --exec herbe.exe         one herbe process per notification
 *   notifyd --forward \\.\pipe\X     chain to another daemon (dunst, tiramisu)
 *                                    started with WNOTIFY_PIPE=\\.\pipe\X
 *   notifyd --windows                also import the toasts of every Windows app
 *                                    (read from Windows' own notification store)
 *
 * Queries (read-only, safe while the daemon runs thanks to WAL):
 *   notifyd history [-n N] [--app APP] [--search TEXT] [--json]
 *   notifyd show ID
 *   notifyd events [-n N] [--since SEQ]
 *   notifyd stats
 *   notifyd tail                     live events from the running daemon
 */
#define WIN32_LEAN_AND_MEAN
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <windows.h>
#include <shlobj.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <sqlite3.h>

#include "../wnotify/wnotify.h"

#define NOTIFYD_VERSION "0.1"

static const char SCHEMA[] =
#include "schema.inc"
	;

/* ---- small utils ---------------------------------------------------------- */

static void die(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fputs("notifyd: ", stderr);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
	exit(1);
}

static long long now_ms(void)
{
	FILETIME ft;
	GetSystemTimePreciseAsFileTime(&ft);
	ULARGE_INTEGER u = {.LowPart = ft.dwLowDateTime, .HighPart = ft.dwHighDateTime};
	return (long long)(u.QuadPart / 10000ULL) - 11644473600000LL; /* 1601 -> 1970 */
}

typedef struct {
	char *s;
	size_t len, cap;
} sbuf;

static void sb_add(sbuf *b, const char *s, size_t n)
{
	if (b->len + n + 1 > b->cap) {
		b->cap = (b->len + n + 1) * 2;
		b->s = realloc(b->s, b->cap);
	}
	memcpy(b->s + b->len, s, n);
	b->len += n;
	b->s[b->len] = 0;
}
static void sb_cat(sbuf *b, const char *s) { sb_add(b, s, strlen(s)); }
static void sb_printf(sbuf *b, const char *fmt, ...)
{
	char tmp[256];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
	va_end(ap);
	sb_add(b, tmp, n < 0 ? 0 : (size_t)n);
}

static void sb_json_str(sbuf *b, const char *s)
{
	sb_cat(b, "\"");
	for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; p++) {
		if (*p == '"' || *p == '\\') {
			char e[3] = {'\\', (char)*p, 0};
			sb_cat(b, e);
		} else if (*p < 0x20) {
			sb_printf(b, "\\u%04x", *p);
		} else {
			sb_add(b, (const char *)p, 1);
		}
	}
	sb_cat(b, "\"");
}

/* ---- database ------------------------------------------------------------- */

static sqlite3 *db;
static CRITICAL_SECTION db_lock; /* one connection, many pipe threads */
static unsigned int next_id;
static bool have_fts; /* notifications_fts exists and this SQLite has FTS5 */

static void db_exec(const char *sql)
{
	char *err = NULL;
	if (sqlite3_exec(db, sql, NULL, NULL, &err) != SQLITE_OK)
		die("sqlite: %s", err ? err : sqlite3_errmsg(db));
}

static void default_db_path(char *out, size_t cap)
{
	PWSTR base = NULL;
	if (FAILED(SHGetKnownFolderPath(&FOLDERID_LocalAppData, 0, NULL, &base)))
		die("cannot locate %%LOCALAPPDATA%%");
	wchar_t dir[MAX_PATH];
	_snwprintf(dir, MAX_PATH, L"%s\\notifyd", base);
	CoTaskMemFree(base);
	CreateDirectoryW(dir, NULL);
	char *d8 = wn_utf8(dir);
	snprintf(out, cap, "%s\\history.sqlite3", d8);
	free(d8);
}

static void db_open(const char *path, bool readonly)
{
	int flags = readonly ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
	if (sqlite3_open_v2(path, &db, flags | SQLITE_OPEN_FULLMUTEX, NULL) != SQLITE_OK)
		die("cannot open %s: %s", path, sqlite3_errmsg(db));
	sqlite3_busy_timeout(db, 5000);
	if (!readonly)
		db_exec(SCHEMA);
}

static sqlite3_stmt *prep(const char *sql)
{
	sqlite3_stmt *st;
	if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK)
		die("sqlite prepare: %s\n  %s", sqlite3_errmsg(db), sql);
	return st;
}

static void bind_text_or_null(sqlite3_stmt *st, int i, const char *s)
{
	if (s)
		sqlite3_bind_text(st, i, s, -1, SQLITE_TRANSIENT);
	else
		sqlite3_bind_null(st, i);
}

struct client {
	unsigned long pid;
	char exe[MAX_PATH * 3];
};

static void client_info(wn_conn *c, struct client *out)
{
	out->pid = c ? wn_conn_client_pid(c) : 0;
	out->exe[0] = 0;
	if (!out->pid)
		return;
	HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, out->pid);
	if (!h)
		return;
	wchar_t w[MAX_PATH];
	DWORD n = MAX_PATH;
	if (QueryFullProcessImageNameW(h, 0, w, &n)) {
		char *u = wn_utf8(w);
		snprintf(out->exe, sizeof out->exe, "%s", u);
		free(u);
	}
	CloseHandle(h);
}

static wn_server *server;

/* Append one event; returns its seq. Caller holds db_lock. */
static long long event_insert(const char *kind, unsigned int nid, const struct client *cl, const char *json)
{
	sqlite3_stmt *st = prep("INSERT INTO events(ts, kind, notification_id, client_pid, client_exe, data) "
				"VALUES (?1, ?2, ?3, ?4, ?5, ?6)");
	sqlite3_bind_int64(st, 1, now_ms());
	sqlite3_bind_text(st, 2, kind, -1, SQLITE_STATIC);
	if (nid)
		sqlite3_bind_int64(st, 3, nid);
	else
		sqlite3_bind_null(st, 3);
	if (cl && cl->pid)
		sqlite3_bind_int64(st, 4, cl->pid);
	else
		sqlite3_bind_null(st, 4);
	bind_text_or_null(st, 5, cl && cl->exe[0] ? cl->exe : NULL);
	sqlite3_bind_text(st, 6, json ? json : "{}", -1, SQLITE_TRANSIENT);
	if (sqlite3_step(st) != SQLITE_DONE)
		fprintf(stderr, "notifyd: event insert failed: %s\n", sqlite3_errmsg(db));
	sqlite3_finalize(st);
	return sqlite3_last_insert_rowid(db);
}

/* Live feed for `notifyd tail`: a non-standard signal next to the spec ones. */
static void announce(long long seq, const char *kind, unsigned int nid)
{
	if (!server)
		return;
	char line[128];
	snprintf(line, sizeof line, "x-notifyd.Event\t%lld\t%s\t%u", seq, kind, nid);
	wn_server_emit(server, line);
}

static unsigned long long fnv1a(const char *s)
{
	unsigned long long h = 1469598103934665603ULL;
	for (; *s; s++)
		h = (h ^ (unsigned char)*s) * 1099511628211ULL;
	return h;
}

/* Store an image hint once; returns its hash key ("" if malformed). */
static void image_store(const char *value, char out_hash[17])
{
	int w, h, stride, alpha, bps, ch, used = 0;
	out_hash[0] = 0;
	if (sscanf(value, "%d:%d:%d:%d:%d:%d:%n", &w, &h, &stride, &alpha, &bps, &ch, &used) != 6 || !used)
		return;
	snprintf(out_hash, 17, "%016llx", fnv1a(value));
	size_t len;
	unsigned char *px = wn_base64_decode(value + used, &len);
	sqlite3_stmt *st = prep("INSERT OR IGNORE INTO images(hash, width, height, rowstride, has_alpha, "
				"bits_per_sample, channels, pixels) VALUES (?1,?2,?3,?4,?5,?6,?7,?8)");
	sqlite3_bind_text(st, 1, out_hash, -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(st, 2, w);
	sqlite3_bind_int(st, 3, h);
	sqlite3_bind_int(st, 4, stride);
	sqlite3_bind_int(st, 5, alpha);
	sqlite3_bind_int(st, 6, bps);
	sqlite3_bind_int(st, 7, ch);
	sqlite3_bind_blob(st, 8, px, (int)len, SQLITE_TRANSIENT);
	sqlite3_step(st);
	sqlite3_finalize(st);
	free(px);
}

static int is_json_number(const char *s)
{
	char *end;
	if (!*s)
		return 0;
	strtod(s, &end);
	return !*end && strcmp(s, "nan") && strcmp(s, "inf") && strcmp(s, "-inf");
}

/* hints -> JSON object; images replaced by {"image": "<hash>"} */
static char *hints_json(const wn_notify *n)
{
	sbuf b = {0};
	sb_cat(&b, "{");
	for (size_t i = 0; i < n->n_hints; i++) {
		const wn_hint *h = &n->hints[i];
		if (i)
			sb_cat(&b, ",");
		sb_json_str(&b, h->name);
		sb_cat(&b, ":");
		if (!strcmp(h->type, "image")) {
			char hash[17];
			image_store(h->value, hash);
			sb_cat(&b, "{\"image\":");
			sb_json_str(&b, hash);
			sb_cat(&b, "}");
		} else if (!strcmp(h->type, "b") && (!strcmp(h->value, "true") || !strcmp(h->value, "false"))) {
			sb_cat(&b, h->value);
		} else if (strchr("ynqiuxtd", h->type[0]) && h->type[1] == 0 && is_json_number(h->value)) {
			sb_cat(&b, h->value);
		} else {
			sb_json_str(&b, h->value);
		}
	}
	sb_cat(&b, "}");
	return b.s;
}

static char *actions_json(const wn_notify *n)
{
	sbuf b = {0};
	sb_cat(&b, "[");
	for (size_t i = 0; i + 1 < n->n_actions; i += 2) {
		if (i)
			sb_cat(&b, ",");
		sb_cat(&b, "{\"key\":");
		sb_json_str(&b, n->actions[i]);
		sb_cat(&b, ",\"label\":");
		sb_json_str(&b, n->actions[i + 1]);
		sb_cat(&b, "}");
	}
	sb_cat(&b, "]");
	return b.s;
}

static const char *hint_str(const wn_notify *n, const char *name)
{
	const wn_hint *h = wn_hint_get(n, name);
	return h ? h->value : NULL;
}

/* Record a Notify. Returns the id handed back to the client. */
static unsigned int record_notify(const wn_notify *n, const struct client *cl, bool *is_replace,
				  long long windows_id, long long windows_arrival_ms)
{
	EnterCriticalSection(&db_lock);
	db_exec("BEGIN IMMEDIATE");

	unsigned int id = n->replaces_id;
	*is_replace = false;
	if (id) {
		sqlite3_stmt *st = prep("SELECT 1 FROM notifications WHERE id = ?1");
		sqlite3_bind_int64(st, 1, id);
		*is_replace = sqlite3_step(st) == SQLITE_ROW;
		sqlite3_finalize(st);
		/* spec: replacing an unknown id behaves like a new notification;
		 * we keep the requested id only if it does not collide with the future */
		if (!*is_replace && id >= next_id)
			next_id = id + 1;
	} else {
		id = next_id++;
	}

	char *hints = hints_json(n), *actions = actions_json(n);
	const char *urg = hint_str(n, "urgency");

	sqlite3_stmt *st = prep(
		"INSERT INTO notifications(id, first_ts, last_ts, app_name, app_icon, summary, body, urgency,"
		" category, desktop_entry, expire_timeout, actions, hints, client_exe)"
		" VALUES (?1, ?2, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13)"
		" ON CONFLICT(id) DO UPDATE SET last_ts = excluded.last_ts, revision = revision + 1,"
		" app_name = excluded.app_name, app_icon = excluded.app_icon, summary = excluded.summary,"
		" body = excluded.body, urgency = excluded.urgency, category = excluded.category,"
		" desktop_entry = excluded.desktop_entry, expire_timeout = excluded.expire_timeout,"
		" actions = excluded.actions, hints = excluded.hints,"
		" closed_ts = NULL, close_reason = NULL, action_invoked = NULL");
	sqlite3_bind_int64(st, 1, id);
	sqlite3_bind_int64(st, 2, now_ms());
	sqlite3_bind_text(st, 3, n->app_name, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(st, 4, n->app_icon, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(st, 5, n->summary, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(st, 6, n->body, -1, SQLITE_TRANSIENT);
	if (urg)
		sqlite3_bind_int(st, 7, atoi(urg));
	else
		sqlite3_bind_null(st, 7);
	bind_text_or_null(st, 8, hint_str(n, "category"));
	bind_text_or_null(st, 9, hint_str(n, "desktop-entry"));
	sqlite3_bind_int(st, 10, n->expire_timeout);
	sqlite3_bind_text(st, 11, actions, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(st, 12, hints, -1, SQLITE_TRANSIENT);
	bind_text_or_null(st, 13, cl->exe[0] ? cl->exe : NULL);
	if (sqlite3_step(st) != SQLITE_DONE)
		fprintf(stderr, "notifyd: upsert failed: %s\n", sqlite3_errmsg(db));
	sqlite3_finalize(st);

	sbuf ev = {0};
	sb_cat(&ev, "{\"app_name\":");
	sb_json_str(&ev, n->app_name);
	sb_cat(&ev, ",\"app_icon\":");
	sb_json_str(&ev, n->app_icon);
	sb_cat(&ev, ",\"summary\":");
	sb_json_str(&ev, n->summary);
	sb_cat(&ev, ",\"body\":");
	sb_json_str(&ev, n->body);
	sb_printf(&ev, ",\"replaces_id\":%lu,\"expire_timeout\":%ld,\"actions\":", (unsigned long)n->replaces_id,
		  (long)n->expire_timeout);
	sb_cat(&ev, actions);
	sb_cat(&ev, ",\"hints\":");
	sb_cat(&ev, hints);
	sb_cat(&ev, "}");
	const char *kind = *is_replace ? "replace" : "notify";
	long long seq = event_insert(kind, id, cl, ev.s);

	if (windows_id) { /* same transaction: never imported twice, never half-imported */
		sqlite3_stmt *ws = prep("INSERT INTO windows_toasts(windows_id, notification_id, aumid, arrival_ts)"
					" VALUES (?1, ?2, ?3, ?4)");
		sqlite3_bind_int64(ws, 1, windows_id);
		sqlite3_bind_int64(ws, 2, id);
		bind_text_or_null(ws, 3, hint_str(n, "x-windows-aumid"));
		sqlite3_bind_int64(ws, 4, windows_arrival_ms);
		sqlite3_step(ws);
		sqlite3_finalize(ws);
	}

	db_exec("COMMIT");
	LeaveCriticalSection(&db_lock);
	announce(seq, kind, id);

	free(ev.s);
	free(hints);
	free(actions);
	return id;
}

static void record_simple(const char *kind, unsigned int id, const struct client *cl, const char *json)
{
	EnterCriticalSection(&db_lock);
	db_exec("BEGIN IMMEDIATE");
	long long seq = event_insert(kind, id, cl, json);
	db_exec("COMMIT");
	LeaveCriticalSection(&db_lock);
	announce(seq, kind, id);
}

/* NotificationClosed: log + projection + tell subscribers. Idempotent. */
static void close_notification(unsigned int id, int reason)
{
	char json[64];
	snprintf(json, sizeof json, "{\"reason\":%d}", reason);
	EnterCriticalSection(&db_lock);
	db_exec("BEGIN IMMEDIATE");
	sqlite3_stmt *st = prep("UPDATE notifications SET closed_ts = ?2, close_reason = ?3"
				" WHERE id = ?1 AND closed_ts IS NULL");
	sqlite3_bind_int64(st, 1, id);
	sqlite3_bind_int64(st, 2, now_ms());
	sqlite3_bind_int(st, 3, reason);
	sqlite3_step(st);
	bool changed = sqlite3_changes(db) > 0;
	sqlite3_finalize(st);
	long long seq = changed ? event_insert("closed", id, NULL, json) : 0;
	db_exec("COMMIT");
	LeaveCriticalSection(&db_lock);
	if (changed) {
		announce(seq, "closed", id);
		wn_emit_closed(server, id, (enum wn_close_reason)reason);
	}
}

static void action_invoked(unsigned int id, const char *key)
{
	sbuf j = {0};
	sb_cat(&j, "{\"key\":");
	sb_json_str(&j, key);
	sb_cat(&j, "}");
	EnterCriticalSection(&db_lock);
	db_exec("BEGIN IMMEDIATE");
	sqlite3_stmt *st = prep("UPDATE notifications SET action_invoked = ?2 WHERE id = ?1");
	sqlite3_bind_int64(st, 1, id);
	sqlite3_bind_text(st, 2, key, -1, SQLITE_TRANSIENT);
	sqlite3_step(st);
	sqlite3_finalize(st);
	long long seq = event_insert("action", id, NULL, j.s);
	db_exec("COMMIT");
	LeaveCriticalSection(&db_lock);
	announce(seq, "action", id);
	wn_emit_action(server, id, key);
	free(j.s);
}

/* ---- downstream: --exec (one process per notification) --------------------- */

enum mode { MODE_HEADLESS, MODE_EXEC, MODE_FORWARD };
static enum mode mode = MODE_HEADLESS;
static wchar_t *exec_cmd;
static wchar_t forward_pipe[256];

struct shown {
	unsigned int id;
	HANDLE process;
	DWORD pid;
	volatile LONG closing; /* 1: CloseNotification asked for it, 2: superseded by a replace */
	struct shown *next;
};
static struct shown *shown_list;
static CRITICAL_SECTION shown_lock;

/* CommandLineToArgvW-compatible quoting (porter guide, module 12). */
static void quote_arg(wchar_t *dst, size_t cap, const wchar_t *a)
{
	size_t o = wcslen(dst);
	if (o)
		dst[o++] = L' ';
	dst[o++] = L'"';
	size_t bs = 0;
	for (const wchar_t *p = a; *p && o + 4 < cap; p++) {
		if (*p == L'\\') {
			bs++;
			continue;
		}
		size_t reps = *p == L'"' ? bs * 2 + 1 : bs;
		while (reps-- && o + 4 < cap)
			dst[o++] = L'\\';
		bs = 0;
		dst[o++] = *p;
	}
	while (bs-- && o + 3 < cap) {
		dst[o++] = L'\\';
		dst[o++] = L'\\';
	}
	dst[o++] = L'"';
	dst[o] = 0;
}

static BOOL CALLBACK close_windows_of(HWND hwnd, LPARAM pid)
{
	DWORD owner;
	GetWindowThreadProcessId(hwnd, &owner);
	if (owner == (DWORD)pid)
		PostMessageW(hwnd, WM_CLOSE, 0, 0);
	return TRUE;
}

static DWORD WINAPI watch_process(LPVOID arg)
{
	struct shown *s = arg;
	WaitForSingleObject(s->process, INFINITE);
	DWORD code = 4;
	GetExitCodeProcess(s->process, &code);
	/* herbe contract: 0 = action, 2 = dismissed (click/timeout/WM_CLOSE) */
	if (s->closing == 2)
		; /* replaced: the id lives on in the new popup, record nothing */
	else if (s->closing)
		close_notification(s->id, WN_CLOSE_CLOSED);
	else if (code == 0) {
		action_invoked(s->id, "default");
		close_notification(s->id, WN_CLOSE_DISMISSED);
	} else if (code == 2)
		close_notification(s->id, WN_CLOSE_DISMISSED);
	else
		close_notification(s->id, WN_CLOSE_UNDEFINED);

	EnterCriticalSection(&shown_lock);
	for (struct shown **pp = &shown_list; *pp; pp = &(*pp)->next)
		if (*pp == s) {
			*pp = s->next;
			break;
		}
	LeaveCriticalSection(&shown_lock);
	CloseHandle(s->process);
	free(s);
	return 0;
}

static void exec_close(unsigned int id, bool replacing)
{
	EnterCriticalSection(&shown_lock);
	for (struct shown *s = shown_list; s; s = s->next)
		if (s->id == id && !s->closing) {
			InterlockedExchange(&s->closing, replacing ? 2 : 1);
			EnumWindows(close_windows_of, (LPARAM)s->pid);
		}
	LeaveCriticalSection(&shown_lock);
}

static void exec_show(unsigned int id, const wn_notify *n)
{
	exec_close(id, true); /* a replace closes the previous popup first */
	wchar_t cmd[8192] = {0};
	quote_arg(cmd, 8192, exec_cmd);
	wchar_t *app = wn_wide(n->app_name), *sum = wn_wide(n->summary), *body = wn_wide(n->body);
	if (*app)
		quote_arg(cmd, 8192, app);
	quote_arg(cmd, 8192, sum);
	if (*body)
		quote_arg(cmd, 8192, body);
	free(app);
	free(sum);
	free(body);

	STARTUPINFOW si = {.cb = sizeof si};
	PROCESS_INFORMATION pi;
	if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
		fprintf(stderr, "notifyd: cannot start display command (%lu)\n", GetLastError());
		close_notification(id, WN_CLOSE_UNDEFINED);
		return;
	}
	CloseHandle(pi.hThread);
	struct shown *s = calloc(1, sizeof *s);
	s->id = id;
	s->process = pi.hProcess;
	s->pid = pi.dwProcessId;
	EnterCriticalSection(&shown_lock);
	s->next = shown_list;
	shown_list = s;
	LeaveCriticalSection(&shown_lock);
	CloseHandle(CreateThread(NULL, 0, watch_process, s, 0, NULL));
}

/* ---- downstream: --forward (chain to another wnotify daemon) --------------- */

struct idmap {
	unsigned int ours, theirs;
	struct idmap *next;
};
static struct idmap *idmaps;
static CRITICAL_SECTION fwd_lock;
static wn_client *fwd_client; /* request connection, used under fwd_lock */

static unsigned int map_get(unsigned int key, bool ours_to_theirs)
{
	for (struct idmap *m = idmaps; m; m = m->next)
		if ((ours_to_theirs ? m->ours : m->theirs) == key)
			return ours_to_theirs ? m->theirs : m->ours;
	return 0;
}

static void map_set(unsigned int ours, unsigned int theirs)
{
	for (struct idmap *m = idmaps; m; m = m->next)
		if (m->ours == ours) {
			m->theirs = theirs;
			return;
		}
	struct idmap *m = calloc(1, sizeof *m);
	m->ours = ours;
	m->theirs = theirs;
	m->next = idmaps;
	idmaps = m;
}

static char *fwd_call(const char *line)
{
	if (!fwd_client && !(fwd_client = wn_client_open_name(forward_pipe, 2000)))
		return NULL;
	char *r = wn_client_call(fwd_client, line);
	if (!r) { /* downstream restarted: reconnect once */
		wn_client_close(fwd_client);
		fwd_client = wn_client_open_name(forward_pipe, 2000);
		r = fwd_client ? wn_client_call(fwd_client, line) : NULL;
	}
	return r;
}

static void forward_show(unsigned int id, const wn_notify *n)
{
	wn_notify copy = *n;
	EnterCriticalSection(&fwd_lock);
	copy.replaces_id = map_get(id, true);
	char *line = wn_format_notify(&copy);
	char *r = fwd_call(line);
	free(line);
	if (r && !strncmp(r, "OK\t", 3))
		map_set(id, (unsigned int)strtoul(r + 3, NULL, 10));
	LeaveCriticalSection(&fwd_lock);
	if (!r)
		fprintf(stderr, "notifyd: downstream daemon not reachable; notification %u only recorded\n", id);
	free(r);
}

static void forward_close(unsigned int id)
{
	EnterCriticalSection(&fwd_lock);
	unsigned int theirs = map_get(id, true);
	if (theirs) {
		char line[64];
		snprintf(line, sizeof line, "CloseNotification\t%u", theirs);
		free(fwd_call(line));
	}
	LeaveCriticalSection(&fwd_lock);
}

/* Downstream signals -> our ids -> log + upstream. Reconnects forever. */
static DWORD WINAPI forward_signals(LPVOID arg)
{
	(void)arg;
	for (;;) {
		wn_client *sub = wn_client_open_name(forward_pipe, 1000);
		if (!sub) {
			Sleep(1000);
			continue;
		}
		char *ok = wn_client_call(sub, "Subscribe");
		free(ok);
		char *l;
		while ((l = wn_client_read_line(sub))) {
			char **f;
			size_t nf = wn_split(l, &f);
			if (nf >= 3) {
				unsigned int theirs = (unsigned int)strtoul(f[1], NULL, 10);
				EnterCriticalSection(&fwd_lock);
				unsigned int ours = map_get(theirs, false);
				LeaveCriticalSection(&fwd_lock);
				if (ours && !strcmp(f[0], "NotificationClosed"))
					close_notification(ours, atoi(f[2]));
				else if (ours && !strcmp(f[0], "ActionInvoked"))
					action_invoked(ours, f[2]);
			}
			free(f);
			free(l);
		}
		wn_client_close(sub);
	}
}

/* ---- request handler -------------------------------------------------------- */

static void on_request(wn_conn *c, char **f, size_t nf, void *ud)
{
	(void)ud;
	const char *m = f[0];
	if (!strcmp(m, "GetServerInformation")) {
		wn_reply(c, "OK\tnotifyd\tLowLevel-Katas\t" NOTIFYD_VERSION "\t" WN_SPEC_VERSION);
	} else if (!strcmp(m, "GetCapabilities")) {
		if (mode == MODE_FORWARD) {
			EnterCriticalSection(&fwd_lock);
			char *r = fwd_call("GetCapabilities");
			LeaveCriticalSection(&fwd_lock);
			if (r && !strncmp(r, "OK", 2)) {
				size_t n = strlen(r);
				char *x = malloc(n + 32);
				snprintf(x, n + 32, "%s\tx-notifyd-history", r);
				wn_reply(c, x);
				free(x);
				free(r);
				return;
			}
			free(r);
		}
		wn_reply(c, mode == MODE_EXEC ? "OK\tbody\tactions\tx-notifyd-history"
					      : "OK\tbody\tactions\ticon-static\tpersistence\tx-notifyd-history");
	} else if (!strcmp(m, "Notify")) {
		wn_notify n;
		if (wn_parse_notify(f, nf, &n)) {
			wn_reply_err(c, "malformed Notify");
			return;
		}
		struct client cl;
		client_info(c, &cl);
		bool replace;
		unsigned int id = record_notify(&n, &cl, &replace, 0, 0); /* persisted before anything else */
		char reply[32];
		snprintf(reply, sizeof reply, "OK\t%u", id);
		wn_reply(c, reply);
		if (mode == MODE_EXEC)
			exec_show(id, &n);
		else if (mode == MODE_FORWARD)
			forward_show(id, &n);
		wn_notify_free(&n);
	} else if (!strcmp(m, "CloseNotification") && nf > 1) {
		unsigned int id = (unsigned int)strtoul(f[1], NULL, 10);
		struct client cl;
		client_info(c, &cl);
		record_simple("close_request", id, &cl, "{}");
		wn_reply_ok(c);
		if (mode == MODE_EXEC)
			exec_close(id, false); /* the watcher records reason 3 */
		else if (mode == MODE_FORWARD)
			forward_close(id); /* downstream answers with NotificationClosed */
		else
			close_notification(id, WN_CLOSE_CLOSED);
	} else {
		wn_reply_err(c, "unknown method");
	}
}

/* ---- full-text search (FTS5) -------------------------------------------------
 * External-content FTS table over notifications(summary, body, app_name), kept
 * in sync by triggers. Only created when this SQLite was built with FTS5; the
 * history query falls back to LIKE otherwise. */

static bool fts_compiled(void)
{
	return sqlite3_compileoption_used("ENABLE_FTS5") != 0;
}

static bool fts_table_exists(void)
{
	sqlite3_stmt *st = prep("SELECT 1 FROM sqlite_master WHERE name = 'notifications_fts'");
	bool r = sqlite3_step(st) == SQLITE_ROW;
	sqlite3_finalize(st);
	return r;
}

static void fts_setup(void)
{
	if (!fts_compiled()) {
		fprintf(stderr, "notifyd: SQLite %s has no FTS5; --search uses LIKE\n", sqlite3_libversion());
		return;
	}
	bool fresh = !fts_table_exists();
	db_exec("CREATE VIRTUAL TABLE IF NOT EXISTS notifications_fts USING fts5("
		" summary, body, app_name, content='notifications', content_rowid='id',"
		" tokenize='unicode61 remove_diacritics 2');"
		"CREATE TRIGGER IF NOT EXISTS notifications_fts_ai AFTER INSERT ON notifications BEGIN"
		"  INSERT INTO notifications_fts(rowid, summary, body, app_name)"
		"  VALUES (new.id, new.summary, new.body, new.app_name); END;"
		"CREATE TRIGGER IF NOT EXISTS notifications_fts_ad AFTER DELETE ON notifications BEGIN"
		"  INSERT INTO notifications_fts(notifications_fts, rowid, summary, body, app_name)"
		"  VALUES ('delete', old.id, old.summary, old.body, old.app_name); END;"
		"CREATE TRIGGER IF NOT EXISTS notifications_fts_au AFTER UPDATE OF summary, body, app_name"
		"  ON notifications BEGIN"
		"  INSERT INTO notifications_fts(notifications_fts, rowid, summary, body, app_name)"
		"  VALUES ('delete', old.id, old.summary, old.body, old.app_name);"
		"  INSERT INTO notifications_fts(rowid, summary, body, app_name)"
		"  VALUES (new.id, new.summary, new.body, new.app_name); END;");
	if (fresh) /* database created before FTS existed: index what is already there */
		db_exec("INSERT INTO notifications_fts(notifications_fts) VALUES ('rebuild')");
	have_fts = true;
}

/* ---- Windows toasts --------------------------------------------------------
 * Windows keeps every toast of the Action Center in a SQLite database owned by
 * the WpnUserService, readable by the user:
 *   %LOCALAPPDATA%\Microsoft\Windows\Notifications\wpndatabase.db
 *     Notification(Id, HandlerId, Type, Payload <toast> XML, ArrivalTime FILETIME, ...)
 *     NotificationHandler(RecordId, PrimaryId = AUMID of the app)
 * Undocumented but stable since Windows 10 1607. The official API
 * (UserNotificationListener) needs package identity (MSIX); this does not.
 * Rows vanish when the toast is dismissed from the Action Center: that is
 * recorded as a close with reason 4 (undefined: Windows does not say why). */

static void xml_unescape_into(sbuf *b, const char *s, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		if (s[i] != '&') {
			sb_add(b, s + i, 1);
			continue;
		}
		const char *semi = memchr(s + i, ';', n - i);
		if (!semi || semi - (s + i) > 10) {
			sb_add(b, s + i, 1);
			continue;
		}
		size_t len = (size_t)(semi - (s + i)) + 1;
		const char *e = s + i;
		unsigned long cp = 0;
		if (!strncmp(e, "&amp;", 5)) sb_cat(b, "&");
		else if (!strncmp(e, "&lt;", 4)) sb_cat(b, "<");
		else if (!strncmp(e, "&gt;", 4)) sb_cat(b, ">");
		else if (!strncmp(e, "&quot;", 6)) sb_cat(b, "\"");
		else if (!strncmp(e, "&apos;", 6)) sb_cat(b, "'");
		else if (e[1] == '#') {
			cp = e[2] == 'x' ? strtoul(e + 3, NULL, 16) : strtoul(e + 2, NULL, 10);
			wchar_t w[3] = {0};
			if (cp >= 0x10000) {
				cp -= 0x10000;
				w[0] = (wchar_t)(0xD800 + (cp >> 10));
				w[1] = (wchar_t)(0xDC00 + (cp & 0x3FF));
			} else {
				w[0] = (wchar_t)cp;
			}
			char *u = wn_utf8(w);
			sb_cat(b, u);
			free(u);
		} else {
			sb_add(b, e, len);
		}
		i += len - 1;
	}
}

/* Value of attribute `name` inside the tag starting at `tag` (up to '>'). */
static char *xml_attr(const char *tag, const char *name)
{
	const char *end = strchr(tag, '>');
	size_t nl = strlen(name);
	for (const char *p = tag; p && end && p < end; p++) {
		if (!strncmp(p, name, nl) && p[nl] == '=' && (p[nl + 1] == '"' || p[nl + 1] == '\'') &&
		    (p == tag || p[-1] == ' ' || p[-1] == '\t' || p[-1] == '\n' || p[-1] == '\r')) {
			char q = p[nl + 1];
			const char *v = p + nl + 2, *ve = strchr(v, q);
			if (!ve)
				return NULL;
			sbuf b = {0};
			xml_unescape_into(&b, v, (size_t)(ve - v));
			return b.s ? b.s : _strdup("");
		}
	}
	return NULL;
}

struct toast {
	char *summary, *body, *icon;
};

static void toast_parse(const char *xml, struct toast *t)
{
	sbuf body = {0};
	t->summary = NULL;
	t->icon = NULL;
	int count = 0;
	for (const char *p = xml; (p = strstr(p, "<text")); p++) {
		if (p[5] != '>' && p[5] != ' ' && p[5] != '/')
			continue; /* <textBox> etc. */
		const char *gt = strchr(p, '>');
		if (!gt)
			break;
		if (gt[-1] == '/')
			continue; /* <text/> */
		const char *close = strstr(gt + 1, "</text>");
		if (!close)
			break;
		sbuf one = {0};
		xml_unescape_into(&one, gt + 1, (size_t)(close - gt - 1));
		if (count++ == 0) {
			t->summary = one.s ? one.s : _strdup("");
		} else {
			if (body.len)
				sb_cat(&body, "\n");
			sb_cat(&body, one.s ? one.s : "");
			free(one.s);
		}
		p = close;
	}
	if (!t->summary) { /* header-only toasts */
		const char *h = strstr(xml, "<header");
		t->summary = h ? xml_attr(h, "title") : NULL;
		if (!t->summary)
			t->summary = _strdup("");
	}
	const char *img = strstr(xml, "<image");
	if (img)
		t->icon = xml_attr(img, "src");
	t->body = body.s ? body.s : _strdup("");
	if (!t->icon)
		t->icon = _strdup("");
}

/* "Claude_pzs8sxrjxfjjc!Claude" -> "Claude"; "com.squirrel.Discord.Discord" -> "Discord" */
static char *aumid_name(const char *aumid)
{
	const char *bang = strrchr(aumid, '!');
	if (bang && bang[1])
		return _strdup(bang + 1);
	/* unpackaged apps: "{FOLDERID}\path\program.exe" -> "program" */
	const char *slash = strrchr(aumid, '\\');
	if (slash) {
		char *r = _strdup(slash + 1);
		size_t n = strlen(r);
		if (n > 4 && !_stricmp(r + n - 4, ".exe"))
			r[n - 4] = 0;
		return r;
	}
	const char *dot = strrchr(aumid, '.');
	if (dot && dot[1] && strlen(dot + 1) > 1)
		return _strdup(dot + 1);
	return _strdup(aumid);
}

static char wpn_path[MAX_PATH * 3];

static void windows_store_path(void)
{
	PWSTR base = NULL;
	if (FAILED(SHGetKnownFolderPath(&FOLDERID_LocalAppData, 0, NULL, &base)))
		return;
	char *b8 = wn_utf8(base);
	CoTaskMemFree(base);
	snprintf(wpn_path, sizeof wpn_path, "%s\\Microsoft\\Windows\\Notifications\\wpndatabase.db", b8);
	free(b8);
}

static bool windows_known(long long wid)
{
	EnterCriticalSection(&db_lock);
	sqlite3_stmt *st = prep("SELECT 1 FROM windows_toasts WHERE windows_id = ?1");
	sqlite3_bind_int64(st, 1, wid);
	bool r = sqlite3_step(st) == SQLITE_ROW;
	sqlite3_finalize(st);
	LeaveCriticalSection(&db_lock);
	return r;
}

/* One pass: import new toasts, close the ones that left the store.
 * Returns the number imported, -1 if the store could not be read. */
static int windows_sync(void)
{
	sqlite3 *w;
	if (sqlite3_open_v2(wpn_path, &w, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
		sqlite3_close(w);
		return -1;
	}
	sqlite3_busy_timeout(w, 2000);
	sqlite3_stmt *st;
	if (sqlite3_prepare_v2(w,
			       "SELECT n.Id, h.PrimaryId, n.ArrivalTime, n.Payload FROM Notification n"
			       " JOIN NotificationHandler h ON h.RecordId = n.HandlerId"
			       " WHERE n.Type = 'toast' ORDER BY n.ArrivalTime",
			       -1, &st, NULL) != SQLITE_OK) {
		fprintf(stderr, "notifyd: unexpected wpndatabase schema: %s\n", sqlite3_errmsg(w));
		sqlite3_close(w);
		return -1;
	}
	int imported = 0;
	size_t present_n = 0, present_cap = 64;
	long long *present = malloc(present_cap * sizeof *present);
	while (sqlite3_step(st) == SQLITE_ROW) {
		long long wid = sqlite3_column_int64(st, 0);
		if (present_n == present_cap)
			present = realloc(present, (present_cap *= 2) * sizeof *present);
		present[present_n++] = wid;
		if (windows_known(wid))
			continue;

		const char *aumid = (const char *)sqlite3_column_text(st, 1);
		long long ft = sqlite3_column_int64(st, 2);
		long long arrival_ms = ft / 10000 - 11644473600000LL;
		int plen = sqlite3_column_bytes(st, 3);
		char *xml = malloc((size_t)plen + 1);
		memcpy(xml, sqlite3_column_blob(st, 3), (size_t)plen);
		xml[plen] = 0;

		struct toast t;
		toast_parse(xml, &t);
		char *name = aumid_name(aumid ? aumid : "windows");
		char widbuf[32], arrbuf[32];
		snprintf(widbuf, sizeof widbuf, "%lld", wid);
		snprintf(arrbuf, sizeof arrbuf, "%lld", arrival_ms);

		wn_notify n = {0};
		n.app_name = name;
		n.app_icon = t.icon;
		n.summary = t.summary;
		n.body = t.body;
		n.expire_timeout = -1;
		wn_add_hint(&n, "x-source", "s", "windows");
		wn_add_hint(&n, "x-windows-aumid", "s", aumid ? aumid : "");
		wn_add_hint(&n, "x-windows-id", "x", widbuf);
		wn_add_hint(&n, "x-windows-arrival-ms", "x", arrbuf);
		wn_add_hint(&n, "x-windows-payload", "s", xml);

		struct client cl = {0};
		snprintf(cl.exe, sizeof cl.exe, "windows-toast:%s", aumid ? aumid : "");
		bool replace;
		record_notify(&n, &cl, &replace, wid, arrival_ms);
		imported++;

		/* wn_notify_free would free name/icon/summary/body too: they are ours */
		wn_notify_free(&n);
		free(xml);
	}
	sqlite3_finalize(st);
	sqlite3_close(w);

	/* toasts that left the store */
	EnterCriticalSection(&db_lock);
	sqlite3_stmt *op = prep("SELECT windows_id, notification_id FROM windows_toasts WHERE open = 1");
	size_t gone_n = 0, gone_cap = 16;
	long long (*gone)[2] = malloc(gone_cap * sizeof *gone);
	while (sqlite3_step(op) == SQLITE_ROW) {
		long long wid = sqlite3_column_int64(op, 0);
		bool found = false;
		for (size_t i = 0; i < present_n && !found; i++)
			found = present[i] == wid;
		if (!found) {
			if (gone_n == gone_cap)
				gone = realloc(gone, (gone_cap *= 2) * sizeof *gone);
			gone[gone_n][0] = wid;
			gone[gone_n][1] = sqlite3_column_int64(op, 1);
			gone_n++;
		}
	}
	sqlite3_finalize(op);
	for (size_t i = 0; i < gone_n; i++) {
		sqlite3_stmt *u = prep("UPDATE windows_toasts SET open = 0 WHERE windows_id = ?1");
		sqlite3_bind_int64(u, 1, gone[i][0]);
		sqlite3_step(u);
		sqlite3_finalize(u);
	}
	LeaveCriticalSection(&db_lock);
	for (size_t i = 0; i < gone_n; i++)
		close_notification((unsigned int)gone[i][1], WN_CLOSE_UNDEFINED);
	free(gone);
	free(present);
	return imported;
}

static DWORD WINAPI windows_watch(LPVOID arg)
{
	(void)arg;
	wchar_t *dir = wn_wide(wpn_path);
	wchar_t *slash = wcsrchr(dir, L'\\');
	if (slash)
		*slash = 0;
	HANDLE ch = FindFirstChangeNotificationW(dir, FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE);
	free(dir);
	int n = windows_sync();
	fprintf(stderr, "notifyd: windows toasts: %d imported from %s\n", n, wpn_path);
	for (;;) {
		/* Directory notifications for the WAL are lazy (an open file's size and
		 * mtime are not flushed to the directory on every write): they only
		 * shorten the wait. The 2 s poll is what guarantees freshness. */
		if (ch != INVALID_HANDLE_VALUE) {
			WaitForSingleObject(ch, 2000);
			FindNextChangeNotification(ch);
			Sleep(300); /* let the service finish its transaction */
		} else {
			Sleep(5000);
		}
		windows_sync();
	}
}

/* ---- daemon ----------------------------------------------------------------- */

static HANDLE quit_ev;
static bool watch_windows;
static BOOL WINAPI on_ctrl(DWORD t)
{
	(void)t;
	SetEvent(quit_ev);
	return TRUE;
}

static int run_daemon(const char *db_path)
{
	db_open(db_path, false);
	fts_setup();
	InitializeCriticalSection(&db_lock);
	InitializeCriticalSection(&shown_lock);
	InitializeCriticalSection(&fwd_lock);

	sqlite3_stmt *st = prep("SELECT COALESCE(MAX(id), 0) + 1 FROM notifications");
	sqlite3_step(st);
	next_id = (unsigned int)sqlite3_column_int64(st, 0);
	sqlite3_finalize(st);

	char err[512];
	server = wn_server_start(on_request, NULL, err, sizeof err);
	if (!server)
		die("%s", err);

	char json[1024];
	snprintf(json, sizeof json, "{\"version\":\"%s\",\"mode\":\"%s\",\"sqlite\":\"%s\"}", NOTIFYD_VERSION,
		 mode == MODE_EXEC ? "exec" : mode == MODE_FORWARD ? "forward" : "headless", sqlite3_libversion());
	record_simple("daemon_start", 0, NULL, json);
	fprintf(stderr, "notifyd: recording to %s (next id %u)\n", db_path, next_id);

	if (mode == MODE_FORWARD)
		CloseHandle(CreateThread(NULL, 0, forward_signals, NULL, 0, NULL));
	if (watch_windows) {
		windows_store_path();
		CloseHandle(CreateThread(NULL, 0, windows_watch, NULL, 0, NULL));
	}

	quit_ev = CreateEventW(NULL, TRUE, FALSE, NULL);
	SetConsoleCtrlHandler(on_ctrl, TRUE);
	WaitForSingleObject(quit_ev, INFINITE);

	record_simple("daemon_stop", 0, NULL, "{}");
	sqlite3_close(db); /* checkpoints the WAL */
	return 0;
}

/* ---- queries ---------------------------------------------------------------- */

static void print_time(long long ms)
{
	time_t t = (time_t)(ms / 1000);
	struct tm tmv;
	localtime_s(&tmv, &t);
	char buf[32];
	strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tmv);
	printf("%s", buf);
}

static const char *col(sqlite3_stmt *st, int i)
{
	const unsigned char *t = sqlite3_column_text(st, i);
	return t ? (const char *)t : "";
}

static int cmd_history(int argc, char **argv)
{
	int limit = 20, json = 0;
	const char *app = NULL, *search = NULL;
	for (int i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "-n") && i + 1 < argc)
			limit = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--app") && i + 1 < argc)
			app = argv[++i];
		else if (!strcmp(argv[i], "--search") && i + 1 < argc)
			search = argv[++i];
		else if (!strcmp(argv[i], "--json"))
			json = 1;
		else
			die("history: unknown argument %s", argv[i]);
	}
	bool use_fts = search && fts_compiled() && fts_table_exists();
	if (search && !use_fts)
		fprintf(stderr, "notifyd: no FTS5 index, searching with LIKE\n");
	char sql[2048];
	snprintf(sql, sizeof sql, "%s%s%s",
		"SELECT id, last_ts, app_name, summary, body, urgency, close_reason, action_invoked, revision,"
		" json_object('id', id, 'first_ts', first_ts, 'last_ts', last_ts, 'revision', revision,"
		"  'app_name', app_name, 'app_icon', app_icon, 'summary', summary, 'body', body,"
		"  'urgency', urgency, 'category', category, 'desktop_entry', desktop_entry,"
		"  'expire_timeout', expire_timeout, 'actions', json(actions), 'hints', json(hints),"
		"  'client_exe', client_exe, 'closed_ts', closed_ts, 'close_reason', close_reason,"
		"  'action_invoked', action_invoked)"
		" FROM notifications"
		" WHERE (?1 IS NULL OR app_name = ?1) AND ",
		use_fts ? "(?2 IS NULL OR id IN (SELECT rowid FROM notifications_fts WHERE notifications_fts MATCH ?2))"
			: "(?2 IS NULL OR summary LIKE '%' || ?2 || '%' OR body LIKE '%' || ?2 || '%')",
		" ORDER BY last_ts DESC, id DESC LIMIT ?3");
	sqlite3_stmt *st = prep(sql);
	bind_text_or_null(st, 1, app);
	bind_text_or_null(st, 2, search);
	sqlite3_bind_int(st, 3, limit);
	if (json)
		printf("[");
	int first = 1, rc;
	while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
		if (json) {
			printf("%s%s", first ? "" : ",", col(st, 9));
		} else {
			static const char *reasons[] = {"open", "expired", "dismissed", "closed", "undefined"};
			int reason = sqlite3_column_type(st, 6) == SQLITE_NULL ? 0 : sqlite3_column_int(st, 6);
			printf("#%-5lld ", sqlite3_column_int64(st, 0));
			print_time(sqlite3_column_int64(st, 1));
			printf("  [%s] %s", col(st, 2), col(st, 3));
			if (*col(st, 4))
				printf(" -- %s", col(st, 4));
			printf("  (%s%s%s%s)\n", reasons[reason < 0 || reason > 4 ? 4 : reason],
			       *col(st, 7) ? ", action=" : "", col(st, 7),
			       sqlite3_column_int(st, 8) > 1 ? ", replaced" : "");
		}
		first = 0;
	}
	if (json)
		printf("]\n");
	if (rc != SQLITE_DONE) { /* e.g. FTS5 syntax error in --search */
		fprintf(stderr, "notifyd: query failed: %s\n", sqlite3_errmsg(db));
		sqlite3_finalize(st);
		return 1;
	}
	sqlite3_finalize(st);
	return 0;
}

static int cmd_show(int argc, char **argv)
{
	if (argc < 1)
		die("show ID");
	sqlite3_stmt *st = prep("SELECT seq, ts, kind, COALESCE(client_exe, ''), data FROM events"
				" WHERE notification_id = ?1 ORDER BY seq");
	sqlite3_bind_int64(st, 1, _atoi64(argv[0]));
	int any = 0;
	while (sqlite3_step(st) == SQLITE_ROW) {
		printf("seq %-6lld ", sqlite3_column_int64(st, 0));
		print_time(sqlite3_column_int64(st, 1));
		printf("  %-13s %s\n    %s\n", col(st, 2), col(st, 3), col(st, 4));
		any = 1;
	}
	sqlite3_finalize(st);
	if (!any)
		die("no events for notification %s", argv[0]);
	return 0;
}

static int cmd_events(int argc, char **argv)
{
	long long since = 0;
	int limit = 50;
	for (int i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--since") && i + 1 < argc)
			since = _atoi64(argv[++i]);
		else if (!strcmp(argv[i], "-n") && i + 1 < argc)
			limit = atoi(argv[++i]);
	}
	sqlite3_stmt *st = prep("SELECT seq, ts, kind, COALESCE(notification_id, ''), data FROM"
				" (SELECT * FROM events WHERE seq > ?1 ORDER BY seq DESC LIMIT ?2) ORDER BY seq");
	sqlite3_bind_int64(st, 1, since);
	sqlite3_bind_int(st, 2, limit);
	while (sqlite3_step(st) == SQLITE_ROW) {
		printf("%lld\t", sqlite3_column_int64(st, 0));
		print_time(sqlite3_column_int64(st, 1));
		printf("\t%s\t%s\t%s\n", col(st, 2), col(st, 3), col(st, 4));
	}
	sqlite3_finalize(st);
	return 0;
}

static int cmd_stats(void)
{
	sqlite3_stmt *st = prep("SELECT (SELECT COUNT(*) FROM events), (SELECT COUNT(*) FROM notifications),"
				" (SELECT COUNT(*) FROM notifications WHERE closed_ts IS NULL),"
				" (SELECT COUNT(*) FROM images), (SELECT MIN(ts) FROM events)");
	if (sqlite3_step(st) == SQLITE_ROW) {
		printf("events: %lld\nnotifications: %lld\nopen: %lld\nimages: %lld\nsince: ",
		       sqlite3_column_int64(st, 0), sqlite3_column_int64(st, 1), sqlite3_column_int64(st, 2),
		       sqlite3_column_int64(st, 3));
		if (sqlite3_column_type(st, 4) != SQLITE_NULL)
			print_time(sqlite3_column_int64(st, 4));
		printf("\n");
	}
	sqlite3_finalize(st);
	st = prep("SELECT COALESCE(app_name, '?'), COUNT(*) FROM notifications GROUP BY 1 ORDER BY 2 DESC LIMIT 10");
	printf("top apps:\n");
	while (sqlite3_step(st) == SQLITE_ROW)
		printf("  %6lld  %s\n", sqlite3_column_int64(st, 1), col(st, 0));
	sqlite3_finalize(st);
	return 0;
}

static int cmd_tail(void)
{
	wn_client *c = wn_client_open(2000);
	if (!c)
		die("notifyd is not running");
	free(wn_client_call(c, "Subscribe"));
	char *l;
	while ((l = wn_client_read_line(c))) {
		printf("%s\n", l);
		fflush(stdout);
		free(l);
	}
	return 0;
}

static void usage(void)
{
	puts("notifyd " NOTIFYD_VERSION " - persistent notification daemon (wnotify + SQLite)\n\n"
	     "  notifyd [--db PATH] [--exec CMD | --forward PIPE] [--windows]   run the daemon\n"
	     "      --windows  also record the toasts of every Windows app\n"
	     "  notifyd [--db PATH] history [-n N] [--app APP] [--search QUERY] [--json]\n"
	     "      QUERY is FTS5 syntax when available (words, \"phrases\", prefix*, AND/OR/NOT)\n"
	     "  notifyd [--db PATH] show ID\n"
	     "  notifyd [--db PATH] events [-n N] [--since SEQ]\n"
	     "  notifyd [--db PATH] stats\n"
	     "  notifyd tail\n\n"
	     "default database: %LOCALAPPDATA%\\notifyd\\history.sqlite3");
}

int wmain(int wargc, wchar_t **wargv)
{
	SetConsoleOutputCP(CP_UTF8);
	_setmode(_fileno(stdout), _O_BINARY);
	char **argv = calloc(wargc + 1, sizeof *argv);
	for (int i = 0; i < wargc; i++)
		argv[i] = wn_utf8(wargv[i]);

	char db_path[MAX_PATH * 3] = {0};
	int i = 1;
	for (; i < wargc; i++) {
		if (!strcmp(argv[i], "--db") && i + 1 < wargc) {
			snprintf(db_path, sizeof db_path, "%s", argv[++i]);
		} else if (!strcmp(argv[i], "--exec") && i + 1 < wargc) {
			mode = MODE_EXEC;
			exec_cmd = wargv[++i];
		} else if (!strcmp(argv[i], "--windows")) {
			watch_windows = true;
		} else if (!strcmp(argv[i], "--forward") && i + 1 < wargc) {
			mode = MODE_FORWARD;
			wcsncpy(forward_pipe, wargv[++i], 255);
		} else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
			usage();
			return 0;
		} else {
			break;
		}
	}
	if (!db_path[0])
		default_db_path(db_path, sizeof db_path);

	if (i >= wargc)
		return run_daemon(db_path);

	const char *cmd = argv[i++];
	if (!strcmp(cmd, "tail"))
		return cmd_tail();
	db_open(db_path, true);
	if (!strcmp(cmd, "history"))
		return cmd_history(wargc - i, argv + i);
	if (!strcmp(cmd, "show"))
		return cmd_show(wargc - i, argv + i);
	if (!strcmp(cmd, "events"))
		return cmd_events(wargc - i, argv + i);
	if (!strcmp(cmd, "stats"))
		return cmd_stats();
	usage();
	return 1;
}
