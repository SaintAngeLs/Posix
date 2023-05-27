#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <signal.h>

// Macro for handling errors with source indication
#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

// Function for running the server program
void run_server()
{
    char *args[] = {"./l4server", "1234", NULL};
    execvp(args[0], args);
    ERR("execvp(server)");
}

// Function for running the client program
void run_client(int time)
{
    char str_time[10];
    sprintf(str_time, "%d", time);
    char *args[] = {"./l4client", "localhost", "1234", str_time, NULL};
    execvp(args[0], args);
    ERR("execvp(client)");
}

// Function for testing the client-server application
void test_application()
{
    pid_t server_pid;
    pid_t client_pid;
    int status;

    // Run the server
    server_pid = fork();
    if (server_pid == -1)
        ERR("fork(server)");
    else if (server_pid == 0)
        run_server();

    // Wait for the server to start
    sleep(1);

    // Run the client
    client_pid = fork();
    if (client_pid == -1)
        ERR("fork(client)");
    else if (client_pid == 0)
        run_client(5); // Specify the time for the client to be notified (in seconds)

    // Wait for the client to finish
    if (waitpid(client_pid, &status, 0) == -1)
        ERR("waitpid(client)");
    else if (!WIFEXITED(status) || WEXITSTATUS(status) != EXIT_SUCCESS)
        fprintf(stderr, "Client process failed\n");

    // Stop the server gracefully by sending SIGINT
    if (kill(server_pid, SIGINT) == -1)
        ERR("kill(server)");

    // Wait for the server to finish
    if (waitpid(server_pid, &status, 0) == -1)
        ERR("waitpid(server)");
    else if (!WIFEXITED(status) || WEXITSTATUS(status) != EXIT_SUCCESS)
        fprintf(stderr, "Server process failed\n");
}

int main()
{
    test_application();

    return 0;
}
