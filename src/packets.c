#include <stdio.h>
#include <string.h>
#include "packets.h"

void handle_unregisted_packet(client_t *client, char *packet);
void handle_registed_packet(client_t *client, char *packet);

void handle_packet(client_t *client, char *packet) {
    if (client->nickname[0] == '\0') {
        handle_unregisted_packet(client, packet);
    } else {
        handle_registed_packet(client, packet);        
    }
}

void handle_unregisted_packet(client_t *client, char *packet) {
    char *command = strtok(packet, " ");
    if (strcmp(command, "NICK") == 0) {
        // TODO add check if nick is already taken
        char *nickname = strtok(NULL, " ");
        if (nickname == NULL) {
            printf("Unregistered user illegal nick, dropping\n");
            client_free(client);
        } else {
            int nicklen = strlen(nickname);
            if (nicklen > 0 && nicklen < NICKNAME_LENGTH) {
                strncpy(client->nickname, nickname, NICKNAME_LENGTH);
                printf("Registered nickname: %s\n", nickname);
            } else {
                printf("Unregistered user illegal nick '%s', dropping\n", nickname);
                client_free(client);
            }
        }
    } else {
        printf("Unregistered user didn't send NICK as first packet, dropping\n");
        client_free(client);
    }
}

void handle_registed_packet(client_t *client, char *packet) {
    printf("Unhandled packet from %s: %s\n", client->nickname, packet);
}
