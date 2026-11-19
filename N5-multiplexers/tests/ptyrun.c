/*
 * ptyrun - drive a console program through a ConPTY, like `expect` lite.
 *
 *   ptyrun [-c cols] [-r rows] [-t total_ms] [-k ms:keys]... -- cmd args...
 *
 * Every -k waits `ms` milliseconds (relative to the previous step) and types
 * `keys`. Escapes: \r \n \t \e \\ \xHH. stdout receives the raw VT stream
 * the program rendered; stderr gets "[exit N]". Used by tests/run-tests.ps1
 * to exercise attach/detach, which needs a real terminal on the client side.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static HANDLE out_r;
static volatile LONG done;

static DWORD WINAPI reader(void *arg)
{
	(void)arg;
	char buf[4096];
	DWORD n, w;
	HANDLE so = GetStdHandle(STD_OUTPUT_HANDLE);
	while (ReadFile(out_r, buf, sizeof buf, &n, NULL) && n)
		WriteFile(so, buf, n, &w, NULL);
	InterlockedExchange(&done, 1);
	return 0;
}

static size_t unescape(const char *s, char *o)
{
	size_t n = 0;
	for (; *s; s++) {
		if (*s != '\\' || !s[1]) { o[n++] = *s; continue; }
		switch (*++s) {
		case 'r': o[n++] = '\r'; break;
		case 'n': o[n++] = '\n'; break;
		case 't': o[n++] = '\t'; break;
		case 'e': o[n++] = 27; break;
		case 'x': {
			char hex[3] = { s[1], s[2], 0 };
			o[n++] = (char)strtol(hex, NULL, 16);
			s += 2;
			break;
		}
		default: o[n++] = *s;
		}
	}
	return n;
}

static void quote(wchar_t *dst, const char *a)
{
	/* good enough for tests: wrap in quotes when there is a space */
	wchar_t w[1024];
	MultiByteToWideChar(CP_UTF8, 0, a, -1, w, 1024);
	if (*dst) wcscat(dst, L" ");
	if (wcschr(w, L' ') || !*w) { wcscat(dst, L"\""); wcscat(dst, w); wcscat(dst, L"\""); }
	else wcscat(dst, w);
}

int main(int argc, char **argv)
{
	SHORT cols = 80, rows = 25;
	DWORD total = 5000;
	const char *steps[64];
	int nsteps = 0, i;
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--")) { i++; break; }
		if (!strcmp(argv[i], "-c")) cols = (SHORT)atoi(argv[++i]);
		else if (!strcmp(argv[i], "-r")) rows = (SHORT)atoi(argv[++i]);
		else if (!strcmp(argv[i], "-t")) total = (DWORD)atoi(argv[++i]);
		else if (!strcmp(argv[i], "-k") && nsteps < 64) steps[nsteps++] = argv[++i];
	}
	if (i >= argc) {
		fprintf(stderr, "usage: ptyrun [-c C] [-r R] [-t ms] [-k ms:keys]... -- cmd...\n");
		return 2;
	}
	static wchar_t cmd[8192];
	for (; i < argc; i++)
		quote(cmd, argv[i]);

	HANDLE in_r, in_w, out_w;
	HPCON pc;
	CreatePipe(&in_r, &in_w, NULL, 0);
	CreatePipe(&out_r, &out_w, NULL, 0);
	COORD sz = { cols, rows };
	if (FAILED(CreatePseudoConsole(sz, in_r, out_w, 0, &pc))) {
		fprintf(stderr, "CreatePseudoConsole failed\n");
		return 2;
	}
	CloseHandle(in_r);
	CloseHandle(out_w);

	STARTUPINFOEXW si = { 0 };
	SIZE_T asz = 0;
	si.StartupInfo.cb = sizeof si;
	si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
	InitializeProcThreadAttributeList(NULL, 1, 0, &asz);
	si.lpAttributeList = malloc(asz);
	InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &asz);
	UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
	                          pc, sizeof pc, NULL, NULL);
	PROCESS_INFORMATION pi;
	if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, EXTENDED_STARTUPINFO_PRESENT,
	                    NULL, NULL, &si.StartupInfo, &pi)) {
		fprintf(stderr, "CreateProcess failed: %lu\n", GetLastError());
		return 2;
	}
	CloseHandle(CreateThread(NULL, 0, reader, NULL, 0, NULL));

	ULONGLONG start = GetTickCount64();
	for (int s = 0; s < nsteps; s++) {
		char keys[1024];
		DWORD ms = (DWORD)atoi(steps[s]), w;
		const char *colon = strchr(steps[s], ':');
		size_t n = colon ? unescape(colon + 1, keys) : 0;
		if (WaitForSingleObject(pi.hProcess, ms) == WAIT_OBJECT_0)
			break;
		if (!strncmp(colon ? colon + 1 : "", "RESIZE", 6)) {
			int c = 0, r = 0;
			sscanf(colon + 7, "%dx%d", &c, &r);
			COORD nz = { (SHORT)c, (SHORT)r };
			ResizePseudoConsole(pc, nz);
			continue;
		}
		WriteFile(in_w, keys, (DWORD)n, &w, NULL);
	}
	ULONGLONG used = GetTickCount64() - start;
	WaitForSingleObject(pi.hProcess, used < total ? (DWORD)(total - used) : 0);
	DWORD code = STILL_ACTIVE;
	GetExitCodeProcess(pi.hProcess, &code);
	if (code == STILL_ACTIVE)
		TerminateProcess(pi.hProcess, 259);
	Sleep(300);
	ClosePseudoConsole(pc);
	for (int k = 0; k < 40 && !done; k++)
		Sleep(50);
	/* on stdout too: PowerShell 5.1 mangles redirected native stderr */
	printf("\n[exit %lu]\n", code);
	fprintf(stderr, "\n[exit %lu]\n", code);
	return 0;
}
