#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

void usage(char *name)
{
    fprintf(stderr, "USAGE: %s <server_address> <server_port>\n", name);
    exit(EXIT_FAILURE);
}

void start_server()
{
    if (fork() == 0) {
        execl("./l4server2", "./l4server2", "8080", ".", NULL);
        perror("exec");
        exit(EXIT_FAILURE);
    }
}

void start_clients(char *server_address, char *server_port, int num_clients)
{
    for (int i = 0; i < num_clients; i++) {
        if (fork() == 0) {
            execl("./l4client2", "./l4client2", server_address, server_port, NULL);
            perror("exec");
            exit(EXIT_FAILURE);
        }
    }
}

void stop_server(pid_t server_pid)
{
    kill(server_pid, SIGINT);
    wait(NULL);
}

int test_server_and_clients(char *server_address, char *server_port, int num_clients)
{
    pid_t server_pid;
    start_server();
    usleep(100000); // Wait for the server to start

    server_pid = getpid(); // Get the server process ID

    start_clients(server_address, server_port, num_clients);

    // Wait for the clients to finish
    for (int i = 0; i < num_clients; i++) {
        wait(NULL);
    }

    // Stop the server gracefully
    stop_server(server_pid);

    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 3)
        usage(argv[0]);

    char *server_address = argv[1];
    char *server_port = argv[2];
    int num_clients = 5;

    int result = test_server_and_clients(server_address, server_port, num_clients);

    if (result == 0) {
        printf("Test passed\n");
    } else {
        printf("Test failed\n");
    }

    return result;
}