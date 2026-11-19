/*
 * misc.c - users, environment, getopt, wcwidth, sysconf.
 */
#include "compat_internal.h"

uid_t compat_getuid(void)
{
	/* A stable number per user: the RID (last sub-authority) of the SID,
	 * e.g. 1001 for S-1-5-21-...-1001. Close in spirit to a Unix uid. */
	static uid_t uid;
	if (uid)
		return uid;
	HANDLE tok;
	BYTE buf[256];
	DWORD len;
	uid = 1000;
	if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
		if (GetTokenInformation(tok, TokenUser, buf, sizeof buf, &len)) {
			PSID sid = ((TOKEN_USER *)buf)->User.Sid;
			UCHAR n = *GetSidSubAuthorityCount(sid);
			if (n)
				uid = (uid_t)(*GetSidSubAuthority(sid, n - 1) & 0x7fff);
		}
		CloseHandle(tok);
	}
	return uid;
}

static char *env_dup(const char *k, const char *fallback)
{
	const char *v = getenv(k);
	return _strdup(v && *v ? v : fallback);
}

struct passwd *compat_getpwuid(uid_t uid)
{
	static struct passwd pw;
	if (uid != compat_getuid())
		return NULL;
	if (!pw.pw_name) {
		pw.pw_name = env_dup("USERNAME", "user");
		pw.pw_passwd = "x";
		pw.pw_uid = pw.pw_gid = uid;
		pw.pw_gecos = pw.pw_name;
		pw.pw_dir = env_dup("USERPROFILE", "C:\\");
		/* The login shell of a Windows user is %COMSPEC% (cmd.exe). */
		pw.pw_shell = env_dup("COMSPEC", "cmd.exe");
	}
	return &pw;
}

int compat_setenv(const char *k, const char *v, int overwrite)
{
	if (!k || !*k || strchr(k, '=')) {
		errno = EINVAL;
		return -1;
	}
	if (!overwrite && getenv(k))
		return 0;
	/* _putenv_s updates both the CRT copy (getenv) and the OS block
	 * (inherited by CreateProcess). An empty value would delete the
	 * variable on Windows; POSIX allows empty values. */
	return _putenv_s(k, v && *v ? v : " ") == 0 ? 0 : -1;
}

int compat_unsetenv(const char *k)
{
	return _putenv_s(k, "") == 0 ? 0 : -1;
}

long compat_sysconf(int name)
{
	if (name == 4 /* _SC_OPEN_MAX */)
		return _getmaxstdio();
	errno = EINVAL;
	return -1;
}

/* ---- getopt (POSIX subset: no GNU permutation) -------------------------- */
char *compat_optarg;
int compat_optind = 1, compat_opterr = 1, compat_optopt;

int compat_getopt(int argc, char *const argv[], const char *opts)
{
	static int pos = 1;
	if (compat_optind >= argc || !argv[compat_optind] ||
	    argv[compat_optind][0] != '-' || !argv[compat_optind][1])
		return -1;
	if (!strcmp(argv[compat_optind], "--")) {
		compat_optind++;
		return -1;
	}
	int c = (unsigned char)argv[compat_optind][pos];
	const char *o = c != ':' ? strchr(opts, c) : NULL;
	compat_optopt = c;
	if (!o) {
		if (compat_opterr && *opts != ':')
			fprintf(stderr, "%s: illegal option -- %c\n", argv[0], c);
		if (!argv[compat_optind][++pos]) {
			compat_optind++;
			pos = 1;
		}
		return '?';
	}
	if (o[1] == ':') {
		if (argv[compat_optind][pos + 1]) {
			compat_optarg = &argv[compat_optind][pos + 1];
		} else if (compat_optind + 1 < argc) {
			compat_optarg = argv[++compat_optind];
		} else {
			compat_optind++;
			pos = 1;
			if (compat_opterr && *opts != ':')
				fprintf(stderr, "%s: option requires an argument -- %c\n", argv[0], c);
			return *opts == ':' ? ':' : '?';
		}
		compat_optind++;
		pos = 1;
	} else {
		compat_optarg = NULL;
		if (!argv[compat_optind][++pos]) {
			compat_optind++;
			pos = 1;
		}
	}
	return c;
}

