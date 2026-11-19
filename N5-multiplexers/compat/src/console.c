/*
 * console.c - termios on top of the Windows console.
 *
 * A Unix tty is a byte stream plus a line discipline configured by termios.
 * A Windows console is an *event queue* (KEY_EVENT, MOUSE_EVENT,
 * WINDOW_BUFFER_SIZE_EVENT, FOCUS_EVENT...) configured by mode bits:
 *
 *   termios                       console input mode
 *   ICANON                        ENABLE_LINE_INPUT
 *   ECHO                          ENABLE_ECHO_INPUT
 *   ISIG                          ENABLE_PROCESSED_INPUT (Ctrl+C -> signal)
 *   "raw" (arrows as ESC [ A)     ENABLE_VIRTUAL_TERMINAL_INPUT
 *   SIGWINCH                      ENABLE_WINDOW_INPUT
 *
 *   OPOST/ONLCR                   output: !DISABLE_NEWLINE_AUTO_RETURN
 *   (interpreting ESC sequences)  output: ENABLE_VIRTUAL_TERMINAL_PROCESSING
 *
 * In byte mode (dtach/abduco) read(0) turns key events into UTF-8 bytes.
 * In events mode (dvtm + PDCurses) nobody calls read(0): curses reads the
 * events itself and select() must only tell it when a *useful* event waits.
 */
#include "compat_internal.h"

#define T_ISIG   0000001
#define T_ICANON 0000002
#define T_ECHO   0000010
#define T_IEXTEN 0100000
#define T_OPOST  0000001
#define T_ONLCR  0000004
#define T_ICRNL  0000400
#define T_IXON   0002000
#define T_CS8    0000060
#define T_CREAD  0000200

static int events_mode;
static UINT saved_in_cp, saved_out_cp;
static int cp_changed;

void compat_console_events_mode(int on)
{
	events_mode = on;
}

HANDLE console_in(void)
{
	HANDLE h = (HANDLE)_get_osfhandle(0);
	DWORD m;
	if (h == INVALID_HANDLE_VALUE || !GetConsoleMode(h, &m))
		return NULL;
	return h;
}

/*
 * CONOUT$ instead of stdout: TIOCGWINSZ on fd 0 must work even when stdout
 * is redirected (abduco -p, dvtm under a pipe...).
 */
static HANDLE conout(void)
{
	static HANDLE h;
	if (!h || h == INVALID_HANDLE_VALUE)
		h = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
		                FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
		                OPEN_EXISTING, 0, NULL);
	return h;
}

HANDLE console_out(void)
{
	HANDLE h = (HANDLE)_get_osfhandle(1);
	DWORD m;
	if (h == INVALID_HANDLE_VALUE || !GetConsoleMode(h, &m))
		return NULL;
	return h;
}

int console_winsize(struct winsize *ws)
{
	CONSOLE_SCREEN_BUFFER_INFO info;
	HANDLE h = conout();
	if (h == INVALID_HANDLE_VALUE || !GetConsoleScreenBufferInfo(h, &info))
		return -1;
	/* The *window*, not the buffer: a classic conhost buffer has 9001
	 * rows of scrollback. */
	ws->ws_col = (unsigned short)(info.srWindow.Right - info.srWindow.Left + 1);
	ws->ws_row = (unsigned short)(info.srWindow.Bottom - info.srWindow.Top + 1);
	ws->ws_xpixel = ws->ws_ypixel = 0;
	return 0;
}

int console_poll_resize(void)
{
	static struct winsize last;
	struct winsize now;
	if (console_winsize(&now) < 0)
		return 0;
	if (last.ws_row == 0) {
		last = now;
		return 0;
	}
	if (now.ws_row != last.ws_row || now.ws_col != last.ws_col) {
		last = now;
		compat_signal_post(C_SIGWINCH);
		return 1;
	}
	return 0;
}

/* ---- termios <-> console modes ----------------------------------------- */

static int fd_console_handles(int fd, HANDLE *in, HANDLE *out)
{
	HANDLE h = (HANDLE)_get_osfhandle(fd);
	DWORD m;
	if (h == INVALID_HANDLE_VALUE || !GetConsoleMode(h, &m))
		return -1;
	/* termios is per *terminal*: fd 0, 1 and 2 all configure both sides. */
	*in = console_in();
	if (!*in)
		*in = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE,
		                  FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
		                  OPEN_EXISTING, 0, NULL);
	*out = conout();
	return 0;
}

