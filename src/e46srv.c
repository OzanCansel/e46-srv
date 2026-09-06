#include "e46srv.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/queue.h>
#include <arpa/inet.h>

#define E46SRV_LISTEN_BACKLOG 256
#define MAX_EVENTS            256
#define ECHO_BUF_SIZE         65536

struct buffer
{
    void *payload;
    int  len;

    TAILQ_ENTRY(buffer) entries;
};

TAILQ_HEAD(buffer_head, buffer);

struct client_ctx
{
    struct buffer_head bhead;
    int n_awaits;
    int client_fd;
    uint32_t epoll_cfg;
};

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
            inet_ntop(AF_INET6, &addr6->sin6_addr, dst, INET6_ADDRSTRLEN);

            port = ntohs(addr6->sin6_port);
        break;
    }

    sprintf(PORT_STR, ":%d", port);
    strcat(dst, PORT_STR);
}

static void cleanup_client_ctx(struct client_ctx *cli_ctx)
{
    struct buffer *curr;

    while (!TAILQ_EMPTY(&cli_ctx->bhead))
    {
        curr = TAILQ_FIRST(&cli_ctx->bhead);

        free(curr->payload);
        TAILQ_REMOVE(&cli_ctx->bhead, curr, entries);
        free(curr);
    }

    free(cli_ctx);
}

