/*
 * sys/sdt.h — Substrate stub for SystemTap-style Statically Defined
 * Tracing probes.
 *
 * GCC's libgcc/unwind-dw2.c and a few other places sprinkle STAP_PROBE
 * macros at interesting program points so a tracing tool (SystemTap,
 * eBPF, DTrace) can hook them with zero overhead when not tracing.
 *
 * Substrate has no userland tracing framework yet — until it does,
 * these macros expand to nothing.  The signatures match the real
 * SystemTap header so the call sites are syntactically valid.
 *
 * When a real tracer lands, replace this header with the upstream
 * one and the probes light up automatically.
 */
#ifndef _SYS_SDT_H
#define _SYS_SDT_H 1

#define STAP_PROBE(provider, name)                                  do { } while (0)
#define STAP_PROBE1(provider, name, p1)                             do { (void)(p1); } while (0)
#define STAP_PROBE2(provider, name, p1, p2)                         do { (void)(p1); (void)(p2); } while (0)
#define STAP_PROBE3(provider, name, p1, p2, p3)                     do { (void)(p1); (void)(p2); (void)(p3); } while (0)
#define STAP_PROBE4(provider, name, p1, p2, p3, p4)                 do { (void)(p1); (void)(p2); (void)(p3); (void)(p4); } while (0)
#define STAP_PROBE5(provider, name, p1, p2, p3, p4, p5)             do { (void)(p1); (void)(p2); (void)(p3); (void)(p4); (void)(p5); } while (0)
#define STAP_PROBE6(provider, name, p1, p2, p3, p4, p5, p6)         do { (void)(p1); (void)(p2); (void)(p3); (void)(p4); (void)(p5); (void)(p6); } while (0)
#define STAP_PROBE7(provider, name, p1, p2, p3, p4, p5, p6, p7)     do { (void)(p1); (void)(p2); (void)(p3); (void)(p4); (void)(p5); (void)(p6); (void)(p7); } while (0)
#define STAP_PROBE8(provider, name, p1, p2, p3, p4, p5, p6, p7, p8) do { (void)(p1); (void)(p2); (void)(p3); (void)(p4); (void)(p5); (void)(p6); (void)(p7); (void)(p8); } while (0)

/* Convenience aliases used by some upstream codebases. */
#define DTRACE_PROBE  STAP_PROBE
#define DTRACE_PROBE1 STAP_PROBE1
#define DTRACE_PROBE2 STAP_PROBE2
#define DTRACE_PROBE3 STAP_PROBE3
#define DTRACE_PROBE4 STAP_PROBE4
#define DTRACE_PROBE5 STAP_PROBE5

#endif /* _SYS_SDT_H */
