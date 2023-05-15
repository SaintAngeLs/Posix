#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>


#define MAX_PACKET_SIZE 128

typedef struct {
    int address;
    int socket;
} Host;

Host hosts[8];  // Tablica przechowująca informacje o hostach

int create_socket(const char* address, int port) {
    int sockfd;
    struct sockaddr_in server_addr;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, address, &(server_addr.sin_addr)) <= 0) {
        perror("inet_pton");
        exit(EXIT_FAILURE);
    }

    if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    if (listen(sockfd, 8) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    return sockfd;
}

void handle_host_message(int host_socket) {
    char buffer[MAX_PACKET_SIZE];
    ssize_t bytes_read = read(host_socket, buffer, sizeof(buffer));
    if (bytes_read <= 0) {
        // Błąd odczytu lub zamknięcie połączenia
        close(host_socket);
        return;
    }

    int recipient_address = buffer[0];
    int sender_address = buffer[1];

    if (recipient_address == 0) {
        // Wiadomość dla routera
        if (sender_address >= 1 && sender_address <= 8) {
            // Poprawny adres hosta
            hosts[sender_address - 1].address = sender_address;
            write(host_socket, &sender_address, sizeof(int));
        } else {
            // Niepoprawny adres hosta
            char* error_message = "Wrong address";
            write(host_socket, error_message, strlen(error_message) + 1);
        }
    } else if (recipient_address == 9) {
        // Wiadomość do wszystkich hostów
        for (int i = 0; i < 8; i++) {
            if (hosts[i].socket != -1) {
                write(hosts[i].socket, buffer, bytes_read);
            }
        }
    } else {
        // Wiadomość do konkretnego hosta
        if (recipient_address >= 1 && recipient_address <= 8) {
            if (hosts[recipient_address - 1].socket != -1) {
                write(hosts[recipient_address - 1].socket, buffer, bytes_read);
            } else {
                // Nieznany host
                char* error_message = "Unknown host";
                write(host_socket, error_message, strlen(error_message) + 1);
            }
        } else {
            // Niepoprawny adres odbiorcy
            char* error_message = "Invalid recipient address";
            write(host_socket, error_message, strlen(error_message) + 1);
        }
    }
}
int main(int argc, char* argv[]) {
    if (argc < 3) {
    fprintf(stderr, "Usage: %s <address> <port>\n", argv[0]);
    exit(EXIT_FAILURE);
    }
const char* address = argv[1];
int port = atoi(argv[2]);

int router_socket = create_socket(address, port);

for (int i = 0; i < 8; i++) {
    hosts[i].address = i + 1;
    hosts[i].socket = -1;
}

while (1) {
    fd_set rfds;
    int max_fd = router_socket;

    FD_ZERO(&rfds);
    FD_SET(router_socket, &rfds);

    for (int i = 0; i < 8; i++) {
        if (hosts[i].socket != -1) {
            FD_SET(hosts[i].socket, &rfds);
            if (hosts[i].socket > max_fd) {
                max_fd = hosts[i].socket;
            }
        }
    }

    if (select(max_fd + 1, &rfds, NULL, NULL, NULL) < 0) {
        perror("select");
        exit(EXIT_FAILURE);
    }

    if (FD_ISSET(router_socket, &rfds)) {
        int host_socket = accept(router_socket, NULL, NULL);
        if (host_socket < 0) {
            perror("accept");
            exit(EXIT_FAILURE);
        }

        // Znajdowanie wolnego slotu w tablicy hosts
        int free_slot = -1;
        for (int i = 0; i < 8; i++) {
            if (hosts[i].socket == -1) {
                free_slot = i;
                break;
            }
        }

        if (free_slot == -1) {
            // Brak wolnego slotu dla nowego hosta
            close(host_socket);
        } else {
            hosts[free_slot].socket = host_socket;
        }
    }

    for (int i = 0; i < 8; i++) {
        if (hosts[i].socket != -1 && FD_ISSET(hosts[i].socket, &rfds)) {
            handle_host_message(hosts[i].socket);
        }
    }
}

close(router_socket);
return 0;
}