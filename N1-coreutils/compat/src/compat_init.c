#include <compat.h>
#include <windows.h>

void
compat_enable_vt100(void)
{
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE && hOut != NULL) {
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode)) {
            dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, dwMode);
        }
    }

    HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
    if (hErr != INVALID_HANDLE_VALUE && hErr != NULL) {
        DWORD dwMode = 0;
        if (GetConsoleMode(hErr, &dwMode)) {
            dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hErr, dwMode);
        }
    }
}

/* Run VT100 and binary-mode initialization automatically before main() */
static void __cdecl
_compat_auto_init(void)
{
    compat_enable_vt100();
    compat_init_binary_mode();
}

typedef void (__cdecl *_compat_initfn)(void);
#pragma section(".CRT$XCU", read)
__declspec(allocate(".CRT$XCU")) static _compat_initfn _p_compat_auto_init = _compat_auto_init;
