#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/socket.h>
#include "packets.h"
#include "cfuhash.h"

cfuhash_table_t *nicknames_hash; // *char (nickname) -> nickname_t
cfuhash_table_t *channels_hash;  // *char (channel name) -> channel_t
cfuhash_table_t *servers_hash;   // not actually a hash, just a server_t -> server_t mapping

void init_packets() {
    nicknames_hash = cfuhash_new_with_initial_size(1000); 
    cfuhash_set_flag(nicknames_hash, CFUHASH_IGNORE_CASE); 

    channels_hash = cfuhash_new_with_initial_size(1000); 
    cfuhash_set_flag(channels_hash, CFUHASH_IGNORE_CASE); 

    servers_hash = cfuhash_new();
    // don't copy server pointers so that pointer comparison works
    cfuhash_set_flag(servers_hash, CFUHASH_NOCOPY_KEYS); 
}

void send_packet(conn_t *conn, char *packet) {
    network_send(conn, packet, strlen(packet));
}

conn_t *get_conn_for(nickname_t *nick) {
    if (nick->type == LOCAL) {
        return (conn_t*)(((localnick_t*)nick)->client);
    } else if (nick->type == REMOTE) {
        return (conn_t*)(((remotenick_t*)nick)->server);
    } else {
        assert(0);
    }
}

channel_t *channel_create(char *channel_name) {
    channel_t *channel = malloc(sizeof(channel_t));
    if (channel == NULL)
        return NULL;
    printf("Created channel %s\n", channel_name);
    strncpy(channel->name, channel_name, CHANNEL_LENGTH);
    channel->nicknames = cfuhash_new();
    cfuhash_set_flag(channel->nicknames, CFUHASH_IGNORE_CASE); 
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
    assert(cfuhash_num_entries(channel->nicknames) == 0);
    cfuhash_destroy(channel->nicknames);
    free(channel);
}

void channel_broadcast(channel_t *channel, char *packet) {
    char *key;
    nickname_t *channel_nick;
    int res = cfuhash_each(channel->nicknames, &key, (void**)&channel_nick);
    assert(res != 0);
    do {
        if (channel_nick->type == LOCAL) {
            send_packet((conn_t*)(((localnick_t*)channel_nick)->client), packet);
        }
    } while (cfuhash_next(channel->nicknames, &key, (void**)&channel_nick));
}

void server_broadcast(char *packet) {
    void *cur_key, *cur_data;
    size_t key_len, data_len;
    int more = cfuhash_each_data(servers_hash, &cur_key, &key_len, &cur_data, &data_len);
    while (more) {
        server_t *cur_server = (server_t*)cur_key;
        send_packet((conn_t*)cur_server, packet);
        more = cfuhash_next_data(servers_hash, &cur_key, &key_len, &cur_data, &data_len);
    }
}

void send_channel_names(client_t *client, channel_t *channel) {
    char packet[NETWORK_MAX_PACKET_SIZE];
    memset(packet, 0, NETWORK_MAX_PACKET_SIZE); 
    int packet_header_size = snprintf(packet, NETWORK_MAX_PACKET_SIZE, "NAMES %s", channel->name);
    int current_start = packet_header_size;
    char *nick;
    nickname_t *channel_nick;
    int res = cfuhash_each(channel->nicknames, &nick, (void**)&channel_nick);
    assert(res != 0);
    do {
        int nicklen = strlen(nick);
        // another nickname won't fit the packet
        // send the packet and start from start
        if (current_start + nicklen + 1 >= NETWORK_MAX_PACKET_SIZE) {
            send_packet((conn_t*)client, packet);
            current_start = packet_header_size;
            packet[current_start] = '\0';
        }
        packet[current_start] = ' ';
        strcpy(&packet[current_start + 1], nick);
        current_start += nicklen + 1;
    } while (cfuhash_next(channel->nicknames, &nick, (void**)&channel_nick));
    if (current_start != packet_header_size) {
        send_packet((conn_t*)client, packet);
    }
}

int handle_unregistered_packet(client_t *client, char *packet);
int handle_registered_packet(client_t *client, char *packet);

