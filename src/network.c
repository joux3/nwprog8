#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "network.h"

int start_listening();
int make_nonblock(int);
int start_epoll(int);

int network_start() {
    int listen_sock = start_listening();
    if (listen_sock < 0) {
        printf("Failed to open server socket!");
        return -1;
    }

    printf("Network started\n");
    if (start_epoll(listen_sock) < 0) {
        return -1;
    }

    return 0;
}

int start_listening() {
    int listenfd;
    struct sockaddr_in6 servaddr;

    // create socket for listening
    if ((listenfd = socket(AF_INET6, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        return -1;
    }

    // Pick a port and bind socket to it.
    // Accept connections from any address.
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin6_family = AF_INET6;
    servaddr.sin6_addr = in6addr_any;
    servaddr.sin6_port = htons(13337);

    if (bind(listenfd, (struct sockaddr *) &servaddr,
        sizeof(servaddr)) < 0) {
        perror("bind");
        return -1;
    }

    // Set the socket to passive mode, with specified listen queue size
    if (listen(listenfd, NETWORK_LISTEN_Q) < 0) {
        perror("listen");
        return -1;
    }

    make_nonblock(listenfd);

    return listenfd;
}

int make_nonblock(int sockfd) {
    int val = fcntl(sockfd, F_GETFL, 0);
    if (fcntl(sockfd, F_SETFL, val | O_NONBLOCK) < 0) {
        perror("fcntl(sockfd)");
        return -1;
    }
    return 1;
}

int start_epoll(int listen_sock) {
    struct epoll_event ev, events[NETWORK_MAX_EVENTS];
    int conn_sock, nfds, epollfd;

    epollfd = epoll_create1(0);
    if (epollfd == -1) {
        perror("epoll_create1");
        return -1;
    }

    ev.events = EPOLLIN;
    ev.data.fd = listen_sock;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, listen_sock, &ev) == -1) {
        perror("epoll_ctl: listen_sock");
        return -1;
    }

    for (;;) {
        nfds = epoll_wait(epollfd, events, NETWORK_MAX_EVENTS, -1);
        if (nfds == -1) {
            perror("epoll_pwait");
            return -1;
        }

        for (int n = 0; n < nfds; ++n) {
            if (events[n].data.fd == listen_sock) {
                struct sockaddr_in6 cliaddr;
                socklen_t addrlen = sizeof(cliaddr);
                conn_sock = accept(listen_sock,
                        (struct sockaddr *) &cliaddr, &addrlen);
                if (conn_sock == -1) {
                    perror("accept");
                    return -1;
                }
                make_nonblock(conn_sock);
                ev.events = EPOLLIN;
                ev.data.fd = conn_sock;
                if (epoll_ctl(epollfd, EPOLL_CTL_ADD, conn_sock,
                            &ev) == -1) {
                    perror("epoll_ctl: conn_sock");
                    return -1;
                }
            } else {
                char buf[81];
                int read_fd = events[n].data.fd;
                int n = read(read_fd, buf, 80);
                if (n > 0) {
                    buf[n] = '\0';
                    printf("Read data: %s\n", buf); 
                } else if (n == 0) {
                    printf("conn closed\n");
                    close(events[n].data.fd);
                } else {
                    perror("read ");
                    close(events[n].data.fd);
                }
            }
        }
    }
}
