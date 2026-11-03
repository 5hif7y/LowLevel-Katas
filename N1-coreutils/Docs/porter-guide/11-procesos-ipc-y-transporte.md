# Módulo 11: Procesos, IPC y Transporte

Los módulos 01–10 alcanzan para utilidades de un solo proceso. Herramientas
como `rsync`, `ssh`, `tar`, `gpg`, `git` o `curl` necesitan además **procesos
hijos, pipes, sockets y espera de procesos**. Este módulo cubre esa capa.

---

## 1. `fork`/`exec` vs `CreateProcess`

En Unix:

```c
pid = fork();
if (pid == 0) {
    dup2(pipe_in[0], STDIN_FILENO);
    dup2(pipe_out[1], STDOUT_FILENO);
    execvp(cmd[0], cmd);
}
```

En Windows no hay `fork()`. `CreateProcessW` crea el proceso y le pasa los
handles estándar de una vez:

```c
STARTUPINFOW si = { .cb = sizeof si };
PROCESS_INFORMATION pi = { 0 };

si.dwFlags = STARTF_USESTDHANDLES;
si.hStdInput  = child_stdin_read;
si.hStdOutput = child_stdout_write;
si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);

CreateProcessW(NULL, command_line, NULL, NULL,
               TRUE,              /* bInheritHandles */
               0, NULL, NULL, &si, &pi);
```

No hay `exec` que reemplace la imagen: si querés “re-ejecutar el mismo
programa en otro modo”, lanzás otra instancia con argumentos distintos.

---

## 2. Pipes y herencia de handles

Para conectar al hijo por stdin/stdout:

```c
HANDLE child_in_read, child_in_write;
HANDLE child_out_read, child_out_write;
SECURITY_ATTRIBUTES sa = { .nLength = sizeof sa, .bInheritHandle = TRUE };

CreatePipe(&child_in_read,  &child_in_write,  &sa, 0);
CreatePipe(&child_out_read, &child_out_write, &sa, 0);

/* Sólo los extremos del hijo deben heredarse. */
SetHandleInformation(child_in_write,  HANDLE_FLAG_INHERIT, 0);
SetHandleInformation(child_out_read,  HANDLE_FLAG_INHERIT, 0);
```

Después de `CreateProcessW`, el padre cierra `child_in_read` y
`child_out_write`. Si no los cierra, el hijo nunca ve EOF cuando el padre
termina.

Reglas:

- Marcá como heredable **sólo** lo que el hijo necesita.
- Cerrá en el padre los extremos del hijo después de crear el proceso.
- Cerrá en el hijo (o no los heredes) los extremos del padre.
- No uses `bInheritHandles=TRUE` con handles abiertos que no querés heredar.
- Para Unicode, usá `CreateProcessW` y construí la línea de comandos en
  `wchar_t`.

---

## 3. El patrón “re-ejecutar el mismo binario”

Para una copia local, `rsync` usa `fork()` y llama `child_main()` en memoria.
En Windows se puede hacer lo mismo **sin fork** lanzando el mismo `.exe` en
modo `--server`:

```c
pid_t local_child(int argc, char **argv, int *f_in, int *f_out,
                  int (*child_main)(int, char *[]))
{
    char *args[MAX_ARGS];
    int ac = 0, i;

    args[ac++] = "rsync.exe";
    args[ac++] = "--server";
    server_options(args, &ac);   /* opciones ya parseadas en el padre */
    args[ac++] = ".";
    for (i = 0; i < argc; i++)
        args[ac++] = argv[i];

    return piped_child(args, f_in, f_out);
}
```

El hijo re-parsea las opciones y arranca como server. No necesita memoria
compartida porque el file-list todavía no existe: se construye en el hijo.

### Cuándo **no** alcanza

`do_recv()` de rsync hace `fork()` **después** de recibir el file-list. El hijo
ya no puede re-parsear nada: necesita la lista, los filtros y el estado de I/O
que están en memoria. Opciones:

1. **Thread split con contexto explícito**: sacar `am_generator`,
   `am_receiver`, stats, buffers y file-list a una estructura por hilo. Es el
   camino correcto, pero es un refactor grande.
2. **Helper process + file-list serializado**: el proceso principal manda el
   file-list por un pipe al helper usando el mismo formato de wire. Más
   acotado, pero hay que serializar/restaurar el estado.
3. **`RtlCloneUserProcess`**: no documentado, frágil, no apto para producción.

---

## 4. `socketpair` y pump threads

`fd_pair()` de rsync espera un par de descriptores full-duplex y
seleccionables. Windows no tiene `socketpair(AF_UNIX)`, pero se emula con TCP
loopback (ver módulo 07).

