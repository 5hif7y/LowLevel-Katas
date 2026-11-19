# Módulo 22: Go → Odin, el mapa del lenguaje

Portmaster tiene ~117.000 líneas de Go. Este módulo y los dos siguientes salen
de portear su base (`base/`, `safing/structures`, `service/mgr`: ~25.000 líneas
de Go → ~30.000 de Odin, ~170 tests) en el fork `safing/portmaster`, rama
`port/odin`, directorio `odin/`. Convenciones del port: `odin/PORTING.md`;
mapa de etapas: `odin/STAGES.md`.

Go y Odin se parecen en la superficie (sintaxis de C sin punto y coma,
`defer`, slices, paquetes por directorio) y en el fondo son opuestos: Go tiene
GC, runtime con scheduler, interfaces dinámicas y closures; Odin no tiene
nada de eso. El trabajo es decidir **qué reemplaza a cada cosa**.

---

## 1. Errores

```go
var ErrNotFound = errors.New("database entry not found")
func Get(key string) (Record, error)
return fmt.Errorf("failed to open %s: %w", path, err)
```

```odin
Error_Kind :: enum { None, Not_Found, Permission_Denied, Read_Only, Shutting_Down }
Error_Msg  :: struct { code: Error_Kind, len: u8, buf: [255]u8 } // sin alocar, cruza hilos
Error      :: union { Error_Kind, Error_Msg }

get :: proc(db: ^Interface, key: string) -> (r: ^record.Record, err: Error)
```

* Sentinelas → miembros de un `enum`. `errors.Is(err, ErrNotFound)` →
  `is_error(err, .Not_Found)`.
* `%w` → el texto va a un `Error_Msg` con buffer **inline**. Un `string`
  alocado obliga a decidir quién lo libera en cada `return`; el buffer fijo
  no. (Primero fue de 200 bytes: cortaba rutas largas de Windows antes de la
  causa real. Ahora 255, el máximo que entra en el `u8` de largo.)
* Las uniones de error de distintos paquetes **no son intercambiables**: hacen
  falta conversores (`from_storage_error`, `to_storage_error`) que preserven
  el código.

### Trampas

| Go | Lo que rompe en Odin | Arreglo |
|---|---|---|
| `if err != nil` | `Error(Ctx_Error.None)` **no es nil**: es la variante enum con valor 0 | helpers `ctx_error_to_error` que devuelven `nil` para `.None`; o `union #shared_nil` |
| `return os.ErrX` envuelto | `Error(os.Error(nil))` tampoco es nil | `from_os_error` mapea nil→nil |
| `x, err := f(); if err != nil { return }` | `or_return` en un proc con varios resultados exige **resultados nombrados** | nombrarlos; `_ = f() or_return` si sobra el valor |
| cleanup en `defer` que pone `q = nil` | con `return q, err` explícito el valor ya fue copiado; el caller recibe el puntero recién liberado | proc envoltorio para el cleanup, o locales en vez de resultados nombrados |

## 2. Interfaces → vtables

`record.Record`, `storage.Interface`, `accessor.Accessor` y los módulos de
`mgr` son interfaces en Go. En Odin:

```odin
Record_VTable :: struct {
	marshal:      proc(r: ^Record, format: dsd.Format, allocator: mem.Allocator) -> ([]u8, Error),
	get_accessor: proc(r: ^Record) -> (accessor.Accessor, bool),
	is_wrapped:   proc(r: ^Record) -> bool,
	destroy:      proc(r: ^Record),
}

Base :: struct { _vtable: ^Record_VTable, _type: typeid, _key: string, _meta: ^Meta, _refs: i32, ... }

Notification :: struct {
	using base: record.Base,   // SIEMPRE el primer campo: ^Notification ↔ ^Record
	title:   string `json:"Title"`,
	...
}
#assert(offset_of(Notification, base) == 0)
```

* El "embedding" de Go (`record.Base` dentro de cada record) es `using base:`
  como primer campo; un `#assert` lo garantiza para que el cast
  `^Record`→`^T` sea válido.
* **Type assertions** (`if m, ok := s.(Maintainer); ok`) → entradas
  opcionales de la vtable que valen `nil` (`is_maintainer(s)`).
* `using` dos veces el mismo nombre (`using base:` en un struct que ya
  contiene un `Inject_Base` con su propio `using base`) da
  "already declared": renombrar (`using inject:`).

## 3. `interface{}` → uniones cerradas

```odin
Value :: union { string, i64, f64, bool, []string }           // accessor
Value :: union { bool, i64, f64, string, []string, []Value, Value_Map } // config
```

* El literal sin tipo `2` va a la variante `i64` aunque también exista `f64`.
  Una **variable** `int` no convierte sola: `i64(i)`.
* `[]Value(nil)` no parsea ("not a type"): `([]Value)(nil)`.
* `reflect.DeepEqual` no existe y `==` no está definido para structs con
  slices o punteros: comparadores escritos a mano.

