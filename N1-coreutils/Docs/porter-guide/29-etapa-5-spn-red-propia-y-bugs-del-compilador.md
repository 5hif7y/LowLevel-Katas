# Módulo 29: Etapa 5 — SPN, una red propia y dos bugs del compilador

La etapa 5 porta la **SPN** (Safing Privacy Network): la red de hubs por la
que Portmaster tunela conexiones, con autenticación por tokens ciegos,
cripto de sesión (jess), ruteo multi-hop y exit policies. Son ~30 mil líneas
de Go en 14 paquetes, más `cmds/hub` y un cliente de pruebas propio.

Código (fork `port/odin`): `odin/spn/**`, `odin/cmds/{hub,spnclient}`,
`odin/tests/{spn_e2e,chacha20_simd}`. Informes en
[`notas-agentes/`](../../../N6-portmaster/notas-agentes/) (`spn-*.md`,
`state-data.md`, `dns-presentation.md`).

---

## 1. Probar una red sin la red de Safing

La SPN real necesita las zonas de tokens de Safing, su servidor de cuentas y
sus hubs. Nada de eso se puede (ni se debe) usar desde un port. La salida es
una **red self-hosted**, que Go ya casi soportaba:

* **zona de tokens propia**: `spnclient gen-zone` genera el par; el hub
  guarda sólo los verificadores, el cliente los tokens. Con
  `spn/access/selfHostedZoneFile` no se registra ninguna zona de Safing.
* **intel local**: un archivo con los hubs bootstrap marcados `Trusted`, en
  vez del intel que baja el updater.
* **mapa y scope de test**: `-spn-map selfhosted -spn-map-scope test`, porque
  el scope público descarta IPs no globales (127.0.0.1 incluida).

El e2e levanta los hubs como **procesos separados** en loopback y un cliente
que se autentica, abre un túnel y hace eco/HTTP. Dos trampas: dos hubs con la
misma IP se marcan *Superseded* (B va en 127.0.0.2), y el navigator descartaba
hubs sin ubicación geográfica (parche: Location vacía en scopes no públicos).

## 2. Rondas por capas de dependencia

Con 14 paquetes que se importan entre sí, el orden lo da el grafo:

| Ronda | Paquetes |
|---|---|
| A | unit, conf, hub, patrol, sluice, access/token, account |
| B | cabin, terminal, ships |
| C | access, docks |
| D | navigator, crew |
| E | captain, instance, cmds/hub, spnclient, e2e |
| F | hooks servicio↔SPN (firewall, broadcasts, core, ivpn) |

Cada ronda termina con sus tests corridos 3 veces **en primer plano** por
quien integra, no sólo por el agente que la escribió. Las rondas que corren
en paralelo tocan directorios disjuntos; aun así, una ronda puede romper la
compilación de otra a mitad de camino (pasó: tests que fallan por una
edición ajena en curso). Regla: ante un fallo en un paquete compartido,
primero preguntar quién más está editando.

## 3. El primer binario Linux

El hub tiene que correr en un VPS Rocky Linux. Desde Windows:

```bash
odin build odin/cmds/hub -collection:pm=odin -target:linux_amd64 \
    -build-mode:obj -use-single-module -reloc-mode:pic -o:speed -out:spn-hub.o
# en el VPS (sin paquetes -devel: linkear contra los sonames)
gcc -o spn-hub spn-hub.o -l:libssl.so.3 -l:libcrypto.so.3 -l:libsqlite3.so.0 -lpthread -lm
```

`-build-mode:obj` sin `-use-single-module` deja un `.o` por paquete; sin
`-reloc-mode:pic` gcc no linkea un PIE. Las bibliotecas salen de los
símbolos indefinidos del objeto, no de adivinar.

Fue la **primera ejecución** de código Linux del port: el hub arrancó, sirvió
y paró limpio. Lo único que falló fue configuración, y fiel a Go: la dirección
de bind (`-bind-ip`) se usa para escuchar **y** para conectar hacia afuera,
así que un hub atado a 127.0.0.1 no puede salir a Internet (EINVAL). La
prueba final: cliente Windows → túnel ssh → hub en el VPS → loopback del VPS
e Internet, con el puerto bloqueado por firewalld desde afuera.

## 4. El test que corrió 6 horas y 37 minutos

Una corrida repetida de los tests de `captain` quedó "andando" toda la noche.
No era trabajo: era un proceso colgado. La investigación, en orden:

1. **Pilas sin tocar el proceso**: `cdb -p PID -pv` (no invasivo) y `~*k`.
   Dos tests esperaban el mutex de `servicetest`; **ningún** test lo tenía.
2. El log decía `Caught signal ... Segmentation_Fault` en otro test: el
   runner de Odin mata el hilo del test que crashea, pero ese hilo tenía el
   lock tomado y su `defer end()` nunca corrió. Crash → cuelgue eterno.
3. **Reproducir el crash bajo el debugger**: `sxe av` y pila en la primera
   access violation. Caía en `chacha20_simd256._dq_round_simd256`, con
   `movaps xmm0, [rax]` y `rax = 0`.
4. **Leer el ensamblador de ambos lados**: el llamador escribe 3 punteros en
   `[rsp+20h..30h]`; el llamado lee 4 (`[rbp+180h..198h]`). El cuarto es lo
   que haya quedado en la pila: casi siempre válido, a veces 0.
5. **Repro mínima** sin una línea de Portmaster:

| Build | Simd128 | Simd256 |
|---|---|---|
| `-debug` | OK | **segfault** |
| `-o:none` | OK | **segfault** |
| `-o:minimal` / `-o:speed` | OK | OK |

Un proc `#force_inline` con 4 resultados SIMD de 256 bits, cuando **no** se
inlinea, se llama con un ABI distinto del que espera. Workaround: jess usa
`.Simd128`; el test `tests/chacha20_simd` compara cada implementación contra
la portable a 64 profundidades de pila y tiene un switch para volver a
probar Simd256 cuando salga un compilador nuevo.

Lecciones: un test colgado es un **síntoma**, no lentitud; todo loop de
pruebas lleva `timeout`; y "a veces crashea" en código determinista casi
siempre es memoria no inicializada — acá, del compilador.

## 5. Reconstruir no es portar: pblind

Los tokens de la SPN son firmas ciegas (Abe–Okamoto) de `rot256/pblind`. La
primera versión se **reconstruyó del paper** porque no se tenía la fuente:
funcionaba Odin↔Odin, pero no era compatible con Go. Al leer la fuente real
(GPL-3.0, commit fijado en `go.sum`) aparecieron cinco diferencias: el hash
a punto y a escalar son HKDF-SHA512 usado como stream con rechazo, los
mensajes llevan coordenadas sueltas y no puntos comprimidos, el requester
calcula `d` en vez de recibirlo, etc. Todo test Odin↔Odin pasaba con la
versión equivocada: **un protocolo se valida contra la otra punta**, no
contra sí mismo.

De paso apareció el segundo bug de Odin: `field_p384r1.fe_sqrt` calcula
x^(p−3) en vez de x^((p+1)/4) y rechaza todo, incluso `sqrt(4)`. Se detecta
con tres líneas; se esquiva con una exponenciación propia sólo para datos
públicos.

## 6. El crash que desaparece bajo el debugger

El barrido final dejó un e2e con el hub muerto en silencio. Aislado en un
loop (hub nuevo + un cliente, 40 veces): ~1 de 40 terminaba con
`0xC0000005` y nada en el log. Bajo `cdb` lanzado: 40/40 bien. Con `cdb`
adjuntado después de arrancar (para esquivar el *debug heap* de NT, que
cambia el layout de memoria): 70/70 bien. El debugger, cualquiera, alcanzaba
para cambiar el timing.

La salida fue que el proceso escriba **su propio minidump**:
`SetUnhandledExceptionFilter` + `MiniDumpWriteDump` en `lib/crashdump`,
encendido con `PM_CRASH_DUMP_DIR`. En 150 arranques sin debugger cayeron
dos dumps con la misma pila: `sqlite.query_executor → iterator.finish →
chan.close` sobre un mutex en la dirección `0x4090`. Memoria liberada.

El iterador de la base de datos tiene dos dueños: el consumidor y el hilo de
la query. En Go el GC lo mantiene vivo mientras alguno lo use; el port decía
"liberalo cuando los dos terminaron", y un consumidor que cortaba temprano lo
liberaba mientras el storage todavía estaba dentro de `finish`. Arreglo:
**refcount de dos** (`destroy` del consumidor, `finish` del storage,
idempotente) y `destroy` cierra `next` para despertar a un productor
bloqueado en `send`. El test de regresión hace las dos órdenes mil veces;
con el código viejo no falla con un segfault sino que **se cuelga**, que es
la otra cara del mismo contrato roto.

Patrón general: cada vez que en Go hay un objeto compartido entre goroutines
sin dueño explícito, en el port hay una decisión de lifetime que tomar. Si
el contrato es "acordate de liberarlo al final", tarde o temprano alguien lo
libera antes.

## 7. Cerrar una etapa: inventario de deudas

Antes de declarar cerrada la etapa:

```bash
grep -rhoE 'PORT-TODO\([a-z0-9-]+\)' odin --include=*.odin | sort | uniq -c
grep -rn -E 'not ported \(5\)|stage 5' odin --include=*.odin
```

El primer grep mostró deudas de la etapa 5 (pblind, `avoid_hub`, accessor
de hubs) y **de etapas ya cerradas**: `mgr.State.Data` siempre exportado
como `null` (la UI lee `control:paused` de ahí) y lib/dns imprimiendo CAA,
TLSA, DNSKEY… en forma genérica cuando la caché del resolver serializa con
`String()`. El segundo encontró una docena de hooks del servicio
"prometidos para la etapa 5" que nadie llenaba: sin ellos, el firewall nunca
mandaría una conexión por la SPN.

Clasificación de cada marca: **saldar** (es de esta etapa o rompe algo),
**mover** (depende de algo futuro: observation-hub corre sobre la instancia
completa de la etapa 6), **ACCEPTED con razón** (RSA > 4096: Portmaster no
usa RSA en jess). Al cerrar sólo quedan `stage-6` y `unix`.

## Ejercicios

1. Escribí un proc `#force_inline` que devuelva 4 vectores `#simd[8]u32`,
   compilalo con `-debug` y mirá con un debugger cuántos punteros pasa el
   llamador y cuántos lee el llamado.
2. Tomá un test colgado (o fabricá uno: un lock tomado antes de un
   `panic`) y diagnosticalo con `cdb -pv` sin matarlo.
3. Implementá un protocolo de dos partes y validalo contra una
   implementación ajena con vectores fijos; después cambiá un detalle de
   codificación y verificá que tus tests propios **no** lo detectan.
