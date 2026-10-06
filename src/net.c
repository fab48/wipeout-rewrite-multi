#include <stdio.h>
#include <string.h>

#include "net.h"

#if defined(__EMSCRIPTEN__)

bool net_available(void) { return false; }
bool net_init(void) { return false; }
void net_cleanup(void) {}
bool net_socket_open(net_socket_t *sock, uint16_t port, bool broadcast) { sock->open = false; return false; }
void net_socket_close(net_socket_t *sock) { sock->open = false; }
uint16_t net_socket_port(net_socket_t *sock) { return 0; }
int net_socket_recv(net_socket_t *sock, void *buffer, int capacity, net_addr_t *from) { return -1; }
bool net_socket_send(net_socket_t *sock, net_addr_t to, const void *data, int len) { return false; }
bool net_addr_parse(const char *str, uint16_t default_port, net_addr_t *addr) { return false; }
const char *net_addr_to_string(net_addr_t addr, char *buffer, int len) { snprintf(buffer, len, "-"); return buffer; }

#include <emscripten.h>
double net_time(void) { return emscripten_get_now() / 1000.0; }

#else

#if defined(_WIN32)
	#define WIN32_LEAN_AND_MEAN
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#include <windows.h>
	typedef int socklen_t;
	#define NET_INVALID_SOCKET ((intptr_t)INVALID_SOCKET)
	#define net_close_handle(H) closesocket((SOCKET)(H))
	#define net_would_block() (WSAGetLastError() == WSAEWOULDBLOCK)
	// A previous send to a closed port shows up as WSAECONNRESET on the next
	// recvfrom on Windows; it must be ignored for a connectionless socket.
	#define net_ignorable_error() (WSAGetLastError() == WSAECONNRESET || WSAGetLastError() == WSAEMSGSIZE)
#else
	#include <sys/types.h>
	#include <sys/socket.h>
	#include <netinet/in.h>
	#include <arpa/inet.h>
	#include <netdb.h>
	#include <fcntl.h>
	#include <unistd.h>
	#include <errno.h>
	#include <time.h>
	#define NET_INVALID_SOCKET ((intptr_t)-1)
	#define net_close_handle(H) close((int)(H))
	#define net_would_block() (errno == EAGAIN || errno == EWOULDBLOCK)
	#define net_ignorable_error() (errno == ECONNREFUSED || errno == EINTR)
#endif

#include <stdlib.h>

static bool net_initialized = false;

bool net_available(void) {
	return true;
}

bool net_init(void) {
	if (net_initialized) {
		return true;
	}
	#if defined(_WIN32)
		WSADATA wsa;
		if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
			printf("net: WSAStartup failed\n");
			return false;
		}
	#endif
	net_initialized = true;
	return true;
}

void net_cleanup(void) {
	if (!net_initialized) {
		return;
	}
	#if defined(_WIN32)
		WSACleanup();
	#endif
	net_initialized = false;
}

