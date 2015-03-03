#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "network.h"
#include "packets.h"

int start_listening();
int make_nonblock(int);
int start_epoll(int);
int accept_connection(int, int);
int read_for_client(client_t *client);

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

client_t *client_create(int client_fd) {
    client_t *client = malloc(sizeof(client_t));
    if (client != NULL) {
        client->client_fd = client_fd;
        client->buf_used = 0;
    }
    return client;
}

void client_free(client_t *client) {
    close(client->client_fd);
    free(client);
}

int start_epoll(int listen_sock) {
    struct epoll_event ev, events[NETWORK_MAX_EVENTS];
    int nfds, epollfd;

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
                if (accept_connection(epollfd, listen_sock) < 0) {
                    return -1;
                }
            } else if(events[n].events & EPOLLIN) {
                client_t *client = events[n].data.ptr;
                if (read_for_client(client) < 0) {
                    return -1;
                }
            } else {
                // not from listen socket and not about reading? must be a closed socket
                client_t *client = events[n].data.ptr;
                client_free(client);  
            }
        }
    }
}

int accept_connection(int epollfd, int listen_sock) {
    struct epoll_event ev; 
    struct sockaddr_in6 cliaddr;
    socklen_t addrlen = sizeof(cliaddr);
    int conn_sock = accept(listen_sock, (struct sockaddr *) &cliaddr, &addrlen);
    if (conn_sock == -1) {
        perror("accept");
        return -1;
    }
    make_nonblock(conn_sock);
    ev.events = EPOLLIN;
    ev.data.ptr = client_create(conn_sock);
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, conn_sock,
                &ev) == -1) {
        perror("epoll_ctl: conn_sock");
        return -1;
    }
    return 1;
}

// tries to read packets for the given client
int read_for_client(client_t *client) {
    int n = read(client->client_fd, &client->buf[client->buf_used], NETWORK_CLIENT_BUF - client->buf_used);
    if (n > 0) {
        client->buf_used += n;

        int packet_start = 0;
        int packet_size = 1;
        for (int i = 0; i < client->buf_used; i++) {
            if (packet_size > NETWORK_MAX_PACKET_SIZE) {
                printf("Client tried to send a packet too long!\n");
                client_free(client);
                return 1;
            }
            if (client->buf[i] == '\n') {
                client->buf[i] = '\0';
                // pass the packet to the next layer to handle
                handle_packet(client, &client->buf[packet_start]);
                packet_start = i + 1;
                packet_size = 1;
            } 
            packet_size++;
        }
        // copy the rest to the beginning of the buffer
        // at this point, packet_size contains length of the broken packet at end
        if (packet_start != 0 && client->buf_used - packet_start > 0) {
            memcpy(&client->buf, &client->buf[packet_start], client->buf_used - packet_start);
        }
        client->buf_used = client->buf_used - packet_start;
    } else if (n == 0) {
        printf("conn closed\n");
        client_free(client);
    } else {
        perror("read ");
        // TODO: check that errno isn't EAGAIN or EWOULDBLOCK 
        // (though in theory this shouldn't be possible)
        client_free(client);
    }
    return 1; 
}
