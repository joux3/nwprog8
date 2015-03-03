#include <stdio.h>
#include "packets.h"

void handle_packet(client_t *client, char *packet) {
    client = client;
    printf("Got packet: %s\n", packet);
}
