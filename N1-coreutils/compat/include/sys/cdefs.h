#ifndef _SYS_CDEFS_H_
#define _SYS_CDEFS_H_

#ifdef __cplusplus
#define __BEGIN_DECLS extern "C" {
#define __END_DECLS }
#else
#define __BEGIN_DECLS
#define __END_DECLS
#endif

#if defined(_MSC_VER)
#define __dead __declspec(noreturn)
#define __unused
#define __packed
#define __aligned(x) __declspec(align(x))
#define __bounded(a, b, c)
#define __pure
#define __printflike(fmtarg, firstvararg)
#define __scanflike(fmtarg, firstvararg)
#elif defined(__GNUC__) || defined(__clang__)
#define __dead __attribute__((__noreturn__))
#define __unused __attribute__((__unused__))
#define __packed __attribute__((__packed__))
#define __aligned(x) __attribute__((__aligned__(x)))
#define __bounded(a, b, c)
#define __pure __attribute__((__pure__))
#define __printflike(fmtarg, firstvararg) __attribute__((__format__(__printf__, fmtarg, firstvararg)))
#define __scanflike(fmtarg, firstvararg) __attribute__((__format__(__scanf__, fmtarg, firstvararg)))
#else
#define __dead
#define __unused
#define __packed
#define __aligned(x)
#define __bounded(a, b, c)
#define __pure
#define __printflike(fmtarg, firstvararg)
#define __scanflike(fmtarg, firstvararg)
#endif

#endif /* _SYS_CDEFS_H_ */
