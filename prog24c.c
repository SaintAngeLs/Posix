#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

#define MAXBUF 576
volatile sig_atomic_t last_signal = 0;

void usage(char *name)
{
	fprintf(stderr, "USAGE: %s domain port file \n", name);
}


void sigalrm_handler(int sig)
{
	last_signal = sig;
}

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
	sock = socket(PF_INET, SOCK_DGRAM, 0);
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

	addr = *(struct sockaddr_in *)(result->ai_addr); // Copy the socket address from the result into the addr structure

	freeaddrinfo(result); // Free the memory allocated by getaddrinfo

	return addr; // Return the socket address
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
			return len; // End of file reached, return the total bytes read

		buf += c; // Move the buffer pointer
		len += c; // Update the total bytes read
		count -= c; // Update the remaining count of bytes to read

	} while (count > 0);

	return len; // Return total bytes read
}


void sendAndConfirm(int fd, struct sockaddr_in addr, char *buf1, char *buf2, ssize_t size)
{
	struct itimerval ts; // Structure variable for setting the timer

	if (TEMP_FAILURE_RETRY(sendto(fd, buf1, size, 0, &addr, sizeof(addr))) < 0)
		ERR("sendto:"); // Send the data to the specified address

	memset(&ts, 0, sizeof(struct itimerval)); // Clear the timer structure
	ts.it_value.tv_usec = 500000; // Set the timer value to 500 milliseconds
	setitimer(ITIMER_REAL, &ts, NULL); // Set the real-time timer

	last_signal = 0; // Clear the last signal variable

	while (recv(fd, buf2, size, 0) < 0) { // Receive the confirmation
		if (EINTR != errno)
			ERR("recv:"); // Error occurred during receiving

		if (SIGALRM == last_signal)
			break; // Break the loop if the timeout signal (SIGALRM) is received
	}
}

void doClient(int fd, struct sockaddr_in addr, int file)
{
	char buf[MAXBUF]; // Buffer for storing data to be sent
	char buf2[MAXBUF]; // Buffer for storing received confirmation
	int offset = 2 * sizeof(int32_t); // Offset for storing chunk number and last flag
	int32_t chunkNo = 0; // Chunk number
	int32_t last = 0; // Last flag
	ssize_t size; // Size of data read from file
	int counter; // Counter for retry attempts

	do {
		if ((size = bulk_read(file, buf + offset, MAXBUF - offset)) < 0)
			ERR("read from file:"); // Read data from the file

		*((int32_t *)buf) = htonl(++chunkNo); // Set the chunk number in network byte order

		if (size < MAXBUF - offset) {
			last = 1;
			memset(buf + offset + size, 0, MAXBUF - offset - size); // Pad remaining space with zeroes
		}

		*(((int32_t *)buf) + 1) = htonl(last); // Set the last flag in network byte order

		memset(buf2, 0, MAXBUF); // Clear the confirmation buffer
		counter = 0;

		do {
			counter++;
			sendAndConfirm(fd, addr, buf, buf2, MAXBUF); // Send the data and wait for confirmation
		} while (*((int32_t *)buf2) != htonl(chunkNo) && counter <= 5); // Retry until the expected confirmation is received or the maximum retry attempts are reached

		if (*((int32_t *)buf2) != htonl(chunkNo) && counter > 5)
			break; // Break the loop if the confirmation is not received after maximum retry attempts

	} while (size == MAXBUF - offset); // Continue until the entire file is read
}

int main(int argc, char **argv)
{
	int fd, file; // File descriptors
	struct sockaddr_in addr; // Structure variable for socket address

	if (argc != 4) {
		usage(argv[0]); // Display usage information
		return EXIT_FAILURE; // Return failure if incorrect arguments provided
	}

	if (sethandler(SIG_IGN, SIGPIPE))
		ERR("Seting SIGPIPE:"); // Set SIGPIPE signal handler to ignore

	if (sethandler(sigalrm_handler, SIGALRM))
		ERR("Seting SIGALRM:"); // Set SIGALRM signal handler

	if ((file = TEMP_FAILURE_RETRY(open(argv[3], O_RDONLY))) < 0)
		ERR("open:"); // Open the file for reading

	fd = make_socket(); // Create a socket

	addr = make_address(argv[1], argv[2]); // Create a socket address

	doClient(fd, addr, file); // Perform client operations

	if (TEMP_FAILURE_RETRY(close(fd)) < 0)
		ERR("close"); // Close the socket

	if (TEMP_FAILURE_RETRY(close(file)) < 0)
		ERR("close"); // Close the file

	return EXIT_SUCCESS; // Return success
}