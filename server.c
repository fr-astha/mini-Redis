#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include "store.h"

#define PORT "6380"
#define BACKLOG 10
#define BUF_SIZE 512

// Single global store, shared by the (single-threaded, single-process)
// server loop. Kept simple on purpose — no locking needed because we
// handle one client command at a time, synchronously, like real Redis
// does with its single-threaded event loop.
static Store store;

void sigchld_handler(int s) {
    (void)s;
    int saved_errno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0);
    errno = saved_errno;
}

void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) return &(((struct sockaddr_in*)sa)->sin_addr);
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

// Parses a line like "SET name John 30" or "GET name" and executes it,
// writing the reply into `reply` (caller-provided buffer).
static void handle_command(char *line, char *reply, size_t reply_size) {
    char *cmd = strtok(line, " \r\n");
    if (!cmd) { snprintf(reply, reply_size, "ERR empty command\n"); return; }

    if (strcasecmp(cmd, "SET") == 0) {
        char *key = strtok(NULL, " \r\n");
        char *val = strtok(NULL, " \r\n");
        char *ttl_str = strtok(NULL, " \r\n"); // optional
        if (!key || !val) { snprintf(reply, reply_size, "ERR usage: SET key value [ttl]\n"); return; }
        int ttl = ttl_str ? atoi(ttl_str) : 0;
        store_set(&store, key, val, ttl);
        snprintf(reply, reply_size, "OK\n");

    } else if (strcasecmp(cmd, "GET") == 0) {
        char *key = strtok(NULL, " \r\n");
        if (!key) { snprintf(reply, reply_size, "ERR usage: GET key\n"); return; }
        const char *val = store_get(&store, key);
        if (val) snprintf(reply, reply_size, "%s\n", val);
        else snprintf(reply, reply_size, "(nil)\n");

    } else if (strcasecmp(cmd, "DEL") == 0) {
        char *key = strtok(NULL, " \r\n");
        if (!key) { snprintf(reply, reply_size, "ERR usage: DEL key\n"); return; }
        int deleted = store_del(&store, key);
        snprintf(reply, reply_size, "(integer) %d\n", deleted);

    } else {
        snprintf(reply, reply_size, "ERR unknown command '%s'\n", cmd);
    }
}

int main(void) {
    store_init(&store);

    int sockfd, new_fd;
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr;
    socklen_t sin_size;
    struct sigaction sa;
    int yes = 1;
    char s[INET6_ADDRSTRLEN];
    int rv;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            perror("setsockopt");
            exit(1);
        }
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind");
            continue;
        }
        break;
    }
    freeaddrinfo(servinfo);

    if (p == NULL) {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }
    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }

    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    printf("mini-redis: listening on port %s\n", PORT);

    // Single-threaded, one connection handled fully before accepting the
    // next. This is deliberate (matches real Redis's single-threaded
    // event-loop model) and means every client sees the SAME `store` —
    // unlike a fork-per-connection design, where each child would get its
    // own copy of memory and clients couldn't see each other's keys.
    while (1) {
        sin_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_fd == -1) { perror("accept"); continue; }

        inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
        printf("mini-redis: connection from %s\n", s);

        char buf[BUF_SIZE];
        char reply[BUF_SIZE];
        int numbytes;

        while ((numbytes = recv(new_fd, buf, sizeof(buf) - 1, 0)) > 0) {
            buf[numbytes] = '\0';
            handle_command(buf, reply, sizeof(reply));
            send(new_fd, reply, strlen(reply), 0);
        }

        printf("mini-redis: client disconnected\n");
        close(new_fd);
    }

    return 0;
}
