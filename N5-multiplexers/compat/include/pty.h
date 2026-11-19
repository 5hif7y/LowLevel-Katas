/* pty.h - there is no forkpty() on Windows: fork() cannot be emulated.
 * Call sites are patched to use compat_spawn_pty() (ConPTY). */
#ifndef COMPAT_PTY_H
#define COMPAT_PTY_H
#include "compat.h"
#include <termios.h>
#endif
