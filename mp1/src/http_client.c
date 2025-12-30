/*
** http_client.c -- mp1 http client
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <arpa/inet.h>

#define MAXDATASIZE 4096 // max number of bytes we can get at once 

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
	int sockfd;  
	struct addrinfo hints, *servinfo, *p;
	int rv;
	char s[INET6_ADDRSTRLEN];

	if (argc != 2) {
	    fprintf(stderr,"usage: ./http_client http://hostname[:port]/path/to/file \n");
	    exit(1);
	}

    char *url = argv[1];
    if (strncmp(url, "http://", 7) != 0) {
        fprintf(stderr, "Only http:// URLs are supported\n");
        exit(1);
    }

    url += 7; // skip "http://"
    char *path_start = strchr(url, '/');
    if (path_start == NULL) {
        fprintf(stderr, "URL must contain a path\n");
        exit(1);
    }

    char hostname[256];
    char port_str[10] = "80"; // default port
    char path[1024];
    strncpy(path, path_start, sizeof(path)-1);

    *path_start = '\0'; // terminate hostname:port string

    char *port_start = strchr(url, ':');
    if (port_start != NULL) {
        *port_start = '\0';
        strncpy(port_str, port_start + 1, sizeof(port_str)-1);
        port_str[sizeof(port_str)-1] = '\0';
    }
    strncpy(hostname, url, sizeof(hostname)-1);
    hostname[sizeof(hostname)-1] = '\0';

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if ((rv = getaddrinfo(hostname, port_str, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and connect to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("http_client: socket");
			continue;
		}

        inet_ntop(p->ai_family,
            get_in_addr((struct sockaddr *)p->ai_addr),
            s, sizeof s);
        printf("http_client: attempting connection to %s\n", s);

		if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			perror("http_client: connect");
			close(sockfd);
			continue;
		}

		break;
	}

	if (p == NULL) {
		fprintf(stderr, "http_client: failed to connect\n");
		return 2;
	}

	inet_ntop(p->ai_family,
			get_in_addr((struct sockaddr *)p->ai_addr),
			s, sizeof s);
	printf("http_client: connected to %s\n", s);

	freeaddrinfo(servinfo); // all done with this structure

    char request[2048];
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.1\r\n"
             "User-Agent: http_client/1.0\r\n"
             "Host: %s\r\n"
             "Connection: Keep-Alive\r\n"
             "\r\n",
             path, hostname);

    int len = strlen(request);
    if (send(sockfd, request, len, 0) != len) {
        perror("http_client: send");
        close(sockfd);
        exit(1);
    }

    FILE *output_file = fopen("output", "wb");
    if (output_file == NULL) {
        perror("fopen");
        close(sockfd);
        exit(1);
    }

	uint32_t file_size;
	if (recv(sockfd, &file_size, sizeof(file_size), MSG_WAITALL)!= sizeof(file_size)) {
		perror("recv file size");
		close(sockfd);
		exit(1);
	}

	printf("http_client: received %u bytes\n", file_size);

    char buffer[MAXDATASIZE];
    int bytes_received;
    int headers_ended = 0;

    while ((bytes_received = recv(sockfd, buffer, sizeof(buffer), 0)) > 0) {
        buffer[bytes_received] = '\0';
        char *body_start = buffer;

        if (!headers_ended) {
            body_start = strstr(buffer, "\r\n\r\n");
            if (body_start != NULL) {
                headers_ended = 1;
                body_start += 4; // skip past the header delimiter
                size_t body_len = bytes_received - (body_start - buffer);
                if (body_len > 0) {
                    fwrite(body_start, 1, body_len, output_file);
                }
            }
        } else {
            fwrite(buffer, 1, bytes_received, output_file);
        }
    }

    if (bytes_received == -1) {
        perror("recv");
    }

    fclose(output_file);
    close(sockfd);

    printf("File downloaded and saved to 'output'.\n");

	return 0;
}