/* ---- wcwidth ------------------------------------------------------------
 * After Markus Kuhn's public-domain mk_wcwidth(). Windows' wchar_t is UTF-16:
 * astral characters never reach here as one unit, so only the BMP matters.
 */
struct range { unsigned short lo, hi; };

static const struct range zero_width[] = {
	{0x0300,0x036F},{0x0483,0x0489},{0x0591,0x05BD},{0x05BF,0x05BF},
	{0x05C1,0x05C2},{0x05C4,0x05C5},{0x05C7,0x05C7},{0x0610,0x061A},
	{0x064B,0x065F},{0x0670,0x0670},{0x06D6,0x06DC},{0x06DF,0x06E4},
	{0x06E7,0x06E8},{0x06EA,0x06ED},{0x0711,0x0711},{0x0730,0x074A},
	{0x07A6,0x07B0},{0x0900,0x0902},{0x093A,0x093A},{0x093C,0x093C},
	{0x0941,0x0948},{0x094D,0x094D},{0x0951,0x0957},{0x0962,0x0963},
	{0x0E31,0x0E31},{0x0E34,0x0E3A},{0x0E47,0x0E4E},{0x0EB1,0x0EB1},
	{0x0EB4,0x0EBC},{0x0EC8,0x0ECD},{0x1160,0x11FF},{0x1AB0,0x1AFF},
	{0x1DC0,0x1DFF},{0x200B,0x200F},{0x2028,0x202E},{0x2060,0x2064},
	{0x20D0,0x20FF},{0x302A,0x302D},{0x3099,0x309A},{0xFE00,0xFE0F},
	{0xFE20,0xFE2F},{0xFEFF,0xFEFF},
};

static const struct range double_width[] = {
	{0x1100,0x115F},{0x231A,0x231B},{0x2329,0x232A},{0x23E9,0x23EC},
	{0x23F0,0x23F0},{0x23F3,0x23F3},{0x25FD,0x25FE},{0x2614,0x2615},
	{0x2648,0x2653},{0x267F,0x267F},{0x2693,0x2693},{0x26A1,0x26A1},
	{0x26AA,0x26AB},{0x26BD,0x26BE},{0x26C4,0x26C5},{0x26CE,0x26CE},
	{0x26D4,0x26D4},{0x26EA,0x26EA},{0x26F2,0x26F3},{0x26F5,0x26F5},
	{0x26FA,0x26FA},{0x26FD,0x26FD},{0x2705,0x2705},{0x270A,0x270B},
	{0x2728,0x2728},{0x274C,0x274C},{0x274E,0x274E},{0x2753,0x2755},
	{0x2757,0x2757},{0x2795,0x2797},{0x27B0,0x27B0},{0x27BF,0x27BF},
	{0x2B1B,0x2B1C},{0x2B50,0x2B50},{0x2B55,0x2B55},{0x2E80,0x303E},
	{0x3041,0x33FF},{0x3400,0x4DBF},{0x4E00,0x9FFF},{0xA000,0xA4CF},
	{0xA960,0xA97F},{0xAC00,0xD7A3},{0xF900,0xFAFF},{0xFE10,0xFE19},
	{0xFE30,0xFE6F},{0xFF00,0xFF60},{0xFFE0,0xFFE6},
};

static int in_table(unsigned c, const struct range *t, size_t n)
{
	size_t lo = 0, hi = n;
	while (lo < hi) {
		size_t mid = (lo + hi) / 2;
		if (c < t[mid].lo)
			hi = mid;
		else if (c > t[mid].hi)
			lo = mid + 1;
		else
			return 1;
	}
	return 0;
}

int compat_wcwidth(wchar_t wc)
{
	unsigned c = (unsigned)wc;
	if (c == 0)
		return 0;
	if (c < 32 || (c >= 0x7F && c < 0xA0))
		return -1;
	if (c >= 0xD800 && c <= 0xDFFF)
		return 1; /* half of an astral char: assume narrow */
	if (in_table(c, zero_width, sizeof zero_width / sizeof *zero_width))
		return 0;
	if (in_table(c, double_width, sizeof double_width / sizeof *double_width))
		return 2;
	return 1;
}

int compat_wcswidth(const wchar_t *s, size_t n)
{
	int w = 0;
	for (size_t i = 0; i < n && s[i]; i++) {
		int c = compat_wcwidth(s[i]);
		if (c < 0)
			return -1;
		w += c;
	}
	return w;
}
