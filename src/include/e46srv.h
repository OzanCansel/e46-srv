#ifndef _E46SRV_H
#define _E46SRV_H

#include <netinet/in.h>

struct e46srv_cfg
{
    struct sockaddr_in listen;
};

struct e46srv_ctx
{
    int srv_fd;
    int epoll_fd;
};

int e46srv_listen(struct e46srv_cfg *cfg, struct e46srv_ctx* ctx);

#endif