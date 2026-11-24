# Módulo 31: Ronda unix — Linux desde Windows, y root sin riesgo

Las seis etapas dejaron un Portmaster en Odin que corre en Windows y que
**compila** para Linux y macOS: todo lo que dependía del sistema operativo
quedó en stubs `*_other.odin` con un `PORT-TODO(unix)`. Esta ronda los llenó
y, sobre todo, los **corrió**: compilar no es funcionar, y el primer barrido
lo demostró.

Código (fork `port/odin`, commits `32c3f2b3` … `2e018968`): `odin/lib/{dbus,
netlink,tls,httpclient}`, `odin/service/network/proc`, los `*_other.odin` /
`*_linux.odin` de cada paquete, `odin/tests/{wsl_test,netns_test}.sh`.
Informes: `notas-agentes/unix-*.md`, brief `BRIEF-UNIX.md`.

---

## 1. Correr Linux sin salir de Windows

WSL ya tenía todo: Odin del mismo commit, gcc, sqlite, OpenSSL 3, libbpf.
El flujo es copiar el fork a un directorio dentro de WSL y correr `odin test`
ahí:

```sh
bash odin/tests/wsl_test.sh -m mi-espejo --fresh base/log service/process
```

Tres trampas del espejo, las tres pasaron:

* `tar --keep-newer-files` "para copiar sólo lo nuevo" saltó en silencio
  archivos editados (con los errores mandados a `/dev/null`): hubo corridas
  probando código viejo. El espejo ahora pisa todo.
* Un archivo borrado o renombrado en el fork **sigue** en el espejo y da
  redeclaraciones: `--fresh` borra el espejo antes.
* Varios agentes en paralelo: **un espejo por agente**. Y WSL2 comparte los
  puertos de loopback con Windows: un test de Windows y uno de Linux pueden
  pisarse el puerto.

## 2. El primer barrido: 115 de 143

| Causa | Paquetes |
|---|---|
| no hay cliente HTTP/TLS en unix | apprise, updates, spn/access, trafficgen, updatemgr, observation-hub, dsd, metrics… |
| no hay búsqueda de procesos (`/proc`) | nameserver, firewall, netquery, network: fallaban en el **setup** del test |
| el test asume Windows | `.exe`, `C:\`, `>` como flecha del log, WFP, `"Loopback"` como interfaz |

La tercera fila es la más instructiva. Un test que espera `firefox.exe` no
prueba el firewall: prueba Windows. La regla fue **el test unix espera lo que
Go da en Linux**; por ejemplo Go en Linux nombra `C:\Apps\named.exe` como
"C Apps Named", porque `:` y `\` no separan rutas ahí.

## 3. Trampas POSIX que Windows escondía

* **`realpath(3)`**. `os.get_absolute_path` es `GetFullPathNameW` en Windows
  y `realpath` en POSIX: falla si la ruta todavía no existe. Go
  `filepath.Abs` nunca toca el disco. Rompía fstree, los íconos y el índice
  de updates → `utils.abs_path`.
* **El fd 0.** `defer if !ok do net.close(sock)` con `sock` resultado con
  nombre: `return {}, err` lo pone en 0 *antes* del defer. En Windows cerrar
  el handle 0 no hace nada visible; en Linux el fd 0 se libera y el próximo
  `socket()` lo recibe… y lo cierra el siguiente dial fallido. Un listener de
  test moría 1 de cada 11 corridas con ECONNREFUSED. Arreglo: cerrar una
  copia local. Moraleja: en unix 0 es un descriptor válido; nunca usarlo como
  "no hay socket".
* **`close` no despierta `accept`.** En Windows cerrar el socket que escucha
  desbloquea al hilo en `accept()`; en Linux no: hace falta `shutdown`. Un
  test de observation-hub colgaba para siempre.
* **`send` parcial.** En Linux `net.send_tcp` puede devolver bytes escritos
  *y* `Would_Block`; el patrón de Windows "si Would_Block, reintento" perdía
  bytes.
* **Build tags.** `#+build !windows !linux` en una línea es error: el AND son
  dos líneas. Y `x_windows_test.odin` no es tag (el `_test` lo tapa).