int is_registered(client_t *client) {
    return client->nick->nick.nickname[0] != '\0';
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
            send_packet((conn_t*)client, "CLOSE Illegal nickname");
            client_free(client);
        } else {
            int nicklen = strlen(nickname);
            if (nicklen > 0 && nicklen < NICKNAME_LENGTH && nickname[0] != '#') {
                if (!cfuhash_exists(nicknames_hash, nickname)) {
                    strncpy(client->nick->nick.nickname, nickname, NICKNAME_LENGTH);
                    printf("Registered nickname: %s\n", nickname);
	                char packet[NETWORK_MAX_PACKET_SIZE];
                    snprintf(packet, NETWORK_MAX_PACKET_SIZE, "MOTD Welcome to da server, %s!", nickname);
                    send_packet((conn_t*)client, packet);
                    cfuhash_put(nicknames_hash, nickname, client->nick);

                    snprintf(packet, NETWORK_MAX_PACKET_SIZE, "NICK %s", nickname);
                    server_broadcast(packet);
                    return 0;
                } else {
                    printf("Unregistered user nickname taken '%s', dropping\n", nickname);
                    send_packet((conn_t*)client, "CLOSE Nickname taken!");
                    client_free(client);
                }
            } else {
                printf("Unregistered user illegal nick '%s', dropping\n", nickname);
                send_packet((conn_t*)client, "CLOSE Illegal nickname");
                client_free(client);
            }
        }
    } else {
        printf("Unregistered user didn't send NICK as first packet, dropping\n");
        send_packet((conn_t*)client, "CLOSE Please send nickname with NICK");
        client_free(client);
    }
    return STOP_HANDLING;
}

int handle_registered_packet(client_t *client, char *packet) {
    char *command = strtok(packet, " ");
    if (command == NULL) {
        printf("Illegal packet from %s: %s\n", client->nick->nick.nickname, packet);
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

        printf("Message from '%s' to '%s': %s\n", client->nick->nick.nickname, destination, msg);
        char packet[NETWORK_MAX_PACKET_SIZE];
        snprintf(packet, NETWORK_MAX_PACKET_SIZE, "MSG %s %s %s", client->nick->nick.nickname, destination, msg);
        if (cfuhash_exists(nicknames_hash, destination)) {
            send_packet(get_conn_for((nickname_t*)cfuhash_get(nicknames_hash, destination)), packet);
        } else if (cfuhash_exists(channels_hash, destination)) {
            channel_t *channel = cfuhash_get(channels_hash, destination);
            if (!cfuhash_exists(channel->nicknames, client->nick->nick.nickname)) {
                send_packet((conn_t*)client, "CMDREPLY You need to join the channel first");
                return 0;
            }
            channel_broadcast(channel, packet);
        } else {
            send_packet((conn_t*)client, "CMDREPLY Nickname or channel not found");
        }
        return 0;
    } else if (strcmp(command, "JOIN") == 0) {
        char *channel_name = strtok(NULL, " ");
        if (channel_name == NULL || strlen(channel_name) < CHANNEL_MIN_LENGTH || strlen(channel_name) >= CHANNEL_LENGTH || channel_name[0] != '#') {
            send_packet((conn_t*)client, "CMDREPLY Illegal channel name");
            return 0;
        }
        // check if user has too many channels
        int i;
        for (i = 0; i <= USER_MAX_CHANNELS; i++) {
            if (i == USER_MAX_CHANNELS) {
                send_packet((conn_t*)client, "CMDREPLY You have joined too many channels");
                return 0;
            }
            if (client->nick->nick.channels[i] == NULL) {
                break;
            }
        }
        // channel slot i is free at this moment
        channel_t *channel = get_or_create_channel(channel_name);
        if (channel == NULL) {
            send_packet((conn_t*)client, "CMDREPLY Joining channel failed, server memory full");
            printf("User '%s' failed to join channel '%s', get_or_create_channel NULL\n", client->nick->nick.nickname, channel_name);
            return 0;
        }
        if (cfuhash_exists(channel->nicknames, client->nick->nick.nickname)) {
            send_packet((conn_t*)client, "CMDREPLY You have already joined!");
            return 0;
        }
        printf("User '%s' joined channel '%s'\n", client->nick->nick.nickname, channel_name);
        cfuhash_put(channel->nicknames, client->nick->nick.nickname, client->nick);
        client->nick->nick.channels[i] = channel;
        // send the join message to users on the channel
        char packet[NETWORK_MAX_PACKET_SIZE];
        snprintf(packet, NETWORK_MAX_PACKET_SIZE, "JOIN %s %s", client->nick->nick.nickname, channel->name);
        channel_broadcast(channel, packet);
        send_channel_names(client, channel);
        return 0;
    } else if (strcmp(command, "LEAVE") == 0) {
        char *channel_name = strtok(NULL, " ");
        if (channel_name == NULL) {
            send_packet((conn_t*)client, "CMDREPLY Illegal channel name");
            return 0;
        }
        int i;
        for (i = 0; i <= USER_MAX_CHANNELS; i++) {
            if (i == USER_MAX_CHANNELS) {
                send_packet((conn_t*)client, "CMDREPLY You are not on that channel");
                return 0;
            }
            if (client->nick->nick.channels[i] && strcasecmp(client->nick->nick.channels[i]->name, channel_name) == 0) {
                break;
            }
        }
        channel_t *channel = client->nick->nick.channels[i];
        client->nick->nick.channels[i] = NULL;
        void *res = cfuhash_delete(channel->nicknames, client->nick->nick.nickname);
        assert(res != NULL);
        if (cfuhash_num_entries(channel->nicknames) == 0) {
            channel_destroy(channel); 
        } else {
            char packet[NETWORK_MAX_PACKET_SIZE];
            snprintf(packet, NETWORK_MAX_PACKET_SIZE, "LEAVE %s %s", client->nick->nick.nickname, channel->name);
            channel_broadcast(channel, packet);
        }
        printf("User '%s' left channel '%s'\n", client->nick->nick.nickname, channel_name);
        return 0;
    } else if (strcmp(command, "NAMES") == 0) {
        char *channel_name = strtok(NULL, " ");
        if (channel_name == NULL) {
            send_packet((conn_t*)client, "CMDREPLY Illegal channel name");
            return 0;
        }
        int i;
        for (i = 0; i <= USER_MAX_CHANNELS; i++) {
            if (i == USER_MAX_CHANNELS) {
                send_packet((conn_t*)client, "CMDREPLY You are not on that channel");
                return 0;
            }
            if (client->nick->nick.channels[i] && strcasecmp(client->nick->nick.channels[i]->name, channel_name) == 0) {
                break;
            }
        }
        channel_t *channel = client->nick->nick.channels[i];
        send_channel_names(client, channel);
        return 0;
    }
    printf("Unhandled packet from %s: %s\n", client->nick->nick.nickname, packet);
    return 0;
}

