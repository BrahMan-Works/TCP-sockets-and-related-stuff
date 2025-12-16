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

#define READ_BUF_SIZE 4096
#define WRITE_BUF_SIZE 4096

int epfd;

typedef struct
{
    int fd;
    char rBuf[READ_BUF_SIZE];
    size_t rLen;

    char wBuf[WRITE_BUF_SIZE];
    size_t wLen;
    size_t wSent;
} conn_t;

void closeConn(conn_t* c)
{
    epoll_ctl(epfd, EPOLL_CTL_DEL, c->fd, NULL);
    close(c->fd);
    free(c);
}

void handleRead(conn_t* c)
{
    while(true)
    {
        ssize_t n = read(c->fd, c->rBuf + c->rLen, READ_BUF_SIZE- c->rLen);
        if(n > 0)
        {
            c->rLen += n;
            write(STDOUT_FILENO, c->rBuf, c->rLen);
            if(strstr(c->rBuf, "\r\n\r\n"))
            {
                printf("\n--- end of HTTP request ---\n");
            }

            if(c->rLen == READ_BUF_SIZE)
            {
                printf("request too large, closing\n");
                closeConn(c);
                return;
            }
        }
        else if(n == 0)
        {
            printf("client %d disconnected\n", c->fd);
            closeConn(c);
            return;
        }
        else
        {
            if(errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // no more data
                break;
            }
            else
            {
                perror("read");
                closeConn(c);
                return;
            }
        }
    }
}

void handleWrite(conn_t* c)
{
    while(c->wSent < c->wLen)
    {
        ssize_t n = write(c->fd, c->wBuf + c->wSent, c->wLen - c->wSent);
        if(n > 0)
        {
            c->wSent += n;
        }
        else if(n == -1)
        {
            if(errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // kernel buffer full
                return;
            }
            else
            {
                perror("write");
                closeConn(c);
                return;
            }
        }
    }

    closeConn(c);
}

int main()
{
    epfd = epoll_create1(0);

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

                    conn_t* c = malloc(sizeof(conn_t));
                    c->fd = clientFd;
                    c->rLen = 0;

                    const char* body = "Hello, User!\n";
                    c->wLen = snprintf(c->wBuf, WRITE_BUF_SIZE,
                                        "HTTP/1.1 200 OK\r\n"
                                        "content-length: %zu\r\n"
                                        "content-type: text/plain\r\n"
                                        "connection: close\r\n"
                                        "\r\n"
                                        "%s", strlen(body), body);

                    c->wSent = 0;

                    struct epoll_event ev;
                    ev.events = EPOLLOUT | EPOLLET;
                    ev.data.ptr = c;

                    if(epoll_ctl(epfd, EPOLL_CTL_ADD, clientFd, &ev) < 0)
                    {
                        perror("epoll_ctl: client add");
                        close(clientFd);
                        continue;
                    }

                    // printf("accepted client fd = %d\n", clientFd);        
                }
            }
            else
            {
                conn_t* c = events[i].data.ptr;
                if(events[i].events & EPOLLIN)
                {
                    handleRead(c);
                }
                if(events[i].events & EPOLLOUT)
                {
                    handleWrite(c);
                }
            }
        }
    }

    return 0;
}
