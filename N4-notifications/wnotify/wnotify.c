/* wnotify - see wnotify.h / PROTOCOL.md */
#define WIN32_LEAN_AND_MEAN
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wnotify.h"

#define WN_MAX_LINE (64u * 1024u * 1024u) /* image hints can be big */
#define WN_PIPE_BUF (64u * 1024u)

static void *xrealloc(void *p, size_t n)
{
	void *r = realloc(p, n ? n : 1);
	if (!r) {
		fputs("wnotify: out of memory\n", stderr);
		abort();
	}
	return r;
}

static char *xstrdup(const char *s)
{
	size_t n = strlen(s ? s : "") + 1;
	char *r = xrealloc(NULL, n);
	memcpy(r, s ? s : "", n);
	return r;
}

/* ---- utf ---------------------------------------------------------------- */

char *wn_utf8(const wchar_t *w)
{
	int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
	char *r = xrealloc(NULL, n > 0 ? (size_t)n : 1);
	if (n <= 0 || !WideCharToMultiByte(CP_UTF8, 0, w, -1, r, n, NULL, NULL))
		r[0] = 0;
	return r;
}

wchar_t *wn_wide(const char *s)
{
	int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
	wchar_t *r = xrealloc(NULL, (n > 0 ? (size_t)n : 1) * sizeof(wchar_t));
	if (n <= 0 || !MultiByteToWideChar(CP_UTF8, 0, s, -1, r, n))
		r[0] = 0;
	return r;
}

/* ---- codec -------------------------------------------------------------- */

char *wn_escape(const char *s)
{
	size_t n = 0;
	for (const char *p = s; *p; p++)
		n += (*p == '\\' || *p == '\t' || *p == '\n' || *p == '\r') ? 2 : 1;
	char *r = xrealloc(NULL, n + 1), *o = r;
	for (const char *p = s; *p; p++) {
		switch (*p) {
		case '\\': *o++ = '\\'; *o++ = '\\'; break;
		case '\t': *o++ = '\\'; *o++ = 't'; break;
		case '\n': *o++ = '\\'; *o++ = 'n'; break;
		case '\r': *o++ = '\\'; *o++ = 'r'; break;
		default: *o++ = *p;
		}
	}
	*o = 0;
	return r;
}

static void unescape_inplace(char *s)
{
	char *o = s;
	for (char *p = s; *p; p++) {
		if (*p == '\\' && p[1]) {
			p++;
			switch (*p) {
			case 't': *o++ = '\t'; break;
			case 'n': *o++ = '\n'; break;
			case 'r': *o++ = '\r'; break;
			default: *o++ = *p; /* "\\" and unknown escapes keep the char */
			}
		} else {
			*o++ = *p;
		}
	}
	*o = 0;
}

size_t wn_split(char *line, char ***fields)
{
	size_t n = 1;
	for (char *p = line; *p; p++)
		n += *p == '\t';
	char **f = xrealloc(NULL, n * sizeof *f);
	size_t i = 0;
	f[i++] = line;
	for (char *p = line; *p; p++)
		if (*p == '\t') {
			*p = 0;
			f[i++] = p + 1;
		}
	for (i = 0; i < n; i++)
		unescape_inplace(f[i]);
	*fields = f;
	return n;
}

static void buf_put(wn_buf *b, const char *s, size_t n)
{
	if (b->len + n + 1 > b->cap) {
		b->cap = (b->len + n + 1) * 2;
		b->data = xrealloc(b->data, b->cap);
	}
	memcpy(b->data + b->len, s, n);
	b->len += n;
	b->data[b->len] = 0;
}

void wn_buf_field(wn_buf *b, const char *s)
{
	char *e = wn_escape(s ? s : "");
	if (b->len)
		buf_put(b, "\t", 1);
	buf_put(b, e, strlen(e));
	free(e);
}

void wn_buf_fieldf(wn_buf *b, const char *fmt, ...)
{
	char tmp[64];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(tmp, sizeof tmp, fmt, ap);
	va_end(ap);
	wn_buf_field(b, tmp);
}

