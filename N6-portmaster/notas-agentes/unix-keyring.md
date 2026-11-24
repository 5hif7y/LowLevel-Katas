# Unix: jess/truststores — KeyringTrustStore sobre Secret Service (D-Bus) — informe

**Archivos** (`odin/jess/truststores/`): nuevo `keyring_secret_service.odin` (`#+build !windows`, port de go-keyring v0.2.8 `keyring_unix.go` + `secret_service/secret_service.go`); nuevo `keyring_secret_service_test.odin` (`#+build !windows`, Secret Service falso sobre `pm:lib/dbus/dbustest`); reescrito `keyring_other.odin` (`system_keyring_backend`: Secret Service en Linux/BSD, no soportado en macOS con PORT-TODO(darwin) preciso); `keyring.odin`: comentario de cabecera + campo `Keyring_Result.message` (texto de un `.Failed` sin código de SO; `keyring_error_string` lo devuelve). Windows sin cambios (`keyring_windows*.odin`). `lib/dbus` no se tocó.

**Atención (git):** el commit `79a21776` (spn/IVPN, de otro agente) se llevó por delante `keyring.odin` y `keyring_other.odin` con su contenido actual (correcto). Los dos archivos nuevos siguen sin commitear (`??`).

**Portado.**
- `Secret_Service_Backend {using backend: Keyring_Backend, address, call_timeout, prompt_timeout}`; `new_secret_service_backend(address)` ("" = bus de sesión) / `destroy_secret_service_backend`; el del sistema es un global con `prompt_timeout = -1` (Go espera sin límite).
- Set: OpenSession("plain", `<"">`) → colección (`GetLoginCollection`: "login" si `Collections` la lista; ante cualquier error o ausencia, alias `/org/freedesktop/secrets/aliases/default`) → Unlock → `Collection.CreateItem({Item.Label: "Password for '<user>' on '<service>'", Item.Attributes: {username, service}}, (session, "", pw, "text/plain; charset=utf8"), true)` → prompt de CreateItem → `Session.Close`.
- Get: findItem (Unlock colección + `Collection.SearchItems` a{ss} → ao; vacío → `.Not_Found`) → OpenSession → Unlock(item) → `Item.GetSecret(session)` (oayays, campo 2) → Close. Delete: findItem → `Item.Delete` → prompt.
- Unlock = `Service.Unlock(ao)` → (ao, o); prompt ≠ "/" ⇒ `subscribe({path = prompt, interface = Prompt})` ANTES de `Prompt("")`, espera `Completed(b, v)`, suma los paths del resultado; exige exactamente 1 desbloqueado y (si no es el alias) igual al pedido; si no: `failed to unlock correct collection '<path>'` (un prompt descartado termina ahí: Go ignora `dismissed`).
- Errores: sólo "sin resultados" es ErrNotFound; todo error D-Bus pasa como `.Failed` con el texto de godbus (mensaje, o nombre si no hay) vía `dbus.error_string`; tipos inesperados → "dbus.Store: type mismatch". Sin límite de tamaño en unix (go-keyring tampoco).

**PORT-TODO(darwin)** (`keyring_other.odin`): `/usr/bin/security` (add-generic-password -U por stdin de `security -i` con "go-keyring-base64:", límite 4096 del comando, find-generic-password -wa con prefijos base64/hex, delete-generic-password, "could not be found" → ErrNotFound). Requiere proceso hijo con stdin y el llavero real para verificarlo; sin fake posible. Hasta entonces `.Unsupported`. No queda PORT-TODO(unix).

**Hooks.** Ninguno nuevo. `new_secret_service_backend(address)` es el punto de inyección para tests (no hay estado global mutable).

**Diferencias deliberadas con Go.** (1) godbus `SessionBus()` es compartida; acá cada operación abre y cierra su conexión privada (compartir una exigiría refcount para sobrevivir a un reinicio del bus; comportamiento observable igual, y se reconecta sola). (2) `handlePrompt` de Go vuelve con la primera señal de la conexión que no sea Completed (como si el prompt terminara sin resultado → unlock fallido); acá se ignoran otras señales del objeto prompt y se sigue esperando Completed. (3) Llamadas con timeout de 25 s (godbus: sin timeout); el prompt espera sin límite como Go.

**Tests** (`-vet`; 3 corridas cada uno):
- Linux (`lt.sh`, `PM_MIRROR=/root/pm/agent-keyring`): jess/truststores 10/10 ×3; jess 14/14 ×3.
- Windows: jess/truststores 4/4 ×3 (incluye Credential Manager real); jess 14/14 ×3.
- `VET=1 bash odin/tests/crosscheck.sh windows_amd64 linux_amd64 darwin_arm64 -- odin/jess/truststores odin/cmds/updatemgr` → 0 fallas.
- Secret Service falso: sesiones (plain + input "" validados, cerradas al final), colección login / sólo alias (Collections ausente → UnknownMethod → alias; Unlock del alias devuelve el path real), ítems con replace=true, `keyring_round_trip` completo (el mismo de memoria y wincred) sobre login y sobre alias, secuencia EXACTA de llamadas de Set/Get/Delete, atributos/label/content-type almacenados, contraseña con UTF-8, NUL y 0xFF, colección bloqueada con Prompt (Completed antes y después de la respuesta de Prompt, con una señal espuria previa; AddMatch antes de Prompt y RemoveMatch después), prompt descartado (texto exacto de Go, sin ítem creado, `new_keyring_trust_store` falla con ese texto), not-found en Get/Delete, error remoto con y sin mensaje, bus reiniciado (`disconnect_clients`) → reconecta, sin bus → "dbus: failed to connect…".
- Opcional `-define:PM_KEYRING_LIVE=true`: bus de sesión y Secret Service reales con servicio descartable `pm-odin-unittest-<rand>`, borra todo lo escrito. Apagado por defecto; no se corrió (WSL no tiene Secret Service).

**Bugs de Go.** Conservados: `dismissed` ignorado (falla después como "failed to unlock correct collection"); `CheckCollectionPath` cae al alias ante cualquier error (no sólo "path not found"). Corregido: handlePrompt que se rinde ante cualquier señal ajena (ver diferencias).

**Trampas.** (1) `copy([]u8, Byte_Array)` no compila (distinct): `([]u8)(v)`. (2) `when !CONST { return } else {...}` deja imports "sin uso" para -vet si el uso está en la rama muerta: usar `if !CONST { return }` (sigue tipándose). (3) `SearchItems` de go-keyring es el de la **Collection** (devuelve sólo `ao`), no el del Service `(ao, ao)` que menciona la nota de dbus. (4) El handler del bus falso puede emitir la señal Completed antes de devolver la respuesta de Prompt: sólo funciona porque la suscripción (cola) se registra antes del AddMatch/Prompt. (5) El dato que un hilo emisor del fake lee debe tomarse bajo el lock del fake y liberarse después del join.

Sin commit (los dos archivos nuevos).