## 4. Closures → proc + `rawptr`

```go
config.Concurrent.GetAsBool("core/useSystemNotifications", true) // devuelve func() bool
module.mgr.Go("action", func(w *mgr.WorkerCtx) error { return n.actionFunction(w.Ctx(), n) })
```

```odin
use_system := config.concurrent_get_as_bool("core/useSystemNotifications", true) // struct
if config.get(&use_system) { ... }

mgr.manager_go(m, "action", proc(w: ^mgr.Worker_Ctx) -> mgr.Error {
	n := mgr.worker_data(w, Notification)
	return n.action_function(mgr.worker_ctx(w), n)
}, n)
```

* El getter que devuelve un closure se vuelve un struct paramétrico
  `Typed_Option($T)` con `when T == bool { ... }` para elegir el campo de caché.
* Lo que el closure capturaba viaja en `data: rawptr`. Un proc anidado dentro
  de un proc genérico ve `T` en compilación: así se hicieron los callbacks de
  `Event_Mgr($T)`.

## 5. Nombres que Odin no deja usar

| Go | Problema | Odin |
|---|---|---|
| `New()`, `Delete()` | un proc de paquete llamado `new`/`delete` **sombrea el builtin** en todo el paquete | `create`, `new_container`, `delete_key`, `meta_delete` |
| `Where`, `Do` | `where` y `do` son keywords | `where_`, `manager_do` |
| `func Query()` en un paquete que importa `query` | choca con el import | `query_records` |
| campo `mgr *mgr.Manager` | "Illegal declaration cycle of `mgr`" | campo `m` |
| `package log`, `package runtime`, `package container` | los nombres de paquete son **únicos por programa**; `core:testing` importa `core:log`, todo programa tiene `base:runtime` | `package pm_log`, `pm_runtime`, `base_container`; el import sigue llamándose por el directorio: `import "pm:base/log"` → `log.infof` |

## 6. Memoria: el GC que no está

* Regla del port: objetos de larga vida en el heap con dueño explícito
  (`destroy_x`); temporales de una llamada en `context.temp_allocator`,
  vaciado por cada worker al terminar una tarea.
* **Slice literal dentro de un proc vive en el stack.** `Register(&Option{
  PossibleValues: []string{...}})` portado literal deja punteros colgando:
  `register` clona todo al heap y devuelve el `^Option` nuevo.
* `&T{...}` es la dirección de un temporal del stack, igual de peligroso.
* Map literals están deshabilitados por defecto (`#+feature dynamic-literals`):
  `init_annotations: []Annotation` que `register` convierte en map.
* Records que en Go comparten la caché, los feeds de suscripción y el
  hashmap gracias al GC → **refcount atómico** en `record.Base`
  (`record.retain`, `record.destroy` libera una referencia). Ver módulo 23.
* No modificar un `map` mientras se itera (Go lo permite para `delete`):
  juntar claves, borrar después.
* Maps anidados: en Go se mutan a través del padre; en Odin son valores →
  `vp := &m^[part]; sub := &vp.(Value_Map)`.

## 7. Detalles que cuestan una tarde

* `sync.atomic_add` devuelve el valor **viejo**; `atomic.AddInt32` de Go el
  **nuevo**. `if Add(&n, -1) == 0` → `== 1`.
* Constante string indexada con índice variable no compila
  (`HEX :: "01..."; HEX[i]`): hacerla variable.
* No hay `cap()` para slices.
* `strconv` con formato `'g'`/`'f', -1` antepone `+` a los positivos.
* `strconv.parse_i64` acepta `_` y no detecta overflow: parser estricto propio
  para que los mensajes de error coincidan con los tests de Go.
* `@(init)` debe ser `proc "contextless"`: dentro, `context =
  runtime.default_context()` y alocar con el heap allocator (el del test
  runner lo reportaría como leak).
* `#+private` (privado al archivo) va en la **primera línea**;
  `@(private)` antes de `package` no hace eso.
* `-vet` rechaza sombrear (`format, ok := validate(format)`).
* `odin check` también chequea los `*_test.odin`; procs genéricos nunca
  instanciados **no** se chequean: sólo los tests que los instancian
  encuentran sus errores.
* `core:testing` no tiene `errorf`: `testing.expectf(t, false, ...)`.
* `odin test` borra el exe al terminar: `-keep-executable`.

## 8. Herramientas: escapes que se pierden

Escribiendo fuente Odin con heredocs de bash o con algunas herramientas de
edición, `"\\"` llegó como `"\"`, `'\r'` como un CR crudo y `<` como `<`
literal dentro de un test de JSON. Para fuentes con escapes: editor real o
construir el escape en código (`BACKSLASH :: u8(92)`).

---

Siguiente: [23 — Concurrencia sin goroutines](23-concurrencia-sin-goroutines.md).
