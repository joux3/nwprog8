#include <stdio.h>
#include "network.h"
#include "packets.h"

int main() {
    printf("Server started\n");
    init_packets();
    return network_start();
}
