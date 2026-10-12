# Módulo 10: Estudio de Casos de Depuración — Tres Bugs que la Suite Descubrió

Este módulo es distinto de los nueve anteriores. Los otros describen cómo *portear*; éste documenta
tres bugs que **ya estaban** en el porteo, que nadie había visto, y el procedimiento de medición que
llevó hasta la causa de cada uno.

Ninguno de los tres rompía la compilación. Los 21 binarios se construían sin un solo error y el viejo
`test_suite.bat` imprimía `ALL TESTS COMPLETED SUCCESSFULLY!` con los tres presentes. Aparecieron
recién cuando [`tests/run-suite.sh`](../../tests/run-suite.sh) empezó a **comparar** la salida real
contra la esperada en vez de imprimirla para que uno la mirara.

> **La lección transversal:** una suite que sólo imprime no es una suite. La diferencia entre
> `echo "OK"` y `[ "$real" = "$esperado" ]` es la diferencia entre no enterarse de nada y encontrar
> tres bugs en una tarde.

---

## Caso 1 — `yes(1)` nunca termina: Windows no tiene `SIGPIPE`

### Síntoma

```console
$ yes.exe | head -n 3
y
y
y
```

Tres líneas correctas… y **`yes.exe` sigue vivo para siempre**, girando a CPU completa. Con el tiempo
se acumulan procesos huérfanos hasta que algo se cae.

### Por qué el código upstream está bien

`usr.bin/yes/yes.c` es, entero, esto:

```c
	if (argc > 1)
		for (;;)
			puts(argv[1]);
	else
		for (;;)
			puts("y");
```

El valor de retorno de `puts()` se descarta. En Unix eso es correcto: cuando el consumidor cierra el
pipe, el kernel entrega **`SIGPIPE`**, cuya acción por defecto es terminar el proceso. El programa
nunca llega a evaluar un error porque ya está muerto.

### Medición

¿Realmente Windows no puede detectarlo, o el CRT simplemente no lo reporta? Hay que medirlo.

[`tests/probes/probe_broken_pipe.c`](../../tests/probes/probe_broken_pipe.c) chequea el valor de
retorno de `puts()` y se detiene en el primer fallo:

```console
$ probe.exe | head -n 3
[probe] puts() devolvio EOF tras 24577 lineas; errno=22 (Invalid argument)
```

Dos datos:

1. **El CRT sí detecta el pipe roto**: `puts()` devuelve `EOF`.
2. `24577 = 12 × 4096`. No falló en la línea 1 sino al desbordarse el buffer: sobre un pipe, stdout
   queda **block-buffered**, no line-buffered. El primer error aparece recién ahí.

O sea: el problema no es que Windows no se entere, es que **nadie mira**.

### Dos callejones sin salida

**`errno` no sirve.** El CRT reporta `EINVAL` (22), no `EPIPE`. Demasiado ambiguo para decidir sobre él.

**`PeekNamedPipe` tampoco.** La idea era preguntarle al pipe si seguía vivo. Pero `stdout` es un
handle **write-only** y `PeekNamedPipe` exige acceso de *lectura*:
[`probe_peek_healthy.c`](../../tests/probes/probe_peek_healthy.c) lo corre sobre un pipe **sano** y
falla igual.

```console
$ probe_peek_healthy.exe | cat
[peek] GetFileType     = 2 (FILE_TYPE_CHAR)
[peek] PeekNamedPipe   = FALSE  GetLastError=5      <- ERROR_ACCESS_DENIED
[peek] veredicto      = PeekNamedPipe NO SIRVE (falla aunque el pipe este sano)
```

`ERROR_ACCESS_DENIED`, no `ERROR_BROKEN_PIPE`. Como discriminante no distingue nada. Vale la pena
subrayarlo: la primera versión del fix usaba `PeekNamedPipe` y **nunca habría disparado** — habría
parecido correcta y no habría servido.

### El criterio que sí funciona

No hay que preguntar nada. Cuando el chequeo corre, **la escritura ya falló**. Un fallo de escritura
sobre un pipe tiene sólo dos causas y ninguna es transitoria:

- el extremo de lectura se cerró, o
- el pipe está lleno en modo no bloqueante (`EAGAIN`), que sí es recuperable y por eso se excluye.

Alcanza con clasificar el handle (`GetFileType` no necesita permiso de lectura) y descartar `EAGAIN`.
Ver [`compat/src/sigpipe.c`](../../compat/src/sigpipe.c).

### El fix

La capa `compat` envuelve las funciones de salida de stdio (`puts`, `fputs`, `fputc`, `putchar`,
`fwrite`, `printf`, `fprintf`, `fflush`). Si la escritura falla **y** el destino es un pipe, reproduce
la acción por defecto de `SIGPIPE`: `_exit(141)` — 141 = 128 + 13, el mismo código que reporta un shell
Unix. También se emula `signal(SIGPIPE, ...)` para que un programa que quiera ignorarlo pueda hacerlo.

---

## Caso 2 — `ls(1)` trunca los nombres a 8 caracteres: memoria solapada en FTS

### Síntoma

```console
$ ls.exe /tmp/lsx | sed -n l
a.txt$
ab.txt$
abc.txt$
abcd.txt\002$
abcde.tx\002$
abcdef.t\002$
abcdefg.\002$
abcdefgh\002$
```

Nombres de hasta 7 caracteres salen bien; de 8 en adelante salen **cortados a exactamente 8** y con un
byte `\002` pegado.

### Medición

El patrón (`min(len, 8)` + un byte de basura) no sale de la impresión. Se midió el contenido real de
`fts_name` tal como lo ve un consumidor:
[`tests/probes/probe_fts_name.c`](../../tests/probes/probe_fts_name.c).

```console
--- layout ---
sizeof(FTSENT)            = 120
offsetof(fts_name)        = 112
offsetof(fts_statp)       = 104
sizeof(struct stat)       = 96
=> fts_statp arranca en   = 120  (fts_name + 8)

--- entradas ---
lvl=1  namelen=8   name="abcd.txt"  bytes: 61 62 63 64 2e 74 78 74 02
lvl=1  namelen=11  name="abcdefg."  bytes: 61 62 63 64 65 66 67 2e 02 00 00
```

Los bytes identifican al culpable: en `fts_name[8]` hay `02`, y más allá `b6 81`, que es `st_mode`
(`S_IFREG | 0666`). **Es el `struct stat` escribiéndose encima del nombre.**

### Causa

`char fts_name[1]` es el **último** miembro de `FTSENT`, así que el nombre arranca en
`offsetof(fts_name) == sizeof(FTSENT) - 1` y sigue dentro del bloque. En `alloc_ftsent()` el reparto
era:

```c
e->fts_statp = (struct stat *)((char *)e + sizeof(FTSENT));   /* = fts_name + 8 */
e->fts_path  = (char *)e->fts_statp + sizeof(struct stat);
```

`fts_statp` quedaba en `sizeof(FTSENT)`, o sea **`fts_name[8]`**. Y como `stat()` corre *después* del
`memcpy` del nombre, sus 96 bytes pisaban el nombre desde el noveno byte. Los nombres de 8 caracteres
perdían el `NUL` terminador; los más largos quedaban cortados.

### El fix

El nombre tiene que quedarse donde está — es parte del struct. Las extensiones van **después**, en
orden, y el `struct stat` alineado:

```c
statoff = sizeof(FTSENT) + nlen;                  /* primer byte libre tras el nombre */
statoff = (statoff + _Alignof(struct stat) - 1) & ~(_Alignof(struct stat) - 1);
pathoff = statoff + sizeof(struct stat);

e = (FTSENT *)calloc(1, pathoff + plen + 1);
e->fts_statp = (struct stat *)((char *)e + statoff);
e->fts_path  = (char *)e + pathoff;
```

La alineación no es decorativa: `struct stat` contiene `__int64` y exige 8 bytes, mientras que
`sizeof(FTSENT) + nlen` no tiene por qué ser múltiplo de 8.

---

## Caso 3 — El CRT traduce `\n` y el inicializador nunca corre

Este caso son en realidad **dos bugs apilados**, y el segundo ocultaba al primero.

### Síntoma