void wn_buf_free(wn_buf *b)
{
	free(b->data);
	b->data = NULL;
	b->len = b->cap = 0;
}

char *wn_format_notify(const wn_notify *n)
{
	wn_buf b = {0};
	wn_buf_field(&b, "Notify");
	wn_buf_field(&b, n->app_name);
	wn_buf_fieldf(&b, "%lu", (unsigned long)n->replaces_id);
	wn_buf_field(&b, n->app_icon);
	wn_buf_field(&b, n->summary);
	wn_buf_field(&b, n->body);
	wn_buf_fieldf(&b, "%zu", n->n_actions);
	for (size_t i = 0; i < n->n_actions; i++)
		wn_buf_field(&b, n->actions[i]);
	wn_buf_fieldf(&b, "%zu", n->n_hints);
	for (size_t i = 0; i < n->n_hints; i++) {
		wn_buf_field(&b, n->hints[i].name);
		wn_buf_field(&b, n->hints[i].type);
		wn_buf_field(&b, n->hints[i].value);
	}
	wn_buf_fieldf(&b, "%ld", (long)n->expire_timeout);
	return b.data;
}

static int parse_count(const char *s, size_t *out)
{
	char *end;
	unsigned long long v = strtoull(s, &end, 10);
	if (!*s || *end || v > 1u << 20)
		return -1;
	*out = (size_t)v;
	return 0;
}

int wn_parse_notify(char **f, size_t nf, wn_notify *out)
{
	memset(out, 0, sizeof *out);
	if (nf < 9 || strcmp(f[0], "Notify"))
		return -1;
	size_t i = 1, na, nh;
	const char *app = f[i++];
	unsigned long rid = strtoul(f[i++], NULL, 10);
	const char *icon = f[i++], *summary = f[i++], *body = f[i++];
	if (parse_count(f[i++], &na) || na % 2 || i + na >= nf)
		return -1;
	char **acts = f + i;
	i += na;
	if (parse_count(f[i++], &nh) || i + nh * 3 + 1 != nf)
		return -1;

	out->app_name = xstrdup(app);
	out->replaces_id = (uint32_t)rid;
	out->app_icon = xstrdup(icon);
	out->summary = xstrdup(summary);
	out->body = xstrdup(body);
	for (size_t k = 0; k < na; k += 2)
		wn_add_action(out, acts[k], acts[k + 1]);
	for (size_t k = 0; k < nh; k++, i += 3)
		wn_add_hint(out, f[i], f[i + 1], f[i + 2]);
	out->expire_timeout = (int32_t)strtol(f[i], NULL, 10);
	return 0;
}

void wn_notify_free(wn_notify *n)
{
	free(n->app_name);
	free(n->app_icon);
	free(n->summary);
	free(n->body);
	for (size_t i = 0; i < n->n_actions; i++)
		free(n->actions[i]);
	free(n->actions);
	for (size_t i = 0; i < n->n_hints; i++) {
		free(n->hints[i].name);
		free(n->hints[i].type);
		free(n->hints[i].value);
	}
	free(n->hints);
	memset(n, 0, sizeof *n);
}

const wn_hint *wn_hint_get(const wn_notify *n, const char *name)
{
	for (size_t i = 0; i < n->n_hints; i++)
		if (!strcmp(n->hints[i].name, name))
			return &n->hints[i];
	return NULL;
}

void wn_add_hint(wn_notify *n, const char *name, const char *type, const char *value)
{
	/* D-Bus a{sv}: a later value for the same key replaces the earlier one */
	for (size_t i = 0; i < n->n_hints; i++)
		if (!strcmp(n->hints[i].name, name)) {
			free(n->hints[i].type);
			free(n->hints[i].value);
			n->hints[i].type = xstrdup(type);
			n->hints[i].value = xstrdup(value);
			return;
		}
	n->hints = xrealloc(n->hints, (n->n_hints + 1) * sizeof *n->hints);
	n->hints[n->n_hints++] = (wn_hint){xstrdup(name), xstrdup(type), xstrdup(value)};
}

