#ifndef __SOCKET_H
#define __SOCKET_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>

#define IPADDR "192.168.1.21"
#define IPPORT "8192"
#define BUF_SIZE 6

int socket_init(const char *ipaddr, const char *port);

#endif