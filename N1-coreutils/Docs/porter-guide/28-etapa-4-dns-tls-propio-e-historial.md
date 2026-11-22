# Módulo 28: Etapa 4 — DNS, TLS propio e historial, sin tocar el DNS del sistema

La etapa 4 porta la mitad "DNS" de Portmaster: el **resolver** (caché, dedupe,
transportes UDP/TCP/DoT/DoH/mDNS), el **nameserver** local que intercepta las
consultas de cada proceso, **netquery** (la base SQLite detrás de *Network
History* y *Bandwidth*) y **splittun**. Para DoT y DoH hizo falta un cliente
TLS propio, `lib/tls`, sobre SChannel. Al final de la etapa se agregaron las
**bases unix-like**: todo el árbol compila para Linux y macOS aunque sólo se
corra en Windows.

Código (fork `port/odin`): `odin/lib/tls`, `odin/service/{resolver,nameserver,netquery,splittun}/**`.
Commits `7c66afff`, `5b2e0374`, `90421c45` y `ef48c9e5` (cierre). Informes:
[`tls-transports`](../../../N6-portmaster/notas-agentes/tls-transports.md),
[`resolver-core`](../../../N6-portmaster/notas-agentes/resolver-core.md),
[`netquery`](../../../N6-portmaster/notas-agentes/netquery.md),
[`nameserver`](../../../N6-portmaster/notas-agentes/nameserver.md),
[`doh-splittun`](../../../N6-portmaster/notas-agentes/doh-splittun.md),
[`unix-bases`](../../../N6-portmaster/notas-agentes/unix-bases.md).

---

## 1. La regla: el DNS de la máquina es intocable

Un nameserver que se prueba "de verdad" se instala en el puerto 53 y se pone
como DNS del sistema. Ningún test lo hace. El nameserver escucha en un puerto
efímero de `127.0.0.1`, los upstreams son servidores DNS de test en otros
puertos efímeros, y el resolver de los tests **reemplaza** los resolvers y
scopes del sistema (si no, consultas reales a `.lan`/`.home` se colaban y los
tests dependían de la red de la casa). DoT y DoH contra Cloudflare existen
como smoke tests saltados por defecto, corridos una sola vez a mano.

Con eso, el e2e que da confianza es el **self-check de `compat`**: Portmaster
se manda a sí mismo una consulta a un dominio aleatorio, la ve pasar por el
nameserver, por el firewall y por el resolver, y verifica los tres pasos. En
la etapa 3 el paso 2 fallaba "correctamente" (no había resolver); en la etapa 4
pasa de punta a punta.

## 2. TLS sin OpenSSL ni crypto/tls: SChannel

Go trae `crypto/tls`; Odin no. Las opciones eran escribir TLS 1.3 (no), atar
OpenSSL (otra dependencia) o usar el TLS del sistema. En la etapa 2 HTTPS ya
iba por WinHTTP; para DoT hace falta TLS sobre un socket propio, así que
`lib/tls` habla **SSPI/SChannel** directamente:

* handshake en bucle con `InitializeSecurityContextW`: `SEC_I_CONTINUE_NEEDED`,
  `SEC_E_INCOMPLETE_MESSAGE` (faltan bytes, volver a leer), buffers `EXTRA`
  (sobraron bytes del siguiente registro), `SEC_I_RENEGOTIATE`;
* `close_notify` con `ApplyControlToken(SCHANNEL_SHUTDOWN)`;
* toda espera es `WSAPoll` en rebanadas de 50 ms sobre socket no bloqueante —
  nunca `SO_RCVTIMEO` (módulo 27: un recv que vence deja el socket en estado
  indeterminado en Windows);
* para los tests, un **servidor** TLS en memoria (`AcceptSecurityContext`) con
  un certificado autofirmado cuya clave vive en un contenedor CNG con nombre
  único que se borra al terminar.

Trampa: en paralelo, dos tests generaban claves con el mismo nombre
(timestamps en ns que colisionan + `NCRYPT_OVERWRITE_KEY_FLAG`) y el handshake
fallaba con `SEC_E_INTERNAL_ERROR` una de cada tantas corridas. Nombre por
secuencia atómica. Otra: `tls.DialContext(ctx)` sólo acota el *dial*; guardar
ese ctx en la conexión dejaba al lector usando un contexto ya liberado.

## 3. El hueco de DoH: validar contra la IP en vez del dominio

Un resolver DoH se configura como `https://1.1.1.1/dns-query?name=cloudflare-dns.com`.
Go **conecta a la IP** pero manda SNI y **valida el certificado contra el
dominio**. La primera versión del port iba por WinHTTP, que toma host, SNI y
nombre a validar de la URL — o sea, la IP. Funcionaba con Cloudflare (su
certificado incluye la IP), pero un resolver cuyo certificado sólo tiene el
nombre fallaba, y uno con un certificado *para la IP* emitido a otro pasaba.

El arreglo fue llevar DoH a `lib/tls`, que separa "a dónde me conecto" de "a
quién valido". Y la lección es el test: no alcanza con que la consulta buena
pase, **tiene que haber tests que fallen**:

| Certificado del servidor de test | Resultado esperado |
|---|---|
| nombre = dominio del resolver | OK |
| otro dominio | falla |
| sólo la IP del resolver | **falla** |
| sin pin / sin confianza | falla |

