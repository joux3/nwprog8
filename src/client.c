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

#define MAX_LENGHT 64
#define SERVER_PORT "13337"

#define COLOR_RED     "\033[22;31m"
#define COLOR_GREEN   "\033[22;32m"
#define COLOR_YELLOW  "\033[01;33m"
#define COLOR_BLUE    "\033[22;34m"
#define COLOR_MAGENTA "\033[22;35m"
#define COLOR_CYAN    "\033[22;36m"
#define COLOR_RESET   "\033[0m"

int tcp_connect(const char *host) {
	int sockfd, n;
	char server_addr[80];
	char *serv = SERVER_PORT;
	struct addrinfo hints, *res, *ressave;

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if ( (n = getaddrinfo(host, serv, &hints, &res)) != 0) {
		fprintf(stderr, "tcp_connect error for %s, %s: %s\n",
			host, serv, gai_strerror(n));
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
		fprintf(stderr, "tcp_connect error for %s, %s\n", host, serv);
		sockfd = -1;
		return 1;
	} else {
		printf("###Connected to: "COLOR_CYAN"%s "COLOR_RESET"[%s]:%s\n", host, server_addr, serv);
	}

	freeaddrinfo(ressave);
	return sockfd;
}

typedef struct sock_thdata
{
	int thread_no;
	int socket;
} thdata;


typedef struct {
	int id;
	char name[16];
	char messages[10][256];
} channel_t;

typedef struct {
	int num_of_channels;
	channel_t *channels[25];
} channel_list_t;

void add_channel(channel_list_t *channel_list, channel_t *channel) {
	channel_list->channels[channel_list->num_of_channels] = channel;
	channel_list->num_of_channels++;
}

channel_t *current_channel;

// Read user input and send to server
void * send_message(void *ptr) {
	char tx_buff[MAX_LENGHT], line[MAX_LENGHT - 1];
	thdata *data;
	data = (thdata *) ptr;
	
	channel_list_t channel_list;
	channel_list.num_of_channels = 0;
	//printf("give commands\n");
	for(;;) {
		memset(line, '\0', sizeof(line));
		memset(tx_buff, '\0', sizeof(tx_buff));
		scanf(" %[0-9a-zA-ZöÖäÄåÅ!#%&?()/.,:; ]", line);
		strcpy(tx_buff, line);
		strcat(tx_buff, "\n");
		if (strcmp(line, "") != 0) {
			
			char command_string[MAX_LENGHT];
			memset(command_string, 0, sizeof(command_string));
			strcpy(command_string, tx_buff);
			char *command = strtok(command_string, " ");
			
			if (strcmp(command, "/j") == 0) {
				char channel_name[16];
				strcpy(channel_name, strtok(NULL, "\n"));
				if (channel_name[0] == '#' && strlen(channel_name) < 16) {
					channel_t channel;
					strcpy(channel.name, channel_name);
					add_channel(&channel_list, &channel);
					current_channel = &channel;
					
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
			
			//printf("chan %d\n", channel_list.num_of_channels);
			if (channel_list.num_of_channels > 0) {
				memset(tx_buff, '\0', sizeof(tx_buff));
				strcpy(tx_buff, "MSG ");
				strcat(tx_buff, current_channel->name);
				strcat(tx_buff, " ");
				strcat(tx_buff, line);
				strcat(tx_buff, "\n");
				//printf("%s\n", tx_buff);
				write(data->socket, tx_buff, strlen(tx_buff));
				continue;
			}	
			
			//printf("%s",tx_buff);
			
			write(data->socket, tx_buff, strlen(tx_buff));
			
		}	
		
		//sleep(1);
	}
}

// Listen to server and print to stdin
void * read_socket(void *ptr) {
	char rx_buff[MAX_LENGHT];
	char line[MAX_LENGHT];
	int read_n = 0;
	thdata *data;
	data = (thdata *) ptr;
	for(;;) {
		//n = 0;
		while (rx_buff[read_n - 1] != '\n') {
			read_n += read(data->socket, rx_buff + read_n, sizeof(rx_buff));
			
			//sleep(2);
			//printf("read_n %d rx_buf %s rx_buff@n-1 %d \n", read_n, rx_buff, rx_buff[read_n - 1]);
			//printf("rx_buf %d", rx_buff[read_n - 1]);
		}
		if (read_n > 0) {
			memset(line, '\0', sizeof(line));
			char *command = strtok(rx_buff, " ");
			if (strcmp(command, "MSG") == 0) {
				char *sender = strtok(NULL, " ");
				char *channel_name = strcpy(line, strtok(NULL, " "));
				if (strcmp(current_channel->name, channel_name) == 0) {
					strcpy(line, sender);
					strcat(line, "> ");
					strcat(line, strtok(NULL, "\n"));
				}	
							
			} else if (strcmp(command, "MOTD") == 0) {
				strcpy(line, COLOR_GREEN);
				strcat(line , strtok(NULL, "\n"));
				strcat(line, COLOR_RESET);
				
			} else if (strcmp(command, "CLOSE") == 0) {
				strcpy(line, "Server closed connection: ");
				strcat(line, strtok(NULL, "\n"));
				
			} else if (strcmp(command, "KILL") == 0) {
				strcpy(line, COLOR_CYAN);
				strcat(line, strtok(NULL, " "));
				strcat(line, COLOR_RESET);
				strcat(line, " was disconnected: ");
				strcat(line, strtok(NULL, "\n"));
				
			} else if (strcmp(command, "LEAVE") == 0) {
				strcpy(line, strtok(NULL, " "));
				strcat(line, " left the channel ");
				strcat(line, strtok(NULL, "\n"));
				
			} else if (strcmp(command, "NAMES") == 0) {
				strcpy(line, "Users on ");
				strcat(line, strtok(NULL, " "));
				strcat(line, ": ");
				strcat(line, strtok(NULL, "\n"));
				
			} else if (strcmp(command, "JOIN") == 0) {
				strcpy(line, COLOR_CYAN);
				strcat(line, strtok(NULL, " "));
				strcat(line, COLOR_RESET);
				strcat(line, " joined the channel");
			} else {
				strcpy(line, rx_buff);
			}
			printf("%s\n", line);
			
		}
		read_n = 0;
		memset(rx_buff, 0, sizeof(rx_buff));
		//sleep(1);
	}
}	


	
int main(int argc, char **argv) {
	
	int sockfd;
	pthread_t thread1, thread2;
	thdata data_r, data_w;
	
	// Requires server address as a command line argument
	if (argc != 2) {
		fprintf(stderr, "usage: <server_address>\n");

		return 1;
	}
	
	sockfd = tcp_connect(argv[1]);
	//printf("###Type '/help'for commands\n");
	
	data_r.thread_no = 1;
	data_r.socket = sockfd;
	
	data_w.thread_no = 2;
	data_w.socket = sockfd;
	
	// Start threads for sending and receiving messages
	if ((pthread_create (&thread1, NULL,  &read_socket, (void *) &data_r)) != 0) 
		perror("pthread_create"); 
	if ((pthread_create (&thread2, NULL,  &send_message, (void *) &data_w)) !=0)
		perror("pthread_create");

	pthread_join(thread1, NULL);
	pthread_join(thread2, NULL);
	
	return 0;
}


