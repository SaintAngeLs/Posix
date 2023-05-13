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

int sethandler(void (*f)(int), int sigNo)
{
	struct sigaction act;
	memset(&act, 0, sizeof(struct sigaction));
	act.sa_handler = f;
	if (-1 == sigaction(sigNo, &act, NULL))
		return -1;
	return 0;
}

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
	int ret; // Return value of getaddrinfo
	struct sockaddr_in addr; // Structure variable for socket address
	struct addrinfo *result; // Pointer to the result of getaddrinfo
	struct addrinfo hints = {}; // Structure variable for hints to influence getaddrinfo

	hints.ai_family = AF_INET; // Set the address family to IPv4

	// Call getaddrinfo to resolve the address and port
	if ((ret = getaddrinfo(address, port, &hints, &result))) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret)); // Print error message if getaddrinfo fails
		exit(EXIT_FAILURE); // Terminate the program if getaddrinfo fails
	}

	// Copy the socket address from the result into the addr structure
	addr = *(struct sockaddr_in *)(result->ai_addr);

	freeaddrinfo(result); // Free the memory allocated by getaddrinfo

	return addr; // Return the socket address
}

int connect_socket(char *name, char *port)
{
	struct sockaddr_in addr; // Structure variable for socket address
	int socketfd; // Socket file descriptor

	socketfd = make_socket(); // Create a socket using make_socket()

	addr = make_address(name, port); // Create a socket address using make_address()

	// Connect to the remote address
	if (connect(socketfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) < 0) {
		if (errno != EINTR)
			ERR("connect"); // Error occurred during connection
		else {
			fd_set wfds; // File descriptor set for write events
			int status; // Socket status
			socklen_t size = sizeof(int); // Size of socket status
			FD_ZERO(&wfds); // Clear the write file descriptor set
			FD_SET(socketfd, &wfds); // Add socketfd to the write file descriptor set

			// Wait for the socket to become writable using select
			if (TEMP_FAILURE_RETRY(select(socketfd + 1, NULL, &wfds, NULL, NULL)) < 0)
				ERR("select");

			// Check the socket status using getsockopt
			if (getsockopt(socketfd, SOL_SOCKET, SO_ERROR, &status, &size) < 0)
				ERR("getsockopt");

			if (0 != status)
				ERR("connect"); // Error occurred during connection
		}
	}

	return socketfd; // Return the socket file descriptor
}

ssize_t bulk_read(int fd, char *buf, size_t count)
{
	int c; // Bytes read in each iteration
	size_t len = 0; // Total bytes read

	do {
		c = TEMP_FAILURE_RETRY(read(fd, buf, count)); // Read data from the file descriptor

		if (c < 0)
			return c; // Error occurred during reading

		if (0 == c)
			return len; // Reached end of file, return total bytes read

		buf += c; // Move the buffer pointer
		len += c; // Update the total bytes read
		count -= c; // Update the remaining count of bytes to read

	} while (count > 0);

	return len; // Return total bytes read
}

ssize_t bulk_write(int fd, char *buf, size_t count)
{
	int c; // Bytes written in each iteration
	size_t len = 0; // Total bytes written

	do {
		c = TEMP_FAILURE_RETRY(write(fd, buf, count)); // Write data to the file descriptor

		if (c < 0)
			return c; // Error occurred during writing

		buf += c; // Move the buffer pointer
		len += c; // Update the total bytes written
		count -= c; // Update the remaining count of bytes to write

	} while (count > 0);

	return len; // Return total bytes written
}

void prepare_request(char **argv, int32_t data[5])
{
	data[0] = htonl(atoi(argv[3])); // Convert and store operand1 in network byte order
	data[1] = htonl(atoi(argv[4])); // Convert and store operand2 in network byte order
	data[2] = htonl(0); // Store a placeholder value (unused)
	data[3] = htonl((int32_t)(argv[5][0])); // Convert and store the first character of operation in network byte order
	data[4] = htonl(1); // Store a flag indicating request type (1 for calculation request)
}

void print_answer(int32_t data[5])
{
	if (ntohl(data[4])) // Check if the result flag is non-zero
		printf("%d %c %d = %d\n", ntohl(data[0]), (char)ntohl(data[3]), ntohl(data[1]), ntohl(data[2])); // Print the calculation result
	else
		printf("Operation impossible\n"); // Print message indicating that the operation is impossible
}

void usage(char *name)
{
	fprintf(stderr, "USAGE: %s domain port operand1 operand2 operation\n", name); // Print the usage information
}

int main(int argc, char **argv)
{
	int fd; // File descriptor for the connected socket
	int32_t data[5]; // Array to hold request/response data

	if (argc != 6) {
		usage(argv[0]); // Display usage information
		return EXIT_FAILURE; // Return failure if incorrect arguments provided
	}

	if (sethandler(SIG_IGN, SIGPIPE))
		ERR("Seting SIGPIPE:"); // Set SIGPIPE signal handler to ignore

	fd = connect_socket(argv[1], argv[2]); // Connect to the specified domain and port

	prepare_request(argv, data); // Prepare the request data

	if (bulk_write(fd, (char *)data, sizeof(int32_t[5])) < 0)
		ERR("write:"); // Write the request data to the socket

	if (bulk_read(fd, (char *)data, sizeof(int32_t[5])) < (int)sizeof(int32_t[5]))
		ERR("read:"); // Read the response data from the socket

	print_answer(data); // Print the answer based on the response data

	if (TEMP_FAILURE_RETRY(close(fd)) < 0)
		ERR("close"); // Close the socket

	return EXIT_SUCCESS; // Return success
}