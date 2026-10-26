// rt/sdl/winsock - the Winsock calls of the PC network transport (ROADMAP 12.14), as plain functions over integer types.
//
// Why a separate layer: <winsock2.h> pulls in <windows.h>, whose LONG / ULONG typedefs clash with the Amiga-style ones of ace/types.h that
// rt/net.hpp uses.  So the Winsock headers live in rt/sdl/winsock.cpp only, and rt/sdl/net_winsock.cpp (the NetTransport) sees this header.
//
// Conventions (the Amiga bsdsocket code never had to choose: its host order IS the network order):
//   * addresses are IPv4 as a host-order number, 127.0.0.1 = 0x7F000001 (first octet in the top byte): what the engine puts on the wire
//     (JOIN_REPLY) and formats for the screen (engine/netbrowse netFormatIpv4); 0xFFFFFFFF = none;
//   * ports are host-order numbers; the byte swaps happen inside this layer, nowhere else;
//   * descriptors are int32_t (a SOCKET handle fits), -1 = none;
//   * would-block is reported as 0 bytes, never as an error.
#pragma once
#include <stdint.h>

namespace rt {
namespace wsock {

bool startup();                                   // WSAStartup (idempotent)
void cleanup();                                   // WSACleanup if startup() succeeded

int32_t openStream();                             // TCP socket, -1 = cannot
int32_t openDgram();                              // UDP socket, -1 = cannot
void closeFd(int32_t lFd);
void setNonBlocking(int32_t lFd, bool isStream);  // FIONBIO; a stream also gets TCP_NODELAY; a datagram socket stops reporting ICMP resets
bool bindAny(int32_t lFd, uint16_t uwPort, bool isExclusive);   // INADDR_ANY:port; isExclusive = SO_EXCLUSIVEADDRUSE (a second listener on the port fails)
bool listenOn(int32_t lFd, int32_t lBacklog);
int32_t acceptFrom(int32_t lListen);              // a new descriptor, -1 = none waiting
int32_t connectTo(int32_t lFd, uint32_t ulIp, uint16_t uwPort);   // 1 connected, 0 in progress, -1 failed
int32_t socketError(int32_t lFd);                 // SO_ERROR, 0 = none
int32_t sendBytes(int32_t lFd, const void *pData, uint32_t ulLen);               // bytes taken, 0 = would block, < 0 = broken
int32_t recvBytes(int32_t lFd, void *pBuf, uint32_t ulCap);                      // bytes (> 0), 0 = nothing now, < 0 = closed / broken
int32_t sendDgram(int32_t lFd, uint32_t ulIp, uint16_t uwPort, const void *pData, uint32_t ulLen);   // bytes sent, 0 = would block, < 0 = not sent
int32_t recvDgram(int32_t lFd, void *pBuf, uint32_t ulCap, uint32_t &ulIp, uint16_t &uwPort);       // length (> 0), 0 = none waiting
bool localName(int32_t lFd, uint32_t &ulIp, uint16_t &uwPort);   // getsockname: the local address (0 for a socket bound to any) and port of a socket
bool isReady(int32_t lFd, bool isWrite, uint32_t ulMs);          // select on one descriptor; for a connect in progress a failure counts as ready (read SO_ERROR)
void waitReadable(const int32_t *plFds, uint32_t ulCount, uint32_t ulMs);   // select for reading on all of them, at most ulMs
uint32_t resolve(const char *szHost);             // dotted quad or name (getaddrinfo, IPv4), 0xFFFFFFFF = cannot
int32_t lastError();                              // WSAGetLastError

}  // namespace wsock
}  // namespace rt
