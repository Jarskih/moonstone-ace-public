/* Shim for the toolchain's Roadshow SDK headers (sys-include/netdb.h includes <sys/errno.h>, which the bartman toolchain
 * package leaves out). BSD errno values as in the Roadshow / AmiTCP SDK sys/errno.h; only the ones rt/net.cpp uses plus the
 * neighbours. ROADMAP 12.2. */
#ifndef MS_SHIM_SYS_ERRNO_H
#define MS_SHIM_SYS_ERRNO_H
#define EINTR 4
#define EBADF 9
#define EACCES 13
#define EINVAL 22
#define EWOULDBLOCK 35
#define EAGAIN EWOULDBLOCK
#define EINPROGRESS 36
#define EALREADY 37
#define EADDRINUSE 48
#define ECONNABORTED 53
#define ECONNRESET 54
#define ENOTCONN 57
#define ETIMEDOUT 60
#define ECONNREFUSED 61
#define EHOSTUNREACH 65
#endif
