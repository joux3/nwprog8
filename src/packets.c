#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "packets.h"
#include "cfuhash.h"

cfuhash_table_t *nicknames_hash;
cfuhash_table_t *channels_hash;
void init_packets() {
    nicknames_hash = cfuhash_new_with_initial_size(1000); 
    cfuhash_set_flag(nicknames_hash, CFUHASH_IGNORE_CASE); 
    channels_hash = cfuhash_new_with_initial_size(1000); 
    cfuhash_set_flag(channels_hash, CFUHASH_IGNORE_CASE); 
}

void send_packet(client_t *client, char *packet) {
    network_send(client, packet, strlen(packet));
}


channel_t *channel_create(char *channel_name) {
    channel_t *channel = malloc(sizeof(channel_t));
    if (channel == NULL)
        return NULL;
    printf("Created channel %s\n", channel_name);
    strncpy(channel->name, channel_name, CHANNEL_LENGTH);
    channel->clients = cfuhash_new();
    cfuhash_set_flag(channel->clients, CFUHASH_IGNORE_CASE); 
    return channel;
}

channel_t *get_or_create_channel(char *channel_name) {
    channel_t *channel = cfuhash_get(channels_hash, channel_name); 
    if (channel == NULL) {
        channel = channel_create(channel_name);
        if (channel == NULL)
            return NULL;
        cfuhash_put(channels_hash, channel_name, channel);
    }
    return channel;
}

void channel_destroy(channel_t *channel) {
    printf("Destroying channel %s\n", channel->name);
    void *res = cfuhash_delete(channels_hash, channel->name);
    assert(res != NULL);
    assert(cfuhash_num_entries(channel->clients) == 0);
    cfuhash_destroy(channel->clients);
    free(channel);
}

void channel_broadcast(channel_t *channel, char *packet) {
    char *key;
    client_t *channel_client;
    int res = cfuhash_each(channel->clients, &key, (void**)&channel_client);
    assert(res != 0);
    do {
        send_packet(channel_client, packet);
    } while (cfuhash_next(channel->clients, &key, (void**)&channel_client));
}

