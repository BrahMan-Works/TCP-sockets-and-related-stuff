/* sockets.c */
#include <netinet/in.h>
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#define IP "142.251.222.164" /* www.google.com */
#define PORT 80 /* port to do a connection from (HTTP) */

int main()
{
    int s;
    struct sockaddr_in sock;
    char buff[512];
    char* data = "HEAD / HTTP/1.0\r\n\r\n";

    s = socket(AF_INET, SOCK_STREAM, 0);
    if(s < 0)
    {
        printf("socket() error\n");
        return -1;
    }

    sock.sin_addr.s_addr = inet_addr(IP);
    sock.sin_port = htons(PORT);
    sock.sin_family = AF_INET;

    if(connect(s, (struct sockaddr*)& sock, sizeof(struct sockaddr_in)) != 0)
    {
        printf("connect() error\n");
        close(s);
        return -1;
    }

    write(s, data, strlen(data));
    memset(buff, 0, 512);

    read(s, buff, 511);
    close(s);
    printf("\n%s\n", buff);

    return 0;
}
