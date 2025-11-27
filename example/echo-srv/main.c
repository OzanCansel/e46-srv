#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "e46srv.h"

int main(int argc, char **argv)
{
    struct e46srv_cfg cfg;
    struct e46srv_ctx ctx;

    memset(&cfg, 0, sizeof(cfg));
    memset(&ctx, 0, sizeof(ctx));

    cfg.listen.sin_family = AF_INET;
    cfg.listen.sin_port   = htons(5430);
    inet_aton("127.0.0.1", &cfg.listen.sin_addr);

    e46srv_listen(&cfg, &ctx);

    return 0;
}