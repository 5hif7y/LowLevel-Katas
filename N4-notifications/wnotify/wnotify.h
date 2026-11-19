/* wnotify - org.freedesktop.Notifications over a Windows named pipe.
 * See PROTOCOL.md. Plain C11 + Win32, no dependencies. Single .h/.c pair meant
 * to be vendored into each port (tiramisu, dunst, notify-send, history daemon).
 */
#ifndef WNOTIFY_H
#define WNOTIFY_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WN_SPEC_VERSION "1.2"

enum wn_close_reason {
	WN_CLOSE_EXPIRED = 1,
	WN_CLOSE_DISMISSED = 2,
	WN_CLOSE_CLOSED = 3, /* CloseNotification */
	WN_CLOSE_UNDEFINED = 4,
};

typedef struct wn_hint {
	char *name;
	char *type;  /* D-Bus signature char ("s","i","b",...) or "image" */
	char *value; /* textual value, see PROTOCOL.md */
} wn_hint;

typedef struct wn_notify {
	char *app_name;
	uint32_t replaces_id;
	char *app_icon;
	char *summary;
	char *body;
	char **actions; /* flat: key, label, key, label ... */
	size_t n_actions; /* number of strings (even) */
	wn_hint *hints;
	size_t n_hints;
	int32_t expire_timeout;
} wn_notify;

/* ---- codec ------------------------------------------------------------ */

/* Escape one field (\\ \t \n \r). Returns malloc'd string. */
char *wn_escape(const char *s);
/* Split a line (without '\n') in place on TAB and unescape every field.
 * *fields is malloc'd (free it); strings point into `line`. */
size_t wn_split(char *line, char ***fields);

/* Growable line builder. */
typedef struct wn_buf {
	char *data;
	size_t len, cap;
} wn_buf;
void wn_buf_field(wn_buf *b, const char *s); /* appends [\t]escaped(s) */
void wn_buf_fieldf(wn_buf *b, const char *fmt, ...);
void wn_buf_free(wn_buf *b);

char *wn_format_notify(const wn_notify *n); /* "Notify\t..." (no newline), malloc'd */
/* fields[0] must be "Notify". Deep-copies into `out`. 0 on success. */
int wn_parse_notify(char **fields, size_t nf, wn_notify *out);
void wn_notify_free(wn_notify *n);
const wn_hint *wn_hint_get(const wn_notify *n, const char *name);
void wn_add_hint(wn_notify *n, const char *name, const char *type, const char *value);
void wn_add_action(wn_notify *n, const char *key, const char *label);

/* Base64 (RFC 4648) used by the "image" hint. Returns malloc'd strings. */
char *wn_base64_encode(const unsigned char *data, size_t len);
unsigned char *wn_base64_decode(const char *s, size_t *out_len);

/* ---- endpoint --------------------------------------------------------- */

/* WNOTIFY_PIPE, else \\.\pipe\org.freedesktop.Notifications.<SessionId> */
void wn_pipe_name(wchar_t *buf, size_t cap);

/* ---- server ----------------------------------------------------------- */

typedef struct wn_server wn_server;
typedef struct wn_conn wn_conn;

/* Called on the connection's own thread for every request line except
 * "Subscribe" (handled internally). Must call wn_reply exactly once. */
typedef void (*wn_request_fn)(wn_conn *c, char **fields, size_t nf, void *ud);

/* Claims the pipe name and starts the accept thread. Returns NULL and fills
 * err if another daemon owns the name (ERROR_ACCESS_DENIED) or on failure. */
wn_server *wn_server_start(wn_request_fn fn, void *ud, char *err, size_t errcap);
void wn_reply(wn_conn *c, const char *line);  /* line without '\n' */
void wn_reply_ok(wn_conn *c);
/* PID of the process on the other end (GetNamedPipeClientProcessId), 0 if
 * unknown. D-Bus hands the daemon the sender's unique name; this is ours. */
unsigned long wn_conn_client_pid(wn_conn *c);
void wn_reply_err(wn_conn *c, const char *msg);
/* Broadcast a signal line to every subscriber (thread-safe). */
void wn_server_emit(wn_server *s, const char *line);
void wn_emit_closed(wn_server *s, uint32_t id, enum wn_close_reason reason);
void wn_emit_action(wn_server *s, uint32_t id, const char *action_key);
void wn_server_stop(wn_server *s);

/* ---- client ----------------------------------------------------------- */

typedef struct wn_client wn_client;
/* Waits up to timeout_ms for a server instance. NULL if no daemon. */
wn_client *wn_client_open(unsigned timeout_ms);
/* Same, for an explicit pipe (e.g. a downstream daemon when chaining). */
wn_client *wn_client_open_name(const wchar_t *pipe_name, unsigned timeout_ms);
bool wn_client_send(wn_client *c, const char *line);
char *wn_client_read_line(wn_client *c); /* malloc'd, NULL on EOF */
char *wn_client_call(wn_client *c, const char *line); /* send + read reply */
void wn_client_close(wn_client *c);

/* ---- utf-8 / utf-16 helpers -------------------------------------------- */
char *wn_utf8(const wchar_t *w);   /* malloc'd */
wchar_t *wn_wide(const char *s);   /* malloc'd */

#ifdef __cplusplus
}
#endif
#endif