static void modes_to_termios(DWORD in, DWORD out, struct termios *t)
{
	memset(t, 0, sizeof *t);
	t->c_iflag = T_ICRNL | T_IXON;
	t->c_oflag = (out & DISABLE_NEWLINE_AUTO_RETURN) ? 0 : (T_OPOST | T_ONLCR);
	t->c_cflag = T_CS8 | T_CREAD;
	t->c_lflag = T_IEXTEN;
	if (in & ENABLE_LINE_INPUT)      t->c_lflag |= T_ICANON;
	if (in & ENABLE_ECHO_INPUT)      t->c_lflag |= T_ECHO;
	if (in & ENABLE_PROCESSED_INPUT) t->c_lflag |= T_ISIG;
	t->c_cc[0] = 3;     /* VINTR  ^C */
	t->c_cc[1] = 28;    /* VQUIT  ^\ */
	t->c_cc[2] = 127;   /* VERASE: VT input sends DEL for Backspace */
	t->c_cc[3] = 21;    /* VKILL  ^U */
	t->c_cc[4] = 26;    /* VEOF   ^Z: the Windows convention, not ^D */
	t->c_cc[5] = 0;     /* VTIME */
	t->c_cc[6] = 1;     /* VMIN */
	/*
	 * VSUSP: no job control, so suspend is disabled. 0xFF rather than
	 * _POSIX_VDISABLE (0): dtach compares the first input byte with
	 * c_cc[VSUSP] and Ctrl+@ / Ctrl+Space produce a real NUL byte. 0xFF can
	 * never start a UTF-8 sequence.
	 */
	t->c_cc[10] = 0xFF;
	t->c_cc[15] = 22;   /* VLNEXT ^V */
	t->c_win_in_mode = in;
	t->c_win_out_mode = out;
	t->c_win_valid = 1;
}

int compat_tcgetattr(int fd, struct termios *t)
{
	struct vfd *v = vfd_get(fd);
	if (v && v->pty) {
		/* ConPTY does not expose the slave's line discipline. Report a
		 * cooked terminal: the honest "I don't know" answer, and it keeps
		 * dtach's ^L redraw heuristic from typing ^L into cmd.exe. */
		modes_to_termios(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT |
		                 ENABLE_PROCESSED_INPUT, 0, t);
		t->c_win_valid = 0;
		return 0;
	}
	HANDLE in, out;
	DWORD im = 0, om = 0;
	if (fd_console_handles(fd, &in, &out) < 0) {
		errno = ENOTTY;
		return -1;
	}
	GetConsoleMode(in, &im);
	GetConsoleMode(out, &om);
	modes_to_termios(im, om, t);
	return 0;
}

static void restore_codepages(void)
{
	if (cp_changed) {
		SetConsoleCP(saved_in_cp);
		SetConsoleOutputCP(saved_out_cp);
		cp_changed = 0;
	}
}

