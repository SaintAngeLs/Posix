// Cel:

//     Serwer akceptuje połączenia sieciowe TCP
//     Klient sieciowy TCP

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
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

#define BACKLOG 3
volatile sig_atomic_t do_work = 1;

void sigint_handler(int sig)
{
	do_work = 0;
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
int make_socket(int domain, int type)
{
	int sock;
	sock = socket(domain, type, 0);
	if (sock < 0)
		ERR("socket");
	return sock;
}

int bind_local_socket(char *name)
{
    struct sockaddr_un addr; // Structure variable for local UNIX domain socket address
    int socketfd; // Socket file descriptor
    
    if (unlink(name) < 0 && errno != ENOENT) // Check if file already exists and delete it
        ERR("unlink");

    socketfd = make_socket(PF_UNIX, SOCK_STREAM); // Create a UNIX domain socket
    memset(&addr, 0, sizeof(struct sockaddr_un)); // Initialize addr structure with zeros
    addr.sun_family = AF_UNIX; // Set address family to UNIX domain
    strncpy(addr.sun_path, name, sizeof(addr.sun_path) - 1); // Copy the socket path into addr structure
    
    if (bind(socketfd, (struct sockaddr *)&addr, SUN_LEN(&addr)) < 0) // Bind the socket to the address
        ERR("bind");
    
    if (listen(socketfd, BACKLOG) < 0) // Put the socket in the listening state
        ERR("listen");
    
    return socketfd; // Return the socket file descriptor
}

int bind_tcp_socket(uint16_t port)
{
    struct sockaddr_in addr; // Structure variable for TCP/IP socket address
    int socketfd, t = 1; // Socket file descriptor and flag for setsockopt()

    socketfd = make_socket(PF_INET, SOCK_STREAM); // Create a TCP/IP socket
    memset(&addr, 0, sizeof(struct sockaddr_in)); // Initialize addr structure with zeros
    addr.sin_family = AF_INET; // Set address family to IPv4
    addr.sin_port = htons(port); // Set the port in network byte order
    addr.sin_addr.s_addr = htonl(INADDR_ANY); // Set the IP address to any available interface
    
    if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &t, sizeof(t))) // Set socket option to reuse address
        ERR("setsockopt");
    
    if (bind(socketfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) // Bind the socket to the address
        ERR("bind");
    
    if (listen(socketfd, BACKLOG) < 0) // Put the socket in the listening state
        ERR("listen");
    
    return socketfd; // Return the socket file descriptor
}


int add_new_client(int sfd)
{
	int nfd;
	if ((nfd = TEMP_FAILURE_RETRY(accept(sfd, NULL, NULL))) < 0) {
		if (EAGAIN == errno || EWOULDBLOCK == errno)
			return -1;
		ERR("accept");
	}
	return nfd;
}

void usage(char *name)
{
	fprintf(stderr, "USAGE: %s socket port\n", name);
}

ssize_t bulk_read(int fd, char *buf, size_t count)
{
	int c;
	size_t len = 0;
	do {
		c = TEMP_FAILURE_RETRY(read(fd, buf, count));
		if (c < 0)
			return c;
		if (0 == c)
			return len;
		buf += c;
		len += c;
		count -= c;
	} while (count > 0);
	return len;
}

ssize_t bulk_write(int fd, char *buf, size_t count)
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

void calculate(int32_t data[5])
{
	int32_t op1, op2, result, status = 1;
	op1 = ntohl(data[0]);
	op2 = ntohl(data[1]);
	switch ((char)ntohl(data[3])) {
	case '+':
		result = op1 + op2;
		break;
	case '-':
		result = op1 - op2;
		break;
	case '*':
		result = op1 * op2;
		break;
	case '/':
		if (!op2)
			status = 0;
		else
			result = op1 / op2;
		break;
	default:
		status = 0;
	}
	data[4] = htonl(status);
	data[2] = htonl(result);
}
void communicate(int cfd)
{
	ssize_t size; // Size of data read or written
	int32_t data[5]; // Array to hold integer data

	// Read data from the client socket
	if ((size = bulk_read(cfd, (char *)data, sizeof(int32_t[5]))) < 0)
		ERR("read:");

	// If full data was read successfully
	if (size == (int)sizeof(int32_t[5])) {
		calculate(data); // Perform some calculation on the data

		// Write the processed data back to the client socket
		if (bulk_write(cfd, (char *)data, sizeof(int32_t[5])) < 0 && errno != EPIPE)
			ERR("write:");
	}

	// Close the client socket
	if (TEMP_FAILURE_RETRY(close(cfd)) < 0)
		ERR("close");
}

