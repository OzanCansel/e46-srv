#include "e46srv.h"
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <arpa/inet.h>

#define E46SRV_LISTEN_BACKLOG 256
#define MAX_EVENTS            256

static int make_socket_nonblocking(int fd)
{
    int sck_flags = fcntl(fd, F_GETFL);
    sck_flags |= O_NONBLOCK;

    int nonblock_res = fcntl(fd, F_SETFL, sck_flags);

    return nonblock_res;
}

static void ntop(const struct sockaddr *addr, char *dst)
{
    char PORT_STR[16];
    int port;

    switch (addr->sa_family)
    {
        case AF_INET:
        {
            struct sockaddr_in *addr4 = (struct sockaddr_in*)addr;
            inet_ntop(AF_INET, &addr4->sin_addr, dst, INET_ADDRSTRLEN);

            port = ntohs(addr4->sin_port);
        }
        break;
        case AF_INET6:
            struct  sockaddr_in6 *addr6 = (struct sockaddr_in6*)addr;
            inet_ntop(AF_INET, &addr6->sin6_addr, dst, INET6_ADDRSTRLEN);

            port = ntohs(addr6->sin6_port);
        break;
    }

    sprintf(PORT_STR, ":%d", port);
    strcat(dst, PORT_STR);
}

int e46srv_listen(struct e46srv_cfg* cfg, struct e46srv_ctx *ctx)
{
    char LISTEN_IP_STR[64];
    int srv_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    int ok = 1;
    setsockopt(srv_fd, SOL_SOCKET, SO_REUSEADDR, &ok, sizeof(ok));

    int bind_res = bind(
        srv_fd,
        (const struct sockaddr*)&cfg->listen,
        sizeof(cfg->listen)
    );

    if (bind_res < 0)
    {
        fprintf(
            stderr,
            "Could not bind. err: (%d)%s\n",
            errno,
            strerror(errno)
        );

        return -1;
    }

    int listen_res = listen(srv_fd, E46SRV_LISTEN_BACKLOG);

    if (listen_res < 0)
    {
        fprintf(
            stderr,
            "Could not listen. err: (%d)%s\n",
            errno,
            strerror(errno)
        );

        return -2;
    }

    int nonblock_res = make_socket_nonblocking(srv_fd);

    if (nonblock_res < 0)
    {
        fprintf(
            stderr,
            "Could not make server socket non-blocking. err: (%d)%s",
            errno,
            strerror(errno)
        );

        return -3;
    }

    ctx->srv_fd = srv_fd;
    ctx->epoll_fd = epoll_create1(0);

    if (ctx->epoll_fd == -1)
    {
        fprintf(
            stderr,
            "Could not create epoll structure. err: (%d)%s\n",
            errno,
            strerror(errno)
        );

        return -4;
    }

    struct epoll_event ep_req, events[MAX_EVENTS];

    ep_req.events  = EPOLLIN;
    ep_req.data.fd = ctx->srv_fd;

    if (epoll_ctl(ctx->epoll_fd, EPOLL_CTL_ADD, ctx->srv_fd, &ep_req) == -1)
    {
        fprintf(
            stderr,
            "Could not register listener for server sck. err: (%d)%s\n",
            errno,
            strerror(errno)
        );

        return -5;
    }

    ntop((struct sockaddr*)&cfg->listen, LISTEN_IP_STR);

    printf("Listening %s\n", LISTEN_IP_STR);

    for (;;)
    {
        int nfds = epoll_wait(ctx->epoll_fd, events, MAX_EVENTS, -1);

        for (int fd_idx = 0; fd_idx < nfds; fd_idx++)
        {
            struct epoll_event *e = &events[fd_idx];

            // Accept new client
            if (e->data.fd == ctx->srv_fd)
            {
                struct sockaddr client_addr;
                socklen_t client_addr_len = sizeof(client_addr);
                int client_fd = accept(ctx->srv_fd, &client_addr, &client_addr_len);

                if (client_fd == -1)
                {
                    // TODO: Handle error here
                    fprintf(
                        stderr,
                        "Cannot accept client. err: (%d)%s\n",
                        errno,
                        strerror(errno)
                    );
                }
                else
                {
                    char CLI_IP_STR[256];

                    ntop(&client_addr, CLI_IP_STR);

                    // Client accepted
                    printf("Accepted new client. addr: %s\n", CLI_IP_STR);
                }
            }
        }
    }

    return 0;
}