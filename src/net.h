#ifndef NET_H
#define NET_H

#include "types.h"

// Minimal non-blocking UDP sockets (Winsock / BSD sockets). Addresses are
// IPv4, ip and port in host byte order. The web build has no sockets: every
// function fails there and net_available() returns false.

typedef struct {
	uint32_t ip;
	uint16_t port;
} net_addr_t;

typedef struct {
	intptr_t handle;
	bool open;
} net_socket_t;

#define NET_ADDR_BROADCAST 0xffffffffu
#define NET_ADDR_LOOPBACK  0x7f000001u

bool net_available(void);
bool net_init(void);
void net_cleanup(void);

// port 0 binds to any free port
bool net_socket_open(net_socket_t *sock, uint16_t port, bool broadcast);
void net_socket_close(net_socket_t *sock);
uint16_t net_socket_port(net_socket_t *sock);

// Returns the number of bytes received, 0 if nothing is waiting, -1 on error
int net_socket_recv(net_socket_t *sock, void *buffer, int capacity, net_addr_t *from);
bool net_socket_send(net_socket_t *sock, net_addr_t to, const void *data, int len);

// "192.168.1.10" or "192.168.1.10:47800", or a host name
bool net_addr_parse(const char *str, uint16_t default_port, net_addr_t *addr);
const char *net_addr_to_string(net_addr_t addr, char *buffer, int len);
static inline bool net_addr_equal(net_addr_t a, net_addr_t b) {
	return a.ip == b.ip && a.port == b.port;
}

// Monotonic time in seconds, independent of the platform layer
double net_time(void);

#endif