void doServer(int fdL, int fdT)
{
	int cfd, fdmax;
	fd_set base_rfds, rfds;
	sigset_t mask, oldmask;

	FD_ZERO(&base_rfds); // Clear the base read file descriptor set
	FD_SET(fdL, &base_rfds); // Add fdL to the base read file descriptor set
	FD_SET(fdT, &base_rfds); // Add fdT to the base read file descriptor set
	fdmax = (fdT > fdL ? fdT : fdL); // Calculate the maximum file descriptor value

	sigemptyset(&mask); // Initialize an empty signal mask
	sigaddset(&mask, SIGINT); // Add SIGINT to the signal mask
	sigprocmask(SIG_BLOCK, &mask, &oldmask); // Block the signals in the mask

	while (do_work) {
		rfds = base_rfds; // Copy the base read file descriptor set to rfds

		// Wait for activity on the file descriptors using pselect
		if (pselect(fdmax + 1, &rfds, NULL, NULL, NULL, &oldmask) > 0) {
			// Check if fdL has activity (new client connection)
			if (FD_ISSET(fdL, &rfds))
				cfd = add_new_client(fdL); // Accept the new client connection
			else
				cfd = add_new_client(fdT); // Accept the new client connection

			// If a new client connection was established
			if (cfd >= 0)
				communicate(cfd); // Handle communication with the client
		} else {
			// Check for errors or interrupted system call
			if (EINTR == errno)
				continue; // Restart the loop on interrupted system call
			ERR("pselect");
		}
	}

	sigprocmask(SIG_UNBLOCK, &mask, NULL); // Unblock the signals in the mask
}

int main(int argc, char **argv)
{
	int fdL, fdT; // File descriptors for local and TCP sockets
	int new_flags; // Variable to hold new file flags

	if (argc != 3) {
		usage(argv[0]); // Display usage information
		return EXIT_FAILURE; // Return failure if incorrect arguments provided
	}

	if (sethandler(SIG_IGN, SIGPIPE))
		ERR("Seting SIGPIPE:"); // Set SIGPIPE signal handler to ignore

	if (sethandler(sigint_handler, SIGINT))
		ERR("Seting SIGINT:"); // Set SIGINT signal handler to custom function sigint_handler

	fdL = bind_local_socket(argv[1]); // Bind a local UNIX domain socket
	new_flags = fcntl(fdL, F_GETFL) | O_NONBLOCK; // Get current file flags and set non-blocking flag
	fcntl(fdL, F_SETFL, new_flags); // Set new file flags for fdL

	fdT = bind_tcp_socket(atoi(argv[2])); // Bind a TCP/IP socket
	new_flags = fcntl(fdT, F_GETFL) | O_NONBLOCK; // Get current file flags and set non-blocking flag
	fcntl(fdT, F_SETFL, new_flags); // Set new file flags for fdT

	doServer(fdL, fdT); // Start the server to handle client connections

	if (TEMP_FAILURE_RETRY(close(fdL)) < 0)
		ERR("close"); // Close the local socket

	if (unlink(argv[1]) < 0)
		ERR("unlink"); // Remove the local socket file

	if (TEMP_FAILURE_RETRY(close(fdT)) < 0)
		ERR("close"); // Close the TCP socket

	fprintf(stderr, "Server has terminated.\n"); // Print termination message
	return EXIT_SUCCESS; // Return success
}