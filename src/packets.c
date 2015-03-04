#include <stdio.h>
#include <string.h>
#include "packets.h"
#include "cfuhash.h"

cfuhash_table_t *nicknames_hash;
void init_packets() {
    nicknames_hash = cfuhash_new_with_initial_size(1000); 
    cfuhash_set_flag(nicknames_hash, CFUHASH_IGNORE_CASE); 
}

int handle_unregistered_packet(client_t *client, char *packet);
int handle_registered_packet(client_t *client, char *packet);

int is_registered(client_t *client) {
    return client->nickname[0] != '\0';
}

int handle_packet(client_t *client, char *packet) {
    if (is_registered(client)) {
        return handle_registered_packet(client, packet);        
    } else {
        return handle_unregistered_packet(client, packet);
    }
}

void send_packet(client_t *client, char *packet) {
    network_send(client, packet, strlen(packet));
}

int handle_unregistered_packet(client_t *client, char *packet) {
    char *command = strtok(packet, " ");
    if (command != NULL && strcmp(command, "NICK") == 0) {
        char *nickname = strtok(NULL, " ");
        if (nickname == NULL) {
            printf("Unregistered user illegal nick, dropping\n");
            send_packet(client, "CLOSE Illegal nickname");
            client_free(client);
        } else {
            int nicklen = strlen(nickname);
            if (nicklen > 0 && nicklen < NICKNAME_LENGTH) {
                if (!cfuhash_exists(nicknames_hash, nickname)) {
                    strncpy(client->nickname, nickname, NICKNAME_LENGTH);
                    printf("Registered nickname: %s\n", nickname);
                    char packet[255];
                    int n = snprintf(packet, 255, "MOTD Welcome to da server, %s!", nickname);
                    network_send(client, packet, n);
                    cfuhash_put(nicknames_hash, nickname, client);
                    return 0;
                } else {
                    printf("Unregistered user nickname taken '%s', dropping\n", nickname);
                    send_packet(client, "CLOSE Nickname taken!");
                    client_free(client);
                }
            } else {
                printf("Unregistered user illegal nick '%s', dropping\n", nickname);
                send_packet(client, "CLOSE Illegal nickname");
                client_free(client);
            }
        }
    } else {
        printf("Unregistered user didn't send NICK as first packet, dropping\n");
        send_packet(client, "CLOSE Please send nickname with NICK");
        client_free(client);
    }
    return STOP_HANDLING;
}

int handle_registered_packet(client_t *client, char *packet) {
    char *command = strtok(packet, " ");
    if (command != NULL) {
        if (strcmp(command, "MSG") == 0) {
            char *destination = strtok(NULL, " ");
            if (destination != NULL) {
                char *msg = strtok(NULL, "\n");
                if (msg != NULL) {
                    printf("Message from '%s' to '%s': %s\n", client->nickname, destination, msg);
                    if (cfuhash_exists(nicknames_hash, destination)) {
                        char packet[255];
                        snprintf(packet, 255, "MSG %s %s %s", client->nickname, destination, msg);
                        send_packet(((client_t*)cfuhash_get(nicknames_hash, destination)), packet);
                        return 0;
                    } else {
                        // TODO: nick doesn't exist
                    }
                }
            }
        }
    }
    printf("Unhandled packet from %s: %s\n", client->nickname, packet);
    return 0;
}

void handle_disconnect(client_t *client) {
    if (is_registered(client)) {
        cfuhash_delete(nicknames_hash, client->nickname);
        printf("Registered user '%s' disconnected\n", client->nickname);
    } else {
        printf("Unregistered client disconnected\n");
    }
}
