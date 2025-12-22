/* a basic TCP server  */
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>

#define READ_BUF_SIZE 4096
#define WRITE_BUF_SIZE 4096

#define READ_TIMEOUT 5
#define IDLE_TIMEOUT 10

int epfd;

typedef enum
{
    STATE_READING,
    STATE_WRITING
} conn_state_t;

typedef struct
{
    int fd;
    bool closed;

    int timerFd;
    time_t deadline;

    conn_state_t state;

    char rBuf[READ_BUF_SIZE];
    size_t rLen;

    char wBuf[WRITE_BUF_SIZE];
    size_t wLen;
    size_t wSent;

    bool keepAlive;
} conn_t;

void closeConn(conn_t* c)
{
    if(c->closed) return;
    c->closed = true;

    epoll_ctl(epfd, EPOLL_CTL_DEL, c->fd, NULL);
    epoll_ctl(epfd, EPOLL_CTL_DEL, c->timerFd, NULL);

    close(c->fd);
    close(c->timerFd);
    free(c);
}

void resetTimer(conn_t* c, int seconds)
{
    struct itimerspec its = {0};
    its.it_value.tv_sec = seconds;
    timerfd_settime(c->timerFd, 0, &its, NULL);
}

void buildResponse(conn_t* c, int status, const char* statusText, const char* body)
{
    c->wLen = snprintf(c->wBuf, WRITE_BUF_SIZE,
                        "HTTP/1.1 %d %s\r\n"
                        "content-length: %zu\r\n"
                        "content-type: text/plain\r\n"
                        "connection: %s\r\n"
                        "\r\n"
                        "%s", status, statusText, strlen(body), (c->keepAlive ? "keep-alive" : "close"), body);
    c->wSent = 0;
}

void handleRead(conn_t *c)
{
    while (true)
    {
        ssize_t n = read(c->fd, c->rBuf + c->rLen, READ_BUF_SIZE - c->rLen);

        if (n > 0)
        {
            resetTimer(c, READ_TIMEOUT);
            c->rLen += n;

            if (c->rLen == READ_BUF_SIZE)
            {
                closeConn(c);
                return;
            }

            c->keepAlive = true;

            if(strcasestr(c->rBuf, "Connection: Close")) c->keepAlive = false;

            char *lineEnd = strstr(c->rBuf, "\r\n");
            if (!lineEnd)
            {
                closeConn(c);
                return;
            }

            *lineEnd = '\0';

            char method[8];
            char path[256];

            if (sscanf(c->rBuf, "%7s %255s", method, path) != 2)
            {
                closeConn(c);
                return;
            }

            int status;
            const char *statusText;
            const char *body;

            if (strcmp(method, "GET") != 0)
            {
                status = 405;
                statusText = "Method Not Allowed";
                body = "Only GET supported\n";
            }
            else if (strcmp(path, "/") == 0)
            {
                status = 200;
                statusText = "OK";
                body = "welcome to the server\n";
            }
            else if (strcmp(path, "/health") == 0)
            {
                status = 200;
                statusText = "OK";
                body = "OK\n";
            }
            else if (strcmp(path, "/hello") == 0)
            {
                status = 200;
                statusText = "OK";
                body = "hello, user!\n";
            }
            else
            {
                status = 404;
                statusText = "Not Found";
                body = "404 not found\n";
            }

            buildResponse(c, status, statusText, body);
            c->state = STATE_WRITING;

            struct epoll_event ev;
            ev.events = EPOLLOUT | EPOLLET;
            ev.data.ptr = c;
            epoll_ctl(epfd, EPOLL_CTL_MOD, c->fd, &ev);

            c->rLen = 0;
            
            return;
        }
        else if (n == 0)
        {
            closeConn(c);
            return;
        }
        else
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;

            closeConn(c);
            return;
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

    if(c->keepAlive)
    {
        resetTimer(c, IDLE_TIMEOUT);

        c->state = STATE_READING;
        c->rLen = 0;
        c->wLen = 0;
        c->wSent = 0;
        c->keepAlive = true;

        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLET;
        ev.data.ptr = c;
        epoll_ctl(epfd, EPOLL_CTL_MOD, c->fd, &ev);
    }
    else
    {
        closeConn(c);
    }
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
          
            uint32_t evs = events[i].events;
                
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
                        
                        perror("accept");
                        break;
                    }

                    int tFd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
                    if(tFd < 0)
                    {
                        perror("timerfd_create");
                        close(clientFd);
                        continue;
                    }

                    struct itimerspec its = {0};
                    its.it_value.tv_sec = READ_TIMEOUT;
                    timerfd_settime(tFd, 0, &its, NULL);

                    int flags = fcntl(clientFd, F_GETFL, 0);
                    fcntl(clientFd, F_SETFL, flags | O_NONBLOCK);

                    conn_t* c = malloc(sizeof(conn_t));
                    if(!c)
                    {
                        close(clientFd);
                        continue;
                    }

                    struct epoll_event tev;
                    tev.events = EPOLLIN;
                    tev.data.ptr = c;
                    epoll_ctl(epfd, EPOLL_CTL_ADD, tFd, &tev);

                    c->fd = clientFd;
                    c->closed = false;
                    c->timerFd = tFd;
                    c->state = STATE_READING;

                    c->rLen = 0;
                    c->wLen = 0;
                    c->wSent = 0;

                    c->keepAlive = true;

                    struct epoll_event cev;
                    cev.events = EPOLLIN | EPOLLET;
                    cev.data.ptr = c;

                    if(epoll_ctl(epfd, EPOLL_CTL_ADD, clientFd, &cev) < 0)
                    {
                        perror("epoll_ctl: client add");
                        close(clientFd);
                        free(c);
                        continue;
                    }

                    // printf("accepted client fd = %d\n", clientFd);        
                }

                continue;
            }
            else
            {
                conn_t* c = events[i].data.ptr;
                if(!c || c->closed)
                {
                    // fprintf(stderr, "NULL conn ptr\n");
                    continue;
                }

                if(events[i].data.fd == c->timerFd)
                {
                    uint64_t expirations;
                    read(c->timerFd, &expirations, sizeof(expirations));

                    fprintf(stderr, "connection timeout fd = %d\n", c->fd);
                    closeConn(c);
                    continue;
                }

                if(evs &(EPOLLERR | EPOLLHUP))
                {
                    closeConn(c);
                    continue;
                }

                if (!c->closed && c->state == STATE_READING && (evs & EPOLLIN)) {
                    handleRead(c);
                }

                if (!c->closed && c->state == STATE_WRITING && (evs & EPOLLOUT)) {
                    handleWrite(c);
                }
            }
        }
    }

    return 0;
}
