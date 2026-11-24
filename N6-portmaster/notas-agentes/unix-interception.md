# Intercepción Linux (nfqueue + conntrack + iptables + eBPF + dnsmonitor varlink) — informe

Fork `DEV/gitclones/portmaster`, rama `port/odin`, sin commit. Espejo WSL `/root/pm/agent-icpt`.

## Archivos

Nuevos:
- `odin/lib/netlink/` (`pm_netlink`): `netlink.odin` **portable** (nlmsghdr, padding, NLMSG_ERROR/DONE, nfgenmsg, atributos TLV anidados, iteradores, errores con errno), `conn_linux.odin` (socket AF_NETLINK con `core:sys/linux`: dial/bind/getsockname, send, receive con timeout + `interrupt()` por eventfd, `request()` = Execute/dump/ack, set_option, shutdown/free), `conn_other.odin` (misma API → `.Not_Supported`). Tests `netlink_test.odin` (portable, bytes exactos) y `conn_linux_test.odin` (dump RTM_GETLINK de sólo lectura, error del kernel, timeout/interrupt, conn cerrada). El codec es portable para que corra en Windows y el paquete pase el cross-check; sólo el socket es Linux (netlink no existe en otro kernel).
- `service/firewall/interception/nfq/` (`pm_nfq`): `marks.odin`, `nfqueue_codec.odin` (go-nfqueue: config cmd/params/maxlen, verdict con mark, parse de NFQNL_MSG_PACKET), `conntrack_codec.odin` (go-conntrack: query por mark, get por tupla, delete, parse de Con, loopback), `packet.odin` (packet.Packet de nfq.go/packet.go), `queue.odin` (packetHandler, timeout de veredicto, refcount de la cola), `nfq_linux.odin` (cola real), `conntrack_linux.odin` (conntrack.go), `nfq_other.odin`. Tests `nfq_test.odin` (portable, cola con sender falso) y `nfq_privileged_linux_test.odin`.
- `service/firewall/interception/`: `interception_linux.odin`, `nfqueue_linux.odin` (reglas idénticas a Go, activate/deactivate, ensureJumpRulesAtTop, colas, worker de mantenimiento), `iptables.odin` (port de coreos/go-iptables v0.8.0, portable: exec de iptables/ip6tables con `--wait`, `-C`, versión, errores con el mismo texto) + `iptables_test.odin`, `interception_privileged_linux_test.odin`.
- `ebpf/libbpf/` (`pm_libbpf`, bindings a `system:bpf` 1.x + log de libbpf al log de Portmaster), `ebpf/connection_listener/`, `ebpf/bandwidth/`, `ebpf/exec/`: cada uno con decodificación portable (+ test), `_linux.odin` con libbpf cargando **el mismo `bpf_bpfel.o` que Go embebe** (`#load`), `_other.odin` stub, test privilegiado.
- `dnsmonitor/varlink.odin` (portable: llamada varlink, framing NUL, QueryResult, parse de respuestas) + `varlink_test.odin`; `eventlistener_linux.odin` (socket unix a `/run/systemd/resolve/io.systemd.Resolve.Monitor`) + `eventlistener_linux_test.odin` (servidor varlink falso en /tmp, sin root).
- `linux_e2e/` (`doc.odin` + `e2e_privileged_linux_test.odin`): e2e real paquete → firewall → mark → kernel.
- `cmds/portmaster-core/recover_linux.odin` (recover_linux.go).

Tocados: `interception_default.odin` y `dnsmonitor/eventlistener_other.odin` → `#+build !windows` + `#+build !linux`, PORT-TODO(darwin) con motivo; `cmds/portmaster-core/main_other.odin` (recover real en Linux, darwin con PORT-TODO(darwin)).

## Portado vs PORT-TODO

