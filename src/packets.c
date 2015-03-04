#include <stdio.h>
#include <string.h>
#include "packets.h"

void handle_unregisted_packet(client_t *client, char *packet);
void handle_registed_packet(client_t *client, char *packet);

int is_registered(client_t *client) {
    return client->nickname[0] != '\0';
}

void handle_packet(client_t *client, char *packet) {
    if (is_registered(client)) {
        handle_registed_packet(client, packet);        
    } else {
        handle_unregisted_packet(client, packet);
    }
}

void send_packet(client_t *client, char *packet) {
    network_send(client, packet, strlen(packet));
}

void handle_unregisted_packet(client_t *client, char *packet) {
    char *command = strtok(packet, " ");
    if (command != NULL && strcmp(command, "NICK") == 0) {
        // TODO add check if nick is already taken
        char *nickname = strtok(NULL, " ");
        if (nickname == NULL) {
            printf("Unregistered user illegal nick, dropping\n");
            send_packet(client, "CLOSE Illegal nickname\n");
            client_free(client);
        } else {
            int nicklen = strlen(nickname);
            if (nicklen > 0 && nicklen < NICKNAME_LENGTH) {
                strncpy(client->nickname, nickname, NICKNAME_LENGTH);
                printf("Registered nickname: %s\n", nickname);
                char packet[256];
                int n = snprintf(packet, 256, "MOTD Welcome to da server, %s!\n", nickname);
                network_send(client, packet, n);
            } else {
                printf("Unregistered user illegal nick '%s', dropping\n", nickname);
                send_packet(client, "CLOSE Illegal nickname\n");
                client_free(client);
            }
        }
    } else {
        printf("Unregistered user didn't send NICK as first packet, dropping\n");
        send_packet(client, "CLOSE Please send nickname with NICK\n");
        client_free(client);
    }
}

void handle_registed_packet(client_t *client, char *packet) {
    printf("Unhandled packet from %s: %s\n", client->nickname, packet);
}

void handle_disconnect(client_t *client) {
    if (is_registered(client)) {
        printf("Registered user '%s' disconnected\n", client->nickname);
    } else {
        printf("Unregistered client disconnected\n");
    }
}
