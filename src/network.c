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

int start_listening(uint16_t port);
int make_nonblock(int);
int start_epoll(int, int, char*);
int accept_connection(int, int, connection_type);
int read_for_conn(conn_t *conn);

int network_start(char *connect_address) {
    int client_listen_sock = start_listening(NETWORK_CLIENT_PORT);
    int server_listen_sock = start_listening(NETWORK_SERVER_PORT);
    if (client_listen_sock < 0 || server_listen_sock < 0) {
        printf("Failed to open server socket for client or server-server communication!\n");
        return -1;
    }
    
    printf("Network started\n");
    if (start_epoll(client_listen_sock, server_listen_sock, connect_address) < 0) {
        return -1;
    }

    return 0;
}

int start_listening(uint16_t port) {
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
    servaddr.sin6_port = htons(port);

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
        client->conn.fd = client_fd;
        client->conn.type = CLIENT;
        client->buf_used = 0;
        memset(&client->nickname, 0, NICKNAME_LENGTH);
        memset(&client->channels, 0, USER_MAX_CHANNELS * sizeof(channel_t*));
    }
    return client;
}

void client_free(client_t *client) {
    handle_disconnect(client); 
    close(client->conn.fd);
    free(client);
}

server_t *server_create(int server_fd) {
    server_t *server = malloc(sizeof(server_t));
    memset(server, 0, sizeof(server_t));
    if (server != NULL) {
        server->conn.fd = server_fd;
        server->conn.type = SERVER;
        server->buf_used = 0;
    }
    return server;
}

void server_free(server_t *server) {
    handle_server_disconnect(server); 
    close(server->conn.fd);
    free(server);
}

void conn_free(conn_t *conn) {
    if (conn->type == CLIENT) {
        client_free((client_t*)conn);
    } else if (conn->type == SERVER) {
        server_free((server_t*)conn);
    }
}

int start_epoll(int client_listen_sock, int server_listen_sock, char *connect_address) {
    struct epoll_event ev, events[NETWORK_MAX_EVENTS];
    int nfds, epollfd;

    memset(&ev, 0, sizeof(struct epoll_event));

    epollfd = epoll_create1(0);
    if (epollfd == -1) {
        perror("epoll_create1");
        return -1;
    }

    ev.events = EPOLLIN;
    ev.data.ptr = &client_listen_sock;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, client_listen_sock, &ev) == -1) {
        perror("epoll_ctl: client_listen_sock");
        return -1;
    }

    ev.events = EPOLLIN;
    ev.data.ptr = &server_listen_sock;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, server_listen_sock, &ev) == -1) {
        perror("epoll_ctl: server_listen_sock");
        return -1;
    }

    for (;;) {
        nfds = epoll_wait(epollfd, events, NETWORK_MAX_EVENTS, 1000);
        if (nfds == -1) {
            perror("epoll_pwait");
            return -1;
	    }

        for (int n = 0; n < nfds; ++n) {
            if (events[n].data.ptr == &client_listen_sock) {
                if (accept_connection(epollfd, client_listen_sock, CLIENT) < 0) {
                    return -1;
                }
            } else if (events[n].data.ptr == &server_listen_sock) {
                printf("server conn\n");
                if (accept_connection(epollfd, server_listen_sock, SERVER) < 0) {
                    return -1;
                }
            } else if (events[n].events & EPOLLIN) {
                conn_t *conn = events[n].data.ptr;
                if (read_for_conn(conn) < 0) {
                    return -1;
                }
            } else {
                // not from listening sockets and not about reading? must be a closed socket
                conn_t *conn = events[n].data.ptr;
                if (conn->type == CLIENT) {
                    client_free((client_t*)conn);  
                } else if (conn->type == SERVER) {
                    server_free((server_t*)conn);
                }
            }
        }
    }
}

int accept_connection(int epollfd, int listen_sock, connection_type type) {
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
    if (type == SERVER) {
        server_t *server = server_create(conn_sock); // TODO: server_create can return NULL
        ev.data.ptr = server;
        handle_server_connect(server);
    } else if (type == CLIENT) {
        ev.data.ptr = client_create(conn_sock); // TODO: client_create can return NULL
    }
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, conn_sock,
                &ev) == -1) {
        perror("epoll_ctl: conn_sock");
        return -1;
    }
    return 1;
}

// tries to read packets for the given connection
int read_for_conn(conn_t *conn) {
    if (conn->type == CLIENT) {
        client_t *client = (client_t*)conn;
        int n = read(client->conn.fd, &client->buf[client->buf_used], NETWORK_CLIENT_BUF - client->buf_used);
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
    } else if (conn->type == SERVER) {
        printf("read from server\n");
        server_t *server = (server_t*)conn;
        int n = read(server->conn.fd, &server->buf[server->buf_used], NETWORK_SERVER_BUF - server->buf_used);
        if (n > 0) {
            server->buf_used += n;

            int packet_start = 0;
            int packet_size = 1;
            for (int i = 0; i < server->buf_used; i++) {
                if (packet_size > NETWORK_MAX_PACKET_SIZE) {
                    printf("Server tried to send a packet too long!\n");
                    server_free(server);
                    return 1;
                }
                if (server->buf[i] == '\n') {
                    server->buf[i] = '\0';
                    // pass the packet to the next layer to handle
                    if (handle_server_packet(server, &server->buf[packet_start]) == STOP_HANDLING) {
                        return 1;
                    }
                    packet_start = i + 1;
                    packet_size = 1;
                } 
                packet_size++;
            }
            // copy the rest to the beginning of the buffer
            // at this point, packet_size contains length of the broken packet at end
            if (packet_start != 0 && server->buf_used - packet_start > 0) {
                memcpy(&server->buf, &server->buf[packet_start], server->buf_used - packet_start);
            }
            server->buf_used = server->buf_used - packet_start;
        } else if (n == 0) {
            server_free(server);
        } else {
            perror("read ");
            // TODO: check that errno isn't EAGAIN or EWOULDBLOCK 
            // (though in theory this shouldn't be possible)
            server_free(server);
        }
        return 1; 
    } else {
        printf("Unknown conn type!\n");
        return -1;
    }
}

int network_send(conn_t *conn, const void *data, const size_t size) {
    size_t bytes_sent = 0;
    while (bytes_sent < size) {
        int n = write(conn->fd, data, size);
        if (n < 0) {
            // Note: this could also be caused by error EWOULDBLOCK or EAGAIN
            // if this happens very often, userland side send buffering could also be used
            // (or kernel side buffers increased)
            perror("write"); 
            conn_free(conn); 
            return -1;
        } else if (n == 0) {
            conn_free(conn); 
            return -1;
        }
        bytes_sent += n;
    }
    char newline = '\n';
    int n = write(conn->fd, &newline, 1);
    if (n < 0) {
        perror("write"); 
        conn_free(conn); 
        return -1;
    } else if (n == 0) {
        return -1;
    }
    return 1;
}
