/* srv.c */
#include <netinet/in.h>
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#define PORT 8181

int main()
{
    int s, c;
    socklen_t addrlen;
    struct sockaddr_in srv, cli;
    char buff[512];
    const char* data = "httpd v1.0\n";

    memset(&srv, 0, sizeof(srv));
    memset(&cli, 0, sizeof(cli));
    
    s = socket(AF_INET, SOCK_STREAM, 0);
    if(s < 0)
    {
        perror("socket");
        return -1;
    }

    srv.sin_family = AF_INET;
    srv.sin_port = htons(PORT);
    srv.sin_addr.s_addr = INADDR_ANY;

    if(bind(s, (struct sockaddr*)& srv, sizeof(srv)) < 0)
    {
        perror("bind");
        close(s);
        return -1;
    }

    if(listen(s, 7) < 0)
    {
        perror("listen");
        close(s);
        return -1;
    }

    printf("Listening on 0.0.0.0:%d\n", PORT);

    addrlen = sizeof(cli);
    c = accept(s, (struct sockaddr*)& cli, &addrlen);
    if(c < 0)
    {
        perror("accept");
        close(s);
        return -1;
    }

    printf("Client %s:%d connected\n", inet_ntoa(cli.sin_addr), 
                                        ntohs(cli.sin_port));
    
    int rn = read(c, buff, sizeof(buff) - 1);
    if(rn > 0)
    {
        buff[rn] = '\0';
        printf("Request:\n%s\n", buff);
    }

    write(c, data, strlen(data));

    close(c);
    close(s);

    return 0;
}
