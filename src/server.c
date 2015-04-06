#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "network.h"
#include "packets.h"
#include "cfuconf.h"

int get_and_set_port(char *port_str, uint16_t *port) {
    char *endptr;
    long val = strtol(port_str, &endptr, 10);
    if ((errno != 0 && val == 0) ||
        (endptr == port_str) ||
        (val <= 0 || val > UINT16_MAX)) {
        return 0;
    }
    *port = (uint16_t)val;
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: server <config_file>\n");
        return 1;
    } else {
        printf("Reading config from %s...\n", argv[1]);
    }
   
    cfuconf_t *config;     
    char *config_error;
    if (cfuconf_parse_file(argv[1], &config, &config_error) < 0) {
        if (config_error) {
            printf("Error reading configuration!\n");
            free(config_error);
        } else {
            printf("Error reading configuration! Cause: %s\n", config_error);
        }
        return 2;
    }

    char *connect_to = NULL;
    if (cfuconf_get_directive_one_arg(config, "connect_to", &connect_to) < 0) {
        printf("The server to connect can be defined using 'connect_to' config parameter\n");
    } else {
        printf("The server to connect: %s\n", connect_to);
    }

    char *client_port_str = NULL;
    uint16_t client_port = NETWORK_DEFAULT_CLIENT_PORT;
    if (cfuconf_get_directive_one_arg(config, "client_port", &client_port_str) < 0) {
        printf("The port used by clients to connect can be defined with 'client_port' config parameter\n");
    } else if (!get_and_set_port(client_port_str, &client_port)) {
        printf("Invalid port number for 'client_port'!\n");     
        return 2;
    }

    char *server_port_str = NULL;
    uint16_t server_port = NETWORK_DEFAULT_SERVER_PORT;
    if (cfuconf_get_directive_one_arg(config, "server_port", &server_port_str) < 0) {
        printf("The port used by servers to communicate with each other can be defined with 'server_port' config parameter\n");
    } else if (!get_and_set_port(server_port_str, &server_port)) {
        printf("Invalid port number for 'server_port'!\n");     
        return 2;
    }

    cfuconf_destroy(config);

    printf("Using port %d for client connections\n", client_port);
    printf("Using port %d for server communication\n", server_port);
    if (client_port == server_port) {
        printf("Client and server communication ports can't be the same!\n");
        return 2;
    }

    printf("Server starting...\n");
    init_packets();
    return network_start(client_port, connect_to, NETWORK_DEFAULT_SERVER_PORT);
}