Cuando el hijo es `ssh.exe`, sus stdio son **pipes**, no sockets, y `poll()`
no sirve sobre pipes. La solución es un `socketpair` para el padre y dos
threads que bombean:

```text
ssh stdout (pipe) --read--> socketpair[1]
socketpair[0] --recv--> ssh stdin (pipe)
```

El padre sólo ve `socketpair[0]` como `f_in` y el otro extremo como `f_out`.
Así `poll`, `WSAPoll`, `shutdown` y el multiplexado de rsync funcionan sin
cambios.

Cuidados:

- Los threads deben ser dueños de los handles que bombean; no los cierres dos
  veces.
- Al terminar el hijo, el pump de stdout debe hacer `shutdown(SD_SEND)` y
  cerrar su socket para que el padre vea EOF.
- Si el padre cierra `f_out`, el pump de stdin debe salir y cerrar el stdin del
  hijo.
- Un `CreateThread` que falla después de otro exitoso no debe liberar el
  contexto que el primer thread todavía usa.

---

## 5. `waitpid`, códigos de salida y señales

`waitpid()` se emula con:

```c
WaitForSingleObject(process, timeout);
GetExitCodeProcess(process, &code);
status = (int)((code & 0xff) << 8);  /* WIFEXITED(status) == true */
```

`WIFEXITED`, `WEXITSTATUS`, `WIFSIGNALED` y `WTERMSIG` se definen en
`compat/include/sys/wait.h` para que el código upstream siga compilando.

Windows no tiene `SIGCHLD`, `SIGUSR1`/`SIGUSR2` ni `SIGPIPE` con semántica
Unix. En una herramienta de red:

- `SIGCHLD` se reemplaza por la espera explícita del handle.
- `SIGUSR1`/`SIGUSR2` no existen; los handlers se vuelven no-ops o se
  reemplazan por eventos propios.
- `SIGPIPE` se emula mirando el error de escritura (ver módulo 10).

---

## 6. Transporte SSH

El patrón que funcionó para rsync:

1. `do_cmd()` arma `argv` con `ssh`, host, usuario, `--server`, opciones y
   paths remotos.
2. `piped_child()` cita cada argumento para `CreateProcessW`.
3. Se crean pipes para stdin/stdout y `socketpair` para el padre.
4. `CreateProcessW` lanza `ssh.exe`.
5. Los pump threads conectan pipes y sockets.
6. `client_run()` habla el protocolo rsync sobre los sockets.

Puntos críticos:

- **Quoting**: `CreateProcessW` recibe una línea; hay que citar cada `argv` con
  las reglas de `CommandLineToArgvW`. Un path remoto con espacios o quotes
  rompe todo si se concatena a mano.
- **stderr**: se hereda del padre; los prompts de password/host key de
  `ssh.exe` van a la consola, no al pipe de protocolo.
- **EOF**: cuando el hijo cierra stdout, el pump debe cerrar su extremo para
  que el padre vea EOF.
- **Código de salida**: hay que esperar al proceso y propagar el código.

---

## 7. Diagnóstico

Cuando un transporte no arranca, no adivines:

- Logueá la línea de comandos completa que recibe `CreateProcessW`.
- Logueá el primer byte que entra y sale de cada pump (`fd`, `n`, hex).
- Verificá que `WSAPoll` reciba eventos válidos; un `-1` con `WSAEINVAL`
  suele ser `POLLPRI` o un fd que no es socket.
- Usá `cdb.exe` con build `RelWithDebInfo` para ver el stack si el proceso
  cuelga. En un caso real, un `_stricmp` recursivo por un macro
  (`#define _stricmp win32_strcasecmp`) colgó el parseo de argumentos; el
  stack lo mostró en segundos.
- `Process Monitor` sirve para ver handles/archivos, pero no reemplaza el
  logging de la línea de comandos y de los bytes.

---

## 8. Checklist de procesos/IPC

- [ ] ¿El hijo hereda sólo los handles necesarios?
- [ ] ¿El padre cierra los extremos del hijo?
- [ ] ¿El hijo cierra los extremos del padre?
- [ ] ¿Los sockets del padre son no bloqueantes y los de los pumps bloqueantes?
- [ ] ¿`poll`/`WSAPoll` traduce eventos POSIX ↔ Winsock?
- [ ] ¿Hay timeout y EOF en las esperas?
- [ ] ¿Se espera el proceso y se propaga el código de salida?
- [ ] ¿Los threads tienen un dueño claro de cada handle?
- [ ] ¿El test corre contra un peer real, no sólo contra un mock?
