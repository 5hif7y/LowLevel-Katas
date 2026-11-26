# Pendientes del port de Portmaster

Auditoría del 2026-11-25, cerrada el mismo día; regenerada el 2026-11-26. Fuentes: todos los pedidos
de la sesión (desde el 2026-11-19), todo lo que quedó sin hacer o sin
verificar en los informes, y el estado real de los tres repos (fork
`port/odin`, este kata, libnativegui `feat/portmaster-ui`).

Lo que el usuario pidió hacer está [hecho](#hecho-2026-11-25). Quedan dos
listas: lo que espera al usuario y lo **demorado a propósito**.

## Esperan al usuario

- [ ] **Protección real: probar en la VM.** La VM se sacó de Hyper-V, pero su
  disco sigue en `D:\VMs\pm-kext\pm-kext.vhdx` (Windows instalado; las
  credenciales en `D:\VMs\pm-kext-unattend\credentials.txt`). Primero
  registrarla de nuevo, PowerShell como admin en el host:
  ```
  powershell -ExecutionPolicy Bypass -File C:\Users\Shifty\DEV\gitclones\portmaster\odin\tests\winvm\reattach_vm.ps1 -Start
  ```
  Un minuto después (que arranque Windows):
  ```
  powershell -ExecutionPolicy Bypass -File C:\Users\Shifty\DEV\pm-protect\vm_test.ps1 -Bundle C:\Users\Shifty\DEV\pm-protect -CredentialFile D:\VMs\pm-kext-unattend\credentials.txt
  ```
  Primero corre `winkext-test` contra el driver real; después instala y
  chequea la lista blanca, el bloqueo de `example.org` con su prompt (que
  dice qué proceso hizo la conexión) y la desinstalación. El resultado queda
  en `D:\pm-kit-results\protection.log`.
- [ ] **Protección real: instalar en la máquina.** Si la VM dio bien,
  PowerShell como admin, **parado en** `C:\Users\Shifty\DEV\pm-protect`
  (`cd` primero: abre en `system32`):
  `powershell -ExecutionPolicy Bypass -File .\install.ps1`. Entra en período
  de prueba de 30 min: sin `confirm.ps1` se desinstala solo. Pasos y
  detalles: `packaging/windows-odin/README.md` del fork.
- [ ] **Huella SSH del VPS contra la consola del proveedor.** Coincide con
  la que reporta el servidor (`SHA256:YOAIaH…`), pero eso no descarta una
  intercepción desde la primera conexión: sólo la consola del proveedor lo
  hace.

## Demorados a propósito

No se hacen hasta que el usuario lo pida.

- **ChaCha20 SIMD.** `jess` sigue con `.Simd128`
  (`gostdlib.CHACHA20_IMPL`). Se saca el workaround e integra la
  implementación SIMD del usuario cuando su otra sesión avise.
- **Borrar los restos de la VM**: la VM ya no está en Hyper-V, pero quedan
  `D:\VMs\pm-kext` (el disco, 28 GB), `D:\VMs\pm-kext-unattend`,
  `D:\pm-kit`, `D:\pm-kit-dl` y `D:\pm-kit-results`. Hacen falta para la
  prueba de la VM de arriba (`reattach_vm.ps1` usa el disco) y
  `build_bundle.sh` toma el driver de `D:\pm-kit`. Después de la prueba:
  `Remove-VM pm-kext` y borrar esas carpetas.
- **Reportes de bugs** a Safing y a Odin, sin enviar. Borradores en
  `informes-locales/` (excluido de git):
  - a Safing: las dos inyecciones SQL de netquery, los bugs de Go de
    [BUGS.md](BUGS.md), y que el observation-hub de Go nunca arranca su SPN;
  - a Odin: `fe_sqrt` de P-384 y el ABI de ChaCha20 AVX2 (#7780, arreglado en
    dev-2026-11).
- **Android**: sería una app nueva con `VpnService`. Upstream no lo
  soporta.
- **macOS**: 14 `PORT-TODO(darwin)`, sólo compilan. Necesita una Mac o un CI
  pago ([MACOS.md](MACOS.md), `odin/tests/macos`).
- **Driver propio** (Rust + WDK): sólo documentado,
  `odin/tests/winvm/OWN-DRIVER.md`.
- **Ancho de banda de docks en Windows**: 20–26 Mbit/s contra los 32 de Go,
  sin carga (en Linux pasa: 31–34). Es un costo por mensaje en
  `spn/terminal`, no la carga. El test es opcional
  (`-define:PM_DOCKS_BANDWIDTH_TEST`).
- **`test_e2e_measure_hub`** (docks): falló 2/18 en Linux ("IPv4 scope
  violation"). Parece depender del orden de los tests: el de
  `verify_hub_ip` deja configurados los transportes y las redes del hub.
- **`service/firewall/firewall_test.odin`**: usa el mismo `set_parent_config`
  que tenía netquery (toma un cambio de revisión como "aplicado"), así que
  puede tener la misma carrera. Todavía no falló.
- **Hub en Linux detecta su propia IP**: sin confirmar, ahora que netenv
  tiene Linux. Antes había que pasar `spn/publicHub/ip4`.
- **Limpieza:**
  - `/root/spn-test` en el VPS (del 2026-11-23);
  - branch local `backup/pre-reword-portmaster-ui` en libnativegui (la
    versión con "Co-Authored-By", mismo árbol);
  - `pblind_ref.py` en el scratchpad de la sesión;
  - los 5 `.bak` que un agente borró de `/tmp` el 2026-11-19 (`llog.rs.bak`,
    `pt.bak`…) no se recuperan: regenerarlos si otra sesión los usaba;
  - al mergear este kata con N5 va a haber un conflicto chico en la tabla de
    módulos del README.

## Limitaciones aceptadas (no se hacen salvo pedido)

- Ed25519 en TLS en Windows (`ACCEPTED(ed25519-windows)`; "ECDSA es
  suficiente").
- Sin `recover`: un worker o handler que entra en pánico tira el proceso.
- DoH sin HTTP/2.
- pprof y stacks devuelven 501.
- El CLI de jess no se porta.
- RSA de más de 4096 bits y algunas combinaciones de hash de RSA: no están.
- pblind sin P-224 ni P-521.
- Diferencias menores en lib/dns y navigator (`Time.String`).
- sluice sin `SetDeadline` (`ACCEPTED(unused-by-portmaster)`), Hub sin
  `get_accessor`.
- Sin login de Safing: el usuario no quiere cuenta.
- `ACCEPTED(...)` en el código: 32.
- Avisos de stack-reference de dev-2026-11: los 13 casos `&waker` revisados
  son falsos positivos.

## Hecho (2026-11-25)

Commits del fork (`port/odin`, remoto `private`) salvo que diga otra cosa.

**Lo que esperaba una decisión**

- **Huella SSH del VPS**: coincide con la del servidor (falta la consola,
  arriba).
- **Go 1.27.4 en WSL** y los vectores contra Go real (`942be2a6`,
  `odin/tests/govectors/`): dsd, hub, terminal, docks, navigator, pblind y
  tokens idénticos. bbolt importa una base escrita por Go. El DOT del
  navigator **no** era idéntico: arreglado al layout de gographviz.
- **Protección real** (`6339efb5`, `31421059`, `packaging/windows-odin/`):
  - bloquear por defecto (prompt de 60 s), con una lista blanca: Anthropic,
    Claude, DeepSeek, GitHub, el VPS, Microsoft, Localhost y LAN (WSL);
  - el prompt muestra la ruta del ejecutable que hizo la conexión;
  - `install.ps1` instala con período de prueba y se desinstala solo; también
    `rollback.ps1`, `confirm.ps1` y `vm_test.ps1`;
  - `winkext-test` habla el protocolo v2 del driver.

  Paquete armado en `C:\Users\Shifty\DEV\pm-protect`. Falta que el usuario
  lo corra (arriba).
- **Diálogos X11 propios en C** (libnativegui `f789c2b`): message box, menú
  emergente, abrir y guardar archivo. Sin código GPL.
- **Odin dev-2026-11 nativo en WSL** (`/root/odin-2026-11/odin`, LLVM 22;
  `f2f36a5e`): `wsl_test.sh` lo usa.

**UI y libnativegui**

- libnativegui (`2ebcc30`, `f789c2b`, `251e64e`, `5668cf8`; sin
  Co-Authored):
  - X11 con varias ventanas;
  - portapapeles con INCR, bandeja XEmbed, menú contextual, XIM;
  - polígonos con antialiasing.
  - `analyse.c` ya no lee variables sin inicializar (los C4700).
  - Tests: `gw_ext_test`, `gw_ui_test`, `gw_dialog_test`.
  - **CMake en Linux**: configura y compila `libnativegui.a` con
    `NATIVEGUI_X11_DIALOGS=ON` (WSL, cmake del sistema).
- UI (`e83a99f8`, `73be0635`):
  - ventana de prompt separada;
  - pestaña **App settings** (opciones `settable-per-app`);
  - API key (`PM_API_KEY` / `--api-key`);
  - contorno de Natural Earth 110m en el mapa;
  - `--list-prompts` / `--answer`;
  - selftest 26/26, con y sin API key.
- **Mapa del SPN con un hub real**: hub self-hosted en el VPS por su IP
  pública (puerto abierto sólo mientras duró la prueba, después cerrado y
  limpio).

**Port**

- **Tests intermitentes** (`f0c660cb`): resolver, netquery, docks
  (capacidad, latencia), mgr, core_e2e, crew y patrol. Bajo carga: resolver
  0/160, mgr 0/48, core_e2e 0/20, patrol 40/40.
  - Dos eran bugs: el servidor TCP de prueba del resolver perdía el framing,
    y `spn/terminal` dejaba registrada una operación detenida (crew: el
    terminal y su crane quedaban vivos 5 min). Leaks en Linux: 0/56.
- **`PermanentAcceptFinal`**: ya estaba desde el 2026-11-24 (`6e5860c1`); la
  auditoría lo listó por error.
- **Credenciales de proxy en httpclient** (`e029c922`): Basic
  `Proxy-Authorization`. Era el último `PORT-TODO` suelto.
- **Nunca ejecutados, ahora probados:**
  - el chequeo de portal atado a la IP local, con un cliente HTTP mínimo
    sobre `tls.dial_plain` (`5bec1713`);
  - `verify_hub_ip`, la respuesta offline del nameserver y los comandos
    nft/ip/iptables de ivpn en Linux con privilegios (`c35fb4ba`);
  - el observation-hub contra un hub vivo (`e029c922`). Se arregló de paso:
    nunca arrancaba el SPN, tampoco en Go;
  - `winkext-test` v2 (`31421059`, corre en la prueba de la VM).
- **Menores** (`e029c922`):
  - el regex de fingerprint ahora recorre como `allMatches` de Go;
  - `WarningLastUpdated` vuelve a ser un tiempo (bytes iguales a Go);
  - lib/dns lee los floats hex y el `0xFF` crudo en base32;
  - netenv libera los datos del evento de portal;
  - el ICO se elige como go-ico.
- **Servidor de cuentas de Safing** (1 min la primera consulta): era el
  antivirus escaneando el exe recién compilado en `%TEMP%`. Los binarios de
  prueba ahora van a `temp-build/` (`132e925e`).

**Antes, en la misma auditoría**

- Barrido de Linux con dev-2026-11: 155/155 (`e2e03c12`). Encontró:
  - el ícono de las notificaciones apuntaba al árbol de fuentes;
  - `lib/tls` en Linux devolvía EOF en vez de Closed después de `close()`;
  - un test de `spn/access` con una fecha fija que ya venció.
- libnativegui: se sacó "Co-Authored-By" de los commits de la branch
  (force-push).
