/*
** http_server.c -- mp1 http server
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

#define BACKLOG 10   // how many pending connections queue will hold
#define MAX_REQUEST_SIZE 2048
#define CHUNK_SIZE 4096

void sigchld_handler(int s)
{
	(void)s; // quiet unused variable warning

	// waitpid() might overwrite errno, so we save and restore it:
	int saved_errno = errno;

	while(waitpid(-1, NULL, WNOHANG) > 0);

	errno = saved_errno;
}


// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(int argc, char *argv[])
{
	// listen on sock_fd, new connection on new_fd
	int sockfd, new_fd;
	struct addrinfo hints, *servinfo, *p;
	struct sockaddr_storage their_addr; // connector's address info
	socklen_t sin_size;
	struct sigaction sa;
	int yes=1;
	char s[INET6_ADDRSTRLEN];
	int rv;

	if (argc != 2) {
		fprintf(stderr,"usage: ./http_server port\n");
		exit(1);
	}

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

	if ((rv = getaddrinfo(NULL, argv[1], &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("http_server: socket");
			continue;
		}

		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
				sizeof(int)) == -1) {
			perror("setsockopt");
			exit(1);
		}

		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("http_server: bind");
			continue;
		}

		break;
	}

	freeaddrinfo(servinfo); // all done with this structure

	if (p == NULL)  {
		fprintf(stderr, "http_server: failed to bind\n");
		exit(1);
	}

	if (listen(sockfd, BACKLOG) == -1) {
		perror("listen");
		exit(1);
	}

	sa.sa_handler = sigchld_handler; // reap all dead processes
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		perror("sigaction");
		exit(1);
	}

	printf("http_server: waiting for connections on port %s...\n", argv[1]);

	while(1) {  // main accept() loop
		sin_size = sizeof their_addr;
		new_fd = accept(sockfd, (struct sockaddr *)&their_addr,
				&sin_size);
		if (new_fd == -1) {
			perror("accept");
			continue;
		}

		inet_ntop(their_addr.ss_family,
			get_in_addr((struct sockaddr *)&their_addr),
			s, sizeof s);
		printf("http_server: got connection from %s\n", s);

		if (!fork()) { // this is the child process
			close(sockfd); // child doesn't need the listener

            char request[MAX_REQUEST_SIZE];
            int bytes_received = recv(new_fd, request, MAX_REQUEST_SIZE - 1, 0);
            if (bytes_received <= 0) {
                close(new_fd);
                exit(0);
            }
            request[bytes_received] = '\0';

            char method[10], path[1024], version[20];
            sscanf(request, "%s %s %s", method, path, version);

            printf("http_server: received GET request from %s for %s\n", s, path);

            if (strcmp(method, "GET") != 0) {
                char *response = "HTTP/1.1 400 Bad Request\r\n\r\n";
                send(new_fd, response, strlen(response), 0);
            } else {
                char filepath[1025];
                // Treat path as relative to current directory, skip leading '/'
                snprintf(filepath, sizeof(filepath), ".%s", path + 1);
                
                FILE *fp = fopen(filepath, "rb");
                if (fp == NULL) {
                    char *response = "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n";
                    send(new_fd, response, strlen(response), 0);
                } else {
                    fseek(fp, 0, SEEK_END);
                    long filesize = ftell(fp);
                    fseek(fp, 0, SEEK_SET);

                    char header[256];
                    snprintf(header, sizeof(header),
                             "HTTP/1.1 200 OK\r\n"
                             "Content-Length: %ld\r\n"
                             "Connection: close\r\n"
                             "\r\n",
                             filesize);
                    send(new_fd, header, strlen(header), 0);

                    char buffer[CHUNK_SIZE];
                    size_t bytes_read;
                    while ((bytes_read = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
                        if (send(new_fd, buffer, bytes_read, 0) == -1) {
                            perror("send file content");
                            break;
                        }
                    }
                    fclose(fp);
                }
            }
            close(new_fd);
            exit(0);
        }

        close(new_fd);  // parent doesn't need this
    }

	return 0;
}

