# SPN pblind: port fiel de rot256/pblind

Fork: `C:\Users\Shifty\DEV\gitclones\portmaster`, rama `port/odin`. No hice commit ni push.

## Licencia

`rot256/pblind` usa **GPL-3.0** (`LICENSE.txt` en el repo, commit `8244c3966ed3`). Portmaster también es GPL-3.0, así que se puede portar. La licencia y la atribución quedaron en el encabezado de `pblind/pblind.odin`.

Leí el código fuente con WebFetch, archivo por archivo, desde `raw.githubusercontent.com/rot256/pblind/8244c3966ed3/`: keys, info, misc, signer, requester, validate, messages, errors, interaction_test y README. No cloné el repo ni bajé ningún archivo comprimido.

## Archivos cambiados (`odin/spn/access/token/`)

- `pblind/pblind.odin`: encabezado nuevo, curvas, claves, helpers genéricos P-256/P-384.
- `pblind/hash.odin`: reescrito. Incluye el stream HKDF, hashToScalar, hashToPoint, Marshal y el workaround de sqrt P-384.
- `pblind/protocol.odin`: reescrito (mensajes, estados, Check).
- `pblind/pblind_test.odin`: port de TestInteraction más los KAT.
- `pblind.odin` (handler): soporta P-384. P-521 se rechaza con un comentario, sin PORT-TODO.
- `wire.odin`: forma CBOR de Message1 y Message3.
- `pblind_test.odin`: tests de wire nuevos, test P-384 y casos de curva.
- `errors.odin`: comentario de `Unsupported_Curve`.

No queda ningún `PORT-TODO(stage-5)` en access, token, captain ni e2e.

## Diferencias entre la reconstrucción y rot256

| Pieza | Antes (reconstrucción) | Ahora (Go real) |
|---|---|---|
| CompressInfo | SHA-256(`"pblind/info"` ‖ ctr ‖ info), con y par | HKDF-SHA512(secret=info, salt=`"P-256"`/`"P-384"`, info=`"POINT-HASHING"`) leído como stream. `x = rand.Int(stream, P)` con rechazo si x ≥ p. `y = (x³−3x+b)^((p+1)/4)` (ModSqrt). Si no es cuadrado, toma el siguiente x. |
| Hash del desafío | SHA-512(`"pblind/challenge"` ‖ puntos comprimidos ‖ msg) mod n | hashToScalar = el mismo HKDF con info `"SCALAR-HASHING"`, y `rand.Int(stream, N)` (rechazo, sin reducción mod n). La entrada es `Marshal(α) ‖ Marshal(β) ‖ Marshal(Z) ‖ msg`, con los puntos **sin comprimir** (`04‖X‖Y`), sin prefijo de dominio ni longitudes. |
| Message1 | `{A, B}`: bytes SEC1 comprimidos | `{Ax, Ay, Bx, By}`: cuatro `*big.Int` (coordenadas afines) |
| Message3 | `{R, C, S, D}` | `{R, C, S}`. El requester calcula `d = e − C`. |
| ProcessMessage3 | Verificaba `c+d=e` y `a = rG+cY`, `b = sG+dZ` (devolvía `Invalid_Response`) | Rango de R, C y S, después desciega y llama a `PublicKey.Check`. Si falla, devuelve `ErrorInvalidSignature`. |
| Momento del azar | u, s, d en msg1; t1..t4 en msg2 | Se generan en `CreateSigner` y `CreateRequester`, como en Go. |
| PublicKeyFromBytes | Aceptaba SEC1 comprimido y sin comprimir | Solo comprimido (`UnmarshalCompressed`). |
| Check | Rechazaba α o β en el infinito | Igual: Go entra en pánico con `Marshal` y acá devolvemos `false`. La comparación es `W+G == H(...)`. |
| Curva | Solo P-256 | P-256 y P-384 |
| Mensajes y firma | `[32]u8` | `Big = [48]u8` big-endian con padding, uno por cada `*big.Int` |

Coincidían ya desde antes: el álgebra (A = uG, B = dZ + sG, c = e − d, r = u − c·x, ρ/ω/σ/δ), los nombres de campo de la firma (P, W, O, G), `E` y el formato de las claves.

Diferencias deliberadas que quedan (no se ven en el wire con entradas válidas):

- `SecretKeyFromBytes` rechaza claves vacías, de más de byteLen, cero o ≥ n.
- Los escalares aleatorios salen de [1, n) en vez de [0, n).
- Donde Go entra en pánico (punto en el infinito, HKDF agotado), acá se devuelve un error.

### Hallazgo: bug en core:crypto

