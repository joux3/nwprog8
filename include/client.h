#ifndef ClIENT_H
#define CLIENT_H

#define MAX_LENGTH 256
#define DEFAULT_SERVER_PORT "13337"

#define NICKNAME_LENGTH 10
#define CHANNEL_LENGTH 10
#define CHANNEL_MIN_LENGTH 2 // don't allow just "#" as channel name
#define USER_MAX_CHANNELS 10

typedef struct {
	char message[MAX_LENGTH];
	struct tm *tm_p;
	
} message_t;

typedef struct {
	//int id;
	char name[CHANNEL_LENGTH];
	//cfuhash_table_t *messages;
} channel_t;


#endif
