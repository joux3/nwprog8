#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <ctype.h>
#include <time.h>
#include "cfuhash.h"

#define MAX_LENGTH 256
#define DEFAULT_SERVER_PORT "13337"

#define NICKNAME_LENGTH 10
#define CHANNEL_LENGTH 10
#define CHANNEL_MIN_LENGTH 2 // don't allow just "#" as channel name
#define USER_MAX_CHANNELS 10

#define COLOR_RED     "\033[22;31m"
#define COLOR_GREEN   "\033[22;32m"
#define COLOR_YELLOW  "\033[01;33m"
#define COLOR_BLUE    "\033[22;34m"
#define COLOR_MAGENTA "\033[22;35m"
#define COLOR_CYAN    "\033[22;36m"
#define COLOR_RESET   "\033[0m"

#define MOVE_CURSOR_UP "\033[1A"

int tcp_connect(const char *host, const char *serv_port) {
	int sockfd, n;
	char server_addr[80];
	//char *serv = serv_port;
	struct addrinfo hints, *res, *ressave;

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if ( (n = getaddrinfo(host, serv_port, &hints, &res)) != 0) {
		fprintf(stderr, "tcp_connect error for %s, %s: %s\n",
			host, serv_port, gai_strerror(n));
		return -1;
	}
	ressave = res; // so that we can release the memory afterwards

	do {
		sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (sockfd < 0)
			continue;       /* ignore this one */

		if (res->ai_family == AF_INET) {
			inet_ntop(AF_INET, &((struct sockaddr_in *) res->ai_addr)->sin_addr, server_addr, sizeof(server_addr));
		} else if (res->ai_family == AF_INET6) {
			inet_ntop(AF_INET6, &((struct sockaddr_in6 *) res->ai_addr)->sin6_addr, server_addr, sizeof(server_addr));
		} else {
			printf("Bad address\n");
		}
		printf("Trying to connect to %d %s\n", res->ai_family, server_addr);
		if (connect(sockfd, res->ai_addr, res->ai_addrlen) == 0) 
		      break;          /* success */
		perror("Connect failed");
		close(sockfd);  /* ignore this one */
	} while ( (res = res->ai_next) != NULL);

	if (res == NULL) {      /* errno set from final connect() */
		fprintf(stderr, "tcp_connect error for %s, %s\n", host, serv_port);
		sockfd = -1;
		return 1;
	} else {
		printf("###Connected to: "COLOR_CYAN"%s "COLOR_RESET"[%s]:%s\n", host, server_addr, serv_port);
	}

	freeaddrinfo(ressave);
	return sockfd;
}

typedef struct sock_thdata
{
	int thread_no;
	int socket;
	char *nick;
} thdata;

typedef struct {
	char message[MAX_LENGTH];
	struct tm *tm_p;
	
} message_t;

typedef struct {
	//int id;
	char name[CHANNEL_LENGTH];
	cfuhash_table_t *messages;
} channel_t;

cfuhash_table_t *channel_list;

channel_t *channel_create(char *channel_name) {
	channel_t *channel = malloc(sizeof(channel_t));
	if (channel == NULL)
	  return NULL;
	//printf("Created channel %s\n", channel_name);
	strncpy(channel->name, channel_name, CHANNEL_LENGTH);
	channel->messages = cfuhash_new();
	cfuhash_set_flag(channel->messages, CFUHASH_IGNORE_CASE); 
	return channel;
}

channel_t *get_or_add_channel(char *channel_name) {
	channel_t *channel = cfuhash_get(channel_list, channel_name); 
	if (channel == NULL) {
		channel = channel_create(channel_name);
		//if (channel == NULL)
		cfuhash_put(channel_list, channel_name, channel);
		return NULL;
	}
	return channel;
}

/*char *my_channels () {
	if (cfuhash_num_entries(channel_list) == 0) {
		return NULL;
	}
	char channels = char[USER_MAX_CHANNEL * CHANNEL_LENGTH + 9];
	char *channel_;
	int res = cfuhash_each(channel_list, &channel_, (void**)&nickname_struct);
	cfuhash_next
}*/	


channel_t *current_channel;
channel_t *previous_channel;

