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
    log_debug("Created channel %s\n", channel_name);
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
    log_debug("Destroying channel %s\n", channel->name);
    void *res = cfuhash_delete(channels_hash, channel->name);
    assert(res != NULL);
    assert(cfuhash_num_entries(channel->nicknames) == 0);
    cfuhash_destroy(channel->nicknames);
    free(channel);
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

void channel_broadcast(channel_t *channel, char *packet, int broadcast_servers) {
    if (broadcast_servers) {
        server_broadcast(packet);
    }
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

int handle_client_packet(client_t *client, char *packet) {
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
            log_debug("Unregistered user illegal nick, dropping\n");
            send_packet((conn_t*)client, "CLOSE Illegal nickname");
            client_free(client);
        } else {
            int nicklen = strlen(nickname);
            if (nicklen > 0 && nicklen < NICKNAME_LENGTH && nickname[0] != '#') {
                if (!cfuhash_exists(nicknames_hash, nickname)) {
                    strncpy(client->nick->nick.nickname, nickname, NICKNAME_LENGTH);
                    log_debug("Registered nickname: %s\n", nickname);
	                char packet[NETWORK_MAX_PACKET_SIZE];
                    snprintf(packet, NETWORK_MAX_PACKET_SIZE, "MOTD Welcome to da server, %s!", nickname);
                    send_packet((conn_t*)client, packet);
                    cfuhash_put(nicknames_hash, nickname, client->nick);

                    snprintf(packet, NETWORK_MAX_PACKET_SIZE, "NICK %s", nickname);
                    server_broadcast(packet);
                    return 0;
                } else {
                    log_debug("Unregistered user nickname taken '%s', dropping\n", nickname);
                    send_packet((conn_t*)client, "CLOSE Nickname taken!");
                    client_free(client);
                }
            } else {
                log_debug("Unregistered user illegal nick '%s', dropping\n", nickname);
                send_packet((conn_t*)client, "CLOSE Illegal nickname");
                client_free(client);
            }
        }
    } else {
        log_debug("Unregistered user didn't send NICK as first packet, dropping\n");
        send_packet((conn_t*)client, "CLOSE Please send nickname with NICK");
        client_free(client);
    }
    return STOP_HANDLING;
}

int handle_registered_packet(client_t *client, char *packet) {
    char *command = strtok(packet, " ");
    if (command == NULL) {
        log_debug("Illegal packet from %s: %s\n", client->nick->nick.nickname, packet);
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

        log_debug("Message from '%s' to '%s': %s\n", client->nick->nick.nickname, destination, msg);
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
            channel_broadcast(channel, packet, 1);
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
            log_debug("User '%s' failed to join channel '%s', get_or_create_channel NULL\n", client->nick->nick.nickname, channel_name);
            return 0;
        }
        if (cfuhash_exists(channel->nicknames, client->nick->nick.nickname)) {
            send_packet((conn_t*)client, "CMDREPLY You have already joined!");
            return 0;
        }
        log_debug("User '%s' joined channel '%s'\n", client->nick->nick.nickname, channel_name);
        cfuhash_put(channel->nicknames, client->nick->nick.nickname, client->nick);
        client->nick->nick.channels[i] = channel;
        // send the join message to users on the channel
        char packet[NETWORK_MAX_PACKET_SIZE];
        snprintf(packet, NETWORK_MAX_PACKET_SIZE, "JOIN %s %s", client->nick->nick.nickname, channel->name);
        channel_broadcast(channel, packet, 1);
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
        char packet[NETWORK_MAX_PACKET_SIZE];
        snprintf(packet, NETWORK_MAX_PACKET_SIZE, "LEAVE %s %s", client->nick->nick.nickname, channel->name);
        if (cfuhash_num_entries(channel->nicknames) == 0) {
            channel_destroy(channel); 
            server_broadcast(packet);
        } else {
            channel_broadcast(channel, packet, 1);
        }
        log_debug("User '%s' left channel '%s'\n", client->nick->nick.nickname, channel_name);
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
    log_debug("Unhandled packet from %s: %s\n", client->nick->nick.nickname, packet);
    return 0;
}

