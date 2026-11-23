# Módulo 30: Etapa 6 — el programa entero, sin tocar el sistema

Las etapas 1 a 5 portaron paquetes. La etapa 6 los convierte en
**programas**: `portmaster-core` (el servicio que arranca los 41 módulos),
`hub`, `observation-hub`, `updatemgr` y las herramientas de prueba. Es la
etapa más corta en líneas (~4.700 de Go) y la que más decisiones tuvo,
porque un programa real *quiere* tocar la máquina: cargar un driver, poner
permisos de administrador, escuchar en el puerto 53, bajar updates.

Código (fork `port/odin`): `odin/service/{instance,config,debug}.odin`,
`odin/cmds/**`, `odin/lib/{cobra,gotemplate,godiff,govalue}`,
`odin/tests/core_e2e`. Informes: `notas-agentes/etapa6-*.md`,
`httpclient-mtls.md`, `rng-test-tool.md`.

---

## 1. La instancia: cablear 41 módulos y probar que no falta ninguno

`service/instance.go` crea cada módulo en un orden fijo y le pasa la
instancia, que es una interfaz enorme: `Resolver()`, `NetEnv()`,
`Captain()`, ... En el port cada módulo declara un `Instance` struct con los
procs que usa, y la etapa 6 es quien los llena todos.

Con 30 structs y cientos de campos, revisar a ojo no alcanza. El test
recorre cada `Instance` **por reflexión** y falla si queda un proc en nil:

```odin
for f in reflect.struct_fields_zipped(type_of(inst^)) {
	if reflect.is_procedure(f.type) && is_nil(field_ptr(inst, f)) do fail(f.name)
}
```

Es el equivalente al chequeo de tipos que Go te da gratis con interfaces:
en Go no compila si falta un método; en el port falta un hook y nadie se
entera hasta que alguien lo llama.

## 2. Arrancar el programa real sin que toque la máquina

El e2e (`tests/core_e2e`) corre el `portmaster-core.exe` de verdad. Para eso
hubo que listar todo lo que el arranque de Go le hace al sistema y volverlo
inyectable:

| Go al arrancar | En el test |
|---|---|
| carga el driver WFP | `--disable-interception` / kext falso por named pipe |
| nameserver en 127.0.0.1:53 | `--nameserver-address 127.0.0.1:<efímero>` |
| API en 127.0.0.1:817 | `--api-address 127.0.0.1:<efímero>` |
| baja updates de Internet | `config.json` con updates automáticos apagados |
| DNS upstream reales | upstream a un puerto cerrado de loopback |
| notifica al escritorio | `WNOTIFY_PIPE` privado |
| ACL sólo-admin en sus directorios | `--unprivileged` (agregado Odin) |

La última fila fue un hallazgo: Go restringe los directorios de datos a
Administradores. Corriendo como servicio (SYSTEM) no importa; un proceso sin
elevar **se queda afuera de sus propios datos**. Se confirmó en la máquina,
y por eso `--unprivileged` existe.

Y el fix CWE-428 de Go (reescribe el `ImagePath` del servicio en HKLM si no
está entre comillas) sólo corre como servicio real: es la única escritura al
registro, y ningún test la ejecuta.

## 3. Bugs que sólo aparecen cuando todo arranca junto

Con la instancia completa aparecieron cosas que ningún test de paquete veía:

* `rng.stop` y el stop del firewall **esperaban a su propio worker**: el
  grupo corre `stop` como worker del manager y `wait_for_workers` se contaba
  a sí mismo. rng fallaba al minuto; el firewall perdía 5 s al apagar.
* Go: si un módulo falla al arrancar en Windows, el servicio **queda
  colgado** esperando un shutdown que nunca llega.
* Go: los estados del grupo de interception (el self-check de compat)
  **nunca llegaban** a `runtime:system/status`.

Ninguno de los tres es de un paquete: son de la composición.

## 4. Lo que Go trae gratis: portar sólo lo que se usa

Go tiene `cobra`, `text/template`, `r3labs/diff`, `go-keyring`. Odin no. La
regla fue la de siempre: portar **el subconjunto que Portmaster usa**, con
los tests de Go para ese subconjunto. `lib/gotemplate` tiene lexer, parser y
ejecución completos porque el template de observation-hub los usa;
`lib/godiff` sólo diffea structs como hace `observe.go`.

Trampa de coordinación: dos agentes en paralelo necesitaron parsear flags y
cada uno escribió su librería (`lib/cobra` y `lib/cliflags`). Se fusionaron
en la ronda siguiente. Para la próxima, en el brief de las rondas paralelas
hay que decir **quién es dueño de lo compartido**.

## 5. mTLS en Windows: qué acepta SChannel

observation-hub manda sus avisos con certificado de cliente. En Go es
`tls.LoadX509KeyPair`. En Windows:

* una clave CNG **efímera** asociada al certificado: SChannel la rechaza
  (`SEC_E_NO_CREDENTIALS`, WinHTTP 12185);
* la misma clave RSA en un contexto **CAPI** en memoria: funciona, sin
  escribir nada a disco;
* ECDSA: CAPI no la soporta y SChannel sólo la acepta en un **contenedor
  persistido**, o sea en el almacén de claves del usuario. Eso es decisión
  del usuario, no del port.

Efecto lateral útil: el servidor TLS de test de la etapa 4 usa un contenedor
persistido, y si el proceso de test moría antes de limpiar, la clave
quedaba en el almacén del usuario. Ahora cada corrida barre las claves
`pm-tls-test-<pid>-…` cuyo proceso ya no existe: el mismo patrón que los
directorios temporales de tests (dueño muerto = basura; dueño vivo o dudoso
= no se toca).

## 6. Cuando el original no funciona

Dos programas de Go **no podían funcionar** tal como están en el repo:

* `observation-hub`: `NewApprise` devuelve el módulo observer, así que se
  agrega el observer dos veces y apprise nunca: el hub de observación de Go
  jamás manda una notificación.
* `base/rng/test`: `main` vuelve apenas arranca los workers (sale sin
  escribir nada) y nunca arranca el módulo rng.

Ninguno tiene test en Go. Portar línea por línea un programa que no anda da
un programa que no anda: el port tiene que **ejecutar** cada binario al
menos una vez (o explicar por qué no puede, como `winkext-test`, que habla
con el driver real) y escribir el test que Go no tenía.

## Ejercicios

1. Escribí un test por reflexión que falle si algún campo proc de un struct
   de dependencias quedó en nil.
2. Tomá un programa propio y listá todo lo que hace al sistema al arrancar
   (puertos, registro, permisos, red, drivers). ¿Cuáles son inyectables?
3. Buscá en un repo ajeno un `main` que lance trabajo en background y
   vuelva sin esperarlo.