```console
$ head.exe -n 2 three.txt | od -c
0000000   u   n   o  \r  \n   d   o   s  \r  \n
```

`\r\n` donde debería haber `\n`. En una terminal eso **se ve bien**, porque el `\r` es invisible. Lo
que lo delata son las comparaciones de la suite: "esperado" y "real" se imprimían idénticos y aun así
fallaban. Y lo grave:

```console
$ cat.exe crlf.txt | wc -c      # crlf.txt tiene 6 bytes
4                                # cat se comió los \r
$ printf 'bin\000ary\n' | tee.exe out.bin   # 8 bytes de entrada
$ wc -c < out.bin
9                                # tee escribió uno de más
```

**`cat` y `tee` corrompen datos binarios.** Un `cat imagen.png > copia.png` no reproduce el original.

### Causa inmediata: `_setmode` nunca se implementó

El [módulo 03](03-el-sistema-io-y-modos-binario-texto.md) describe `_setmode(_O_BINARY)` como *la*
solución arquitectónica… pero `compat_init.c` sólo llamaba a `compat_enable_vt100()`. Las llamadas a
`_setmode` que el propio módulo muestra **nunca se escribieron**. La documentación describía un fix
que no existía en el código.

### Causa de fondo: la biblioteca estática se comía el inicializador

Al implementarlo, la salida **siguió** saliendo con `\r\n`. La
[sonda del CRT](../../tests/probes/probe_crt_init.c) aclaró por qué:

```console
hook .CRT$XCU ejecutado = SI
fd   GetFileType      _setmode devolvio      errno
0    2                16384                  0
1    3                16384                  0     <- stdout es un pipe
2    1                16384                  0
modo vigente de stdout al entrar a main = 32768 (_O_BINARY)
```

El hook corría bien **en la sonda**. La diferencia es que la sonda definía su propio inicializador,
mientras que las utilidades usaban el de `compat_init.c`… que **nunca se enlazaba**.

El motivo es una regla de las bibliotecas estáticas: **sólo aportan los `.obj` que resuelven algún
símbolo pendiente**. `compat_init.c` no exporta nada que el resto del programa llame —su única razón
de ser es colgar un puntero en la sección `.CRT$XCU`— así que el linker lo descartaba. Y con él, el
inicializador. Sin error, sin warning.

Esto además significa que el **soporte VT100 tampoco funcionaba nunca**, pese a que el módulo 03 lo da
por hecho.

### El fix

`bsd_compat` pasa de `STATIC` a **`OBJECT` library** en `CMakeLists.txt`:

```cmake
add_library(bsd_compat OBJECT ${COMPAT_SOURCES})
```

Como OBJECT library los objetos se enlazan siempre, sin lógica de descarte, y cualquier inicializador
`.CRT$XCU` sobrevive. Es la solución correcta y no un parche: el problema no era el código de
inicialización sino cómo se linkeaba.

Sobre el modo binario en sí, ver [`compat/src/binary_mode.c`](../../compat/src/binary_mode.c):

- **`stdin`/`stdout`/`stderr`** pasan a binario al arrancar, **salvo si son la consola**. Un pipe o una
  redirección quedan byte-exactos; la consola interactiva se deja en modo texto para que `\n` siga
  renderizando como salto de línea y no como un peldaño de escalera.
- **`open(2)`, `fopen(3)` y `freopen(3)`** fuerzan binario salvo pedido explícito de `_O_TEXT`. En Unix
  no hay nada que pedir: binario *es* el comportamiento por defecto.

Sin el segundo punto, `cat crlf.txt | wc -c` seguiría dando 4: el pipe sería binario, pero `cat` ya
habría mutilado el archivo al leerlo.

---

## Cómo verificarlo todo junto

```sh
./tests/run-suite.sh        # 51 aserciones, sección 5 = modo binario vs texto
```

Resultado esperado: `51/51 tests pasaron.` y exit 0.

Las sondas se recompilan con:

```bat
tests\probes\build-probes.bat
```

Vale la pena conservarlas: cada una fija, en código ejecutable, una afirmación sobre el CRT de Windows
que alguien podría volver a suponer mal en el futuro.
