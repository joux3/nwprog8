#include <stdio.h>
#include "network.h"
#include "packets.h"

int main(int argc, char **argv) {
    printf("Server started\n");
    if (argc < 2) {
        printf("To connect to a existing server, pass the address on command line\n");
    } else {
        printf("Using server %s as a connection point to chat network\n", argv[1]);
    }
    init_packets();
    return network_start(argc < 2 ? NULL : argv[1]);
}
