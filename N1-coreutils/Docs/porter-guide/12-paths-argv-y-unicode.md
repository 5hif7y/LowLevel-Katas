# Módulo 12: Paths, `argv` y Unicode

Este módulo cubre un problema que no aparece en coreutils: herramientas que
interpretan `host:path` (rsync, scp, git, curl). En esas herramientas, un path
de Windows puede confundirse con un host remoto.

---

## 1. El problema: `C:\dir` no es un host

Para `rsync`, un argumento `host:path` significa “del otro lado del ssh”. Si el
usuario escribe:

```text
C:\Users\me\site
```

el parser ve `C:` y cree que `C` es el host y `\Users\me\site` el path remoto.
Por eso hay que normalizar **antes** de cualquier `check_for_hostspec()`.

---

## 2. Namespace virtual

La solución más robusta es traducir los paths locales a un namespace POSIX
interno:

```text
C:\Users\me\site      ->  /c/Users/me/site
C:/Users/me/site/     ->  /c/Users/me/site/
\\server\share\site   ->  /unc/server/share/site
~\site                ->  /c/Users/me/site
site\assets           ->  site/assets
```

Reglas:

1. Detectar **primero** un path Windows:
   - letra de unidad + `:` (`X:` / `X:/` / `X:\`);
   - UNC (`\\server\share`);
   - path absoluto POSIX (`/...`).
2. Si no es Windows ni remoto, es relativo: sólo cambiar `\` por `/`.
3. Expandir `~` y `~/`/`~\` a `%USERPROFILE%`.
4. No tocar argumentos remotos (`host:path`, `rsync://...`, `:path`).
5. Mantener un único separador interno (`/`).

Ejemplo de normalización de argumentos:

```c
void win32_normalize_argv_paths(int argc, char **argv)
{
    for (int i = 0; i < argc; i++) {
        if (win32_is_remote_arg(argv[i]))
            continue;
        char buf[PATH_MAX * 4];
        if (win32_path_to_unix(argv[i], buf, sizeof buf) == 0)
            argv[i] = _strdup(buf);
    }
}
```

`win32_is_remote_arg()` tiene que distinguir:

- `C:\dir`, `C:/dir`, `\\srv\share` → **local**;
- `rsync://host/module` → **remoto**;
- `host:path`, `user@host:path`, `:path` → **remoto**;
- cualquier otra cosa → **local**.

---

## 3. Traducción inversa

El resto de rsync sigue usando `open`, `stat`, `opendir`, `chdir`, `getcwd`.
Todos esos wrappers traducen `/c/...` de vuelta a Win32:

```c
int win32_path_to_win(const char *in, char *out, size_t outsz)
{
    if (in[0] == '/' && isalpha((unsigned char)in[1]) &&
        (in[2] == '/' || in[2] == '\0')) {
        snprintf(out, outsz, "%c:\\%s", in[1], in + 3);
        /* cambiar '/' por '\' */
        return 0;
    }
    if (!strncmp(in, "/unc/", 5)) {
        snprintf(out, outsz, "\\\\%s", in + 5);
        /* cambiar '/' por '\' */
        return 0;
    }
    if (in[0] == '/') {
        snprintf(out, outsz, "%c:\\%s", current_drive_letter(), in + 1);
        return 0;
    }
    /* relativo: sólo cambiar separadores */
    return 0;
}
```

`getcwd()` debe devolver el path virtual (`/c/...`) y `chdir()` debe traducir
antes de llamar a `_chdir`.

---

## 4. Quoting de `CreateProcessW`

`CreateProcessW` recibe **una sola línea de comandos**. Cada argumento hay que
citarlo con las reglas de `CommandLineToArgvW`:

- si el argumento está vacío o tiene espacios/tabs/comillas, va entre `"`;
- las barras invertidas antes de una comilla se duplican;
- las barras invertidas al final del argumento se duplican antes de la comilla
  de cierre.

Pseudocódigo:

```c
void quote_arg(const char *arg, string *out)
{
    int quote = (*arg == '\0') || strpbrk(arg, " \t\"") != NULL;
    if (quote) append(out, '"');
    for (const char *p = arg; *p; ) {
        size_t n = 0;
        while (*p == '\\') { n++; p++; }
        if (*p == '"') {
            append_n(out, '\\', n * 2 + 1);
            append(out, '"');
        } else if (*p == '\0') {
            append_n(out, '\\', n * 2);
            break;
        } else {
            append_n(out, '\\', n);
            append(out, *p++);
        }
    }
    if (quote) append(out, '"');
}
```

### El caso `cmd.exe` y `\"`

En un batch de `cmd.exe`, una barra invertida antes de la comilla de cierre se
come la comilla:

```bat
rsync.exe -av "C:\Users\me\site\" user@vps:/dest/
```

El parser de Windows puede interpretar `\"` como una comilla literal y unir
argumentos. Solución: usar forward slash (`"C:/Users/me/site/"`) o no dejar
`\` antes de la comilla. Documentalo en el README de la herramienta.

---

## 5. `CP_ACP` vs `CP_UTF8`

Hay tres capas distintas:

1. **`argv` de MSVC**: llega en la code page ANSI (`CP_ACP`), no en UTF-8.
2. **Sistema de archivos**: las APIs `_open`/`_stat64`/`opendir` también son
   ANSI; las `_w...` son UTF-16.
3. **Protocolo remoto**: lo que espera el servidor (normalmente UTF-8 en
   Linux).

No mezcles:

- Si el origen es `argv`/consola, usá `CP_ACP` para pasar a wide.
- Si el origen es un archivo de configuración en UTF-8, usá `CP_UTF8`.
- Si el destino es el protocolo remoto, enviá UTF-8.
- Si el destino es el filesystem, usá `_w...` con UTF-16.

El port mínimo puede usar ANSI y funcionar con ASCII/Latin-1. Para Unicode
serio hay que pasar a wide en toda la cadena de archivos.

---

## 6. Long paths y nombres reservados

- `MAX_PATH` = 260 por defecto. Para más: manifest `longPathAware` y prefijo
  `\\?\` (`\\?\C:\...`) o `\\?\UNC\server\share\...`.
- Nombres reservados: `CON`, `PRN`, `AUX`, `NUL`, `COM1`..`COM9`,
  `LPT1`..`LPT9`.
- Caracteres prohibidos: `\ / : * ? " < > |`.
- No se permite terminar en punto o espacio.
- NTFS es case-insensitive: `Makefile` y `makefile` colisionan.
- ADS (`archivo:stream`) no existe en POSIX.

Un sincronizador no puede ignorar estas reglas: un `rsync` que “funciona” con
paths ASCII puede fallar con un nombre reservado o un path de 300 caracteres.

---

## 7. Tests de paths

Mínimo:

- `C:\dir`, `C:/dir/`, `\\srv\share`, `~\dir`, `~/dir/`, `dir\sub`;
- path con espacios;
- path con acentos/emoji (si se soporta Unicode);
- path > 260 caracteres;
- nombre reservado (`CON`, `NUL`);
- trailing slash vs trailing backslash;
- argumento remoto `user@host:/path` que **no** debe traducirse;
- argumento `:path` (host vacío) que **no** debe traducirse.
