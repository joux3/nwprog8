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

int tcp_connect(const char *host) {
	int sockfd, n;
	char server_addr[80];
	char *serv = "13337";
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
		printf("###Connected to: %s [%s:%s]\n", host, server_addr, serv);
	}

	freeaddrinfo(ressave);
	return sockfd;
}

typedef struct sock_thdata
{
	int thread_no;
	int socket;
} thdata;

// Read user input and send to server
void * send_message(void *ptr) {
	char tx_buff[MAX_LENGHT], line[MAX_LENGHT - 1];
	thdata *data;
	data = (thdata *) ptr;
	//printf("give commands\n");
	for(;;) {
		scanf(" %[0-9a-zA-ZöÖäÄåÅ ]", line);
		strcpy(tx_buff, line);
		strcat(tx_buff, "\n");
		if (strcmp(line, "") != 0) {
			write(data->socket, tx_buff, strlen(tx_buff));
		}
		memset(line, '\0', sizeof(line));
		memset(tx_buff, '\0', sizeof(tx_buff));
		//sleep(1);
	}
}

// Listen to server and print to stdin
void * read_socket(void *ptr) {
	char rx_buff[MAX_LENGHT];
	char line[MAX_LENGHT];
	int n;
	thdata *data;
	data = (thdata *) ptr;
	for(;;) {
		n = read(data->socket, rx_buff, sizeof(rx_buff));
		if (n > 0) {
			char *command = strtok(rx_buff, " ");
			if (strcmp(command, "MSG") == 0) {
				strcpy(line, strtok(NULL, " "));
				strcat(line, ": ");
				strcat(line, strtok(NULL, "\n"));				
			} else if (strcmp(command, "MOTD") == 0) {
				strcpy(line , strtok(NULL, "\n"));
				
			} else {
				strcpy(line, rx_buff);
			}
			printf("%s\n", line);
			memset(line, '\0', sizeof(line));
		}
		n = 0;
		memset(rx_buff, '\0', sizeof(rx_buff));
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

