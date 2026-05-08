#include "socket.h"

int socket_init(const char *ipaddr, const char *port)
{
    int s_fd = -1;
    int ret = -1;
    struct sockaddr_in s_addr;
    memset(&s_addr, 0, sizeof(struct sockaddr_in));
    
    s_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s_fd == -1) {
        perror("socket");
        return -1;
    }
    
    s_addr.sin_family = AF_INET;
    s_addr.sin_port = htons(atoi(port));
    inet_aton(ipaddr, &s_addr.sin_addr);
    
    ret = bind(s_fd, (struct sockaddr *)&s_addr, sizeof(struct sockaddr_in));
    if (-1 == ret) {
        perror("bind");
        close(s_fd);
        return -1;
    }
    
    ret = listen(s_fd, 10);
    if (-1 == ret) {
        perror("listen");
        close(s_fd);
        return -1;
    }
    
    return s_fd;
}