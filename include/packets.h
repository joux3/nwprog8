#ifndef PACKETS_H
#define PACKETS_H

#include "network.h"

// handles a packet for the given client
// handle_packet MUST NOT assume that any data pointed by
// packet will be valid after the function call
void handle_packet(client_t *client, char *packet);

#endif
