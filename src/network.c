#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include "network.h"
#include "packets.h"
#include "logging.h"

int start_listening(uint16_t port);
int make_nonblock(int);
int start_epoll(int, int, int, int, void*, size_t);
int accept_connection(int, int, connection_type);
int read_for_conn(conn_t *conn);
int create_connect_fd();

int connected = 0;
int connect_fd = -1;

int network_start(uint16_t client_port, int socket_domain, int socket_protocol, void *connect_address, size_t connect_address_size, uint16_t server_port) {
    int client_listen_sock = start_listening(client_port);
    int server_listen_sock = start_listening(server_port);
    if (client_listen_sock < 0 || server_listen_sock < 0) {
        log_error("Failed to open server socket for client or server-server communication!\n");
        return -1;
    }
    
    log_info("Network started\n");
    if (start_epoll(client_listen_sock, server_listen_sock, socket_domain, socket_protocol, connect_address, connect_address_size) < 0) {
        return -1;
    }

    return 0;
}

int start_listening(uint16_t port) {
    int listenfd;
    struct sockaddr_in6 servaddr;

    // create socket for listening
    if ((listenfd = socket(AF_INET6, SOCK_STREAM, 0)) < 0) {
        log_error("Failed to create socket! Error: %s\n", strerror(errno));
        return -1;
    }

    // enable SO_REUSEADDR
    int yes = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
    {
        log_error("Failed to set SO_REUSEADDR! Error: %s\n", strerror(errno));
    }

    // Pick a port and bind socket to it.
    // Accept connections from any address.
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin6_family = AF_INET6;
    servaddr.sin6_addr = in6addr_any;
    servaddr.sin6_port = htons(port);

    if (bind(listenfd, (struct sockaddr *) &servaddr,
        sizeof(servaddr)) < 0) {
        log_error("Failed to bind to port %d! Error: %s\n", port, strerror(errno));
        return -1;
    }

    // Set the socket to passive mode, with specified listen queue size
    if (listen(listenfd, NETWORK_LISTEN_Q) < 0) {
        log_error("Failed to listen to socket! Error: %s\n", strerror(errno));
        return -1;
    }

    make_nonblock(listenfd);

    return listenfd;
}

int make_nonblock(int sockfd) {
    int val = fcntl(sockfd, F_GETFL, 0);
    if (fcntl(sockfd, F_SETFL, val | O_NONBLOCK) < 0) {
        log_error("Failed to make socket nonblocking! Error: %s\n", strerror(errno));
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
        localnick_t *nick = malloc(sizeof(localnick_t));
        // TODO nick == NULL
        client->nick = nick;
        nick->client = client;
        nick->nick.type = LOCAL;
        memset(&nick->nick.nickname, 0, NICKNAME_LENGTH);
        memset(&nick->nick.channels, 0, USER_MAX_CHANNELS * sizeof(channel_t*));
    }
    return client;
}

void client_close(client_t *client) {
    close(client->conn.fd);
    free(client->nick);
    free(client);
}

void client_free(client_t *client) {
    handle_client_disconnect(client); 
    client_close(client);
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
    if (server->conn.fd == connect_fd) {
        connected = 0;
    }
    free(server);
}

void conn_free(conn_t *conn) {
    if (conn->type == CLIENT) {
        client_free((client_t*)conn);
    } else if (conn->type == SERVER) {
        server_free((server_t*)conn);
    }
}

