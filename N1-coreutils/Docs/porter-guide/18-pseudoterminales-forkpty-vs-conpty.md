# Módulo 18: Pseudoterminales — `forkpty` vs ConPTY

Los módulos 01–13 cubren utilidades que leen bytes y escriben bytes. Un
multiplexor de terminal (dtach, abduco, dvtm, screen, tmux) necesita algo más:
**hacerle creer a otro programa que está en una terminal real** y quedarse con
ambos extremos. En Unix eso es una pseudoterminal (pty). En Windows 10 1809+
es **ConPTY**. Este módulo explica por qué no son equivalentes y cómo se
cierra la brecha. El código de referencia es `N5-multiplexers/compat/src/pty.c`.

---

## 1. El contrato Unix

```c
int master;
pid_t pid = forkpty(&master, NULL, &term, &ws);
if (pid == 0) {              /* hijo: stdin/stdout/stderr = esclavo */
    execvp(argv[0], argv);
    _exit(127);
}
/* padre: UN fd */
read(master, buf, n);        /* lo que el hijo escribió */
write(master, "ls\r", 3);    /* lo que el hijo va a leer */
ioctl(master, TIOCSWINSZ, &ws);  /* redimensionar -> SIGWINCH al hijo */
```

Cuatro propiedades que el código upstream da por hechas:

1. **Un solo descriptor**, bidireccional y apto para `select()`.
2. **La rama hijo** corre código del padre entre `fork` y `exec` (`setsid`,
   `dup2`, `chdir`, `setenv`, cerrar fds...).
3. **EOF/EIO** en el master cuando el hijo y todos sus descendientes cierran el esclavo.
4. **termios** del esclavo legible desde el master (`tcgetattr(master)`).

## 2. El contrato ConPTY

```c
CreatePipe(&in_r,  &in_w,  NULL, 0);    /* nosotros escribimos en in_w  */
CreatePipe(&out_r, &out_w, NULL, 0);    /* nosotros leemos de out_r     */
CreatePseudoConsole(size, in_r, out_w, 0, &hpc);
CloseHandle(in_r); CloseHandle(out_w);  /* ¡obligatorio! ver §5 */

STARTUPINFOEXW si = { .StartupInfo.cb = sizeof si };
/* ... InitializeProcThreadAttributeList ... */
UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                          hpc, sizeof hpc, NULL, NULL);
CreateProcessW(app, cmdline, NULL, NULL, FALSE,
               EXTENDED_STARTUPINFO_PRESENT, NULL, NULL, &si.StartupInfo, &pi);
```

| Propiedad Unix | ConPTY | Cómo se compensa |
| :--- | :--- | :--- |
| 1 fd bidireccional y seleccionable | **2 pipes anónimos, no seleccionables** | *pump threads* + socketpair (§3) |
| Rama hijo entre fork/exec | no existe | se traduce a parámetros de `CreateProcess` (cwd, bloque de entorno, handles) |
| EOF cuando el hijo sale | **nunca**: conhost sigue vivo | hilo vigía + `ClosePseudoConsole()` (§5) |
| `tcgetattr(master)` | no hay API | se informa "cocido" (§7) |
| Lo que el hijo escribe llega tal cual | **conhost re-renderiza** todo a su dialecto VT | `TERM=xterm-256color`; repintado gratis (§6) |

La última fila es la más sutil: en Unix, la pty es un *cable*. En Windows, entre
el hijo y vos hay **un emulador de terminal completo** (conhost en modo
headless) que mantiene su propia pantalla y te manda *su* versión de ella.

## 3. Devolverle a `select()` un descriptor: el patrón pump

Mismo patrón que el módulo 11 usó para `ssh.exe`:

```text
out_r  --ReadFile-->  [hilo out]  --send-->  sock[1]
sock[1] --recv-->     [hilo in]   --WriteFile--> in_w
sock[0]  = "master" que recibe el programa (fd virtual, módulo 19)
```

- El socketpair es TCP loopback (`127.0.0.1:0`, nunca `0.0.0.0`: firewall) con
  `TCP_NODELAY`: una tecla por paquete, sin los 40 ms de Nagle.
- El hilo *out* hace `shutdown(SD_SEND)` al ver EOF del pipe: así el programa
  lee `0` en el master.
- Cada hilo es dueño de su handle y lo cierra; un contador de referencias
  libera la estructura cuando el último (vfd, pump in, pump out, vigía) suelta.

## 4. La rama hijo, traducida

dvtm hace esto entre `fork` y `exec`:

```c
setsid(); sigprocmask(SIG_SETMASK, &empty, NULL);
dup2(vt2ed[0], STDIN_FILENO);            /* opcional: stdin desde un pipe  */
for (fd = 3; fd < maxfd; fd++) close(fd);
setenv("TERM", vt_term, 1);
chdir(cwd);
execvp(p, argv);
```

En Windows cada línea se convierte en un **parámetro** de la creación:

| Rama hijo | `compat_spawn_pty()` |
| :--- | :--- |
| `setenv(K, V)` | bloque `CREATE_UNICODE_ENVIRONMENT` = entorno actual + overrides |
| `chdir(cwd)` | `lpCurrentDirectory` |
| `dup2(pipe, 0)` | `STARTF_USESTDHANDLES` + ese handle en `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` |
| `close(3..max)` | `bInheritHandles=FALSE`, o `HANDLE_LIST` explícita |
| `execvp` (busca en `PATH`) | `SearchPathW` × `PATHEXT` **antes** de `CreateProcessW` |
| `setsid`/`sigprocmask` | no aplica |

