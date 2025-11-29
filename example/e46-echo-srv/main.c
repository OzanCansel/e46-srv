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

    cfg.listen.sin_family      = AF_INET;
    cfg.listen.sin_port        = htons(5430);
    cfg.listen.sin_addr.s_addr = INADDR_ANY;
    cfg.print                  = 0;

    e46srv_listen(&cfg, &ctx);

    return 0;
}