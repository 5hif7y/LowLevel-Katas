# lib/dns: formato de presentación de todos los tipos de miekg (deuda de etapa 4)

## Contra qué se portó

- `go.mod` fija **`github.com/miekg/dns v1.1.72`**. Las fuentes las saqué del clon
  local `DEV/gitclones/miekg-dns` (tag `v1.1.72`, commit `cb21f4d2 Release 1.1.72`),
  leídas nomás. WebFetch no sirve para esto: se niega a devolver código textual.
- Lo que porté: `types.go` (structs + `String()`), `scan_rr.go` (`parse()`),
  `scan.go` (`stringToCm`, `locCheck*`, `stringToNodeID`), `msg_helpers.go`
  (nsec, apl, ipsec gateway, base32/base64/hex, octet), `msg_generate.go` y
  `types_generate.go` (las reglas que generan `zmsg.go`/`ztypes.go`), `tsig.go`
  (sólo el struct y su `String`), `reverse.go` (`StringToAlgorithm`, `StringToCertType`).

## Diseño

- Cada tipo de `TypeToRR` tiene ahora su struct en el union `RR`
  (`odin/lib/dns/rdata.odin`), con los **mismos tags `dns:"..."` que Go**. Los
  embebidos (`CDS{DS}`, `KEY{DNSKEY}`, `SIG{RRSIG}`, `NXT{NSEC}`, ...) usan `using`.
- Go genera pack/unpack/len/copy a partir de esos tags. Acá un **único motor por
  reflexión** aplica las reglas del generador campo por campo, en tiempo de
  ejecución, con el mismo resultado:
  - los mismos bytes;
  - el mismo envoltorio de error `"TIPO.Campo: "`;
  - el mismo corte temprano `off == len(msg)`;
  - los mismos `len` estimados, incluidos los a mano de NSEC/NSEC3/CSYNC;
  - los nombres dentro del rdata se descomprimen al desempaquetar y se escriben
    sin comprimir, pero quedan registrados en el mapa de compresión.
- `String()` está en `rdata_string.odin` y `parse()` en `rdata_scan.odin`, los
  dos token por token. Los tipos que ya estaban (A, MX, SOA, TXT, SVCB, ...) no
  cambiaron: los switches pasaron a `#partial` con un `case:` que deriva al motor.
- `net.IP` pasa a ser `net.Address`: nil es la IP nil de Go, IP4 es la de 4
  bytes e IP6 la de 16. En `APLPrefix` la máscara queda guardada como largo de
  prefijo.
- Los cables que ya funcionaban siguen byte a byte igual: las pruebas de
  wire/length/msg viejas pasan sin cambios.

## Tipos cubiertos (todos los de TypeToRR v1.1.72 que antes eran genéricos)

AFSDB, AMTRELAY, APL, AVC, CAA, CDNSKEY, CDS, CERT, CSYNC, DHCID, DLV, DNSKEY, DS,
EID, EUI48, EUI64, GID, GPOS, HINFO, HIP, IPSECKEY, ISDN, KEY, KX, L32, L64, LOC,
LP, NAPTR, NID, NIMLOC, NINFO, NSAPPTR, NSEC, NSEC3, NSEC3PARAM, NXT, OPENPGPKEY,
PX, RESINFO, RKEY, RP, RRSIG, RT, SIG, SMIMEA, SPF, SSHFP, TA, TALINK, TKEY,
TLSA, TSIG, UID, UINFO, URI, X25, ZONEMD.

**Genéricos a propósito: ninguno de TypeToRR.** Sólo siguen como `RFC3597` los
tipos que no están en TypeToRR, igual que en Go (ATMA, UNSPEC, AXFR/IXFR/MAILA/MAILB,
TYPEnnnn). TSIG, como en Go, no tiene parser: da "TSIG records do not have a
presentation format", pero se lee en la forma `\#`.

Se replicaron a propósito estas rarezas de Go:
- TKEY toma el algoritmo literal.
- NID/L64 aceptan un token con error del lexer.
- EUI64 reporta "bad EUI68 Address".
- AMTRELAY con D=1 no empaqueta ni desempaqueta el gateway.
- El Value de CAA y el Target de URI quedan crudos al desempaquetar.
- Los errores con `wrappedErr` (gateway de IPSECKEY/AMTRELAY, familia y
  dirección de APL) salen con el texto vacío: `dns: : "tok" at line...`.
- `uint32(float)` sigue la semántica de amd64.

Divergencias que quedan:
- `parse_float_go` rechaza floats hexadecimales (`0x1p-2`), que Go acepta. Sólo
  afecta a GPOS/LOC escritos así.
- Base32: un byte `0xFF` en el input (que Go trata como pad por `byte(NoPadding)`)
  no se replica.

## $INCLUDE / $GENERATE

