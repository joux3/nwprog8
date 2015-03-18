#ifndef NETWORK_H
#define NETWORK_H

#include "chat.h"

typedef enum {
	SERVER, CLIENT
} connection_type;

#define NETWORK_MAX_EVENTS 10
#define NETWORK_LISTEN_Q 10
#define NETWORK_CLIENT_PORT 13337
#define NETWORK_SERVER_PORT 13338

#define NETWORK_CLIENT_BUF 2048
#define NETWORK_SERVER_BUF 65536
#define NETWORK_MAX_PACKET_SIZE 256

typedef struct {
	int fd;
	connection_type type;
} conn_t;

typedef struct {
	conn_t conn;
	int buf_used;
	char buf[NETWORK_CLIENT_BUF];	
	char nickname[NICKNAME_LENGTH];
	channel_t *channels[USER_MAX_CHANNELS];
} client_t;

typedef struct {
	conn_t conn;
	int buf_used;
	char buf[NETWORK_SERVER_BUF];	
} server_t;

// inits the network socket and starts running the event loop
int network_start();

// sends data to the given connection
// returns 1 if successful, < 0 if failure
// (+ closes the connection if failure)
int network_send(conn_t *conn, const void *data, const size_t size);

// frees data associated with a client_t and closes the related fd
void client_free(client_t *client);

#endif
