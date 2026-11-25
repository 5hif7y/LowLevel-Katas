# Pendientes del port de Portmaster

Auditoría del 2026-11-25. Fuentes: todos los pedidos de la sesión (desde el
2026-11-19), todo lo que quedó sin hacer o sin verificar en los informes, y el
estado real de los tres repos (fork `port/odin`, este kata, libnativegui
`feat/portmaster-ui`). Los tres estaban limpios y pusheados.

Cada ítem dice **dónde** está y **qué falta**. Al cerrar uno, se tacha y se
anota la fecha y el commit.

## Esperan una decisión del usuario

- [ ] **ChaCha20 SIMD.** `jess` usa `.Simd128` (`gostdlib.CHACHA20_IMPL`) por
  el bug AVX2 de Odin dev-2026-10. dev-2026-11 ya trae el arreglo upstream.
  Se saca el workaround e integra la implementación SIMD del usuario cuando
  su otra sesión termine y lo avise. Hasta entonces, no tocar.
- [ ] **Borrar la VM `pm-kext`** y las carpetas `D:\VMs\pm-kext`,
  `D:\VMs\pm-kext-unattend`, `D:\pm-kit`, `D:\pm-kit-dl`, `D:\pm-kit-results`
  (los comandos se pasaron el 2026-11-25). Sin confirmar si ya se hizo.
