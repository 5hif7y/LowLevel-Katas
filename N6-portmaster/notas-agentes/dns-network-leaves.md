# lib/dns + service/network/{packet,netutils,reference,socket,iphelper,state} — informe

Tests (x3): dns 16, packet 6, netutils 6, reference 1, socket 1, iphelper 3, state 6 — OK.

Convenciones para otros agentes:
- IPs = `net.Address` de core:net (nil = net.IP nil). Semántica net.IP de Go en `pm:service/network/packet` ip.odin: parse_ip, ip_to4 (desmapea ::ffff:), ip_equal (v4 == su forma v4-mapped), ip_string (`<nil>`), IP_Net, parse_cidr, ip_net_contains.
- dns.RR = union de valores (A, AAAA, NS, CNAME, PTR, DNAME, MX, SOA, TXT, SRV, NULL, OPT, SVCB, HTTPS, RFC3597…). `rr.(*dns.A)` → `rr.(dns.A)`; `rr.Header()` → `rr_header(&rr)`. Constantes Ada_Case (`dns.Type_A`, `dns.Rcode_Name_Error`). RR y Msg son dueños de sus strings/secciones (rr_destroy, rr_copy, msg_destroy, msg_copy).
- Tablas de state: snapshots inmutables con refcount (retain bajo read lock, release después de leer el PID).

lib/dns: pack/unpack con las reglas de compresión de Go, EDNS0 (SUBNET, COOKIE, PADDING, EDE, LOCAL), SVCB/HTTPS completos, String() estilo dig, NewRR, helpers de labels, ReverseAddr. Verificado contra capturas armadas a mano y RFCs — **no contra fuentes de miekg/dns ni una corrida real de Go** (pendiente: clonar github.com/miekg/dns y portar sus tests en la etapa 4).
PORT-TODO(etapa 4): Client/Server/Conn/Exchange/ResponseWriter; Msg.Truncate; directivas de zona; tipos sin struct (CAA, DS, DNSKEY, RRSIG) se imprimen como RFC3597; A con rdlength 0 → 0.0.0.0.
packet: decoder propio en lugar de gopacket (IPv4 con opciones/fragmentos, IPv6 con headers de extensión, TCP, UDP, ICMPv4/v6, IGMP); `Layers` con bit_set `present`.
iphelper: GetExtendedTcpTable/UdpTable propios; filas acotadas por el tamaño del buffer.

Hook: state.deps.ipv6_enabled ← netenv (ronda B).

Bugs de Go corregidos: lookup UDP dual-stack chequeaba lastUpdateAt de la tabla IPv4 en vez de la dual-stack; iphelper podía leer más allá del buffer si el conteo de entradas es inconsistente.
Conservados: `dnsSDDomainRegex` contiene `\a-z` = rango BEL..'z' en Go → acepta comillas, `<>` y controles en nombres .local (decide qué nombres acepta Portmaster); MatchesAddress/MatchesIP comparan el extremo opuesto a su propia doc (sin callers); UDP exists() devuelve false en el primer bind sin estado.

Trampas: iterar map de maps con `for k, &v in outer` y `delete_key(&v, x)` modifica una COPIA del header del map interno → usar `&outer[k]`; slices de tablas que un refresh concurrente reemplaza (en Go las mantiene el GC) → snapshots con refcount; inicializador global que llama un proc ("requiring a context cannot be called at global scope"); variantes de union deben ser tipos distintos (HTTPS :: struct{using svcb: SVCB}).