## 4. Root sin riesgo: network namespaces

La intercepción de Linux (nfqueue, iptables, conntrack, eBPF) necesita root y
cambia reglas del firewall. El usuario autorizó root **sólo dentro de WSL**;
igual se fue un paso más lejos: cada paquete corre en un network namespace
propio y descartable.

```sh
unshare --net sh -c 'ip link set lo up; iptables -I OUTPUT ... -j NFQUEUE ...; odin test ...'
```

Las reglas viven y mueren con el namespace. `netns_test.sh` además hashea las
reglas del namespace principal antes y después y falla si cambiaron; cada
test privilegiado se niega a correr si su namespace es el de init. Lo único
que sale del namespace es la carga de módulos del kernel (nfnetlink_queue,
xt_NFQUEUE…) y los programas eBPF mientras dura el test, que se despegan al
terminar.

Dos sorpresas del entorno: WSL tiene su propio namespace de PIDs (el PID que
ve eBPF no es el de `/proc`), y `os.process_exec` de Odin usa `execveat`, que
el tracepoint `sys_enter_execve` no ve.

## 5. Cuando la forma de Odin pierde información de Go

Dos casos donde la API "simplificada" del port se comía algo que Go usa:

* El hook "SPN conectando" de Go recibe el `hub.Announcement`; la forma Odin
  del hook no lo pasaba porque el único usuario Windows lo ignoraba. El de
  Linux lo necesita (ruta al hub fuera del túnel IVPN). En vez de cambiar la
  firma en ocho archivos, el captain expone el hub **mientras corren los
  hooks** (Go los invoca sincrónicamente antes de conectar).
* Go decide `PermanentAcceptFinal` con un *type assertion*: sólo el paquete
  de nfqueue lo tiene. En Odin no hay type assertion sobre una vtable: es una
  entrada **opcional** que cae a `permanent_accept`. El detalle fino: el
  `tracedPacket` de Go embebe la interfaz, que no tiene ese método, así que un
  paquete trazado **también** cae al accept común; el tracer de Odin deja la
  entrada en nil a propósito.

## 6. Un cliente D-Bus, tres usuarios

NetworkManager (netenv), el Secret Service (keyring) y las notificaciones de
escritorio hablan D-Bus. Se escribió **un** cliente en Odin puro (sin libdbus)
con un bus falso para tests, y los tres lo usan. Trampa de las
notificaciones: los daemons (dunst, GNOME, KDE) mandan `ActionInvoked` y
`NotificationClosed` sólo a la conexión que llamó `Notify`. En Windows el
notifyd de N4 avisa a todos los suscriptores, así que separar conexiones
funcionaba ahí; en Linux perdía los clicks. Una sola conexión compartida.

## 7. Coordinación: lo que salió mal

* El `.gitignore` de Go excluye todo `vendor/` y `testdata/`. También los del
  port: un clon nuevo de `port/odin` no compilaba. Nadie lo notó en seis
  etapas porque los archivos siempre estaban en el disco.
* El coordinador commiteó con `git add -A` mientras un agente editaba: el
  commit se llevó la mitad del keyring y quedó un commit que no compila en
  unix. No se reescribió la historia publicada; se completó en el commit
  siguiente y quedó documentado. Desde entonces: rutas explícitas siempre.

## Ejercicios

1. Buscá en un código propio un `defer` que cierre un resultado con nombre.
   ¿Qué cierra si se sale por un `return` con valor cero?
2. Escribí un test que use `unshare --net` para agregar una regla de
   iptables y verifique que el namespace principal no cambió.
3. Tomá una API "simplificada" de un port propio y listá qué información del
   original se perdió. ¿Algún consumidor de otro OS la necesita?