void send_channel_names(client_t *client, channel_t *channel) {
    char packet[256];
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
            if (nicklen > 0 && nicklen < NICKNAME_LENGTH && nickname[0] != '#') {
                if (!cfuhash_exists(nicknames_hash, nickname)) {
                    strncpy(client->nickname, nickname, NICKNAME_LENGTH);
                    printf("Registered nickname: %s\n", nickname);
	                char packet[256];
                    int n = snprintf(packet, 256, "MOTD Welcome to da server, %s!", nickname);
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
    if (command == NULL) {
        printf("Illegal packet from %s: %s\n", client->nickname, packet);
        return 0;
    }
    if (strcmp(command, "MSG") == 0) {
        if (strcmp(command, "MSG") != 0) {
            return 0;
        }
        char *destination = strtok(NULL, " ");
        if (destination == NULL) {
            return 0;
        }
        char *msg = strtok(NULL, "\n");
        if (msg == NULL) {
            return 0;
        }

        printf("Message from '%s' to '%s': %s\n", client->nickname, destination, msg);
        char packet[256];
        snprintf(packet, 256, "MSG %s %s %s", client->nickname, destination, msg);
        if (cfuhash_exists(nicknames_hash, destination)) {
            send_packet(((client_t*)cfuhash_get(nicknames_hash, destination)), packet);
        } else if (cfuhash_exists(channels_hash, destination)) {
            channel_t *channel = cfuhash_get(channels_hash, destination);
            if (!cfuhash_exists(channel->clients, client->nickname)) {
                send_packet(client, "CMDREPLY You need to join the channel first");
                return 0;
            }
            channel_broadcast(channel, packet);
        } else {
            send_packet(client, "CMDREPLY Nickname or channel not found");
        }
        return 0;
    } else if (strcmp(command, "JOIN") == 0) {
        char *channel_name = strtok(NULL, " ");
        if (channel_name == NULL || strlen(channel_name) < CHANNEL_MIN_LENGTH || strlen(channel_name) >= CHANNEL_LENGTH || channel_name[0] != '#') {
            send_packet(client, "CMDREPLY Illegal channel name");
            return 0;
        }
        // check if user has too many channels
        int i;
        for (i = 0; i <= USER_MAX_CHANNELS; i++) {
            if (i == USER_MAX_CHANNELS) {
                send_packet(client, "CMDREPLY You have joined too many channels");
                return 0;
            }
            if (client->channels[i] == NULL) {
                break;
            }
        }
        // channel slot i is free at this moment
        channel_t *channel = get_or_create_channel(channel_name);
        if (channel == NULL) {
            send_packet(client, "CMDREPLY Joining channel failed, server memory full");
            printf("User '%s' failed to join channel '%s', get_or_create_channel NULL\n", client->nickname, channel_name);
            return 0;
        }
        if (cfuhash_exists(channel->clients, client->nickname)) {
            send_packet(client, "CMDREPLY You have already joined!");
            return 0;
        }
        printf("User '%s' joined channel '%s'\n", client->nickname, channel_name);
        cfuhash_put(channel->clients, client->nickname, client);
        client->channels[i] = channel;
        // send the join message to users on the channel
        char packet[256];
        snprintf(packet, 256, "JOIN %s %s", client->nickname, channel->name);
        channel_broadcast(channel, packet);
        send_channel_names(client, channel);
        return 0;
    } else if (strcmp(command, "LEAVE") == 0) {
        char *channel_name = strtok(NULL, " ");
        if (channel_name == NULL) {
            send_packet(client, "CMDREPLY Illegal channel name");
            return 0;
        }
        int i;
        for (i = 0; i <= USER_MAX_CHANNELS; i++) {
            if (i == USER_MAX_CHANNELS) {
                send_packet(client, "CMDREPLY You are not on that channel");
                return 0;
            }
            if (client->channels[i] && strcasecmp(client->channels[i]->name, channel_name) == 0) {
                break;
            }
        }
        channel_t *channel = client->channels[i];
        client->channels[i] = NULL;
        void *res = cfuhash_delete(channel->clients, client->nickname);
        assert(res != NULL);
        if (cfuhash_num_entries(channel->clients) == 0) {
            channel_destroy(channel); 
        } else {
            char packet[256];
            snprintf(packet, 256, "LEAVE %s %s", client->nickname, channel->name);
            channel_broadcast(channel, packet);
        }
        printf("User '%s' left channel '%s'\n", client->nickname, channel_name);
        return 0;
    } else if (strcmp(command, "NAMES")) {
        char *channel_name = strtok(NULL, " ");
        if (channel_name == NULL) {
            send_packet(client, "CMDREPLY Illegal channel name");
            return 0;
        }
        int i;
        for (i = 0; i <= USER_MAX_CHANNELS; i++) {
            if (i == USER_MAX_CHANNELS) {
                send_packet(client, "CMDREPLY You are not on that channel");
                return 0;
            }
            if (client->channels[i] && strcasecmp(client->channels[i]->name, channel_name) == 0) {
                break;
            }
        }
        channel_t *channel = client->channels[i];
        send_channel_names(client, channel);
    }
    printf("Unhandled packet from %s: %s\n", client->nickname, packet);
    return 0;
}

void remove_from_channels(client_t *client) {
    for (int i = 0; i < USER_MAX_CHANNELS; i++) {
        if (client->channels[i] != NULL) {
            channel_t *channel = client->channels[i];
            void *res = cfuhash_delete(channel->clients, client->nickname);
            assert(res != NULL);
            if (cfuhash_num_entries(channel->clients) == 0) {
                channel_destroy(channel); 
            } else {
                // TODO don't send duplicate KILLs if users share multiple channels
                char packet[256];
                snprintf(packet, 256, "KILL %s client disconnected", client->nickname);
                channel_broadcast(channel, packet);
            }
        }
    }
}

void handle_disconnect(client_t *client) {
    if (is_registered(client)) {
        void *res = cfuhash_delete(nicknames_hash, client->nickname);
        assert(res != NULL);
        remove_from_channels(client);
        printf("Registered user '%s' disconnected\n", client->nickname);
    } else {
        printf("Unregistered client disconnected\n");
    }
}
