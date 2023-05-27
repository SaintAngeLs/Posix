// Budzik - semafory

// Napisz prosty wielowątkowy serwer czasu wraz z klientem, komunikacja ma odbywać się poprzez UDP w domenie INET. Klient wysyła za ile sekund chce aby serwer go powiadomił po czym czeka na "budzenie" z serwera (które powinna nastąpić w miarę dokładnie po tylu sekundach ile sobie zażyczył). Serwer dla każdego zapytania uruchamia oddzielny wątek, który odmierza czas i udziela odpowiedzi. Klient po otrzymaniu odpowiedzi wyświetla stosowny komunikat i się kończy. Jeśli odpowiedź nie nadejdzie to klient ma mieć własny mechanizm timeout'u awaryjnego, który jeśli zadziała ma skutkować komunikatem TIMEOUT i zakończeniem programu.

// Serwer ma mieć limit 10 aktywnych klientów, jeśli podłączy się 11ty to ma dostać natychmiast specjalną wiadomość o braku zasobów. Limit ma być zaimplementowany na semaforze. Wolno użyć tylko jednego gniazda UDP po stronie serwera, wszystkie datagramy maja składać się wyłącznie z jednej liczby typu int16_t.

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

// Macro for handling errors with source indication
#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

// Number of active clients allowed
#define FS_NUM 10

// Global flag for controlling the server loop
volatile sig_atomic_t do_work = 1;

// Signal handler for SIGINT (Ctrl+C) to stop the server gracefully
void sigint_handler(int sig)
{
    do_work = 0;
}

// Function for setting signal handlers
int sethandler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if (-1 == sigaction(sigNo, &act, NULL))
        return -1;
    return 0;
}

// Function for creating a socket
int make_socket(int domain, int type)
{
    int sock;
    sock = socket(domain, type, 0);
    if(sock < 0)
        ERR("socket");
    return sock;
}

// Function for binding an INET socket
int bind_inet_socket(uint16_t port, int type)
{
    struct sockaddr_in addr;
    int socketfd, t = 1;
    socketfd = make_socket(PF_INET, type);
    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &t, sizeof(t)))
        ERR("setsockopt");
    if (bind(socketfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        ERR("bind");
    return socketfd;
}

// Function for displaying usage information
void usage(char *name)
{
    fprintf(stderr, "USAGE: %s  port\n", name);
}

// Structure for passing arguments to the client communication thread
struct arguments {
    int fd;  // Socket file descriptor
    int16_t time;  // Time requested by the client
    struct sockaddr_in addr;  // Client address
    sem_t *semaphore;  // Semaphore for limiting active clients
};

// Thread function for communicating with a client
void *communicateDgram(void *arg)
{
    struct arguments *args = (struct arguments *)arg;
    int tt;
    fprintf(stderr, "Will sleep for %d\n", ntohs(args->time));
    for (tt = ntohs(args->time); tt > 0; tt = sleep(tt))
        ;
    if (TEMP_FAILURE_RETRY(sendto(args->fd, (char *)(&(args->time)), sizeof(int16_t), 0, &(args->addr),
                                  sizeof(args->addr))) < 0 &&
        errno != EPIPE)
        ERR("sendto");
    if (sem_post(args->semaphore) == -1)
        ERR("sem_post");
    free(args);
    return NULL;
}

// Function for handling the server logic
void doServer(int fd)
{
    int16_t time;
    int16_t deny = -1;
    deny = htons(deny);
    pthread_t thread;
    struct sockaddr_in addr;
    struct arguments *args;
    socklen_t size = sizeof(struct sockaddr_in);
    sem_t semaphore;
    if (sem_init(&semaphore, 0, FS_NUM) != 0)
        ERR("sem_init");
    while (do_work) {
        if (recvfrom(fd, (char *)&time, sizeof(int16_t), 0, &addr, &size) < 0) {
            if (errno == EINTR)
                continue;
            ERR("recvfrom:");
        }
        if (TEMP_FAILURE_RETRY(sem_trywait(&semaphore)) == -1) {
            switch (errno) {
            case EAGAIN:
                if (TEMP_FAILURE_RETRY(
                            sendto(fd, (char *)&deny, sizeof(int16_t), 0, &addr, sizeof(addr))) < 0 &&
                    errno != EPIPE)
                    ERR("sendto");
            case EINTR:
                continue;
            }
            ERR("sem_wait");
        }
        if ((args = (struct arguments *)malloc(sizeof(struct arguments))) == NULL)
            ERR("malloc:");
        args->fd = fd;
        args->time = time;
        args->addr = addr;
        args->semaphore = &semaphore;
        if (pthread_create(&thread, NULL, communicateDgram, (void *)args) != 0)
            ERR("pthread_create");
        if (pthread_detach(thread) != 0)
            ERR("pthread_detach");
    }
}

// Main function for the server
int main(int argc, char **argv)
{
    int fd;
    if (argc != 2) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (sethandler(SIG_IGN, SIGPIPE))
        ERR("Seting SIGPIPE:");
    if (sethandler(sigint_handler, SIGINT))
        ERR("Seting SIGINT:");
    fd = bind_inet_socket(atoi(argv[1]), SOCK_DGRAM);
    doServer(fd);
    if (TEMP_FAILURE_RETRY(close(fd)) < 0)
        ERR("close");
    fprintf(stderr, "Server has terminated.\n");
    return EXIT_SUCCESS;
}