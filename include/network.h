#ifndef NETWORK_H
#define NETWORK_H

#include "chat_constants.h"

#define NETWORK_MAX_EVENTS 10
#define NETWORK_LISTEN_Q 10
#define NETWORK_PORT 13337

#define NETWORK_CLIENT_BUF 2048
#define NETWORK_MAX_PACKET_SIZE 256

typedef struct {
	int client_fd;
	int buf_used;
	char buf[NETWORK_CLIENT_BUF];	
	char nickname[NICKNAME_LENGTH];
} client_t;

int network_start();

#endif