int start_epoll(int client_listen_sock, int server_listen_sock, int socket_domain, int socket_protocol, void *connect_address, size_t connect_address_size) {
    struct epoll_event ev, events[NETWORK_MAX_EVENTS];
    int nfds, epollfd;

    memset(&ev, 0, sizeof(struct epoll_event));

    epollfd = epoll_create1(0);
    if (epollfd == -1) {
        log_error("Failed to call epoll_create1! Error: %s\n", strerror(errno));
        return -1;
    }

    ev.events = EPOLLIN;
    ev.data.ptr = &client_listen_sock;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, client_listen_sock, &ev) == -1) {
        log_error("Failed to call epoll_ctl for client_listen_sock! Error: %s\n", strerror(errno));
        return -1;
    }

    ev.events = EPOLLIN;
    ev.data.ptr = &server_listen_sock;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, server_listen_sock, &ev) == -1) {
        log_error("Failed to call epoll_ctl for server_listen_sock! Error: %s\n", strerror(errno));
        return -1;
    }

    int connect_epoll_registered = 0;
    
    for (;;) {
        // if we have the address of another server to connect to
        if (connect_address) {
            if (!connected) {
                // connect not called yet
                connect_fd = create_connect_fd(socket_domain, socket_protocol);
                int res = connect(connect_fd, (struct sockaddr *)connect_address, connect_address_size);
                log_debug("Server connecting to network...\n");
                if (res < 0 && errno != EINPROGRESS) {
                    log_error("Failed to call connect! Error: %s\n", strerror(errno));
                    return -1;
                }
                connected = 1;
                connect_epoll_registered = 0;
            } else {
                // connect called, poll its status
                int error;
                socklen_t error_len = sizeof(error);
                int res = getsockopt(connect_fd, SOL_SOCKET, SO_ERROR, &error, &error_len) < 0;
                if (res < 0) {
                    log_error("Failed to call getsockopt! Error: %s\n", strerror(errno));
                    return -1;
                } else if (error != 0) { 
                    // connection attempt failed
                    log_debug("connection error: %s\n", strerror(error));
                    connected = 0;
                    close(connect_fd);
                } else if (!connect_epoll_registered) {
                    // connection to another server ok,
                    // start handling this connection via epoll aswell
                    log_info("Connected to network!\n");
                    connect_epoll_registered = 1; 
                    memset(&ev, 0, sizeof(struct epoll_event));
                    ev.events = EPOLLIN;
                    server_t *server = server_create(connect_fd); // TODO: server_create can return NULL
                    ev.data.ptr = server;
                    handle_server_connect(server);
                    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, connect_fd,
                                &ev) == -1) {
                        log_error("Failed to call epoll_ctl for conn_sock! Error: %s\n", strerror(errno));
                        return -1;
                    }
                }
            }
        }

        nfds = epoll_wait(epollfd, events, NETWORK_MAX_EVENTS, 1000);
        if (nfds == -1) {
            log_error("Failed to call epoll_wait! Error: %s\n", strerror(errno));
            return -1;
        }

        for (int n = 0; n < nfds; ++n) {
            if (events[n].data.ptr == &client_listen_sock) {
                // connecting client
                if (accept_connection(epollfd, client_listen_sock, CLIENT) < 0) {
                    return -1;
                }
            } else if (events[n].data.ptr == &server_listen_sock) {
                // connecting server
                log_info("Server connected to ours!\n");
                if (accept_connection(epollfd, server_listen_sock, SERVER) < 0) {
                    return -1;
                }
            } else if (events[n].events & EPOLLIN) {
                // data available for a connection (or an error condition)
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
        log_error("Failed to call accept! Error: %s\n", strerror(errno));
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
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, conn_sock, &ev) == -1) {
        log_error("Failed to call epoll_ctl for conn_sock! Error: %s\n", strerror(errno));
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
                    log_debug("Client tried to send a packet too long!\n");
                    client_free(client);
                    return 1;
                }
                if (client->buf[i] == '\n') {
                    client->buf[i] = '\0';
                    // pass the packet to the next layer to handle
                    if (handle_client_packet(client, &client->buf[packet_start]) == STOP_HANDLING) {
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
            log_error("Failed to call read! Error: %s\n", strerror(errno));
            // TODO: check that errno isn't EAGAIN or EWOULDBLOCK 
            // (though in theory this shouldn't be possible)
            client_free(client);
        }
        return 1; 
    } else if (conn->type == SERVER) {
        server_t *server = (server_t*)conn;
        int n = read(server->conn.fd, &server->buf[server->buf_used], NETWORK_SERVER_BUF - server->buf_used);
        if (n > 0) {
            server->buf_used += n;

            int packet_start = 0;
            int packet_size = 1;
            for (int i = 0; i < server->buf_used; i++) {
                if (packet_size > NETWORK_MAX_PACKET_SIZE) {
                    log_debug("Server tried to send a packet too long!\n");
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
            log_error("Failed to call read! Error: %s\n", strerror(errno));
            // TODO: check that errno isn't EAGAIN or EWOULDBLOCK 
            // (though in theory this shouldn't be possible)
            server_free(server);
        }
        return 1; 
    } else {
        log_error("Unknown conn type!\n");
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
            log_error("Failed to call write! Perhaps a kernel buffer is full? Error: %s\n", strerror(errno));
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
        log_error("Failed to call write! Perhaps a kernel buffer is full? Error: %s\n", strerror(errno));
        conn_free(conn); 
        return -1;
    } else if (n == 0) {
        return -1;
    }
    return 1;
}

int create_connect_fd(int socket_domain, int socket_protocol) {
    int connect_fd;
    if ((connect_fd = socket(socket_domain, SOCK_STREAM, socket_protocol)) < 0) {
        log_error("Failed to create connect socket? Error: %s\n", strerror(errno));
        return -1;
    }     

    make_nonblock(connect_fd);
    return connect_fd;
}