void remove_from_channels(nickname_t *nick, char *reason) {
    // local nicknames that already know about the disconnect
    cfuhash_table_t *already_sent = cfuhash_new();
    for (int i = 0; i < USER_MAX_CHANNELS; i++) {
        if (nick->channels[i] != NULL) {
            channel_t *channel = nick->channels[i];
            void *res = cfuhash_delete(channel->nicknames, nick->nickname);
            assert(res != NULL);
            if (cfuhash_num_entries(channel->nicknames) == 0) {
                channel_destroy(channel); 
            } else {
                char *key;
                nickname_t *channel_nick;
                char packet[NETWORK_MAX_PACKET_SIZE];
                snprintf(packet, NETWORK_MAX_PACKET_SIZE, "KILL %s %s", nick->nickname, reason);
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

void handle_client_disconnect(client_t *client) {
    if (is_registered(client)) {
        void *res = cfuhash_delete(nicknames_hash, client->nick->nick.nickname);
        assert(res != NULL);
        remove_from_channels((nickname_t*)client->nick, "client disconnected");
        log_debug("Registered user '%s' disconnected\n", client->nick->nick.nickname);
        char packet[NETWORK_MAX_PACKET_SIZE];
        snprintf(packet, NETWORK_MAX_PACKET_SIZE, "KILL %s client disconnected", client->nick->nick.nickname);
        server_broadcast(packet);
    } else {
        log_debug("Unregistered client disconnected\n");
    }
}

void kill_nickname(char *nickname, char *reason) {
    nickname_t *res = (nickname_t*)cfuhash_delete(nicknames_hash, nickname);
    if (res) {
        if (res->type == LOCAL) {
            client_t *client = ((localnick_t*)res)->client;
            remove_from_channels(res, reason);
            log_debug("Nickname '%s' killed\n", client->nick->nick.nickname);
            char packet[NETWORK_MAX_PACKET_SIZE];
            snprintf(packet, NETWORK_MAX_PACKET_SIZE, "KILL %s %s", client->nick->nick.nickname, reason);
            send_packet((conn_t*)client, packet);
            shutdown(client->conn.fd, SHUT_WR);
            client_close(client);
        } else if (res->type == REMOTE) {
            remove_from_channels(res, reason);
            log_debug("Nickname '%s' killed\n", res->nickname);
            free(res);
        } else {
            assert(0);
        }
    }
}

int handle_server_packet(server_t *server, char *packet) {
    log_debug("Packet from another server [%d]: %s\n", server->conn.fd, packet);
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
        log_debug("Nickname %s joined the network on another server\n", nickname);
        if (cfuhash_exists(nicknames_hash, nickname)) {
            log_debug("Nickname collision for '%s'!\n", nickname); 
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
    } else if (strcmp(command, "MSG") == 0) {
        char *sender = strtok(NULL, " ");
        if (sender == NULL) {
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
        if (cfuhash_exists(nicknames_hash, destination)) {
            nickname_t *target = (nickname_t*)cfuhash_get(nicknames_hash, destination);
            if (target->type == LOCAL) {
                char packet[NETWORK_MAX_PACKET_SIZE];
                snprintf(packet, NETWORK_MAX_PACKET_SIZE, "MSG %s %s %s", sender, destination, msg);
                send_packet(get_conn_for(target), packet);
            }
        } else if (cfuhash_exists(channels_hash, destination)) {
            channel_t *channel = (channel_t*)cfuhash_get(channels_hash, destination);
            char packet[NETWORK_MAX_PACKET_SIZE];
            snprintf(packet, NETWORK_MAX_PACKET_SIZE, "MSG %s %s %s", sender, destination, msg);
            channel_broadcast(channel, packet, 0);
        }
    } else if (strcmp(command, "JOIN") == 0) {
        char *nickname = strtok(NULL, " ");
        if (nickname == NULL) {
            return 0;
        }
        char *channel_name = strtok(NULL, "\n");
        if (channel_name == NULL) {
            return 0;
        }
        nickname_t *nick = cfuhash_get(nicknames_hash, nickname);
        if (nick && nick->type == REMOTE &&
            ((remotenick_t*)nick)->server == server) {
            channel_t *channel = get_or_create_channel(channel_name);
            cfuhash_put(channel->nicknames, nick->nickname, nick);
            char packet[NETWORK_MAX_PACKET_SIZE];
            snprintf(packet, NETWORK_MAX_PACKET_SIZE, "JOIN %s %s", nick->nickname, channel->name);
            channel_broadcast(channel, packet, 0);
            int placed = 0;
            for (int i = 0; i < USER_MAX_CHANNELS; i++) {
                if (nick->channels[i] == NULL) {
                    nick->channels[i] = channel;
                    placed = 1; 
                    break;
                }
            }
            assert(placed);
        } else {
            log_warn("Received a JOIN packet from another server for unknown nickname or for nickname that isn't originally from that server!\n");
            return 0;
        }
    } else if (strcmp(command, "LEAVE") == 0) {
        char *nickname = strtok(NULL, " ");
        if (nickname == NULL) {
            return 0;
        }
        char *channel_name = strtok(NULL, "\n");
        if (channel_name == NULL) {
            return 0;
        }
        nickname_t *nick = cfuhash_get(nicknames_hash, nickname);
        if (nick && nick->type == REMOTE &&
            ((remotenick_t*)nick)->server == server) {
            channel_t *channel = cfuhash_get(channels_hash, channel_name);
            if (channel && cfuhash_exists(channel->nicknames, nick->nickname)) {
                int removed = 0;
                for (int i = 0; i < USER_MAX_CHANNELS; i++) {
                    if (nick->channels[i] == channel) {
                        nick->channels[i] = NULL;
                        removed = 1;
                        break;
                    }
                }
                if (!removed) {
                    log_warn("POSSIBLE CRITICAL FAILURE, channel_t thinks user is on channel but nickname_t doesn't!\n");
                }
                cfuhash_delete(channel->nicknames, nick->nickname); 
                if (cfuhash_num_entries(channel->nicknames) == 0) {
                    channel_destroy(channel);
                } else {
                    char packet[NETWORK_MAX_PACKET_SIZE];
                    snprintf(packet, NETWORK_MAX_PACKET_SIZE, "LEAVE %s %s", nick->nickname, channel->name);
                    channel_broadcast(channel, packet, 0);
                }
            }
        } else {
            log_warn("Received a LEAVE packet from another server for unknown nickname or for nickname that isn't originally from that server!\n");
            return 0;
        }
    }
    return 0;
}

void handle_server_connect(server_t *server) {
    // tell the server about all the nicknames we know of 
    char *nickname;
    nickname_t *nickname_struct;
    int res = cfuhash_each(nicknames_hash, &nickname, (void**)&nickname_struct);
    char packet[NETWORK_MAX_PACKET_SIZE];
    while (res != 0) {
        snprintf(packet, NETWORK_MAX_PACKET_SIZE, "NICK %s", nickname);
        send_packet((conn_t*)server, packet);
        // send the channels for the nickname
        for (int i = 0; i < USER_MAX_CHANNELS; i++) {
            if (nickname_struct->channels[i] != NULL) {
                channel_t *channel = nickname_struct->channels[i];
                snprintf(packet, NETWORK_MAX_PACKET_SIZE, "JOIN %s %s", nickname, channel->name);
                send_packet((conn_t*)server, packet);
            }
        }
        res = cfuhash_next(nicknames_hash, &nickname, (void**)&nickname_struct);
    }
    cfuhash_put_data(servers_hash, server, sizeof(server), server, sizeof(server), NULL);
}

int remove_from_server(void *key, size_t key_size, void *data, size_t data_size, void *arg) {
    key = key; // skip unused warnings. we don't really need those parameters
    key_size = key_size;
    data_size = data_size;
    // return true to remove from nicknames_hash
    nickname_t *nick = (nickname_t*)data;
    if (nick->type != REMOTE) {
        return 0;
    }
    remotenick_t *remotenick = (remotenick_t*)nick;
    if (remotenick->server == (server_t*)arg) {
        return 1;
    } else {
        return 0;
    }
}

void free_nickname(void *data) {
    free(data);
}

void handle_server_disconnect(server_t *server) {
    log_debug("Server %d disconnected\n", server->conn.fd);
    void *data = cfuhash_delete_data(servers_hash, server, sizeof(server));
    assert(data != NULL);
    // kill all the nicknames associated with this server
    char packet[NETWORK_MAX_PACKET_SIZE];
    char *nickname;
    nickname_t *nickname_struct;
    int res = cfuhash_each(nicknames_hash, &nickname, (void**)&nickname_struct);
    while (res != 0) {
        if (nickname_struct->type == REMOTE) {
            remotenick_t *remotenick = (remotenick_t*)nickname_struct;
            if (remotenick->server == server) {
                snprintf(packet, NETWORK_MAX_PACKET_SIZE, "KILL %s netsplit", nickname);
                server_broadcast(packet);
                remove_from_channels(nickname_struct, "netsplit");
            }
        }
        res = cfuhash_next(nicknames_hash, &nickname, (void**)&nickname_struct);
    }
    // finally remove from the nicknames from the hashtable
    cfuhash_foreach_remove(nicknames_hash, &remove_from_server, &free_nickname, server);
}

