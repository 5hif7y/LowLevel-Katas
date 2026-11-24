# Brief para las rondas unix-like (leer además de BRIEF-COMUN.md)

Las 6 etapas del port a Odin están cerradas para Windows. Ahora se implementa
lo que en Windows quedó como stub `*_other.odin` con `PORT-TODO(unix)`.
**Esta regla reemplaza la de BRIEF-COMUN que decía "Linux fuera de alcance".**

## Entorno Linux de prueba

WSL `archlinux` en esta máquina: Odin dev-2026-10 (mismo commit que Windows),
gcc/clang, sqlite3 y OpenSSL 3 (`libssl.so.3`, `libcrypto.so.3`) ya instalados.
Se corre desde Git Bash, en la raíz del fork:

```sh
bash odin/tests/wsl_test.sh -m <tu-nombre> [--fresh] [-n 3] <pkg>...
bash odin/tests/netns_test.sh -m <tu-nombre> <pkg>...   # tests privilegiados (netfilter, eBPF)
```

`wsl_test.sh` copia el fork (sin `.git`, pisando todo) a `/root/pm/<espejo>` y
corre `odin test odin/<pkg> -collection:pm=odin -vet`; una línea por corrida
(OK / FAIL / TIMEOUT / BUILD) y el log de las que fallan. **Usá un espejo
propio** (otros agentes corren a la vez) y `--fresh` después de borrar o
renombrar archivos. `netns_test.sh` corre cada paquete como root dentro de un
network namespace nuevo y falla si cambian las reglas del namespace
principal. Para comandos sueltos: `wsl.exe -d archlinux -- sh -c '...'` (con
`MSYS_NO_PATHCONV=1`; para algo no trivial, pasá el script por stdin con
`sh -s`: un script como argumento lo vuelve a interpretar el shell de WSL).
(Durante la ronda unix se usaba un `lt.sh` del scratchpad del coordinador,
del que salieron estos dos scripts.)

## Convenciones

- `*_other.odin` (`#+build !windows`) pasa a ser la implementación unix real.
  Preferir `core:sys/posix` (sirve para linux y darwin). Lo que sólo existe
  en Linux (`/proc`, netlink, `SO_BINDTODEVICE`) va bajo `when ODIN_OS ==
  .Linux` o en `*_linux.odin`, y darwin queda con `PORT-TODO(darwin)` y un
  motivo preciso (no un stub mudo).
- Fidelidad a Go: el `_linux.go` / `_unix.go` / `_default.go` del paquete es
  la especificación. Para dependencias (gopsutil, go-keyring, etc.) no hay
  módulo de Go local: se pueden leer en GitHub (fuente pública, sin datos
  del usuario en la URL).
- Tests: un test que asume Windows (rutas `C:\`, `.exe`, WFP, named pipes)
  se separa en `_windows_test.odin` con `#+build windows` explícito (el
  sufijo `_test` tapa el `_windows`) y, si el comportamiento existe en unix,
  se escribe el equivalente en `*_other_test.odin` con `#+build !windows`.
  Nunca debilitar una aserción para que pase: si Go en Linux da otro
  resultado, el test unix espera el resultado de Go en Linux.
- Al terminar no debe quedar `PORT-TODO(unix)` en tu alcance salvo lo que el
  coordinador te marcó como fuera de alcance; lo de darwin queda como
  `PORT-TODO(darwin)`.

## Prohibido (también dentro de WSL)

- Correr como root cosas que cambien el sistema: iptables/nftables, rutas,
  `ip link`, sysctl, systemd, `/etc/resolv.conf`. Instalar paquetes (pacman):
  pedírselo al coordinador.
- Escuchar en el puerto 53 o en cualquier puerto < 1024.
- Leer `/proc` y `/sys`, abrir sockets en loopback y crear archivos en /tmp:
  permitido.

## Verificación obligatoria antes del informe

1. Linux: cada paquete de tu alcance, **3 corridas** con `lt.sh`.
2. Windows: los mismos paquetes, 3 corridas (comando de BRIEF-COMUN): no
   puede romperse nada de lo ya cerrado.
3. `VET=1 bash odin/tests/crosscheck.sh windows_amd64 linux_amd64 darwin_arm64 -- <tus dirs>`: 0 fallas.
