/* net_ps2.h: included before the network's files (Makefile): no sockets
   on the PS2 yet (Quake/net_sys.h has none for it), the loopback only */
typedef int sys_socket_t;
#define INVALID_SOCKET (-1)