void remove_from_channels(client_t *client, char *reason) {
    // local nicknames that already know about the disconnect
    cfuhash_table_t *already_sent = cfuhash_new();
    for (int i = 0; i < USER_MAX_CHANNELS; i++) {
        if (client->nick->nick.channels[i] != NULL) {
            channel_t *channel = client->nick->nick.channels[i];
            void *res = cfuhash_delete(channel->nicknames, client->nick->nick.nickname);
            assert(res != NULL);
            if (cfuhash_num_entries(channel->nicknames) == 0) {
                channel_destroy(channel); 
            } else {
                char *key;
                nickname_t *channel_nick;
                char packet[NETWORK_MAX_PACKET_SIZE];
                snprintf(packet, NETWORK_MAX_PACKET_SIZE, "KILL %s %s", client->nick->nick.nickname, reason);
                int res = cfuhash_each(channel->nicknames, &key, (void**)&channel_nick);
                assert(res != 0);
                do {
                    if (channel_nick->type == LOCAL) {
                        client_t *channel_client = ((localnick_t*)channel_nick)->client;
                        if (!cfuhash_exists_data(already_sent, channel_client, sizeof(client_t*))) {
                            // only send if the client doesn't already know about the disconnect
                            send_packet((conn_t*)channel_client, packet);
                            cfuhash_put_data(already_sent, channel_client, sizeof(client_t*), NULL, 0, NULL);
                        }
                    }
                } while (cfuhash_next(channel->nicknames, &key, (void**)&channel_nick));
            }
        }
    }
    cfuhash_destroy(already_sent);
}

