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
    servaddr.sin6_port = htons(NETWORK_CLIENT_PORT);

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
    memset(client, 0, sizeof(client_t));
    if (client != NULL) {
        client->client_fd = client_fd;
        client->buf_used = 0;
        memset(&client->nickname, 0, NICKNAME_LENGTH);
        memset(&client->channels, 0, USER_MAX_CHANNELS * sizeof(channel_t*));
    }
    return client;
}

void client_free(client_t *client) {
    handle_disconnect(client); 
    close(client->client_fd);
    free(client);
}

int start_epoll(int listen_sock) {
    struct epoll_event ev, events[NETWORK_MAX_EVENTS];
    int nfds, epollfd;

    memset(&ev, 0, sizeof(struct epoll_event));

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
    memset(&ev, 0, sizeof(struct epoll_event));
    ev.events = EPOLLIN;
    ev.data.ptr = client_create(conn_sock); // TODO: client_create can return NULL
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
                if (handle_packet(client, &client->buf[packet_start]) == STOP_HANDLING) {
                    return 1;
                }
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
        client_free(client);
    } else {
        perror("read ");
        // TODO: check that errno isn't EAGAIN or EWOULDBLOCK 
        // (though in theory this shouldn't be possible)
        client_free(client);
    }
    return 1; 
}

int network_send(client_t *client, const void *data, const size_t size) {
    size_t bytes_sent = 0;
    while (bytes_sent < size) {
        int n = write(client->client_fd, data, size);
        if (n < 0) {
            // Note: this could also be caused by error EWOULDBLOCK or EAGAIN
            // if this happens very often, userland side send buffering could also be used
            // (or kernel side buffers increased)
            perror("write"); 
            client_free(client); 
            return -1;
        } else if (n == 0) {
            client_free(client); 
            return -1;
        }
        bytes_sent += n;
    }
    char newline = '\n';
    int n = write(client->client_fd, &newline, 1);
    if (n < 0) {
        perror("write"); 
        client_free(client); 
        return -1;
    } else if (n == 0) {
        return -1;
    }
    return 1;
}