Portado todo lo del alcance: interception_linux.go, nfqueue_linux.go, nfq/{nfq,packet,conntrack}.go, ebpf/{connection_listener,bandwidth,exec}, dnsmonitor/eventlistener_linux.go + varlinktypes.go, recover_linux.go (comando y flag `--recover-iptables`). Quedan sólo PORT-TODO(darwin): intercepción, monitor DNS y recover-iptables (Go tampoco tiene nada en darwin). `cmdbase/service_other.odin` conserva su PORT-TODO(unix) (no es recover; fuera de mi alcance).

## Hooks / pedidos al coordinador

- **PermanentAcceptFinal**: Go lo usa el firewall por type assertion (conexiones salientes propias de Portmaster → mark 1709). `nfq.permanent_accept_final` está listo, pero `network/packet.Packet_VTable` no tiene la entrada: agregar `permanent_accept_final` (nil → permanent_accept) y usarla en `firewall.issue_verdict` (hoy siempre permanent_accept, como dice su comentario).
- `interception.ipv6_enabled` / `nfq.ipv6_enabled` = `netenv.ipv6_enabled` (variables para tests); `interception.ebpf_enabled` (sólo Odin, tests), `jump_rule_check_delays`, `bandwidth_collect_interval`, `dnsmonitor.varlink_socket_path`.
- Proceso: con `odin test odin/service/process` y `network/state` verdes en Linux se agregó el e2e (ver abajo).

## Tests (todos 3 corridas)

Linux sin privilegios (`lt.sh`): netlink 8, interception 6 (4 portables + 2 privilegiados saltados), nfq 13 (2 saltados), dnsmonitor 6, libbpf 1 (lee /sys/kernel/btf/vmlinux), connection_listener 4, bandwidth 4, exec 3, linux_e2e 2 (1 portable + el privilegiado saltado), y además service/firewall 15 (consumidor) → ok ×3.
Linux privilegiados, **root sólo dentro de `unshare --net`** (lo + dummy `pmd0` 10.99.0.1/24, fd99::1): `scratchpad/icpt/priv.sh <pkg>` = `unshare --net sh priv_inner.sh <pkg>` con `-define:PM_LINUX_PRIVILEGED_TESTS=true`; cada test además se niega a correr si `/proc/self/ns/net == /proc/1/ns/net`. nfq, interception, connection_listener, bandwidth, exec, linux_e2e → ok ×3. **Después de cada corrida `iptables -S`/`ip6tables -S` (filter, mangle, nat, raw) + `nft list ruleset` del namespace principal: md5 4282adb6… idéntico a la línea base** (sin reglas PORTMASTER, sin cambios).
Windows (comando de BRIEF-COMUN) ×3: netlink 4, interception 12 (8 previos + 4), nfq 11, dnsmonitor 8, windowskext 3, windowskext2 3, libbpf 1, connection_listener 3, bandwidth 3, exec 2, linux_e2e 1 (portable; el e2e es Linux), firewall 16 — ok.
Cross-check `VET=1 crosscheck.sh windows_amd64 linux_amd64 darwin_arm64` sobre los 15 dirs tocados/relacionados (incl. service/firewall, cmds): **0 fallas**.
recover-iptables: portmaster-core compilado en Linux; en namespace limpio sale 0 sin errores; con restos (cadenas + saltos v4/v6 de una corrida caída) los borra todos, sale 0; `--recover-iptables` (flag) igual.

Qué prueban los privilegiados: verdicts reales (accept entregado, drop/block no entregado, timeout de 20 s → drop, ICMP), conntrack (entrada con connmark 1710 → delete_marked_connection / delete_all_marked_connection la borran; delete_unmarked borra mark 0 no-loopback y deja loopback), reglas exactas y saltos primero en v4/v6, ensureJumpRulesAtTop (regla desplazadora → reinserta sin dejar TEMPORARY_RULE; el worker de arranque lo hace solo), start/stop ×2 sin restos, eBPF connect UDP/TCP propio, contadores de bandwidth UDP+TCP (sockops en el cgroup raíz) y "reported", exec de `/bin/true` con argv. **e2e**: firewall+network+process+profile reales + interception: UDP y TCP propios aceptados y entregados (atribución por /proc → Portmaster); con `filter/defaultAction=block` un datagrama de un `bash` hijo a 10.99.0.2 sale con BlockAlways (1711) y lo cuenta la regla REJECT; el tráfico propio sigue pasando.