void handle_disconnect(client_t *client) {
    if (is_registered(client)) {
        void *res = cfuhash_delete(nicknames_hash, client->nick->nick.nickname);
        assert(res != NULL);
        remove_from_channels(client, "client disconnected");
        printf("Registered user '%s' disconnected\n", client->nick->nick.nickname);
        // TODO tell other servers about the disconnect
    } else {
        printf("Unregistered client disconnected\n");
    }
}

void kill_nickname(char *nickname, char *reason) {
    nickname_t *res = (nickname_t*)cfuhash_delete(nicknames_hash, nickname);
    if (res) {
        if (res->type == LOCAL) {
            client_t *client = ((localnick_t*)res)->client;
            remove_from_channels(client, reason);
            printf("Nickname '%s' killed\n", client->nick->nick.nickname);
            char packet[NETWORK_MAX_PACKET_SIZE];
            snprintf(packet, NETWORK_MAX_PACKET_SIZE, "KILL %s %s", client->nick->nick.nickname, reason);
            send_packet((conn_t*)client, packet);
            shutdown(client->conn.fd, SHUT_WR);
            client_close(client);
        } else if (res->type == REMOTE) {
            // TODO remove from channels
            //remove_from_channels(client, reason);
            printf("Nickname '%s' killed\n", res->nickname);
        } else {
            assert(0);
        }
    }
}

int handle_server_packet(server_t *server, char *packet) {
    printf("Packet from another server [%d]: %s\n", server->conn.fd, packet);
    // broadcast the packet across rest of the network
    void *cur_key, *cur_data;
    size_t key_len, data_len;
    int more = cfuhash_each_data(servers_hash, &cur_key, &key_len, &cur_data, &data_len);
    while (more) {
        server_t *cur_server = (server_t*)cur_key;
        if (cur_server != server) {
            send_packet((conn_t*)cur_server, packet);
        }
        more = cfuhash_next_data(servers_hash, &cur_key, &key_len, &cur_data, &data_len);
    }
    // handle the packet
    char *command = strtok(packet, " "); 
    if (command == NULL) {
        return 0;
    }

    if (strcmp(command, "NICK") == 0) {
        char *nickname = strtok(NULL, " ");
        if (nickname == NULL) {
            return 0;
        }
        printf("Nickname %s joined the network on another server\n", nickname);
        if (cfuhash_exists(nicknames_hash, nickname)) {
            printf("Nickname collision for '%s'!\n", nickname); 
	        char packet[NETWORK_MAX_PACKET_SIZE];
            snprintf(packet, NETWORK_MAX_PACKET_SIZE, "KILL %s nickname collision", nickname);
            server_broadcast(packet);
            kill_nickname(nickname, "nickname collision");
        } else {
            remotenick_t *nick = malloc(sizeof(remotenick_t));
            nick->nick.type = REMOTE;
            nick->server = server;
            strncpy(nick->nick.nickname, nickname, NICKNAME_LENGTH);
            memset(&nick->nick.channels, 0, USER_MAX_CHANNELS * sizeof(channel_t*));
            cfuhash_put(nicknames_hash, nickname, nick);
        }
    } else if (strcmp(command, "KILL") == 0) {
        char *nickname = strtok(NULL, " ");
        if (nickname == NULL) {
            return 0;
        }
        char *reason = strtok(NULL, "\n");
        if (nickname == NULL) {
            return 0;
        }
        kill_nickname(nickname, reason);
    }
    return 0;
}

void handle_server_connect(server_t *server) {
    // tell the server about all the nicknames we know of 
    char *nickname;
    void *data;
    int res = cfuhash_each(nicknames_hash, &nickname, &data);
    char packet[NETWORK_MAX_PACKET_SIZE];
    while (res != 0) {
        snprintf(packet, NETWORK_MAX_PACKET_SIZE, "NICK %s", nickname);
        send_packet((conn_t*)server, packet);
        res = cfuhash_next(nicknames_hash, &nickname, &data);
        // TODO: tell the server about all the channels for each and every nick
    }
    cfuhash_put_data(servers_hash, server, sizeof(server), server, sizeof(server), NULL);
}

void handle_server_disconnect(server_t *server) {
    // TODO kill all the nicknames associated with this server
    printf("Server %d disconnected\n", server->conn.fd);
    void *data = cfuhash_delete_data(servers_hash, server, sizeof(server));
    assert(data != NULL);
}