int compat_tcsetattr(int fd, int act, const struct termios *t)
{
	HANDLE in, out;
	if (vfd_get(fd))
		return 0; /* pty master: nothing to configure */
	if (fd_console_handles(fd, &in, &out) < 0) {
		errno = ENOTTY;
		return -1;
	}
	DWORD im, om;
	struct termios same;
	if (t->c_win_valid) {
		modes_to_termios(t->c_win_in_mode, t->c_win_out_mode, &same);
	}
	if (t->c_win_valid && same.c_lflag == t->c_lflag && same.c_oflag == t->c_oflag) {
		/* Untouched copy (orig_term): restore the exact original bits. */
		im = t->c_win_in_mode;
		om = t->c_win_out_mode;
		restore_codepages();
	} else {
		GetConsoleMode(in, &im);
		GetConsoleMode(out, &om);
		im &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
		if (t->c_lflag & T_ICANON) im |= ENABLE_LINE_INPUT;
		if ((t->c_lflag & T_ECHO) && (t->c_lflag & T_ICANON)) im |= ENABLE_ECHO_INPUT;
		if (t->c_lflag & T_ISIG) im |= ENABLE_PROCESSED_INPUT;
		im |= ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS;
		if (!(t->c_lflag & T_ICANON)) {
			/* Raw: arrows/F-keys arrive as ESC sequences, and mouse
			 * selection must not swallow keystrokes. */
			im |= ENABLE_VIRTUAL_TERMINAL_INPUT;
			im &= ~ENABLE_QUICK_EDIT_MODE;
		} else {
			im &= ~ENABLE_VIRTUAL_TERMINAL_INPUT;
		}
		om |= ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
		if (t->c_oflag & T_OPOST)
			om &= ~DISABLE_NEWLINE_AUTO_RETURN;
		else
			om |= DISABLE_NEWLINE_AUTO_RETURN;
		if (!cp_changed) {
			saved_in_cp = GetConsoleCP();
			saved_out_cp = GetConsoleOutputCP();
			cp_changed = 1;
		}
		SetConsoleCP(CP_UTF8);
		SetConsoleOutputCP(CP_UTF8);
	}
	if (act == 2 /* TCSAFLUSH */)
		FlushConsoleInputBuffer(in);
	if (!SetConsoleMode(in, im) || !SetConsoleMode(out, om)) {
		errno = EINVAL;
		return -1;
	}
	return 0;
}

void compat_cfmakeraw(struct termios *t)
{
	t->c_iflag &= ~(0000001 | 0000002 | 0000010 | 0000040 | 0000100 |
	                0000200 | T_ICRNL | T_IXON);
	t->c_oflag &= ~T_OPOST;
	t->c_lflag &= ~(T_ECHO | 0000100 | T_ICANON | T_ISIG | T_IEXTEN);
	t->c_cflag |= T_CS8;
	t->c_cc[6] = 1;
	t->c_cc[5] = 0;
}

/* ---- input ------------------------------------------------------------- */

static unsigned char inbuf[256];
static size_t inlen;
static wchar_t high_surrogate;

static void put_utf8(unsigned cp)
{
	if (inlen + 4 > sizeof inbuf)
		return;
	if (cp < 0x80) {
		inbuf[inlen++] = (unsigned char)cp;
	} else if (cp < 0x800) {
		inbuf[inlen++] = (unsigned char)(0xC0 | (cp >> 6));
		inbuf[inlen++] = (unsigned char)(0x80 | (cp & 0x3F));
	} else if (cp < 0x10000) {
		inbuf[inlen++] = (unsigned char)(0xE0 | (cp >> 12));
		inbuf[inlen++] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
		inbuf[inlen++] = (unsigned char)(0x80 | (cp & 0x3F));
	} else {
		inbuf[inlen++] = (unsigned char)(0xF0 | (cp >> 18));
		inbuf[inlen++] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
		inbuf[inlen++] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
		inbuf[inlen++] = (unsigned char)(0x80 | (cp & 0x3F));
	}
}

static void key_to_bytes(const KEY_EVENT_RECORD *k)
{
	wchar_t c = k->uChar.UnicodeChar;
	if (!k->bKeyDown || !c)
		return;
	for (WORD i = 0; i < (k->wRepeatCount ? k->wRepeatCount : 1); i++) {
		if (c >= 0xD800 && c <= 0xDBFF) {
			high_surrogate = c;     /* UTF-16 pair split over 2 events */
			continue;
		}
		if (c >= 0xDC00 && c <= 0xDFFF && high_surrogate) {
			unsigned cp = 0x10000 + (((unsigned)high_surrogate - 0xD800) << 10) +
			              ((unsigned)c - 0xDC00);
			high_surrogate = 0;
			put_utf8(cp);
			continue;
		}
		put_utf8(c);
	}
}

static int is_modifier_vk(WORD vk)
{
	switch (vk) {
	case VK_SHIFT: case VK_CONTROL: case VK_MENU: case VK_CAPITAL:
	case VK_NUMLOCK: case VK_SCROLL: case VK_LWIN: case VK_RWIN:
	case VK_LSHIFT: case VK_RSHIFT: case VK_LCONTROL: case VK_RCONTROL:
	case VK_LMENU: case VK_RMENU:
		return 1;
	}
	return 0;
}