`execvp` resuelve `PATH`. `CreateProcessW(NULL, cmdline)` también busca, pero
parseando la *línea de comandos* (`C:\Program Files\x.exe` prueba primero
`C:\Program.exe`) y sólo agrega `.exe`. Resolvé vos: `vim` → `vim.bat`,
`powershell` → `powershell.exe`.

### Trampa medida: el hijo hereda el stdio del padre

```text
> dtach -N s cmd /c echo hola > out.txt
> type out.txt
hola                         <- ¡la salida NO pasó por la pseudoconsola!
```

Si el padre tiene stdio redirigido (archivo, pipe, `NUL` de un demonio) y no
usás `STARTF_USESTDHANDLES`, un hijo de consola hereda esos *valores* de handle
y escribe ahí, salteándose ConPTY. Solución: **siempre** `STARTF_USESTDHANDLES`;
los handles que dejes en `NULL` los reemplaza la pseudoconsola.

## 5. Ciclo de vida: el EOF que nunca llega

En Unix, cuando el hijo muere el master devuelve EIO. Con ConPTY, conhost no
muere con el hijo: **el pipe de salida sigue abierto** hasta `ClosePseudoConsole()`.
Y si cerrás demasiado pronto, perdés el último cuadro que conhost no alcanzó a
renderizar.

Secuencia usada:

1. Un hilo vigía hace `WaitForSingleObject(proceso)`.
2. Registra el código de salida y **postea `SIGCHLD`** (módulo 19).
3. Espera 150 ms de silencio en el pipe de salida (tope 2 s).
4. `ClosePseudoConsole()` → conhost sale → `ReadFile` falla con
   `ERROR_BROKEN_PIPE` → el pump hace `shutdown` → `read(master) == 0`.

Dos reglas que no son opcionales:

- **Cerrá tus copias de `in_r` y `out_w`** después de `CreatePseudoConsole`.
  Si no, aunque conhost salga, el pipe sigue teniendo un escritor (vos) y nunca
  ves EOF.
- **Nunca llames `ClosePseudoConsole` desde el hilo que debería estar leyendo
  la salida.** En builds de Windows 10 bloquea hasta que conhost vacía su
  salida; si nadie lee, deadlock. Por eso se llama desde el vigía o desde un
  hilo dedicado cuando el programa cierra el master.

Cerrar el master (`close(master)`) en Unix cuelga la sesión (SIGHUP). Acá se
traduce a `ClosePseudoConsole()`, que manda `CTRL_CLOSE_EVENT` a todos los
procesos de esa consola.

Y para el código upstream que espera EIO: `vt_process()` de dvtm hace
`if (read(...) < 0) return -1` y espera `errno == EIO`. Nuestro master es un
socket que devuelve `0`. El parche es una línea: `read()==0` ⇒ `errno = EIO`.

## 6. `TIOCSWINSZ`, `SIGWINCH` y el repintado

```c
ioctl(master, TIOCSWINSZ, &ws);   ->  ResizePseudoConsole(hpc, {cols, rows})
kill(-pid, SIGWINCH);             ->  ¿?
```

Un programa de consola Windows no recibe `SIGWINCH`: recibe un
`WINDOW_BUFFER_SIZE_EVENT` en su cola de entrada, y `ResizePseudoConsole` ya lo
encola. Entonces, ¿qué hacer con el `kill(SIGWINCH)` explícito?

Mirá *para qué* lo usa upstream: dtach y abduco lo mandan **al re-adjuntarse**,
para que el programa se redibuje sobre la pantalla limpia del cliente nuevo.
ConPTY tiene la pantalla completa en conhost y **la re-emite entera cuando
cambia el tamaño**. Implementación:

- si hubo un `TIOCSWINSZ` real desde el último `SIGWINCH`, no hacer nada (ya repintó);
- si no, *menear* el alto: `rows+1` y de vuelta `rows`.

Resultado: dtach/abduco en Windows **repintan al re-adjuntarse aunque el
programa no sepa redibujarse**, cosa que en Unix no pasa. Por la misma razón el
método de redibujado por defecto de dtach pasa de `ctrl_l` a `winch`: con
`ctrl_l` se tipearía un `^L` literal dentro de `cmd.exe`.

## 7. `termios` del esclavo y `TERM`

- `tcgetattr(master)`: ConPTY no expone la disciplina de línea del esclavo. Se
  devuelve un termios *cocido* (`ICANON|ECHO|ISIG`). Es la respuesta honesta
  ("no sé") y evita que la heurística de dtach mande `^L`.
- `TERM` para los hijos: el hijo no habla con tu emulador, habla con conhost.
  dvtm exporta `TERM=dvtm-256color` en Unix porque *él* interpreta la salida;
  en Windows quien interpreta la salida del hijo es conhost, y lo que dvtm
  recibe ya es el dialecto de conhost. Exportá `xterm-256color`: los programas
  con terminfo (vim/less de Git for Windows o MSYS) no conocerían
  `dvtm-256color` de todos modos.

## 8. Checklist de ConPTY

- [ ] ¿`CreatePseudoConsole`/`Resize`/`Close` se resuelven con `GetProcAddress`
      para dar un error claro en Windows < 1809?
- [ ] ¿Cerraste `in_r` y `out_w` después de crear la pseudoconsola?
- [ ] ¿`STARTF_USESTDHANDLES` siempre, y `HANDLE_LIST` para lo heredado?
- [ ] ¿El ejecutable se resuelve con `PATH` × `PATHEXT` antes de `CreateProcessW`?
- [ ] ¿`ClosePseudoConsole` corre fuera del hilo lector, después de vaciar la salida?
- [ ] ¿El programa ve `read()==0`/`EIO` cuando el hijo termina?
- [ ] ¿`SIGWINCH` hacia el hijo significa "repintá", no "matá"?
- [ ] ¿`TERM` describe a conhost, no a tu emulador?
