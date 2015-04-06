#include <stdio.h>
#include "network.h"
#include "packets.h"
#include "cfuconf.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: server <config_file>\n");
        return 1;
    } else {
        printf("Reading config from %s...\n", argv[1]);
    }
   
     

    printf("Server starting...\n");
    init_packets();
    return network_start(NETWORK_DEFAULT_CLIENT_PORT, argc < 2 ? NULL : argv[1], NETWORK_DEFAULT_SERVER_PORT);
}