En Odin dev-2026-10, `core:crypto/_fiat/field_p384r1.fe_sqrt` está roto:

- La cadena de suma calcula x^(p−3) en vez de x^((p+1)/4). El exponente del paso 395 termina en `…3fffffff`, cuando debería terminar en `…40000000`, y además se aplican dos cuadrados de más.
- Falla con cualquier entrada; por ejemplo, sqrt(4) devuelve inválido.
- Por eso `_weierstrass.pt_set_sec_bytes` no puede descomprimir ningún punto P-384.

Workaround: `field_sqrt` en `hash.odin`, que hace square-and-multiply con el exponente fijo, solo para P-384 y solo sobre datos públicos. Para P-256 se sigue usando `fe_sqrt` de core, que está bien. No toqué core.

## Curvas

- **P-256**: soportada. Es la que usan Safing (`pblind1`) y nuestras zonas.
- **P-384**: soportada en la librería y en el handler (`curve_name = "P-384"`).
- **P-521**: rechazada con `Unsupported_Curve`, porque `core:crypto/_weierstrass` solo tiene P-256 y P-384 (no hay campo ni escalar P-521). Ninguna zona de Safing la usa.
- **P-224**: aparece en el test de Go, pero tampoco está en core y Portmaster no la nombra (da `No_Curve`).

## Tests (`-vet`, desde la raíz del fork, 3 corridas cada uno)

| Paquete | #1 | #2 | #3 | Nota |
|---|---|---|---|---|
| `spn/access/token/pblind` (10 tests) | ok | ok | ok | |
| `spn/access/token` (21 tests) | ok | ok | ok | |
| `spn/access` (14 tests) | ok | ok | ok | |
| `spn/captain` (6 tests) | ok | segfault | ok | Ver abajo |
| `tests/spn_e2e` | ok (46 s) | ok (43 s) | ok (43 s) | |
| `crosscheck.sh` windows_amd64 / linux_amd64 / darwin_arm64 (pblind, token, access) | ok | ok | ok | 0 fallas |

Sobre `spn/captain`:

- En 9 corridas en el árbol compartido hubo 2 caídas: un segfault (rc 139) y otra corrida que no imprimió "Finished".
- Mientras tanto, otros agentes estaban editando crew, navigator, hub, sluice y jess, que captain importa.
- Armé una copia aislada en el scratchpad: HEAD más **solo** mis cambios de token. Ahí corrió 8 de 8 en verde, lo que indica que las caídas no vienen de pblind.
- Las últimas 3 corridas en el fork dieron 3 de 3 en verde.

Vectores de prueba:

- **Del Go real**: el par de claves de `pblind_test.go` (`HbwG…` → `285o…`), que fija `SecretKey.Bytes` y `PublicKey.Bytes`. También el parseo de la clave real de `pblind1`. `test_interaction` reproduce la matriz de `interaction_test.go` (curvas × mensajes × infos), sin salidas esperadas, igual que Go.
- **KAT derivados a mano (no salen de Go)**: CompressInfo (5 infos × 2 curvas, incluida la info de Portmaster `"\x07pblind1\x01"`), hashToScalar (3 × 2) y una corrida completa determinística por curva (Message1, E, Message3 y firma, byte por byte).
  - Los calculé con una transcripción independiente en Python del código Go: math/big, HKDF y `crypto/rand.Int` escritos a mano, con aritmética EC afín propia.
  - El script está en `C:\Users\Shifty\AppData\Local\Temp\claude\bb82ec514398ee79\43a5acfc-3b5c-4055-8fb0-9462027dda5f\scratchpad\pblind_ref.py`. Es efímero; copiarlo si se quiere conservar.
  - Odin coincide con todos.
- **Wire**: vectores CBOR de Message1 y Message3 según las reglas de fxamacker (BigIntConvertShortest).

## Qué no se puede verificar sin correr Go

- Que los KAT coincidan con la salida real de Go. Coinciden con mi lectura del código fuente, no con una ejecución.
- La semántica de `crypto/rand.Int` con un reader propio en la versión de Go que compila Portmaster. Si alguna versión futura lo cambiara (como hizo Go con el parámetro `random` de otras APIs), hashToScalar y hashToPoint cambiarían.
- El stream de `x/crypto/hkdf` en la versión pinneada. Se asume el HKDF estándar RFC 5869.
- La serialización exacta de fxamacker para Message1/2/3 dentro de `dsd` (orden de campos, bignum o entero). Está modelada en `cbor.odin`, pero no está contrastada contra bytes reales de Go.
- La interoperabilidad de punta a punta con el emisor real `pblind1` de Safing: no hay tokens ni capturas de Go disponibles.
