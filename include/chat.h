#ifndef CHAT_H
#define CHAT_H

#define NICKNAME_LENGTH 10
#define CHANNEL_LENGTH 10
#define USER_MAX_CHANNELS 10

typedef struct {
    char name[CHANNEL_LENGTH];
} channel_t;

#endif
