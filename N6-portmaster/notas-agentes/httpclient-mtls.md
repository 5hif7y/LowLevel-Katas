# httpclient: certificados de cliente (mTLS) para el apprise de observation-hub

Fork `DEV/gitclones/portmaster`, rama `port/odin`. No hice commit ni push.

## Qué quedó

- **`lib/tls/keypair.odin`** (portable): `load_x509_key_pair(cert_file, key_file)` y `x509_key_pair(cert_pem, key_pem)`, equivalentes a `tls.LoadX509KeyPair` / `tls.X509KeyPair`. Incluye un decodificador PEM con la semántica de `pem.Decode` (saltea los bloques mal formados y los headers RFC 1421) y clasifica la clave como `parsePrivateKey`: primero PKCS#1, después PKCS#8 (RSA/EC/Ed25519) y por último SEC1. Los textos de error son los de Go: `tls: failed to find any PEM data in …`, `…PEM inputs may have been switched`, `…after skipping PEM blocks of the following types: [A B]`, `tls: found a certificate rather than a key…`, `x509: malformed certificate`, `tls: failed to parse private key`, `tls: private key type does not match public key type`, `tls: private key does not match public key` y `open <f>: The system cannot find the file specified.`. Devuelve un `^Certificate` que se libera con `certificate_destroy`.
- **`lib/tls/keypair_windows.odin`**:
  - La cadena va a un store **en memoria** (`CERT_STORE_PROV_MEMORY`).
  - La clave se importa como clave CNG **efímera** (`NCryptImportKey PKCS8_PRIVATEKEY`, sin nombre) solo para validarla y comparar la clave pública con la del cert. Después se libera.
  - Para usarla en el handshake, las RSA se reimportan en un contexto CAPI `CRYPT_VERIFYCONTEXT`, que es efímero y no deja contenedor, y se asocian con `CERT_KEY_CONTEXT_PROP_ID`. El contexto del cert pasa a ser dueño de esa clave.
  - Acá también está `create_test_key_pair_pem` para los tests: genera la clave efímera exportable, el cert autofirmado y el PEM en los 4 formatos.
- **`tls_other.odin`**: stubs que devuelven "not supported", con `PORT-TODO(unix)` (OpenSSL: d2i_X509 / d2i_AutoPrivateKey / X509_check_private_key).
- **lib/tls, cliente**: `Config.certificate` (Go `Certificates[0]`) se pasa a `AcquireCredentialsHandle`.
- **lib/tls, servidor de test**: `server_credentials(..., require_client_cert = true)` agrega `ASC_REQ_MUTUAL_AUTH` + `SCH_CRED_NO_SYSTEM_MAPPER`. Si el cliente no presenta cert, `accept` falla. Se agregó `https_server_windows.odin`, un servidor HTTPS loopback de test que registra la request y el SHA-256 del cert del cliente.
- **lib/httpclient**:
  - `Client.client_certificate: ^tls.Certificate` → `WINHTTP_OPTION_CLIENT_CERT_CONTEXT` en cada request.
  - `Client.insecure_skip_verify` (solo tests) → `WINHTTP_OPTION_SECURITY_FLAGS`.
  - Unix: el stub ya devolvía `.Not_Supported` y no cambió.
- **observation-hub**:
  - `-apprise-client-cert/-key` funcionan como en Go: si falla la carga, `failed to load client cert/key: <texto de Go>`; si no, un `httpclient.Client{timeout = 10s, client_certificate}` que se pasa a `apprise.set_client`.
  - Saqué el `PORT-TODO(stage-6)`.
  - base/apprise no necesitó cambios de código (`set_client` ya existía), solo un test nuevo.

## Hallazgo clave (medido)

SChannel firma el CertificateVerify del cliente desde LSASS, y LSASS **no ve una clave CNG efímera** del proceso. Lo probé en Windows 10 22H2:

| Cómo se asocia la clave CNG efímera | Resultado |
|---|---|
| Con `CERT_KEY_CONTEXT` o `CERT_NCRYPT_KEY_HANDLE_PROP_ID`, cliente de lib/tls | `AcquireCredentialsHandle` → `SEC_E_NO_CREDENTIALS` |
| Lo mismo, cliente WinHTTP | `12185 ERROR_WINHTTP_CLIENT_CERT_NO_PRIVATE_KEY` |
| Con `CERT_NCRYPT_KEY_HANDLE_TRANSFER_PROP_ID` | El proceso se cuelga comiendo memoria o segfaultea |

