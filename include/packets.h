#ifndef PACKETS_H
#define PACKETS_H

#include "network.h"

#define STOP_HANDLING 17273

void init_packets();

// handles a packet for the given client
// handle_packet MUST NOT assume that any data pointed by
// packet will be valid after the function call
// return value: returns STOP_HANDLING if the network layer should stop handling this client
int handle_client_packet(client_t *client, char *packet);

// called before client_t is freed from memory
void handle_client_disconnect(client_t *client);

// handles a packet for the given server
// handle_packet MUST NOT assume that any data pointed by
// packet will be valid after the function call
// return value: returns STOP_HANDLING if the network layer should stop handling this server
int handle_server_packet(server_t *server, char *packet);

// called when a new server connection happens
void handle_server_connect(server_t *server);

// called before server_t is freed from memory
void handle_server_disconnect(server_t *server);

#endif
