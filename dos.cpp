#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <errno.h>

#define MAX_THREADS 1000
#define REQUESTS_PER_THREAD 1000
#define REQUEST_DELAY_US 10000

typedef struct {
    char host[256];
    char port[16];
} target_info;

int total_requests = 0;
int failed_requests = 0;

int make_socket(const char *host, const char *port) {
    struct addrinfo hints, *servinfo, *p;
    int sock;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port, &hints, &servinfo) != 0) {
        fprintf(stderr, "getaddrinfo failed for %s:%s\n", host, port);
        return -1;
    }

    for (p = servinfo; p != NULL; p = p->ai_next) {
        sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock == -1) continue;

        if (connect(sock, p->ai_addr, p->ai_addrlen) == -1) {
            close(sock);
            continue;
        }
        freeaddrinfo(servinfo);
        return sock;
    }

    freeaddrinfo(servinfo);
    return -1;
}

void *send_requests(void *arg) {
    target_info *tgt = (target_info *)arg;

    for (int i = 0; i < REQUESTS_PER_THREAD; i++) {
        int sock = make_socket(tgt->host, tgt->port);
        if (sock == -1) {
            fprintf(stderr, "Connection failed to %s:%s (errno: %d)\n", 
                   tgt->host, tgt->port, errno);
            __sync_fetch_and_add(&failed_requests, 1);
            usleep(REQUEST_DELAY_US);
            continue;
        }

        const char *http_request = 
            "GET / HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Connection: close\r\n\r\n";

        if (send(sock, http_request, strlen(http_request), 0) < 0) {
            fprintf(stderr, "Send failed (errno: %d)\n", errno);
            __sync_fetch_and_add(&failed_requests, 1);
        } else {
            __sync_fetch_and_add(&total_requests, 1);
        }
        close(sock);
        usleep(REQUEST_DELAY_US);
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        printf("Usage: %s <host:port>\n", argv[0]);
        printf("Example: %s localhost:8080\n", argv[0]);
        return 1;
    }

    // Разбираем host:port безопасно
    target_info target;
    char *colon = strchr(argv[1], ':');
    if (!colon) {
        fprintf(stderr, "Invalid format. Use host:port\n");
        return 1;
    }
    
    strncpy(target.host, argv[1], colon - argv[1]);
    target.host[colon - argv[1]] = '\0';
    strncpy(target.port, colon + 1, sizeof(target.port) - 1);
    target.port[sizeof(target.port) - 1] = '\0';

    printf("Testing server at %s:%s\n", target.host, target.port);

    // Проверяем, доступен ли сервер
    int test_sock = make_socket(target.host, target.port);
    if (test_sock == -1) {
        fprintf(stderr, "Cannot connect to server at %s:%s\n", 
               target.host, target.port);
        fprintf(stderr, "1. Make sure server is running\n");
        fprintf(stderr, "2. Check firewall settings\n");
        return 1;
    }
    close(test_sock);

    pthread_t threads[MAX_THREADS];
    printf("Starting load test with %d threads...\n", MAX_THREADS);

    for (int i = 0; i < MAX_THREADS; i++) {
        if (pthread_create(&threads[i], NULL, send_requests, &target) != 0) {
            perror("Failed to create thread");
            return 1;
        }
    }

    for (int i = 0; i < MAX_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("\nLoad test completed!\n");
    printf("Total requests: %d\n", total_requests);
    printf("Failed requests: %d\n", failed_requests);
    printf("Success rate: %.2f%%\n", 
          (float)total_requests / (total_requests + failed_requests) * 100);

    return 0;
}