// Treść #

// Serwer TCP przyjmuje połączenia od klientów, każdy z klientów przesyła do serwera 3 losowe liczby z przedziału [1-1000] co 0.75 sekundy. W odpowiedzi na każdą z liczb serwer przesyła maksymalną liczbę jaką dostał do tej pory. Jeśli klient w odpowiedzi otrzyma tą samą liczbę co wysłał to ma wypisać słowo „HIT” na stdout. Klient kończy się po 3 próbach, serwer działa aż do otrzymania SIGINT, w reakcji na który ma wypisać ile w sumie liczb dostał. Dane mają być przesyłane jako liczby (binarnie) a nie jako tekst. Serwer ma być programem jedno procesowym i jedno wątkowym.
// Etapy #

//     (4p) Pojedynczy klient łączy się z serwerem i przesyła mu losową liczbę, serwer wypisuje co dostał i się kończy. Serwer kończy się po jednym połączeniu z klientem 2.(3p) Serwer akceptuje wiele połączeń jedno po drugim (nie w tym samym czasie), odsyła klientom to co wysłali zwiększone o 1, klient wypisuje co dostał i po 3 próbach się kończy. Serwer nie ma poprawnego zakończenia, zamykamy C-c.
//     (4p) Serwer nadal pracuje szeregowo, wyznacza aktualną maksymalną wartość otrzymaną i ją odsyła klientom. Klient rozpoznaje kiedy dostaje tą samą liczbę jak wysłał i wypisuje „HIT”.
//     (6p) Serwer przyjmuje klientów równolegle, oblicza ile było liczb przesłanych do niego i w reakcji na SIGINT kończy się wypisując tę liczbę

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

#define BACKLOG 3
#define MAX_NUMBERS 3

volatile sig_atomic_t do_work = 1;
int totalNumbers = 0; // Liczba wszystkiFch odebranych liczb

void sigint_handler(int sig)
{
    do_work = 0;
    printf("Total numbers received: %d\n", totalNumbers);
    exit(EXIT_SUCCESS);
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

int add_new_client(int sfd)
{
    int nfd;
    if ((nfd = TEMP_FAILURE_RETRY(accept(sfd, NULL, NULL))) < 0)
    {
        if (EAGAIN == errno || EWOULDBLOCK == errno)
            return -1;
        ERR("accept");
    }
    return nfd;
}

ssize_t bulk_read(int fd, void *buf, size_t count)
{
    int c;
    size_t len = 0;
    do
    {
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
    do
    {
        c = TEMP_FAILURE_RETRY(write(fd, buf, count));
        if (c < 0)
            return c;
        buf += c;
        len += c;
        count -= c;
    } while (count > 0);
    return len;
}

void handle_client(int cfd)
{
    int32_t numbers[MAX_NUMBERS]; // Przechowuje odebrane liczby
    int32_t maxNumber = 0;  // Maksymalna liczba otrzymana dotychczas
    int attempts = 0;             // Licznik prób
    while (attempts < MAX_NUMBERS)
    {
        ssize_t bytesRead = bulk_read(cfd, numbers, sizeof(numbers));
        if (bytesRead < 0)
        {
            ERR("read");
        }
        else if (bytesRead == 0)
        {
            break; // Zakończ połączenie, jeśli nie otrzymano żadnych danych
        }
        else if (bytesRead == sizeof(numbers))
        {
            for (int i = 0; i < MAX_NUMBERS; i++)
            {
                int32_t receivedNumber = ntohl(numbers[i]);
                totalNumbers++; // Zwiększ liczbę wszystkich odebranych liczb
                printf("Received number: %d\n", receivedNumber);
                if (receivedNumber == maxNumber)
                {
                    printf("HIT\n");
                }
                else if (receivedNumber > maxNumber)
                {
                    maxNumber = receivedNumber;
                }
                attempts++;
                bulk_write(cfd, &maxNumber, sizeof(maxNumber));
            }
        }
        else
        {
            printf("Invalid data received\n");
        }
    }
    close(cfd);
}

void doServer(int fd)
{
    int cfd, fdmax;
    fd_set base_rfds, rfds;
    FD_ZERO(&base_rfds);
    FD_SET(fd, &base_rfds);
    fdmax = fd;
    while (do_work)
    {
        rfds = base_rfds;
        if (select(fdmax + 1, &rfds, NULL, NULL, NULL) < 0)
        {
            if (EINTR == errno)
                continue;
            ERR("select");
        }
        if (FD_ISSET(fd, &rfds))
        {
            cfd = add_new_client(fd);
            if (cfd >= 0)
            {
                handle_client(cfd);
            }
        }
    }
}

int main()
{
    int fd;
    if (sethandler(sigint_handler, SIGINT) < 0)
    {
        ERR("Setting SIGINT handler failed");
    }
    fd = make_socket(PF_INET, SOCK_STREAM);
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(12345),
        .sin_addr.s_addr = INADDR_ANY,
    };
    if (bind(fd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) < 0)
    {
        ERR("bind");
    }
    if (listen(fd, BACKLOG) < 0)
    {
        ERR("listen");
    }
    doServer(fd);
    close(fd);
    return 0;
}