Busqué con grep `NewZoneParser|ParseZone|ZoneParser|ReadRR(|INCLUDE|GENERATE` en
`service/ spn/ base/ cmds/`: no hay ningún uso. Portmaster llama sólo a `dns.NewRR`
(nsutil, netutils, resolver-env, rrcache) sobre strings que arma él o que escribió
con `RR.String()`. Los dos PORT-TODO pasaron a `ACCEPTED(no-portmaster-use)` y el
comportamiento no cambió. En lib/dns ya no queda ningún PORT-TODO.

## Tests portados (strings textuales)

- `parse_test.go`: TestNSEC, TestParseLOC (incluidos los inválidos), TestStringToCm,
  TestParseDS, TestHIP, TestRfc1982, TestILNP, TestGposEidNimloc, TestPX, TestEUIxx,
  TestUserRR, TestParseRRSIGTimestamp, TestParseTLSA, TestParseSMIMEA,
  TestParseSSHFP, TestParseHINFO, TestParseISDN, TestParseCAA, TestPackCAA,
  TestParseURI, TestParseAVC, TestParseCSYNC, TestParseBadNAPTR, TestParseAPL,
  TestParseAPLErrors, TestParseZONEMD, TestParseIPSECKEY, TestParseAMTRELAY,
  TestParseOPENPGPKEY, TestParseRRSIGAlgNames.
- Además, a los tests ya portados les devolví los registros originales de Go, que
  antes estaban reescritos con MX/MINFO/SOA:
  - TestLineNumberError2: TALINK.
  - TestComment: DNSKEY/NSEC.
  - TestParseTokenOverflow: el TLSA largo.
  - TestUnbalancedParens: RRSIG.
  - TestUnpackRRWithHeaderInvalidLengths: SSHFP.
  - TestUnexpectedNewline: PX.
- `types_test.go`: TestCmToM, TestSplitN, TestSprintTxtOctet, TestRPStringer.
- `scan_test.go`: TestParseTA.
- `dns_test.go`:
  - TestPackUnpack (DNSKEY/RRSIG), TestPackNAPTR y TestTKEY (round-trip de
    cable byte a byte).
  - TestNoRdataPack y TestNoRdataUnpack ahora recorren todo TypeToRR.
- `msg_test.go`: TestPackDomainNameNSECTypeBitmap. TestLenDynamicA ahora incluye L32.
- `msg_helpers_test.go`:
  - TestPackDataNsec, menos el caso "overflow", que depende del buffer fijo de Go.
  - TestPackDataAplPrefix(_Failures), TestPackDataApl,
    TestUnpackDataAplPrefix(_Errors) y TestUnpackDataApl.
- No porté TestPrintfVerbsRdata, porque `dns.Field` no está portado.
- Tests propios:
  - Cada tipo hace el round-trip texto→cable→texto y pasa por RFC 3597.
    `len` es ≥ al largo empaquetado (es una cota, como en Go), y también se
    prueba la copia.
  - Un mensaje comprimido con todos los tipos se empaqueta y desempaqueta; el
    texto que sale es el de Go (el caso de rrcache).
  - Ownership con el allocator de tracking, incluidos los parse y unpack fallidos.
  - Rarezas de cable: CAA truncado, el error `DS.KeyTag: ...` y el signer de
    RRSIG comprimido.

## Resultados (foreground, `-vet`)

| verificación | corrida 1 | corrida 2 | corrida 3 |
|---|---|---|---|
| `odin test odin/lib/dns` | 173/173 ok | 173/173 ok | 173/173 ok |
| `odin test odin/service/resolver` | 46/46 ok | 46/46 ok | 46/46 ok |
| `odin test odin/service/nameserver` | 10/10 ok | 10/10 ok | 10/10 ok |
| `odin test odin/service/firewall` | 16/16 ok | 16/16 ok | 16/16 ok |
| `VET=1 crosscheck.sh windows_amd64 linux_amd64 darwin_arm64 -- odin/lib/dns` | ok/ok/ok | | |

Después cambié sólo comentarios (pasé no-ASCII a ASCII). Volví a correr lib/dns y
el crosscheck: 173/173 y ok/ok/ok.

## Archivos

- Nuevos: `odin/lib/dns/rdata.odin`, `rdata_string.odin`, `rdata_scan.odin`,
  `rdata_test.odin`.
- Tocados: `rr.odin` (union y dispatch), `pack.odin`, `string.odin`, `scan.odin`
  (ACCEPTED), `dns_test.odin`, `msg_test.odin`, `parse_test.odin`.

Sin commits. No toqué mgr, status ni notifications.

Pendiente fuera de alcance: `odin/STAGES.md:68` todavía dice que lib/dns "no
[está verificado] contra fuentes de miekg". Ahora sí lo está, pero ese archivo
no es de esta tarea.
