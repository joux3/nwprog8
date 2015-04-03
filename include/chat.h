#ifndef CHAT_H
#define CHAT_H

#include "network.h"
#include "cfuhash.h"

#define NICKNAME_LENGTH 10
#define CHANNEL_LENGTH 10
#define CHANNEL_MIN_LENGTH 2 // don't allow just "#" as channel name
#define USER_MAX_CHANNELS 10

typedef struct {
    char name[CHANNEL_LENGTH];
    cfuhash_table_t *clients; // nickname -> client_t
} channel_t;

typedef enum {
	REMOTE, LOCAL
} nickname_type;

typedef struct {
	char nickname[NICKNAME_LENGTH];
    nickname_type type;
} nickname_t;

struct client_struct;
typedef struct {
    nickname_t nick;
	channel_t *channels[USER_MAX_CHANNELS];
    struct client_struct *client;
} localnick_t;

struct server_struct;
typedef struct {
    nickname_t nick;
    struct server_struct *server;
} remotenick_t;

#endif