// Read user input and send to server
void * send_message(void *ptr) {
	char tx_buff[MAX_LENGTH], line[MAX_LENGTH - 1];
	thdata *data;
	data = (thdata *) ptr;
	channel_list = cfuhash_new_with_initial_size(USER_MAX_CHANNELS);
	
	strcpy(tx_buff, "NICK ");
	strcat(tx_buff, data->nick);
	strcat(tx_buff, "\n");
	write(data->socket, tx_buff, strlen(tx_buff));

	for(;;) {
		memset(line, '\0', sizeof(line));
		memset(tx_buff, '\0', sizeof(tx_buff));
		scanf(" %[0-9a-zA-ZöÖäÄåÅ!#%&?()/.,:; ]", line);
		printf(MOVE_CURSOR_UP);
		strcpy(tx_buff, line);
		strcat(tx_buff, "\n");
		if (strcmp(line, "") != 0) {
			
			char command_string[MAX_LENGTH];
			memset(command_string, 0, sizeof(command_string));
			strcpy(command_string, tx_buff);
			char *command = strtok(command_string, " ");
			// Join channel
			if (strcmp(command, "/j") == 0) {
				if (cfuhash_num_entries(channel_list) >= USER_MAX_CHANNELS) {
					printf("Max channel count already reached\n");
					continue;
				}
				char channel_name[CHANNEL_LENGTH];
				strcpy(channel_name, strtok(NULL, "\n"));
				
				if (channel_name[0] == '#' && strlen(channel_name) <= CHANNEL_LENGTH) {
					if (get_or_add_channel(channel_name) != NULL) {
						current_channel = get_or_add_channel(channel_name);
						printf("Active channel changed to %s\n", current_channel->name);
						continue;
					}
					previous_channel = current_channel;
					current_channel = get_or_add_channel(channel_name);
					
					strcpy(tx_buff, "JOIN ");
					strcat(tx_buff, channel_name);
					strcat(tx_buff, "\n");
					write(data->socket, tx_buff, strlen(tx_buff));
					continue;
				} else {
					printf("Illegal channel name\n");
					continue;
				}
			}
			// TODO: Private message
			
			// Leave channel	
			if (strcmp(line, "/l") == 0) {
				strcpy(tx_buff, "LEAVE ");
				strcat(tx_buff, current_channel->name);
				strcat(tx_buff, "\n");
				current_channel = previous_channel;
				printf("You left the channel "COLOR_CYAN"%s"COLOR_RESET"\n", current_channel->name);
				cfuhash_delete(channel_list, current_channel->name);
				write(data->socket, tx_buff, strlen(tx_buff));
				continue;
			} 
			
			// Users on channel	
			if (strcmp(line, "/names") == 0) {
				strcpy(tx_buff, "NAMES ");
				strcat(tx_buff, current_channel->name);
				strcat(tx_buff, "\n");
				write(data->socket, tx_buff, strlen(tx_buff));
				continue;
			}
			// Help
			if (strcmp(line, "/help") == 0) {
				puts("/j #<channel_name>  Join channel or change active channel");
				puts("/l                  Leave channel");
				puts("/names              Show users on channel");
				continue;
			}
			// Quit
			if (strcmp(line, "/quit") == 0) {
				cfuhash_destroy(channel_list);
				exit(0);
			}
			
			// Send message to 
			if (line[0] != '/' && cfuhash_num_entries(channel_list) > 0) {
				memset(tx_buff, '\0', sizeof(tx_buff));
				strcpy(tx_buff, "MSG ");
				strcat(tx_buff, current_channel->name);
				strcat(tx_buff, " ");
				strcat(tx_buff, line);
				strcat(tx_buff, "\n");
				write(data->socket, tx_buff, strlen(tx_buff));
				continue;
			}	
			
			printf("Unknown command\n");
			
			//write(data->socket, tx_buff, strlen(tx_buff));
			
		}	
		
		//sleep(1);
	}
}