void wn_add_action(wn_notify *n, const char *key, const char *label)
{
	n->actions = xrealloc(n->actions, (n->n_actions + 2) * sizeof *n->actions);
	n->actions[n->n_actions++] = xstrdup(key);
	n->actions[n->n_actions++] = xstrdup(label);
}

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char *wn_base64_encode(const unsigned char *d, size_t len)
{
	char *r = xrealloc(NULL, (len + 2) / 3 * 4 + 1), *o = r;
	size_t i = 0;
	for (; i + 2 < len; i += 3) {
		unsigned v = d[i] << 16 | d[i + 1] << 8 | d[i + 2];
		*o++ = B64[v >> 18];
		*o++ = B64[v >> 12 & 63];
		*o++ = B64[v >> 6 & 63];
		*o++ = B64[v & 63];
	}
	if (i < len) {
		unsigned v = d[i] << 16 | (i + 1 < len ? d[i + 1] << 8 : 0);
		*o++ = B64[v >> 18];
		*o++ = B64[v >> 12 & 63];
		*o++ = i + 1 < len ? B64[v >> 6 & 63] : '=';
		*o++ = '=';
	}
	*o = 0;
	return r;
}

unsigned char *wn_base64_decode(const char *s, size_t *out_len)
{
	size_t n = strlen(s);
	unsigned char *r = xrealloc(NULL, n / 4 * 3 + 3);
	size_t o = 0;
	unsigned v = 0;
	int bits = 0;
	for (size_t i = 0; i < n; i++) {
		const char *p = strchr(B64, s[i]);
		if (s[i] == '=' || !s[i])
			break;
		if (!p)
			continue; /* skip whitespace / junk */
		v = v << 6 | (unsigned)(p - B64);
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			r[o++] = (unsigned char)(v >> bits);
		}
	}
	*out_len = o;
	return r;
}

/* ---- endpoint ----------------------------------------------------------- */

void wn_pipe_name(wchar_t *buf, size_t cap)
{
	DWORD n = GetEnvironmentVariableW(L"WNOTIFY_PIPE", buf, (DWORD)cap);
	if (n > 0 && n < cap)
		return;
	DWORD sid = 0;
	ProcessIdToSessionId(GetCurrentProcessId(), &sid);
	_snwprintf(buf, cap, L"\\\\.\\pipe\\org.freedesktop.Notifications.%lu", (unsigned long)sid);
	buf[cap - 1] = 0;
}

/* ---- line reader (shared by server conns and client) --------------------- */

typedef struct rbuf {
	char *data;
	size_t len, cap;
} rbuf;

/* Pull a complete line out of rb if present. */
static char *rbuf_take_line(rbuf *rb)
{
	char *nl = rb->len ? memchr(rb->data, '\n', rb->len) : NULL;
	if (!nl)
		return NULL;
	size_t n = (size_t)(nl - rb->data);
	char *line = xrealloc(NULL, n + 1);
	memcpy(line, rb->data, n);
	line[n] = 0;
	if (n && line[n - 1] == '\r')
		line[n - 1] = 0;
	memmove(rb->data, nl + 1, rb->len - n - 1);
	rb->len -= n + 1;
	return line;
}

static void rbuf_reserve(rbuf *rb)
{
	if (rb->cap - rb->len < WN_PIPE_BUF) {
		rb->cap = rb->cap ? rb->cap * 2 : WN_PIPE_BUF * 2;
		rb->data = xrealloc(rb->data, rb->cap);
	}
}

/* ---- server ------------------------------------------------------------- */

struct wn_conn {
	wn_server *srv;
	HANDLE pipe;
	HANDLE thread;
	HANDLE rd_ev, wr_ev;
	CRITICAL_SECTION wlock;
	rbuf rb;
	bool subscribed;
	volatile LONG finished;
	wn_conn *next;
};