/* 1 = this event would make read()/getch() return something. */
static int useful_event(const INPUT_RECORD *r)
{
	switch (r->EventType) {
	case KEY_EVENT:
		if (!r->Event.KeyEvent.bKeyDown)
			return 0;
		if (events_mode)
			return r->Event.KeyEvent.uChar.UnicodeChar ||
			       !is_modifier_vk(r->Event.KeyEvent.wVirtualKeyCode);
		return r->Event.KeyEvent.uChar.UnicodeChar != 0;
	case MOUSE_EVENT:
		return events_mode && r->Event.MouseEvent.dwEventFlags != MOUSE_MOVED;
	default:
		return 0;
	}
}

/*
 * The console handle is "signaled" whenever the queue is not empty, even if
 * it only holds key-up or focus events. Left there, those would make
 * select() spin (or worse, report readable and then block in read()).
 * Drop the useless prefix of the queue; resize events become SIGWINCH.
 */
int console_input_ready(void)
{
	HANDLE h = console_in();
	if (!h)
		return 0;
	if (inlen)
		return 1;
	for (;;) {
		INPUT_RECORD recs[32];
		DWORD n = 0, junk = 0;
		if (!GetNumberOfConsoleInputEvents(h, &n) || n == 0)
			return 0;
		if (!PeekConsoleInputW(h, recs, 32, &n) || n == 0)
			return 0;
		while (junk < n && !useful_event(&recs[junk])) {
			if (recs[junk].EventType == WINDOW_BUFFER_SIZE_EVENT)
				console_poll_resize();
			junk++;
		}
		if (junk < n)
			return 1;
		ReadConsoleInputW(h, recs, junk, &n); /* discard exactly those */
	}
}

int console_read(void *buf, size_t n)
{
	HANDLE h = console_in();
	while (!inlen) {
		INPUT_RECORD recs[32];
		DWORD got = 0;
		if (!ReadConsoleInputW(h, recs, 32, &got)) {
			errno = EIO;
			return -1;
		}
		for (DWORD i = 0; i < got; i++) {
			if (recs[i].EventType == KEY_EVENT)
				key_to_bytes(&recs[i].Event.KeyEvent);
			else if (recs[i].EventType == WINDOW_BUFFER_SIZE_EVENT)
				console_poll_resize();
		}
	}
	size_t k = inlen < n ? inlen : n;
	memcpy(buf, inbuf, k);
	memmove(inbuf, inbuf + k, inlen - k);
	inlen -= k;
	return (int)k;
}

/* ---- output ------------------------------------------------------------ */

/*
 * UTF-8 -> UTF-16 -> WriteConsoleW. Writing UTF-8 bytes with WriteFile under
 * code page 65001 mostly works, but a multi-byte character split across two
 * write() calls (the pty hands us arbitrary 4 KiB chunks) becomes two U+FFFD.
 * Keep the incomplete tail for the next call.
 */
int console_write(HANDLE h, const char *buf, size_t n)
{
	static char carry[2][4];
	static int carrylen[2];
	int slot = h == GetStdHandle(STD_ERROR_HANDLE) ? 1 : 0;

	size_t total = carrylen[slot] + n;
	char *tmp = malloc(total);
	wchar_t *w = malloc((total + 1) * sizeof *w);
	if (!tmp || !w) {
		free(tmp); free(w);
		errno = ENOMEM;
		return -1;
	}
	memcpy(tmp, carry[slot], carrylen[slot]);
	memcpy(tmp + carrylen[slot], buf, n);

	/* find the last complete UTF-8 sequence boundary */
	size_t end = total, back = 0;
	while (back < 3 && back < total &&
	       ((unsigned char)tmp[total - 1 - back] & 0xC0) == 0x80)
		back++;
	if (back < total) {
		unsigned char lead = (unsigned char)tmp[total - 1 - back];
		int need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
		if (need > (int)back + 1)
			end = total - 1 - back;
	}
	carrylen[slot] = (int)(total - end);
	memcpy(carry[slot], tmp + end, carrylen[slot]);

	int wn = MultiByteToWideChar(CP_UTF8, 0, tmp, (int)end, w, (int)total + 1);
	DWORD done = 0;
	for (int off = 0; off < wn; off += done) {
		if (!WriteConsoleW(h, w + off, wn - off, &done, NULL) || !done)
			break;
	}
	free(tmp);
	free(w);
	return (int)n;
}