// Listen to server and print to stdin
void * read_socket(void *ptr) {
	char rx_buff[MAX_LENGTH];
	char line[MAX_LENGTH];
	char tmp[MAX_LENGTH];
	int read_n = 0;
	thdata *data;
	data = (thdata *) ptr;
	
	time_t msg_time;
	struct tm *tm_p;
	
	
	for(;;) {
		memset(rx_buff, 0, sizeof(rx_buff));
		while (rx_buff[read_n - 1] != '\n') {
			read_n += read(data->socket, rx_buff + read_n, sizeof(rx_buff));
		}
		
		while (read_n > 0) {
			memset(line, '\0', sizeof(line));
			char *rx_line = strtok(rx_buff, "\n");
			char *rx_line_next = strtok(NULL, "\n");
			char *command = strtok(rx_line, " ");
			
			msg_time = time(NULL);
			tm_p = localtime(&msg_time);
			sprintf(tmp, "[%.2d:%.2d] ", tm_p->tm_hour, tm_p->tm_min);
			strcpy(line, tmp);
		
			if (strcmp(command, "MSG") == 0) {
				
				char *sender = strtok(NULL, " ");
				char *channel_name = strcpy(line, strtok(NULL, " "));
				if (strcmp(current_channel->name, channel_name) == 0) {
					strcpy(line, tmp);
					strcat(line, sender);
					strcat(line, "> ");
					strcat(line, strtok(NULL, "\n"));
					
					
				}	
							
			} else if (strcmp(command, "MOTD") == 0) {
				strcat(line, COLOR_GREEN);
				strcat(line , strtok(NULL, "\n"));
				strcat(line, COLOR_RESET);
				
			} else if (strcmp(command, "CLOSE") == 0) {
				strcat(line, "Server closed connection: ");
				strcat(line, strtok(NULL, "\n"));
				
			} else if (strcmp(command, "KILL") == 0) {
				strcat(line, COLOR_CYAN);
				strcat(line, strtok(NULL, " "));
				strcat(line, COLOR_RESET);
				strcat(line, " was disconnected: ");
				strcat(line, strtok(NULL, "\n"));
				
			} else if (strcmp(command, "LEAVE") == 0) {
				strcat(line, COLOR_CYAN);
				strcat(line, strtok(NULL, " "));
				strcat(line, COLOR_RESET);
				strcat(line, " left the channel ");
				strcat(line, strtok(NULL, "\n"));
				
			} else if (strcmp(command, "NAMES") == 0) {
				strcat(line, "Users on ");
				strcat(line, strtok(NULL, " "));
				strcat(line, ": ");
				strcat(line, COLOR_CYAN);
				strcat(line, strtok(NULL, "\n"));
				strcat(line, COLOR_RESET);
				
			} else if (strcmp(command, "JOIN") == 0) {
				strcat(line, COLOR_CYAN);
				strcat(line, strtok(NULL, " "));
				strcat(line, COLOR_RESET);
				strcat(line, " joined the channel");
			} else {
				strcpy(line, rx_buff);
			}
			printf("%s\n", line);
			
			if(rx_line_next != NULL) {
				strcpy(rx_line, rx_line_next);
			} else {
				read_n = 0;
			}
		}
	}
}	


	
int main(int argc, char **argv) {
	
	int sockfd;
	pthread_t thread1, thread2;
	thdata data_r, data_w;
	
	// Requires server address as a command line argument
	if (argc < 3) {
		fprintf(stderr, "usage: ./client <nick> <server_address> <server_port>\n");
		return 1;
	}
	
	if (strlen(argv[1]) > NICKNAME_LENGTH) {
		fprintf(stderr, "Nickname too long");
		return 1;
	}
	if (argc < 4) {
		argv[3] = DEFAULT_SERVER_PORT;
	}
	
	sockfd = tcp_connect(argv[2], argv[3]);
	printf("###Type '/help'for commands\n");
	
	
	
	data_r.thread_no = 1;
	data_r.socket = sockfd;
	
	data_w.thread_no = 2;
	data_w.socket = sockfd;
	data_w.nick = argv[1];
	
	// Start threads for sending and receiving messages
	if ((pthread_create (&thread1, NULL,  &read_socket, (void *) &data_r)) != 0) 
		perror("pthread_create"); 
	if ((pthread_create (&thread2, NULL,  &send_message, (void *) &data_w)) !=0)
		perror("pthread_create");

	pthread_join(thread1, NULL);
	pthread_join(thread2, NULL);
	
	return 0;
}