struct wn_server {
	wchar_t name[256];
	wn_request_fn fn;
	void *ud;
	HANDLE stop_ev;
	HANDLE accept_thread;
	HANDLE next_pipe; /* instance waiting for a client */
	CRITICAL_SECTION lock;
	wn_conn *conns;
};

/* Overlapped I/O that can be interrupted by the server stop event. */
static bool ov_io(wn_conn *c, bool write, void *buf, DWORD n, DWORD *done)
{
	OVERLAPPED ov = {0};
	ov.hEvent = write ? c->wr_ev : c->rd_ev;
	BOOL ok = write ? WriteFile(c->pipe, buf, n, NULL, &ov) : ReadFile(c->pipe, buf, n, NULL, &ov);
	if (!ok && GetLastError() != ERROR_IO_PENDING)
		return false;
	if (!write) {
		HANDLE w[2] = {ov.hEvent, c->srv->stop_ev};
		if (WaitForMultipleObjects(2, w, FALSE, INFINITE) != WAIT_OBJECT_0) {
			CancelIoEx(c->pipe, &ov);
			GetOverlappedResult(c->pipe, &ov, done, TRUE);
			return false;
		}
	}
	return GetOverlappedResult(c->pipe, &ov, done, TRUE) && (*done || !n || write);
}

static bool conn_write_line(wn_conn *c, const char *line)
{
	size_t n = strlen(line);
	char *tmp = xrealloc(NULL, n + 1);
	memcpy(tmp, line, n);
	tmp[n] = '\n';
	bool ok = true;
	EnterCriticalSection(&c->wlock);
	for (size_t off = 0; ok && off < n + 1;) {
		DWORD done = 0;
		ok = ov_io(c, true, tmp + off, (DWORD)(n + 1 - off), &done);
		off += done;
	}
	LeaveCriticalSection(&c->wlock);
	free(tmp);
	return ok;
}

void wn_reply(wn_conn *c, const char *line) { conn_write_line(c, line); }

unsigned long wn_conn_client_pid(wn_conn *c)
{
	ULONG pid = 0;
	if (!GetNamedPipeClientProcessId(c->pipe, &pid))
		return 0;
	return pid;
}
void wn_reply_ok(wn_conn *c) { conn_write_line(c, "OK"); }

void wn_reply_err(wn_conn *c, const char *msg)
{
	wn_buf b = {0};
	wn_buf_field(&b, "ERR");
	wn_buf_field(&b, msg);
	conn_write_line(c, b.data);
	wn_buf_free(&b);
}

static DWORD WINAPI conn_main(LPVOID arg)
{
	wn_conn *c = arg;
	for (;;) {
		char *line;
		while (!(line = rbuf_take_line(&c->rb))) {
			if (c->rb.len > WN_MAX_LINE)
				goto out;
			rbuf_reserve(&c->rb);
			DWORD got = 0;
			if (!ov_io(c, false, c->rb.data + c->rb.len, (DWORD)(c->rb.cap - c->rb.len), &got) || !got)
				goto out;
			c->rb.len += got;
		}
		char **f;
		size_t nf = wn_split(line, &f);
		if (!strcmp(f[0], "Subscribe")) {
			EnterCriticalSection(&c->srv->lock);
			c->subscribed = true;
			LeaveCriticalSection(&c->srv->lock);
			wn_reply_ok(c);
		} else {
			c->srv->fn(c, f, nf, c->srv->ud);
		}
		free(f);
		free(line);
	}
out:
	EnterCriticalSection(&c->srv->lock);
	c->subscribed = false;
	LeaveCriticalSection(&c->srv->lock);
	DisconnectNamedPipe(c->pipe);
	InterlockedExchange(&c->finished, 1);
	return 0;
}

