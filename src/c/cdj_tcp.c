#include "cdj_tcp.h"
#include <unistd.h>

int
cdj_tcp_handshake(int fd)
{
    return write(fd, CDJ_TCP_HANDSHAKE, CDJ_TCP_HANDSHAKE_LEN)
               == CDJ_TCP_HANDSHAKE_LEN ? 0 : -1;
}