bool net_socket_open(net_socket_t *sock, uint16_t port, bool broadcast) {
	sock->open = false;
	if (!net_init()) {
		return false;
	}

	intptr_t handle = (intptr_t)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (handle == NET_INVALID_SOCKET) {
		printf("net: socket() failed\n");
		return false;
	}

	int yes = 1;
	if (broadcast) {
		setsockopt(handle, SOL_SOCKET, SO_BROADCAST, (const char *)&yes, sizeof(yes));
	}

	// Large buffers: a client that is busy loading a track for a second must
	// not lose everything the host sent in the meantime
	int buffer_size = 1 << 20;
	setsockopt(handle, SOL_SOCKET, SO_RCVBUF, (const char *)&buffer_size, sizeof(buffer_size));
	setsockopt(handle, SOL_SOCKET, SO_SNDBUF, (const char *)&buffer_size, sizeof(buffer_size));

	#if defined(_WIN32)
		// Don't report ICMP port unreachable as errors on later receives
		#ifndef SIO_UDP_CONNRESET
			#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
		#endif
		BOOL report = FALSE;
		DWORD returned = 0;
		WSAIoctl((SOCKET)handle, SIO_UDP_CONNRESET, &report, sizeof(report), NULL, 0, &returned, NULL, NULL);

		u_long non_blocking = 1;
		ioctlsocket((SOCKET)handle, FIONBIO, &non_blocking);
	#else
		fcntl((int)handle, F_SETFL, fcntl((int)handle, F_GETFL, 0) | O_NONBLOCK);
	#endif

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(port);
	if (bind(handle, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		printf("net: bind to port %d failed\n", port);
		net_close_handle(handle);
		return false;
	}

	sock->handle = handle;
	sock->open = true;
	return true;
}

void net_socket_close(net_socket_t *sock) {
	if (sock->open) {
		net_close_handle(sock->handle);
	}
	sock->open = false;
}

uint16_t net_socket_port(net_socket_t *sock) {
	if (!sock->open) {
		return 0;
	}
	struct sockaddr_in addr;
	socklen_t addr_len = sizeof(addr);
	if (getsockname(sock->handle, (struct sockaddr *)&addr, &addr_len) != 0) {
		return 0;
	}
	return ntohs(addr.sin_port);
}

int net_socket_recv(net_socket_t *sock, void *buffer, int capacity, net_addr_t *from) {
	if (!sock->open) {
		return -1;
	}
	for (;;) {
		struct sockaddr_in addr;
		socklen_t addr_len = sizeof(addr);
		int received = recvfrom(sock->handle, (char *)buffer, capacity, 0, (struct sockaddr *)&addr, &addr_len);
		if (received < 0) {
			if (net_would_block()) {
				return 0;
			}
			if (net_ignorable_error()) {
				continue;
			}
			return -1;
		}
		if (received == 0) {
			continue; // empty datagram: nothing for us
		}
		from->ip = ntohl(addr.sin_addr.s_addr);
		from->port = ntohs(addr.sin_port);
		return received;
	}
}

bool net_socket_send(net_socket_t *sock, net_addr_t to, const void *data, int len) {
	if (!sock->open) {
		return false;
	}
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(to.ip);
	addr.sin_port = htons(to.port);
	int sent = sendto(sock->handle, (const char *)data, len, 0, (struct sockaddr *)&addr, sizeof(addr));
	return sent == len;
}

bool net_addr_parse(const char *str, uint16_t default_port, net_addr_t *addr) {
	if (!str || !str[0] || !net_init()) {
		return false;
	}

	char host[128];
	int port = default_port;
	const char *colon = strrchr(str, ':');
	size_t host_len = colon ? (size_t)(colon - str) : strlen(str);
	if (host_len == 0 || host_len >= sizeof(host)) {
		return false;
	}
	memcpy(host, str, host_len);
	host[host_len] = '\0';
	if (colon) {
		port = atoi(colon + 1);
		if (port <= 0 || port > 65535) {
			return false;
		}
	}

	struct addrinfo hints, *result = NULL;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	if (getaddrinfo(host, NULL, &hints, &result) != 0 || !result) {
		return false;
	}
	struct sockaddr_in *in = (struct sockaddr_in *)result->ai_addr;
	addr->ip = ntohl(in->sin_addr.s_addr);
	addr->port = port;
	freeaddrinfo(result);
	return true;
}

const char *net_addr_to_string(net_addr_t addr, char *buffer, int len) {
	snprintf(buffer, len, "%u.%u.%u.%u:%u",
		(addr.ip >> 24) & 0xff, (addr.ip >> 16) & 0xff, (addr.ip >> 8) & 0xff, addr.ip & 0xff,
		addr.port
	);
	return buffer;
}

double net_time(void) {
	#if defined(_WIN32)
		static LARGE_INTEGER frequency = {0};
		if (frequency.QuadPart == 0) {
			QueryPerformanceFrequency(&frequency);
		}
		LARGE_INTEGER counter;
		QueryPerformanceCounter(&counter);
		return (double)counter.QuadPart / (double)frequency.QuadPart;
	#else
		struct timespec ts;
		clock_gettime(CLOCK_MONOTONIC, &ts);
		return ts.tv_sec + ts.tv_nsec / 1e9;
	#endif
}

#endif