int e46srv_listen(struct e46srv_cfg* cfg, struct e46srv_ctx *ctx)
{
    char READ_BUF[ECHO_BUF_SIZE];
    char LISTEN_IP_STR[64];
    int srv_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (srv_fd == -1)
    {
        fprintf(
            stderr,
            "Cannot create server socket. err: %d(%s)",
            errno,
            strerror(errno)
        );

        return -1;
    }

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
            "Could not bind. err: %d(%s)\n",
            errno,
            strerror(errno)
        );

        return -2;
    }

    int listen_res = listen(srv_fd, E46SRV_LISTEN_BACKLOG);

    if (listen_res < 0)
    {
        fprintf(
            stderr,
            "Could not listen. err: %d(%s)\n",
            errno,
            strerror(errno)
        );

        return -3;
    }

    int nonblock_res = make_socket_nonblocking(srv_fd);

    if (nonblock_res == -1)
    {
        fprintf(
            stderr,
            "Could not make server socket non-blocking. err: %d(%s)",
            errno,
            strerror(errno)
        );

        return -4;
    }

    ctx->srv_fd = srv_fd;
    ctx->lowat  = cfg->lowat;
    ctx->hiwat  = cfg->hiwat;
    ctx->epoll_fd = epoll_create1(0);

    if (ctx->epoll_fd == -1)
    {
        fprintf(
            stderr,
            "Could not create epoll structure. err: %d(%s)\n",
            errno,
            strerror(errno)
        );

        return -5;
    }

    struct epoll_event ep_req, events[MAX_EVENTS];

    ep_req.events  = EPOLLIN;
    ep_req.data.fd = ctx->srv_fd;

    if (epoll_ctl(ctx->epoll_fd, EPOLL_CTL_ADD, ctx->srv_fd, &ep_req) == -1)
    {
        fprintf(
            stderr,
            "Could not register listener for server sck. err: %d(%s)\n",
            errno,
            strerror(errno)
        );

        return -6;
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
                        "Cannot accept client. err: %d(%s)\n",
                        errno,
                        strerror(errno)
                    );
                }
                else
                {
                    char CLI_IP_STR[256];

                    ntop(&client_addr, CLI_IP_STR);

                    printf("Accepted new client. addr: %s\n", CLI_IP_STR);

                    ep_req.events  = EPOLLIN;

                    if (make_socket_nonblocking(client_fd) == -1)
                    {
                        fprintf(
                            stderr,
                            "Could not make client socket non-blocking. fd: %d, err: %d(%s)\n",
                            client_fd,
                            errno,
                            strerror(errno)
                        );

                        close(client_fd);

                        continue;
                    }

                    struct client_ctx *cli_ctx = (struct client_ctx*)malloc(sizeof(*cli_ctx));

                    if (cli_ctx == NULL)
                    {
                        close(client_fd);

                        fprintf(
                            stderr,
                            "Could not allocate client_ctx. fd: %d, err: %d(%s)\n",
                            client_fd,
                            errno,
                            strerror(errno)
                        );

                        continue;
                    }

                    memset(cli_ctx, 0, sizeof(*cli_ctx));

                    TAILQ_INIT(&cli_ctx->bhead);
                    cli_ctx->epoll_cfg = EPOLLIN;
                    cli_ctx->client_fd = client_fd;

                    ep_req.data.ptr = cli_ctx;

                    // Inform kernel to listen `client_fd` as well.
                    if (epoll_ctl(ctx->epoll_fd, EPOLL_CTL_ADD, client_fd, &ep_req) == -1)
                    {
                        fprintf(
                            stderr,
                            "Could not register read event for client. fd: %d, err: %d(%s)\n",
                            client_fd,
                            errno,
                            strerror(errno)
                        );

                        free(cli_ctx);
                        close(client_fd);
                    }
                }
            }
            else if (e->events & (EPOLLHUP | EPOLLRDHUP))
            {
                // Somehow peer closed the socket unexpectedly, so we close as well.
                struct client_ctx *cli_ctx = (struct client_ctx*)e->data.ptr;

                close(cli_ctx->client_fd);
                cleanup_client_ctx(cli_ctx);

                e->data.ptr = NULL;
            }
            else
            {
                struct client_ctx *cli_ctx = e->data.ptr;
                int client_fd = cli_ctx->client_fd;

                // If writeable and there are awaiting bytes
                if ((e->events & EPOLLOUT) == EPOLLOUT && cli_ctx->n_awaits > 0)
                {
                    if (!TAILQ_EMPTY(&cli_ctx->bhead))
                    {
                        struct buffer *curr = TAILQ_FIRST(&cli_ctx->bhead);

                        ssize_t n_written = send(client_fd, curr->payload, curr->len, MSG_NOSIGNAL);

                        if (n_written > 0)
                        {
                            if (n_written < curr->len)
                            {
                                memmove(curr->payload, curr->payload + n_written, curr->len - n_written);

                                curr->len = curr->len - n_written;
                                cli_ctx->n_awaits -= n_written;
                            }
                            else
                            {
                                cli_ctx->n_awaits -= n_written;

                                free(curr->payload);
                                TAILQ_REMOVE(&cli_ctx->bhead, curr, entries);
                                free(curr);
                            }
                        }
                        else if (n_written == 0)
                        {
                            break;
                        }
                    }
                }

                // If configured to EPOLLIN and there are readable bytes
                if ((cli_ctx->epoll_cfg & EPOLLIN) && e->events & EPOLLIN)
                {
                    int nread = recv(client_fd, READ_BUF, sizeof(READ_BUF) - 1, 0);

                    if (nread > 0)
                    {
                        READ_BUF[nread] = '\0';

                        if (cfg->print)
                            printf("fd: %d => %s", client_fd, READ_BUF);

                        // Buffer directly if there awaiting bytes
                        if (cli_ctx->n_awaits > 0)
                        {
                            struct buffer *buf = (struct buffer*)malloc(sizeof(*buf));

                            buf->payload = malloc(nread);
                            buf->len     = nread;

                            TAILQ_INSERT_TAIL(&cli_ctx->bhead, buf, entries);

                            cli_ctx->n_awaits += buf->len;
                        }
                        else
                        {
                            ssize_t nwritten = send(client_fd, READ_BUF, nread, MSG_NOSIGNAL);

                            // Successfull write
                            if (nwritten > 0)
                            {
                                // Partial write
                                if (nwritten < nread)
                                {
                                    // Buffer leftover part
                                    struct buffer *buf = (struct buffer*)malloc(sizeof(*buf));

                                    int leftover = nread - nwritten;

                                    buf->payload = malloc(leftover);
                                    buf->len     = leftover;

                                    TAILQ_INSERT_TAIL(&cli_ctx->bhead, buf, entries);

                                    cli_ctx->n_awaits += buf->len;
                                }
                            }
                            else
                            {
                                // Write is not successfull, so buffer it
                                struct buffer *buf = (struct buffer*)malloc(sizeof(*buf));

                                buf->payload = malloc(nread);
                                buf->len     = nread;

                                TAILQ_INSERT_TAIL(&cli_ctx->bhead, buf, entries);

                                cli_ctx->n_awaits += buf->len;
                            }
                        }
                    }
                    else if (nread == 0) // Means, client closed the connection.
                    {
                        close(client_fd);
                        cleanup_client_ctx(cli_ctx);
                        cli_ctx = NULL;

                        printf("Peer closed the connection. fd: %d\n", client_fd);
                    }
                    else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != ENOBUFS)
                    {
                        // Unexpected errno, so close connection
                        close(client_fd);
                        cleanup_client_ctx(cli_ctx);
                        cli_ctx = NULL;

                        fprintf(
                            stderr,
                            "recv failed with unexpected errno so closing client. fd: %d, err: %d(%s)\n",
                            client_fd,
                            errno,
                            strerror(errno)
                        );
                    }
                }

                if (cli_ctx)
                {
                    uint32_t desired = 0;

                    if (cli_ctx->n_awaits > 0)
                        desired |= EPOLLOUT;

                    // If not listening READ events but awaiting bytes under the low watermark
                    // Start to listen READ events
                    if ((cli_ctx->epoll_cfg & EPOLLIN) == 0 && cli_ctx->n_awaits < ctx->lowat)
                        desired |= EPOLLIN;

                    // If already listening READ events and awaiting bytes still under the high watermark
                    // Keep listening READ events
                    if (((cli_ctx->epoll_cfg & EPOLLIN) == EPOLLIN) && cli_ctx->n_awaits < ctx->hiwat)
                        desired |= EPOLLIN;

                    if (cli_ctx->epoll_cfg != desired)
                    {
                        e->events = desired;

                        if (epoll_ctl(ctx->epoll_fd, EPOLL_CTL_MOD, client_fd, e) == 0)
                        {
                            cli_ctx->epoll_cfg = desired;
                        }
                        else
                        {
                            fprintf(
                                stderr,
                                "Could not configure desired event for client. fd: %d, err: %d(%s)\n",
                                client_fd,
                                errno,
                                strerror(errno)
                            );
                        }
                    }
                }
            }
        }
    }

    return 0;
}