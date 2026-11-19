/* notify-send for Windows - libnotify's CLI, speaking wnotify (PROTOCOL.md).
 *
 *   notify-send [OPTIONS] SUMMARY [BODY]
 *     -u, --urgency=low|normal|critical   hint urgency (byte)
 *     -t, --expire-time=MS                expire_timeout (-1 server default, 0 never)
 *     -a, --app-name=NAME
 *     -i, --icon=ICON                     app_icon (path or name)
 *     -c, --category=TYPE[,TYPE...]       hint category (string)
 *     -h, --hint=TYPE:NAME:VALUE          TYPE: int|double|string|byte|boolean|
 *                                               or a D-Bus char (s,i,u,b,y,x,t,n,q,d)
 *     -r, --replace-id=ID
 *     -p, --print-id
 *     -A, --action=[KEY=]LABEL            repeatable; implies waiting, prints KEY
 *     -w, --wait                          wait until closed, print nothing
 *     -e, --transient                     hint transient=true
 *   Extras (no D-Bus tools on Windows, so they live here):
 *         --server-info | --capabilities | --close=ID | --monitor
 */
#define WIN32_LEAN_AND_MEAN
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wnotify.h"

static void usage(int code)
{
	fputs("Usage: notify-send [OPTION...] SUMMARY [BODY]\n"
	      "  -u, --urgency=LEVEL        low, normal, critical\n"
	      "  -t, --expire-time=MS       timeout in milliseconds\n"
	      "  -a, --app-name=NAME        application name\n"
	      "  -i, --icon=ICON            icon path or name\n"
	      "  -c, --category=TYPE        notification category\n"
	      "  -h, --hint=TYPE:NAME:VALUE extra data (int, double, string, byte, boolean)\n"
	      "  -r, --replace-id=ID        replace notification ID\n"
	      "  -p, --print-id             print the notification ID\n"
	      "  -A, --action=[KEY=]LABEL   add an action; prints the invoked KEY\n"
	      "  -w, --wait                 wait for the notification to be closed\n"
	      "  -e, --transient            transient notification\n"
	      "      --server-info          GetServerInformation\n"
	      "      --capabilities         GetCapabilities\n"
	      "      --close=ID             CloseNotification\n"
	      "      --monitor              print every signal (ActionInvoked, NotificationClosed)\n",
	      code ? stderr : stdout);
	exit(code);
}

static const char *hint_type(const char *t)
{
	if (!strcmp(t, "int")) return "i";
	if (!strcmp(t, "double")) return "d";
	if (!strcmp(t, "string")) return "s";
	if (!strcmp(t, "byte")) return "y";
	if (!strcmp(t, "boolean")) return "b";
	if (strlen(t) == 1 && strchr("sbynqiuxtd", t[0])) return t;
	return NULL;
}

static void fail(const char *msg)
{
	fprintf(stderr, "notify-send: %s\n", msg);
	exit(1);
}

/* "--name=value", "--name value", "-xvalue", "-x value" */
static const char *optval(int argc, char **argv, int *i, const char *s, const char *l)
{
	const char *a = argv[*i];
	size_t ll = strlen(l);
	if (!strncmp(a, l, ll) && a[ll] == '=')
		return a + ll + 1;
	if (!strcmp(a, l) || !strcmp(a, s)) {
		if (*i + 1 >= argc)
			usage(1);
		return argv[++*i];
	}
	if (s[0] && !strncmp(a, s, 2) && a[2])
		return a + 2;
	return NULL;
}

static int is_opt(const char *a, const char *s, const char *l)
{
	size_t ll = strlen(l);
	return !strcmp(a, s) || !strcmp(a, l) || (!strncmp(a, l, ll) && a[ll] == '=') ||
	       (s[0] && !strncmp(a, s, 2) && a[2] && a[1] != '-');
}

