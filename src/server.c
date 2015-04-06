#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
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

// try to get the first possible address match
// we can't do actual connects yet
int get_addr(char *host, uint16_t port, int *socket_domain, int *socket_protocol, void **host_address, size_t *host_address_size) {
	struct addrinfo hints, *res, *ressave;
    char port_str[16];
    sprintf(port_str, "%d", port);
    int n;

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if ( (n = getaddrinfo(host, port_str, &hints, &res)) != 0) {
		printf("Error for %s, %s: %s\n", host, port_str, gai_strerror(n));
		return 0;
	}
	ressave = res; // so that we can release the memory afterwards
    int result = 0;

	do {
		int sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (sockfd < 0)
			continue;       /* ignore this one */

		if (res->ai_family != AF_INET && res->ai_family != AF_INET6) {
            close(sockfd);
            continue;
        }

        result = 1;
        *host_address = malloc(res->ai_addrlen);
        memcpy(*host_address, res->ai_addr, res->ai_addrlen);
        *host_address_size = res->ai_addrlen;
        *socket_domain = res->ai_family;
        *socket_protocol = res->ai_protocol;
        break; // we found AF_INET or AF_INET6 
	} while ( (res = res->ai_next) != NULL);

	freeaddrinfo(ressave);
    return result;
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
        connect_to = strdup(connect_to);
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

    void *server_addr = NULL;
    size_t server_addr_size;
    int socket_domain, socket_protocol;
    if (connect_to && !get_addr(connect_to, server_port, &socket_domain, &socket_protocol, &server_addr, &server_addr_size)) {
        printf("Failed to find host %s...\n", connect_to);
        return 3;
    }

    printf("Server starting...\n");
    init_packets();
    return network_start(client_port, socket_domain, socket_protocol, server_addr, server_addr_size, server_port);
}