La clave RSA en un `CRYPT_VERIFYCONTEXT` de CAPI, en cambio, anda con WinHTTP y con el cliente de lib/tls.

- **ECDSA:** CAPI no tiene ECDSA, y la única vía que SChannel acepta es un contenedor CNG **persistido**. Como eso quedaría en `%APPDATA%\Microsoft\Crypto\Keys` en un uso de producción, no lo hice. Las claves EC se parsean y validan, pero la carga falla con `tls: ECDSA client certificates are not supported yet (Odin port: SChannel cannot use an ephemeral ECDSA key)`. Quedó como `PORT-TODO(stage-6)` y anotado en STAGES.md. **Decisión pendiente para el orquestador:** ¿se acepta un contenedor con nombre único que se borre en `certificate_destroy`?
- **Riesgo con TLS 1.3:** una clave CAPI no hace RSA-PSS. Contra un servidor que solo hable TLS 1.3 (cliente Win11) podría fallar. En Win10, WinHTTP negocia TLS 1.2.

## Tests

- **lib/tls** (`keypair_test.odin`):
  - mTLS con RSA PKCS#1 y PKCS#8: el servidor recibe el cert correcto (comparado por SHA-256).
  - EC PKCS#8 y SEC1: el texto de "not supported".
  - Sin cert: el servidor rechaza la conexión y no llega ninguna request.
  - 12 casos de error con texto Go, una cadena con bloques extra, y lectura de archivos: archivo faltante y carga correcta.
- **httpclient** (`mtls_windows_test.odin`):
  - Con cert: 200 y el servidor vio el cert.
  - Sin cert: `.TLS_Failed` (WinHTTP 12044) y ninguna request.
  - Servidor que no pide cert: anda con el cert configurado.
- **base/apprise** (`notify_windows_test.odin`): `send` con y sin cert contra el servidor mTLS.
- **observation-hub** (`apprise_windows_test.odin` + `observe_test`):
  - Archivo faltante, clave de otro cert, flag de clave vacío.
  - El par correcto: la request POST llega con el cert. Sin cert, no llega.

Todos los PEM se generan en el test, en un temp dir que después se borra.

**Verificación:**
- 3× en foreground con `odin test -vet`: lib/httpclient 14, lib/tls 16, base/apprise 4, observation-hub 9, service/updates 15. Todo OK las tres veces y sin procesos colgados.
- `VET=1 crosscheck.sh windows_amd64 linux_amd64 darwin_arm64` sobre los 4 dirs tocados: 0 fallas.
- `odin check -vet` de todos los paquetes que importan lib/tls o lib/httpclient: OK.

## Stores y claves

- `Cert:\CurrentUser\My` tiene un solo cert, `CN=localhost` del 11/8/2026, que ya estaba antes. En Root, CA y TrustedPeople no entró nada nuevo. No se escribió ningún archivo en `Crypto\RSA` (CAPI).
- **Ojo:** durante los experimentos maté por PID un test colgado y dos corridas segfaultearon. Eso dejó 5 contenedores `pm-tls-test-<pid>-…` del servidor de test de lib/tls, que persiste su clave. Verifiqué que esos PIDs ya no existían y los borré con `certutil -csp "Microsoft Software Key Storage Provider" -delkey -user`. Ahora `certutil -key -user | grep pm-tls-test` da 0.
- **Lección:** el servidor de test de lib/tls filtra su contenedor si el proceso muere antes de `self_signed_destroy`. Podría agregarse una limpieza de los `pm-tls-test-<pid>` cuyo PID ya no esté vivo.

## Archivos

- **Nuevos:**
  - `odin/lib/tls/{keypair.odin, keypair_windows.odin, keypair_test.odin, https_server_windows.odin}`
  - `odin/lib/httpclient/mtls_windows_test.odin`
  - `odin/base/apprise/notify_windows_test.odin`
  - `odin/cmds/observation-hub/apprise_windows_test.odin`
- **Modificados:**
  - `odin/lib/tls/{tls.odin, tls_windows.odin, tls_other.odin, server_windows.odin, sspi_windows.odin}`
  - `odin/lib/httpclient/{client.odin, client_windows.odin, winhttp_windows.odin}`
  - `odin/cmds/observation-hub/{apprise.odin, observe_test.odin}`
  - `odin/STAGES.md`