static char *call(wn_client *c, const char *line)
{
	char *r = wn_client_call(c, line);
	if (!r)
		fail("no reply from the notification daemon");
	if (!strncmp(r, "ERR", 3)) {
		char **f;
		size_t n = wn_split(r, &f);
		fprintf(stderr, "notify-send: %s\n", n > 1 ? f[1] : "error");
		exit(1);
	}
	return r;
}

int wmain(int wargc, wchar_t **wargv)
{
	SetConsoleOutputCP(CP_UTF8);
	int argc = wargc;
	char **argv = malloc(sizeof(char *) * (argc + 1));
	for (int i = 0; i < argc; i++)
		argv[i] = wn_utf8(wargv[i]);
	argv[argc] = NULL;

	wn_notify n = {.app_name = _strdup("notify-send"), .app_icon = _strdup(""),
		       .summary = NULL, .body = _strdup(""), .expire_timeout = -1};
	int print_id = 0, wait = 0, has_actions = 0;
	const char *mode = NULL, *close_id = NULL;
	const char *pos[2] = {0};
	int npos = 0;

	for (int i = 1; i < argc; i++) {
		const char *a = argv[i], *v;
		if (a[0] != '-' || !a[1]) {
			if (npos >= 2)
				fail("too many arguments (quote SUMMARY and BODY)");
			pos[npos++] = a;
		} else if (!strcmp(a, "--")) {
			while (++i < argc && npos < 2)
				pos[npos++] = argv[i];
		} else if (!strcmp(a, "-?") || !strcmp(a, "--help")) {
			usage(0);
		} else if (is_opt(a, "-u", "--urgency") && (v = optval(argc, argv, &i, "-u", "--urgency"))) {
			const char *u = !strcmp(v, "low") ? "0" : !strcmp(v, "normal") ? "1" : !strcmp(v, "critical") ? "2" : NULL;
			if (!u)
				fail("unknown urgency (low, normal, critical)");
			wn_add_hint(&n, "urgency", "y", u);
		} else if (is_opt(a, "-t", "--expire-time") && (v = optval(argc, argv, &i, "-t", "--expire-time"))) {
			n.expire_timeout = atoi(v);
		} else if (is_opt(a, "-a", "--app-name") && (v = optval(argc, argv, &i, "-a", "--app-name"))) {
			free(n.app_name);
			n.app_name = _strdup(v);
		} else if (is_opt(a, "-i", "--icon") && (v = optval(argc, argv, &i, "-i", "--icon"))) {
			free(n.app_icon);
			n.app_icon = _strdup(v);
		} else if (is_opt(a, "-c", "--category") && (v = optval(argc, argv, &i, "-c", "--category"))) {
			wn_add_hint(&n, "category", "s", v);
		} else if (is_opt(a, "-h", "--hint") && (v = optval(argc, argv, &i, "-h", "--hint"))) {
			char *dup = _strdup(v), *c1 = strchr(dup, ':'), *c2 = c1 ? strchr(c1 + 1, ':') : NULL;
			if (!c2)
				fail("hint must be TYPE:NAME:VALUE");
			*c1 = *c2 = 0;
			const char *t = hint_type(dup);
			if (!t)
				fail("hint TYPE must be int, double, string, byte or boolean");
			const char *val = c2 + 1;
			if (*t == 'b')
				val = (!_stricmp(val, "true") || !strcmp(val, "1")) ? "true" : "false";
			wn_add_hint(&n, c1 + 1, t, val);
			free(dup);
		} else if (is_opt(a, "-r", "--replace-id") && (v = optval(argc, argv, &i, "-r", "--replace-id"))) {
			n.replaces_id = (uint32_t)strtoul(v, NULL, 10);
		} else if (!strcmp(a, "-p") || !strcmp(a, "--print-id")) {
			print_id = 1;
		} else if (is_opt(a, "-A", "--action") && (v = optval(argc, argv, &i, "-A", "--action"))) {
			char key[256];
			const char *eq = strchr(v, '=');
			if (eq) {
				snprintf(key, sizeof key, "%.*s", (int)(eq - v), v);
				wn_add_action(&n, key, eq + 1);
			} else {
				snprintf(key, sizeof key, "%zu", n.n_actions / 2);
				wn_add_action(&n, key, v);
			}
			has_actions = 1;
		} else if (!strcmp(a, "-w") || !strcmp(a, "--wait")) {
			wait = 1;
		} else if (!strcmp(a, "-e") || !strcmp(a, "--transient")) {
			wn_add_hint(&n, "transient", "b", "true");
		} else if (!strcmp(a, "--server-info") || !strcmp(a, "--capabilities") || !strcmp(a, "--monitor")) {
			mode = a;
		} else if (!strncmp(a, "--close", 7) && (v = optval(argc, argv, &i, "", "--close"))) {
			mode = "--close";
			close_id = v;
		} else {
			fprintf(stderr, "notify-send: unknown option %s\n", a);
			usage(1);
		}
	}

	wn_client *c = wn_client_open(2000);
	if (!c)
		fail("no notification daemon is running (start tiramisu, dunst or notifyd)");

	if (mode && !strcmp(mode, "--server-info")) {
		char **f;
		char *r = call(c, "GetServerInformation");
		size_t nf = wn_split(r, &f);
		printf("name: %s\nvendor: %s\nversion: %s\nspec_version: %s\n", nf > 1 ? f[1] : "",
		       nf > 2 ? f[2] : "", nf > 3 ? f[3] : "", nf > 4 ? f[4] : "");
		return 0;
	}
	if (mode && !strcmp(mode, "--capabilities")) {
		char **f;
		char *r = call(c, "GetCapabilities");
		size_t nf = wn_split(r, &f);
		for (size_t i = 1; i < nf; i++)
			printf("%s\n", f[i]);
		return 0;
	}
	if (mode && !strcmp(mode, "--close")) {
		char line[64];
		snprintf(line, sizeof line, "CloseNotification\t%s", close_id);
		call(c, line);
		return 0;
	}
	if (mode && !strcmp(mode, "--monitor")) {
		call(c, "Subscribe");
		char *l;
		while ((l = wn_client_read_line(c))) {
			char **f;
			size_t nf = wn_split(l, &f);
			for (size_t i = 0; i < nf; i++)
				printf("%s%s", i ? "\t" : "", f[i]);
			printf("\n");
			fflush(stdout);
			free(f);
			free(l);
		}
		return 0;
	}

	if (npos < 1)
		fail("no summary specified");
	n.summary = _strdup(pos[0]);
	if (npos > 1) {
		free(n.body);
		n.body = _strdup(pos[1]);
	}

	/* Subscribe on a second connection *before* Notify, so a fast close
	 * cannot be missed (same race libnotify avoids by adding the match rule first). */
	wn_client *sub = NULL;
	if (wait || has_actions) {
		sub = wn_client_open(2000);
		if (!sub)
			fail("cannot open signal connection");
		free(call(sub, "Subscribe"));
	}

	char *line = wn_format_notify(&n);
	char *r = call(c, line);
	char **f;
	size_t nf = wn_split(r, &f);
	unsigned long id = nf > 1 ? strtoul(f[1], NULL, 10) : 0;
	if (print_id) {
		printf("%lu\n", id);
		fflush(stdout);
	}

	if (sub) {
		char *l;
		while ((l = wn_client_read_line(sub))) {
			char **g;
			size_t ng = wn_split(l, &g);
			if (ng >= 3 && strtoul(g[1], NULL, 10) == id) {
				if (!strcmp(g[0], "ActionInvoked") && has_actions) {
					printf("%s\n", g[2]);
					fflush(stdout);
				} else if (!strcmp(g[0], "NotificationClosed")) {
					break;
				}
			}
			free(g);
			free(l);
		}
		wn_client_close(sub);
	}
	wn_client_close(c);
	return 0;
}
