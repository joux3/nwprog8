#ifndef CHAT_H
#define CHAT_H

#include "cfuhash.h"

#define NICKNAME_LENGTH 10
#define CHANNEL_LENGTH 10
#define CHANNEL_MIN_LENGTH 2 // don't allow just "#" as channel name
#define USER_MAX_CHANNELS 10

typedef struct {
    char name[CHANNEL_LENGTH];
    cfuhash_table_t *clients; // nickname -> client_t
} channel_t;

#endif