- [ ] **Reportes de bugs**, sin enviar, como pidió el usuario. Los borradores
  están en `informes-locales/` (excluido de git):
  - a Safing: las dos inyecciones SQL de netquery y los bugs de Go de
    [BUGS.md](BUGS.md);
  - a Odin: `fe_sqrt` de P-384 (sigue en dev-2026-11) y el ABI de ChaCha20 AVX2
    (issue upstream #7780, arreglado en dev-2026-11).
- [ ] **Huella de la clave SSH del VPS** (`SHA256:YOAIaH…`). Se aceptó en la
  primera conexión (2026-11-22) y nunca se comparó contra la del servidor.
- [ ] **Toolchain de Go** (instalarlo necesita OK). Sin él, los vectores
  esperados se derivaron de las reglas del encoder de Go, no de una ejecución
  real: bytes de dsd (JSON/CBOR), del hub, terminal, docks y navigator, tokens
  de pblind contra pblind1 de Safing, el DOT del navigator y el importador
  bbolt contra una base real del Portmaster Go.
- [ ] **Protección real en la máquina del usuario** (admin + driver WFP). Sólo
  se probó en la VM. Los comandos de `pm-test` corren sin interceptar
  (`--disable-interception`), sólo para probar.
- [ ] **Android.** Es parte del objetivo (todos los dispositivos bajo una sola
  SPN), pero upstream no lo soporta. Sería una app nueva con `VpnService`;
  nunca se planeó.
- [ ] **macOS**: 14 `PORT-TODO(darwin)`, sólo compilan. Necesita una Mac o un CI
  pago. La investigación está en [MACOS.md](MACOS.md) y en el fork,
  `odin/tests/macos` (workflow de ejemplo, apagado).
- [ ] **Driver propio** (Rust + WDK, test-signing): sólo documentado,
  `odin/tests/winvm/OWN-DRIVER.md`. El WDK no está instalado.
- [ ] **Diálogos X11 de libnativegui.** `GW_ShowMessageBox` y los diálogos de
  archivo esperaban MessageBox-X11 y sofd (`x_fib`), que no están en el repo.
  Quedaron detrás de `NATIVEGUI_X11_DIALOGS` (apagado: imprimen a stderr y
  devuelven "cancelado"). Hay que elegir: traer esas librerías (sofd es GPL)
  o escribir diálogos propios en C. La UI no los usa.
- [ ] **Barrido de tests de Linux con dev-2026-11.** El WSL se liberó para el
  trabajo de X11. El barrido completo (`odin/tests/wsl_test.sh`) no se volvió
  a correr desde el cambio de compilador. El 2026-11-25 sí corrieron en WSL
  `portmaster-core` y `portmaster-ui` compilados con dev-2026-11.

## UI y libnativegui

- [ ] **Recompilar `pm-test\portmaster-ui.exe`**, que es la v1 (estaba
  abierta). Cerrarla desde la bandeja y, en el fork:
  `odin build odin/cmds/portmaster-ui -collection:pm=odin -out:C:\Users\Shifty\DEV\pm-test\portmaster-ui.exe`.
- [ ] **Mapa del SPN con hubs reales**: sin probar en vivo. El core sólo acepta
  hubs con IP pública; habría que conectar el core de prueba al hub del VPS.
- [ ] **Contorno de los continentes** en el mapa. No hay datos en el repo;
  descargar Natural Earth o world-atlas (dominio público) necesita OK.
- [ ] **Ventana de prompt separada** (la UI original la tiene; acá los prompts
  van en la ventana principal).
- [ ] **Ajustes por app** de las opciones `settable-per-app` (hoy sólo
  reglas y acción por defecto) y autenticación por API key.
- [ ] **X11 en libnativegui**: portapapeles (selecciones / INCR), bandeja
  (XEmbed o StatusNotifierItem por D-Bus), menú contextual, método de entrada
  (XIM: teclas muertas, IME). Hoy son stubs documentados; sin bandeja, cerrar
  la ventana sale de la UI.
- [ ] **CMake de libnativegui en Linux**: no se probó (WSL no tiene cmake). Se
  compiló con gcc directo, igual que `build_linux.sh`.
- [ ] **Avisos C4700 en `analyse.c`** (código de Wagner): variables usadas sin
  inicializar (`x_size`, `y_size`, `g`, `nox`, `noy`). Bugs anteriores a la
  branch; la UI no usa ese código.

## Trabajo técnico del port

- [ ] **Tests intermitentes:**
  - reconexión TCP del resolver, ~4/40 bajo carga (arreglo propuesto: esperar
    "abandoned" después de q2);
  - netquery en Linux, "history must be enabled", ~1/12;
  - capacidad de docks bajo carga de CPU (18,5 % contra un piso de 20 %);
  - service/mgr y core_e2e, por tiempos bajo carga (pasan solos).
- [ ] **`PermanentAcceptFinal`**: la entrada opcional en `Packet_VTable` que
  usaría `firewall.issue_verdict` quedó como propuesta.
- [ ] **Credenciales de proxy en httpclient**
  (`lib/httpclient/client.odin:181`, único `PORT-TODO` suelto): `user:pass@`
  se descarta; falta mandarlo como `Proxy-Authorization`.
- [ ] **Nunca ejecutados:**
  - el chequeo de portal cautivo atado a la IP local permitida (WinHTTP no
    deja elegirla);
  - el test de ancho de banda de docks (`-define:PM_DOCKS_BANDWIDTH_TEST`) y
    `verify_hub_ip`;
  - la respuesta offline del nameserver;
  - observation-hub contra un SPN vivo;
  - los comandos nft/ip/iptables de ivpn en Linux (sólo se verifican en tests);
  - `cmds/winkext-test` (habla el protocolo v1; el driver es v2).
- [ ] **Confirmar** que el hub en Linux detecta su propia IP ahora que netenv
  tiene Linux (antes había que pasar `spn/publicHub/ip4` a mano).
- [ ] **Servidor de cuentas de Safing**: la primera consulta tardó 1 min y las
  siguientes 3,4 s. No se investigó.
- [ ] **Menores conocidos:**
  - el regex de fingerprint del perfil corta en una coincidencia vacía;
  - `WarningLastUpdated` sigue siendo string;
  - lib/dns no maneja los floats hex de GPOS/LOC ni un `0xFF` crudo en base32;
  - netenv no libera los datos del evento de portal cautivo;
  - binmeta elige la imagen más grande del ICO (no se comparó contra go-ico);
  - sluice sin `SetDeadline`, Hub sin `get_accessor`.

## Limpieza

- [ ] `/root/spn-test` en el VPS: quedó del 2026-11-23. Los paquetes de prueba
  ya se desinstalaron.
- [ ] Branch local `backup/pre-reword-portmaster-ui` en libnativegui (es la
  versión con "Co-Authored-By", mismo árbol): borrar cuando no haga falta.
- [ ] `pblind_ref.py` en el scratchpad de la sesión.
- [ ] El 2026-11-19 un agente borró 5 archivos `.bak` de `/tmp` (`llog.rs.bak`,
  `pt.bak`…). No se pueden recuperar; regenerarlos si otra sesión los usaba.
- [ ] Al mergear este kata con N5 va a haber un conflicto chico en la tabla de
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
- Diferencias menores en lib/dns y navigator (DOT, `Time.String`).
- Sin login de Safing: el usuario no quiere cuenta.
- 31 `ACCEPTED(...)` en el código.
- Avisos de stack-reference de dev-2026-11: los 13 casos `&waker` revisados
  son falsos positivos.

## Hecho en esta auditoría (2026-11-25)

- libnativegui: se sacó "Co-Authored-By" de los 6 commits de la branch
  (force-push). La regla quedó en memoria.
- libnativegui `2ebcc30`, backend X11:
  - compila y pasa sus tests en WSLg: `gw_ext_test` 53/53, `gw_ui_test` 33/33;
  - se arreglaron bugs de Wagner que gcc 16 rechaza;
  - texto del teclado sin depender del locale;
  - UTF-8 trunca y termina siempre (también en Win32).
- Fork `26327cb7`:
  - `build_linux.sh`;
  - fuentes por distro, más `PM_UI_FONT`;
  - UI y core en Linux con `--selftest` 22/22;
  - STAGES.md y PORTING.md al día;
  - sección "Marcadores al 2026-11-25".
- Kata: FEATURES.md y README.md al día; este archivo.