## Bugs de Go

Corregidos:
1. `StartNfqueueInterception` pone `isRunning=true` antes de activar y nunca lo vuelve atrás si falla la activación o una cola: el siguiente start devuelve "already running" sin nada corriendo. El port destruye lo abierto y resetea el flag.
2. dnsmonitor Linux: los CNAME se guardaban con el nombre crudo de la pregunta (sin punto final, pisándose) mientras AddCNAMEs camina desde el FQDN → **nunca se registró un CNAME** (mismo bug que en Windows). Ahora FQDN(dueño del RR) → FQDN(destino). Además cualquier respuesta con `name` (NS, PTR…) contaba como CNAME → sólo tipo 5; una respuesta sin `rr` era deref nil → se salta.
3. go-conntrack `Get` manda con NLM_F_ACK y lee un solo datagrama: el ack queda en el socket y lo lee el próximo request (secuencia equivocada). `netlink.request` lee hasta el ack/DONE y descarta secuencias ajenas.
4. `InitNFCT` dos veces filtraba el handle anterior → se cierra.
Defensivo: un delete de conntrack sin tuplas vaciaría la tabla entera → se rechaza.
Conservados: `Block()` sólo mira ICMP (PermanentBlock mira ICMP e ICMPv6); `DeleteMarkedConnection` devuelve nil aunque falle el delete; el loopback entrante sí recibe 1710; **bandwidth.c (objeto precompilado) usa la dirección origen como destino en los probes UDP IPv6** → esas actualizaciones nunca matchean (arreglarlo exige recompilar el .o); el tracer exec sólo ve `execve` (no `execveat`).

## Trampas

- `odin check`/`test` de un paquete chequea los `_test.odin` de los importados: imports usados sólo dentro de `when PRIVILEGED` rompían `service/firewall` en Linux por -vet → `@(require) import`.
- **WSL corre en su propio PID namespace**: `bpf_get_current_pid_tgid()` da el TGID del namespace raíz ≠ `getpid()` → los tests eBPF matchean por puerto. Consecuencia real (también en Go): dentro de contenedores/WSL la atribución por PID del connection listener no coincide con /proc.
- `os.process_exec` de Odin lanza con `execveat` → invisible para `sys_enter_execve`; el test usa `sh -c 'exec …'`.
- Loopback: 1700 hace RETURN antes de `CONNMARK --save-mark` → todo paquete loopback (ambos sentidos) pasa por la cola; los tests aceptan los entrantes mientras esperan.
- El firewall usa veredictos permanentes: un bloqueo cae en la regla REJECT de 1711, no 1701.
- Listener TCP sin accept + connect bloqueante: con el backlog lleno el connect cuelga ~2 min → connect no bloqueante.
- `ODIN_ENDIAN` no es `endian.Byte_Order` → `endian.PLATFORM_BYTE_ORDER`. Los wrappers de `core:sys/linux` no aceptan sockaddr_nl → `linux.syscall(SYS_bind…)`. `iptables -S` muestra marks en hex (0x6a5). `%v` de un `[]string` en Odin pone comillas y comas → el texto de error se arma a mano como Go (`running [/usr/sbin/iptables …]`).
- Módulos del kernel autocargados por las reglas del namespace (globales, sin reglas en el namespace principal): xt_mark, xt_connmark, xt_comment, xt_nat, ipt_REJECT/ip6t_REJECT, nf_reject_ipv4/6, nf_conntrack_netlink, dummy.
- Odin: switch sobre enum sin `#partial`; `[4]u8` por valor no es direccionable para `x[:]`.
