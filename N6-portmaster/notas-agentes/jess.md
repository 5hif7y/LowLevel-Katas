# safing/jess v0.3.5 → odin/jess (~13.900 líneas, 13 paquetes, 38 tests)

Verificado contra salidas de Go: vectores de filesig (checksums JSON byte a byte, firmas JSON simples y múltiples, armor firmado re-codificado a los mismos bytes CBOR y MsgPack), las 4 claves de firma reales de Portmaster (service/configure/updates.go) cargan y re-exportan idénticas, verificadores de zonas SPN parsean como lhash y el token unittest hashea a su verificador, flujo de signature_test de updates. Vectores oficiales BLAKE3, scrypt RFC 7914 (N=2^20 contra Python), Salsa20, HKDF RFC 5869 y sobre BLAKE2b, clave RSA de OpenSSL (DER byte-idéntico, PSS, OAEP), ECDH P-224/256/384/521 contra OpenSSL. Round trips de las 14 suites y del protocolo wire (803 mensajes, rekey cada 100).

Implementado a mano porque core:crypto no lo tiene: BLAKE3, scrypt, Salsa20, SHA-512/224, HMAC/HKDF genéricos sobre BLAKE2b-256, códec DER PKCS#1, base58 y base64 raw de Go, P-224/521 sobre core:math/big (no constant-time), MsgPack mínimo, subconjunto de gjson/sjson/pretty (sus bytes exactos se firman).

PORT-TODO: CLI; KeyringTrustStore (Credential Manager); RSA > 4096 bits (límite de core); OAEP/PSS con hashes que core no tiene; signets protegidos (TODO en Go también). Tiempos de signet en JSON en UTC (Go: offset local).

Desajustes Go ↔ core:crypto: clave privada ed25519 de Go = seed‖pub (64 B) vs seed de 32 B; secretos ECDH NIST de Go sin ceros a la izquierda (big.Int.Bytes) vs ancho fijo — alimenta HKDF, importa para compatibilidad; ecdh.ecdh rechaza salidas X25519 todo-cero y Go no → scalarmult directo; PSS "auto" salt vs explícito; sin x509.
Trampas: GC → arena por sesión + arena scratch; defer registrado después de un return temprano nunca corre; `ecdh` choca con core:crypto/ecdh → jess_ecdh; string apuntando a un Error pasado por valor queda colgando; un typo en el primo de P-521 lo encontraron los vectores.
Quirks de Go conservados: el cliente wire nunca sale de SendApply (el rekey no ocurre); SelectSignets devuelve nil en el primer mismatch; recipients=false se arrastra; CryptoHashID de BLAKE2s = BLAKE2b_256.
