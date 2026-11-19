# Módulo 24: Compatibilidad binaria con Go — dsd, gencode, SQLite y config.json

Un port por etapas convive con el original: la UI de Portmaster (Angular)
sigue hablando la API de siempre, una base creada por el Portmaster de Go
tiene que abrirse en el port y viceversa, y `config.json` lo editan los dos.
Por eso la regla de esta etapa fue **mismos bytes**, no "mismo significado".

Código: `odin/structures/{varint,container,dsd}`, `odin/base/database/record`,
`odin/base/database/storage/sqlite`, `odin/base/config/persistence.odin`.

---

## 1. dsd: un byte de formato y después los datos

`safing/structures/dsd` ("dynamic structured data") antepone un byte al
payload: `J` (74) JSON, `C` (67) CBOR, `G` (71) GenCode, `M` MsgPack, `Y`
YAML, `Z` GZIP, `L` lista, `1` raw. Todo lo que Portmaster guarda o manda por
la API pasa por ahí.

### Los encoders del core no sirven

| `core:encoding/json` | Go `encoding/json` |
|---|---|
| floats a precisión fija | el más corto que vuelve al mismo valor; exponente desde `1e21` / `1e-6` |
| `[]u8` como array de números | base64 |
| maps en orden de iteración | ordenados por clave |
| sin escape HTML | `<` `>` `&` → `<` `>` `&` |
| rechaza punteros | sigue punteros, `nil` → `null` |

`core:encoding/cbor` achica floats a half/single por defecto y sólo lee el tag
`cbor`. fxamacker (el CBOR de Go) usa la cabecera entera más corta, floats en
`0xfa`/`0xfb`, NaN/Inf como half y toma el tag `json` si no hay `cbor`.

Así que dsd tiene **codecs reflectivos propios** (`json_encode.odin`,
`json_decode.odin`, `cbor.odin`) escritos contra las reglas de los encoders
de Go, sobre `runtime.Type_Info`:

* tags `json:"name,omitempty"` y `"-"`; campos `using` aplanados como el
  embedding de Go;
* `MarshalIndent` reproducido;
* decodificación de claves case-insensitive (como Go), claves desconocidas
  ignoradas, rango de enteros chequeado, u64 completo.

Sin toolchain de Go en la máquina, los vectores de bytes de los tests se
derivaron de las reglas del encoder, no de una corrida real de Go: es la
deuda de verificación de esta etapa (anotada en `STAGES.md`).

CBOR de maps: Go los escribe en orden aleatorio, el port ordenado. La igualdad
byte a byte de maps es imposible en ambos sentidos; decodificar no cambia.

### GZIP sin compresor

`core:compress` descomprime gzip pero **no tiene encoder deflate**. El port
escribe un gzip válido con bloques deflate *stored* (sin comprimir):
cualquier lector, Go incluido, lo abre; los bytes difieren. PORT-TODO:
deflate real.

## 2. Meta gencode: 34 bytes exactos

Cada record lleva su `Meta` (Created, Modified, Expires, Deleted + flags) en
formato GenCode, little-endian, **34 bytes**. El sobre completo:

```
01                versión del formato de record
23                varint(35): largo de la sección meta
47 <34 bytes>     'G' + meta gencode
4a {...}          'J' + datos JSON del record
```

Fijado por tests contra los bytes que produce Go. El codec se registra en dsd
desde un `@(init)`.

`varint`: `Unpack8` de Go devuelve `read=1` para la forma de 2 bytes (bug).
El port devuelve 2; no cambia ningún archivo porque todos los bytes de
formato/versión son < 128.

## 3. SQLite: el mismo archivo

El backend `sqlite` de Go usa el ORM `bob`, `sql-migrate` y modelos
generados. El port va directo por un binding propio (`odin/vendor/sqlite3`,
contra `sqlite3.lib` de vcpkg) y conserva:

* ruta `<location>/db.sqlite` y esquema de la tabla `records`;
* el SQL de las migraciones, **cargado del árbol Go** con `#load`;
* la tabla `gorp_migrations` con los mismos ids (`0_settings.sql`,
  `1_initial.sql`): un archivo creado por Go se reconoce como migrado;
* el upsert **byte-idéntico** al que genera bob (hay un test que compara el
  texto SQL).

Badger y bbolt (motores de base de datos enteros escritos en Go) no se
portean: una base declarada `bbolt`/`badger` se sirve con sqlite en
`<root>/<name>/sqlite`. El importador de datos bbolt existentes está
planificado en `storage/REPLACED.md`.

`SQLITE_TRANSIENT` es `(sqlite3_destructor_type)-1` en C; Odin no puede formar
una constante proc desde -1 → el parámetro se declara `uintptr` y
`TRANSIENT :: ~uintptr(0)`.

## 4. config.json

`config.json` lo escriben Go y el port con `MarshalIndent`: mismo orden,
misma indentación, el mismo `<`. El test de round trip compara bytes. El
quirk de Go en `updateExpertiseLevel`/`updateReleaseLevel` (el default activo
gana sobre el valor del usuario) se conserva a propósito.

## 5. Regla para la siguiente etapa

1. Antes de portear un formato, buscar sus tests en Go y portarlos **primero**:
   son la especificación.
2. Si el core de Odin tiene un encoder, comparar sus defaults con los de Go
   antes de usarlo. En JSON, CBOR y floats, no coinciden.
3. Cuando un bug de Go afecta bytes en disco, conservarlo y documentarlo; si
   sólo afecta comportamiento, corregirlo con `NOTE` en el código.
4. Capturar vectores reales de Go en cuanto haya un toolchain (pendiente).

---

Anterior: [23](23-concurrencia-sin-goroutines.md) · Siguiente: [25 — Notificaciones que no se olvidan](25-notificaciones-persistentes-portmaster-notifyd.md).
