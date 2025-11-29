#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stddef.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define LISTEN_EVENTS_SIZE 512
#define N_BATCHES          128
#define BATCH_SIZE         16384

void usage()
{
    printf("usage: e46_stress -c <srv_ip> -p <srv_port>\n");
}

static int make_socket_nonblocking(int fd)
{
    int sck_flags = fcntl(fd, F_GETFL);
    sck_flags |= O_NONBLOCK;

    int nonblock_res = fcntl(fd, F_SETFL, sck_flags);

    return nonblock_res;
}

void fill(uint8_t (*data)[N_BATCHES][BATCH_SIZE])
{
    char alphabet[] = "abcdefghijklmnopqrstuvwxyz";

    for (int i = 0; i < N_BATCHES; i++)
    {
        for (int j = 0; j < BATCH_SIZE; j++)
        {
            (*data)[i][j] = alphabet[rand() % (sizeof(alphabet) - 2)];
        }
    }
}

int main(int argc, char **argv)
{
    char BUFFER[2048];
    uint8_t batches[N_BATCHES][BATCH_SIZE];
    size_t batch_idx = 0;

    int opt;
    int srv_port = 5430;
    int n_conns  = 16;
    const char *srv_ip = "127.0.0.1";

    while ((opt = getopt(argc, argv, "c:p:n:h")) != -1)
    {
        switch (opt)
        {
            case 'c':
                srv_ip = optarg;
                break;
            case 'p':
                srv_port = atoi(optarg);
                break;
            case 'n':
                n_conns = atoi(optarg);
                break;
            case 'h':
                usage();
                return 0;
        }
    }

    srand(time(NULL));
    fill((uint8_t(*)[N_BATCHES][BATCH_SIZE])batches);

    printf("Connecting to %s:%d, n_connections: %d\n", srv_ip, srv_port, n_conns);

    struct sockaddr_in srv;

    srv.sin_family      = AF_INET;
    srv.sin_port        = htons(srv_port);
    srv.sin_addr.s_addr = inet_addr(srv_ip);

    int epoll_fd = epoll_create1(0);

    if (epoll_fd == -1)
    {
        fprintf(
            stderr,
            "Could not create epoll structure in kernel. err: %d(%s)\n",
            errno,
            strerror(errno)
        );

        return 1;
    }

    for (int i = 0; i < n_conns; i++)
    {
        int client_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

        if (connect(client_fd, (struct sockaddr*)&srv, sizeof(srv)) == -1)
        {
            fprintf(
                stderr,
                "Cannot create client socket. err: %d(%s)\n",
                errno,
                strerror(errno)
            );

            return 1;
        }

        if (make_socket_nonblocking(client_fd) == -1)
        {
            fprintf(
                stderr,
                "Could not make socket nonblocking. fd: %d, err: %d(%s)\n",
                client_fd,
                errno,
                strerror(errno)
            );

            return 1;
        }

        printf("Client %d connected to %s:%d\n", client_fd, srv_ip, srv_port);

        struct epoll_event req;

        req.data.fd = client_fd;
        req.events  = EPOLLOUT | EPOLLIN; // Notify when writeable and readable

        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &req) == -1)
        {
            fprintf(
                stderr,
                "Cannot register fd to epoll. fd: %d, err: %d(%s)",
                client_fd,
                errno,
                strerror(errno)
            );

            return 1;
        }
    }

    for (;;)
    {
        struct epoll_event events[LISTEN_EVENTS_SIZE];

        int nevents = epoll_wait(epoll_fd, (struct epoll_event*)&events, LISTEN_EVENTS_SIZE, 0);

        for (int i = 0; i < nevents; i++)
        {
            struct epoll_event *e = &events[i];
            int client_fd = e->data.fd;

            // Drain echo messages
            if (e->events & EPOLLIN)
            {
                batch_idx += recv(client_fd, BUFFER, sizeof(BUFFER), MSG_TRUNC);
            }

            if (e->events & EPOLLOUT)
            {
                batch_idx += send(client_fd, batches[batch_idx % N_BATCHES], BATCH_SIZE, 0);
            }
        }
    }
}