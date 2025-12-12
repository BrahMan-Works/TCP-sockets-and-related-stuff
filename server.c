/* a basic TCP server  */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>

int main()
{
    int listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if(listenFd < 0)
    {
        perror("socket");
        exit(1);
    }

    int flags = fcntl(listenFd, F_GETFL, 0);
    fcntl(listenFd, F_SETFL, flags | O_NONBLOCK);

    int opt = 1;
    setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(8080);

    if(bind(listenFd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        perror("bind");
        exit(1);
    }

    if(listen(listenFd, SOMAXCONN) < 0)
    {
        perror("listen");
        exit(1);
    }

    int epfd = epoll_create1(0);
    if(epfd < 0)
    {
        perror("epoll_create1");
        exit(1);
    }

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = listenFd;

    if(epoll_ctl(epfd, EPOLL_CTL_ADD, listenFd, &ev) < 0)
    {
        perror("epoll_ctl");
        exit(1);
    }

    struct epoll_event events[128];

    while(true)
    {
        int n = epoll_wait(epfd, events, 128, -1);
        if(n < 0)
        {
            perror("epoll_wait");
            continue;
        }

        for(int i = 0; i < n; ++i)
        {
            int fd = events[i].data.fd;

            if(fd == listenFd)
            {
                while(true)
                {
                    struct sockaddr_in clientAddr;
                    socklen_t clientLen = sizeof(clientAddr);

                    int clientFd = accept(listenFd, (struct sockaddr*)&clientAddr, &clientLen);
                    if(clientFd < 0)
                    {
                        if(errno == EAGAIN || errno == EWOULDBLOCK) break;
                        else
                        {
                            perror("accept");
                            break;
                        }
                    }

                    int flags = fcntl(clientFd, F_GETFL, 0);
                    fcntl(clientFd, F_SETFL, flags | O_NONBLOCK);

                    struct epoll_event ev;
                    ev.events = EPOLLIN | EPOLLET;
                    ev.data.fd = clientFd;

                    if(epoll_ctl(epfd, EPOLL_CTL_ADD, clientFd, &ev) < 0)
                    {
                        perror("epoll_ctl: client add");
                        close(clientFd);
                        continue;
                    }

                    printf("accepted client fd = %d\n", clientFd);
                }
            }
            else
            {
                // handle client I/O here
            }
        }
    }

    return 0;
}
