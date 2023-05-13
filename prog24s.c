// Napisz dwa programy pracujące w architekturze klient-serwer poprzez połączenie UDP. 
// Zadanie programu klienta polega na wysłaniu pliku podzielonego na datagramy. 
// Zadanie programu serwera polega na odbieraniu plików przesyłanych przez socket i 
// wypisywaniu ich na ekran (bez informacji o pliku z którego dane pochodzą).

// Każdy wysłany do serwera pakiet musi być potwierdzony odpowiednim komunikatem 
// zwrotnym, w razie braku takiego zwrotnego komunikatu (czekamy 0,5s) należy 
// ponawiać wysłanie pakietu. W razie 5 kolejnych niepowodzeń program klienta 
// powinien zakończyć działanie. Potwierdzenia też mogą zaginąć w sieci, ale 
// program powinien sobie i z tym radzić - serwer nie może dwa razy wypisać tego 
// samego fragmentu tekstu.

// Wszystkie dodatkowe dane (wszystko poza tekstem z pliku) przesyłane między 
// serwerem i klientem mają mieć postać liczb typu int32_t. Należ przyjąć, 
// że rozmiar przesyłanych jednorazowo danych (tekst z pliku i dane sterujące) nie 
// może przekroczyć 576B. Naraz serwer może odbierać maksymalnie 5 plików, 6 jednoczesna 
// transmisja ma być zignorowana.

// Program serwer jako parametr przyjmuje numer portu na którym będzie pracował, 
// program klient przyjmuje jako parametry adres i port serwera oraz nazwę pliku.

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
#define MAXBUF 576
#define MAXADDR 5

struct connections {
	int free;
	int32_t chunkNo;
	struct sockaddr_in addr;
};

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

void usage(char *name)
{
	fprintf(stderr, "USAGE: %s port\n", name);
}

int bind_inet_socket(uint16_t port, int type)
{
	struct sockaddr_in addr; // Structure variable for socket address
	int socketfd, t = 1; // Socket file descriptor and temporary variable

	socketfd = make_socket(PF_INET, type); // Create a socket using make_socket()

	memset(&addr, 0, sizeof(struct sockaddr_in)); // Clear the socket address structure
	addr.sin_family = AF_INET; // Set the address family to IPv4
	addr.sin_port = htons(port); // Set the port in network byte order
	addr.sin_addr.s_addr = htonl(INADDR_ANY); // Set the IP address to any available

	if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &t, sizeof(t)))
		ERR("setsockopt"); // Set socket options (SO_REUSEADDR) to reuse the address

	if (bind(socketfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		ERR("bind"); // Bind the socket to the specified address and port

	if (SOCK_STREAM == type)
		if (listen(socketfd, BACKLOG) < 0)
			ERR("listen"); // Listen for incoming connections on a TCP socket

	return socketfd; // Return the socket file descriptor
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

int findIndex(struct sockaddr_in addr, struct connections con[MAXADDR])
{
	int i, empty = -1, pos = -1;

	for (i = 0; i < MAXADDR; i++) {
		if (con[i].free)
			empty = i;
		else if (0 == memcmp(&addr, &(con[i].addr), sizeof(struct sockaddr_in))) {
			pos = i;
			break;
		}
	}

	if (-1 == pos && empty != -1) {
		con[empty].free = 0;
		con[empty].chunkNo = 0;
		con[empty].addr = addr;
		pos = empty;
	}

	return pos; // Return the index of the connection
}

void doServer(int fd)
{
	struct sockaddr_in addr; // Structure variable for client socket address
	struct connections con[MAXADDR]; // Array of connections
	char buf[MAXBUF]; // Buffer for receiving data
	socklen_t size = sizeof(addr); // Size of client socket address
	int i; // Loop variable
	int32_t chunkNo, last; // Variables for chunk number and last flag

	for (i = 0; i < MAXADDR; i++)
		con[i].free = 1; // Initialize connection array

	for (;;) {
		if (TEMP_FAILURE_RETRY(recvfrom(fd, buf, MAXBUF, 0, &addr, &size) < 0))
			ERR("read:"); // Read data from the socket

		if ((i = findIndex(addr, con)) >= 0) {
			chunkNo = ntohl(*((int32_t *)buf)); // Extract chunk number from the received buffer
			last = ntohl(*(((int32_t *)buf) + 1)); // Extract last flag from the received buffer

			if (chunkNo > con[i].chunkNo + 1)
				continue; // Skip processing if the chunk number is not in sequence

			else if (chunkNo == con[i].chunkNo + 1) {
				if (last) {
					printf("Last Part %d\n%s\n", chunkNo, buf + 2 * sizeof(int32_t));
					con[i].free = 1; // Set the connection as free
				} else {
					printf("Part %d\n%s\n", chunkNo, buf + 2 * sizeof(int32_t));
				}
				con[i].chunkNo++; // Increment the chunk number for the connection
			}

			if (TEMP_FAILURE_RETRY(sendto(fd, buf, MAXBUF, 0, &addr, size)) < 0) {
				if (EPIPE == errno)
					con[i].free = 1; // Set the connection as free if the send operation fails with EPIPE
				else
					ERR("send:"); // Error occurred during sending
			}
		}
	}
}

int main(int argc, char **argv)
{
	int fd; // File descriptor for the socket

	if (argc != 2) {
		usage(argv[0]); // Display usage information
		return EXIT_FAILURE; // Return failure if incorrect arguments provided
	}

	if (sethandler(SIG_IGN, SIGPIPE))
		ERR("Seting SIGPIPE:"); // Set SIGPIPE signal handler to ignore

	fd = bind_inet_socket(atoi(argv[1]), SOCK_DGRAM); // Bind the socket to the specified port

	doServer(fd); // Start the server

	if (TEMP_FAILURE_RETRY(close(fd)) < 0)
		ERR("close"); // Close the socket

	fprintf(stderr, "Server has terminated.\n"); // Print termination message

	return EXIT_SUCCESS; // Return success
}