static HANDLE make_instance(wn_server *s, bool first)
{
	return CreateNamedPipeW(s->name,
				PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED |
					(first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
				PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
				PIPE_UNLIMITED_INSTANCES, WN_PIPE_BUF, WN_PIPE_BUF, 0, NULL);
}

static void conn_destroy(wn_conn *c)
{
	WaitForSingleObject(c->thread, INFINITE);
	CloseHandle(c->thread);
	CloseHandle(c->pipe);
	CloseHandle(c->rd_ev);
	CloseHandle(c->wr_ev);
	DeleteCriticalSection(&c->wlock);
	free(c->rb.data);
	free(c);
}

/* Join and free connections whose thread has finished. */
static void reap(wn_server *s)
{
	EnterCriticalSection(&s->lock);
	wn_conn **pp = &s->conns, *dead = NULL;
	while (*pp) {
		wn_conn *c = *pp;
		if (c->finished) {
			*pp = c->next;
			c->next = dead;
			dead = c;
		} else {
			pp = &c->next;
		}
	}
	LeaveCriticalSection(&s->lock);
	while (dead) {
		wn_conn *n = dead->next;
		conn_destroy(dead);
		dead = n;
	}
}

static DWORD WINAPI accept_main(LPVOID arg)
{
	wn_server *s = arg;
	HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
	for (;;) {
		HANDLE pipe = s->next_pipe;
		OVERLAPPED ov = {0};
		ov.hEvent = ev;
		ResetEvent(ev);
		BOOL ok = ConnectNamedPipe(pipe, &ov);
		DWORD e = GetLastError();
		if (!ok && e == ERROR_IO_PENDING) {
			HANDLE w[2] = {ev, s->stop_ev};
			if (WaitForMultipleObjects(2, w, FALSE, INFINITE) != WAIT_OBJECT_0) {
				CancelIoEx(pipe, &ov);
				DWORD d;
				GetOverlappedResult(pipe, &ov, &d, TRUE);
				break;
			}
			DWORD d;
			ok = GetOverlappedResult(pipe, &ov, &d, FALSE);
		} else if (!ok && e == ERROR_PIPE_CONNECTED) {
			ok = TRUE;
		}
		/* keep one instance listening before serving this one */
		s->next_pipe = make_instance(s, false);
		if (ok) {
			wn_conn *c = calloc(1, sizeof *c);
			c->srv = s;
			c->pipe = pipe;
			c->rd_ev = CreateEventW(NULL, TRUE, FALSE, NULL);
			c->wr_ev = CreateEventW(NULL, TRUE, FALSE, NULL);
			InitializeCriticalSection(&c->wlock);
			EnterCriticalSection(&s->lock);
			c->next = s->conns;
			s->conns = c;
			LeaveCriticalSection(&s->lock);
			c->thread = CreateThread(NULL, 0, conn_main, c, 0, NULL);
		} else {
			CloseHandle(pipe);
		}
		reap(s);
		if (s->next_pipe == INVALID_HANDLE_VALUE)
			break;
	}
	CloseHandle(ev);
	return 0;
}

wn_server *wn_server_start(wn_request_fn fn, void *ud, char *err, size_t errcap)
{
	wn_server *s = calloc(1, sizeof *s);
	wn_pipe_name(s->name, sizeof s->name / sizeof s->name[0]);
	s->fn = fn;
	s->ud = ud;
	s->next_pipe = make_instance(s, true);
	if (s->next_pipe == INVALID_HANDLE_VALUE) {
		DWORD e = GetLastError();
		if (err) {
			char *n8 = wn_utf8(s->name);
			snprintf(err, errcap,
				 e == ERROR_ACCESS_DENIED ? "another notification daemon owns %s"
							   : "CreateNamedPipe(%s) failed",
				 n8);
			free(n8);
		}
		free(s);
		return NULL;
	}
	InitializeCriticalSection(&s->lock);
	s->stop_ev = CreateEventW(NULL, TRUE, FALSE, NULL);
	s->accept_thread = CreateThread(NULL, 0, accept_main, s, 0, NULL);
	return s;
}

void wn_server_emit(wn_server *s, const char *line)
{
	EnterCriticalSection(&s->lock);
	for (wn_conn *c = s->conns; c; c = c->next)
		if (c->subscribed && !c->finished)
			conn_write_line(c, line);
	LeaveCriticalSection(&s->lock);
}

void wn_emit_closed(wn_server *s, uint32_t id, enum wn_close_reason reason)
{
	char line[64];
	snprintf(line, sizeof line, "NotificationClosed\t%lu\t%d", (unsigned long)id, (int)reason);
	wn_server_emit(s, line);
}

void wn_emit_action(wn_server *s, uint32_t id, const char *action_key)
{
	wn_buf b = {0};
	wn_buf_field(&b, "ActionInvoked");
	wn_buf_fieldf(&b, "%lu", (unsigned long)id);
	wn_buf_field(&b, action_key);
	wn_server_emit(s, b.data);
	wn_buf_free(&b);
}

void wn_server_stop(wn_server *s)
{
	if (!s)
		return;
	SetEvent(s->stop_ev);
	WaitForSingleObject(s->accept_thread, INFINITE);
	CloseHandle(s->accept_thread);
	if (s->next_pipe != INVALID_HANDLE_VALUE)
		CloseHandle(s->next_pipe);
	/* conn threads see stop_ev in their reads and exit */
	EnterCriticalSection(&s->lock);
	wn_conn *c = s->conns;
	s->conns = NULL;
	LeaveCriticalSection(&s->lock);
	while (c) {
		wn_conn *n = c->next;
		conn_destroy(c);
		c = n;
	}
	DeleteCriticalSection(&s->lock);
	CloseHandle(s->stop_ev);
	free(s);
}

/* ---- client ------------------------------------------------------------- */

struct wn_client {
	HANDLE pipe;
	rbuf rb;
};

wn_client *wn_client_open(unsigned timeout_ms)
{
	wchar_t name[256];
	wn_pipe_name(name, 256);
	return wn_client_open_name(name, timeout_ms);
}

wn_client *wn_client_open_name(const wchar_t *name, unsigned timeout_ms)
{
	ULONGLONG deadline = GetTickCount64() + timeout_ms;
	for (;;) {
		HANDLE h = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
		if (h != INVALID_HANDLE_VALUE) {
			wn_client *c = calloc(1, sizeof *c);
			c->pipe = h;
			return c;
		}
		DWORD e = GetLastError();
		ULONGLONG now = GetTickCount64();
		if (now >= deadline)
			return NULL;
		if (e == ERROR_PIPE_BUSY)
			WaitNamedPipeW(name, (DWORD)(deadline - now));
		else if (e == ERROR_FILE_NOT_FOUND)
			Sleep(50); /* daemon not up yet */
		else
			return NULL;
	}
}

bool wn_client_send(wn_client *c, const char *line)
{
	size_t n = strlen(line);
	char *tmp = xrealloc(NULL, n + 1);
	memcpy(tmp, line, n);
	tmp[n] = '\n';
	size_t off = 0;
	while (off < n + 1) {
		DWORD w = 0;
		if (!WriteFile(c->pipe, tmp + off, (DWORD)(n + 1 - off), &w, NULL))
			break;
		off += w;
	}
	free(tmp);
	return off == n + 1;
}

char *wn_client_read_line(wn_client *c)
{
	char *line;
	while (!(line = rbuf_take_line(&c->rb))) {
		if (c->rb.len > WN_MAX_LINE)
			return NULL;
		rbuf_reserve(&c->rb);
		DWORD got = 0;
		if (!ReadFile(c->pipe, c->rb.data + c->rb.len, (DWORD)(c->rb.cap - c->rb.len), &got, NULL) || !got)
			return NULL;
		c->rb.len += got;
	}
	return line;
}

char *wn_client_call(wn_client *c, const char *line)
{
	return wn_client_send(c, line) ? wn_client_read_line(c) : NULL;
}

void wn_client_close(wn_client *c)
{
	if (!c)
		return;
	CloseHandle(c->pipe);
	free(c->rb.data);
	free(c);
}
