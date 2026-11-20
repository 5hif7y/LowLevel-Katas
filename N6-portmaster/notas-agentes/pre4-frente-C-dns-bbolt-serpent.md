# Pre-etapa 4, frente C — lib/dns contra miekg, importador bbolt, Serpent

Clones de referencia read-only (versiones del go.mod): `gitclones/miekg-dns` (v1.1.72), `gitclones/bbolt` (v1.4.3), `gitclones/aead-serpent`.

## lib/dns vs miekg/dns v1.1.72 — 123 tests x3
Tests de miekg portados (msg, length, dns, labels, types, edns, svcb, truncate, ~40 de parse/scan). **36 divergencias corregidas**, entre ellas: Unpack conserva los RRs parseados antes de un error (Go descarta la sección entera y no sigue); conteos de answer/authority/additional mayores que los registros presentes eran error (en Go no); textos de error byte a byte (`MX.Mx: dns: …`); msg_len empaquetaba el mensaje (Go estima con msgLenWithCompressionMap — los mapas de compresión del estimado y del empaquetado real se verificaron iguales en el borde de 16 KiB); msg_pack_into agregaba al contenido previo del buffer (offsets de compresión mal); pack rechazaba nombres > 255 (Go sólo chequea al desempaquetar); sólo 5 de 16 opciones EDNS0; "flags: do, co;" vs "flags: do co;"; 9 reglas de SVCB; sprintName perezoso (`\065` queda `\065`); el tokenizer a mano reemplazado por un port fiel de zlexer/ZoneParser (línea:columna exactas, $ORIGIN, $TTL, owners implícitos); TTL > MaxUint32 ya no da la vuelta; IsDomainName con escape final; CompareDomainName pliega sólo ASCII. Nuevo: msg_truncate (lo necesita el nameserver), ZoneParser/ReadRR, canonical_name.
Bug de Go corregido: un error de valor SVCB pierde su texto (`dns: : "tok" at line…`). Conservados: wrap uint16 de KEEPALIVE en String(); Msg.Len cuenta TXT escapado (Truncate depende de eso); el typo "bas svcbipv6hint"; pack sin límite de 255; question count mentiroso = error.
PORT-TODO: A/AAAA sin dirección (Go IP nil) → 0.0.0.0/::; tipos sin struct (CAA, DS, DNSKEY, RRSIG, NSEC, TLSA…) como RFC3597; $INCLUDE/$GENERATE; TSIG, Client/Server (etapa 4).
Trampas: literales posicionales exigen todos los campos (no se puede agregar un flag a A sin romper consumidores); Go usa escapes octales en strings de test (`\127` = 'W'), Odin no los tiene.

## Importador bbolt — 10 + 3 tests x3
Lector read-only (archivo en memoria): tamaño de página como getPageSize, dos metas con magic/versión/checksum FNV-64a (gana txid mayor), páginas branch/leaf/overflow, buckets anidados e inline, offsets acotados, profundidad ≤ 64 (archivo corrupto → error, Go hace panic si ambos metas son inválidos). Import: bucket `"\x00"` → record.new_raw_wrapper → sqlite en una transacción. core/base: si existe `bbolt/db.bbolt` y no `sqlite/db.sqlite`, importa y renombra a `db.bbolt.imported` (nunca borra); si falla, borra el sqlite parcial y reintenta en el próximo arranque. Writer bbolt sólo para tests (paquete aparte por ciclo de imports).
PORT-TODO: verificar contra un archivo de un Portmaster Go real; sin importador badger; sólo little-endian.

## Serpent — 7 tests x3
Port de aead/serpent (16/24/32 B); las 3468 vectores NESSIE completos (incluidas iteraciones 100/1000) + tests de aead. rng: opción "serpent" habilitada (el cipher se fija al crear el generador; Go lo relee en cada rekey). test_feeder dormía 20 ms fijos y fallaba 1 de 9 → polling.
