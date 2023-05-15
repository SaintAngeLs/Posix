#define _GNU_SOURCE
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

int make_socket(void)
{
	int sock;
	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock < 0)
		ERR("socket");
	return sock;
}

struct sockaddr_in make_address(char *address, char *port)
{
	struct sockaddr_in addr;
	struct addrinfo *result;
	struct addrinfo hints = {};
	hints.ai_family = AF_INET;
	int ret;
	if ((ret = getaddrinfo(address, port, &hints, &result))) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret));
		exit(EXIT_FAILURE);
	}
	addr = *(struct sockaddr_in *)(result->ai_addr);
	freeaddrinfo(result);
	return addr;
}

int connect_socket(char *name, char *port)
{
	struct sockaddr_in addr;
	int socketfd;
	socketfd = make_socket();
	addr = make_address(name, port);
	if (connect(socketfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) < 0) {
		ERR("connect");
	}
	return socketfd;
}

ssize_t bulk_read(int fd, void *buf, size_t count)
{
	int c;
	size_t len = 0;
	do {
		c = TEMP_FAILURE_RETRY(read(fd, buf, count));
		if (c < 0)
			return c;
		if (c == 0)
			return len;
		buf += c;
		len += c;
		count -= c;
	} while (count > 0);
	return len;
}

ssize_t bulk_write(int fd, const void *buf, size_t count)
{
	int c;
	size_t len = 0;
	do {
		c = TEMP_FAILURE_RETRY(write(fd, buf, count));
		if (c < 0)
			return c;
		buf += c;
		len += c;
		count -= c;
	} while (count > 0);
	return len;
}

void prepare_request(char **argv, int32_t data[3])
{
	data[0] = htonl(atoi(argv[1]));
	data[1] = htonl(atoi(argv[2]));
	data[2] = htonl(atoi(argv[3]));
}

void print_response(int32_t response)
{
	printf("Max number received so far: %d\n", ntohl(response));
}

void usage(char *name)
{
	fprintf(stderr, "USAGE: %s domain port number\n", name);
}

int main(int argc, char **argv)
{
	int fd;
	int32_t request[3];
	int32_t response;
	if (argc != 4) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}
	fd = connect_socket(argv[1], argv[2]);
	prepare_request(argv, request);
	if (bulk_write(fd, request, sizeof(request)) < 0) {
		ERR("write");
	}
	if (bulk_read(fd, &response, sizeof(response)) < 0) {
		ERR("read");
	}
	print_response(ntohl(response));
	close(fd);
	return EXIT_SUCCESS;
}