Diferencia documentada: Go negocia HTTP/2 (`ForceAttemptHTTP2`); el port ofrece
sólo `http/1.1` por ALPN, con keep-alive (hasta 2 conexiones ociosas, 1 min) y
reintento si la conexión reusada estaba cerrada.

## 4. Nameserver: lo que Go no hacía

`miekg/dns` le da a Go un servidor DNS completo; Portmaster lo usa sólo en UDP.
El port escribe su servidor (UDP y TCP en la misma dirección, un worker de
`mgr` por request UDP y por conexión TCP) y agrega dos cosas que faltaban:

* **TCP**: un cliente que recibe una respuesta truncada reintenta por TCP; Go
  lo rechazaba. Con TCP apareció otro bug: `network.new_connection_from_dns_request`
  suponía UDP y buscaba la conexión sólo por el extremo origen. Una conexión
  TCP sólo está en la tabla con **ambos extremos** → ahora recibe protocolo y
  extremo del servidor, y las consultas TCP quedan atribuidas a su proceso.
* **Truncado** al tamaño EDNS0 (o 512). miekg mandaba cualquier tamaño y un
  cliente con buffer de 512 recibía un datagrama cortado.

Bug de Go corregido: `writeDNSResponse` tenía la condición invertida (devolvía
error justo cuando el reintento sin la sección extra funcionaba).

## 5. netquery: bytes idénticos y dos inyecciones SQL

netquery es *Network History* y *Bandwidth* (ver la
[auditoría](../../../N6-portmaster/AUDITORIA-features-de-pago.md)). El port
mantiene el **schema byte-idéntico** al que genera el ORM de Go — con sus
rarezas (`external  NOT NULL`, `allowed ,`) — porque así **abre un
`history.db` escrito por el Portmaster Go**. El test compara el `CREATE` y
`sqlite_master`, no "algo equivalente".

Al portar el lenguaje de consultas JSON de la UI aparecieron dos inyecciones
SQL en la API de Go:

1. el alias `as` se validaba con `[a-zA-Z]+` **sin anclar** → cualquier texto
   con una letra se concatenaba al SQL;
2. `databases` se concatenaba sin validar.

La API escucha en loopback con autenticación, pero cualquier proceso local con
un token de la UI podía inyectar. Arreglo: identificador SQL anclado (sólo
letras rompía la UI, que manda `first_connection`) y lista blanca
`main`/`history`.

Lección general de la etapa: **portar los tests de Go verbatim es la
especificación, pero leer el código para portarlo es la auditoría**. Ningún
test de Go cubría estos casos.

## 6. Bases unix-like: compilar para donde todavía no se corre

Objetivo del usuario: que el port funcione algún día en Linux/macOS, pero
desarrollar sólo en Windows. La forma barata de no cerrarse esa puerta es que
**todo compile para los otros targets desde hoy**:

```bash
VET=1 bash odin/tests/crosscheck.sh windows_amd64 linux_amd64 darwin_arm64
```

Al empezar fallaban 69 de 113 paquetes en Linux; al cerrar, todos pasan
(salvo `windows_kext/kextinterface` y el display falso por named pipe, Windows
por naturaleza). La convención:

* código Win32 en `x_windows.odin`, **sin cambios** (movido textual);
* `x_other.odin` con `#+build !windows`: mismas firmas, devuelve
  `.Not_Supported` / `.No_Server` / cero, y un `PORT-TODO(unix): <qué hace el
  _linux.go>` apuntando al archivo Go;
* la lógica portable no se duplica nunca: queda en el archivo sin sufijo.

Trampas de Odin que salieron:

| Construcción | Qué pasa | Arreglo |
|---|---|---|
| `x_other.odin` | `_other` **no** es un tag de Odin: se compila en todos lados | `#+build !windows` |
| `x_windows_test.odin` | el `_test` final tapa el sufijo de OS | `#+build windows` explícito |
| test sólo de Windows | `odin check` incluye `_test.odin`: rompe el cross-check | tag en el test |
| helper `@(private="file")` | no se ve desde el `_windows_test` hermano | `@(private)` |
| import usado sólo bajo `when ODIN_OS == .Windows` | `-vet` lo marca sin usar en Linux | `@(require) import` |
| handle Win32 en un struct compartido | el stub cambiaba el layout | `distinct` del mismo tamaño |

La lista de subsistemas que faltan para Linux (nfqueue/eBPF, `/proc`, netlink,
resolv.conf/systemd-resolved, notificaciones D-Bus, TLS con otra biblioteca,
...) está en `odin/PORTING.md` → "Unix-like", derivada de los `_linux.go` del
árbol Go.

## Ejercicios

1. Escribí el test que falta en tu propio cliente TLS: ¿qué pasa si el
   servidor presenta un certificado válido para la IP pero no para el nombre?
2. Tomá un paquete con código Win32 mezclado con lógica y separalo en
   `_windows.odin` + `_other.odin` sin mover ni una línea de lógica portable.
   Verificá con `odin check -target:linux_amd64 -vet`.
3. Buscá en un proyecto propio una expresión regular de validación sin `^…$`
   que proteja una concatenación de SQL o de shell.
