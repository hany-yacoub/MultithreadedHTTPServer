#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>

#include "connection_queue.h"
#include "http.h"

#define BUFSIZE 512
#define LISTEN_QUEUE_LEN 5
#define N_THREADS 5

int keep_going = 1;
const char *serve_dir;

void handle_sigint(int signo) {
    keep_going = 0;
}

// Consumer thread start function
void *consumer_thread_func(void *arg) {

    while (keep_going != 0){
        connection_queue_t *queue = (connection_queue_t *) arg;
        int client_fd = connection_dequeue(queue);
        if (client_fd == -1){ //if error or shutdown
            exit(1);
        }

        // printf("New client connected\n");

        char fullpath_buf[BUFSIZE];
        char filename_buf[BUFSIZE];

        if (read_http_request(client_fd, filename_buf) == 1){
            fprintf(stderr, "Error reading http request\n");
            close(client_fd);
            exit(1);
        }

        //No possible error checking
        strcpy(fullpath_buf, serve_dir);
        strcpy(fullpath_buf+strlen(serve_dir), filename_buf);

        if (write_http_response(client_fd, fullpath_buf) == 1){
            fprintf(stderr, "Error writing http request\n");
            close(client_fd);
            exit(1);
        }

        if (close(client_fd) == -1) {
            perror("close");
            exit(1);
        }

        // printf("Client disconnected\n");
    }

    return NULL; // Not reached
}

int main(int argc, char **argv) {
    // First command is directory to serve, second command is port
    if (argc != 3) {
        printf("Usage: %s <directory> <port>\n", argv[0]);
        return 1;
    }

    // Catch SIGINT so we can clean up properly
    struct sigaction sigact;
    sigact.sa_handler = handle_sigint;
    if (sigfillset(&sigact.sa_mask) == -1){
        perror("sigfill");
        return 1;
    }
    sigact.sa_flags = 0; // Note the lack of SA_RESTART
    if (sigaction(SIGINT, &sigact, NULL) == -1) {
        perror("sigaction");
        return 1;
    }

    // Uncomment the lines below to use these definitions:
    serve_dir = argv[1];
    const char *port = argv[2];

    // Set up hints - we'll take either IPv4 or IPv6, TCP socket type
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // We'll be acting as a server
    struct addrinfo *server;

    // Set up address info for socket() and connect()
    int ret_val = getaddrinfo(NULL, port, &hints, &server);
    if (ret_val != 0) {
        fprintf(stderr, "getaddrinfo failed: %s\n", gai_strerror(ret_val));
        return 1;
    }
    // Initialize socket file descriptor
    int sock_fd = socket(server->ai_family, server->ai_socktype, server->ai_protocol);
    if (sock_fd == -1) {
        perror("socket");
        freeaddrinfo(server);
        return 1;
    }
    // Bind socket to receive at a specific port
    if (bind(sock_fd, server->ai_addr, server->ai_addrlen) == -1) {
        perror("bind");
        freeaddrinfo(server);
        close(sock_fd);
        return 1;
    }
    freeaddrinfo(server);
    // Designate socket as a server socket
    if (listen(sock_fd, LISTEN_QUEUE_LEN) == -1) {
        perror("listen");
        close(sock_fd);
        return 1;
    }

    //Backup current signal mask and block all signals so threads inherit all signals blocked.
    sigset_t old_set; //sigint allowed 
    sigset_t new_set; //all blocked (so threads are all blocked)

    if (sigfillset(&new_set) == -1){
        perror("sigfillset");
        close(sock_fd);
        return 1;
    }

    if (sigprocmask(SIG_SETMASK, &new_set, &old_set) == -1){
        perror("sigprocmask");
        close(sock_fd);
        return 1;
    }

    //Create queue
    connection_queue_t queue;
    if (connection_queue_init(&queue) == -1){
        fprintf(stderr, "Error initializing queue\n");
        close(sock_fd);
        return 1;
    }

    //Create threads with blocked signals
    pthread_t consumer_threads[N_THREADS];
    for (int i = 0; i < N_THREADS; i++) {
        if ((ret_val = pthread_create(consumer_threads + i, NULL, consumer_thread_func, &queue)) != 0) {
            fprintf(stderr, "pthread_create: %s\n", strerror(ret_val));
            return 1;
        }
    }

    //Restore signal mask with sigint allowed. Only main thread receives sigint.
    if (sigprocmask(SIG_SETMASK, &old_set, NULL) == -1){
        perror("sigprocmask");
        close(sock_fd);
        return 1;
    }

    while (keep_going != 0) {
        // Wait to receive a connection request from client
        
        // printf("Waiting for client to connect\n");
        int client_fd = accept(sock_fd, NULL, NULL);
        if (client_fd == -1) {
            if (errno != EINTR) {
                perror("accept");
                close(sock_fd);
                return 1;
            } else {
                break;
            }
        }

        //Pass client_fd to a worker thread
        if (connection_enqueue(&queue, client_fd) == -1){ //if error or shutdown
            close(client_fd);
            break;
        }

        // printf("Client disconnected\n");
    }

    //Reached after SIGINT
    if (connection_queue_shutdown(&queue) == -1){
        fprintf(stderr, "Error shutting down queue\n");
        close(sock_fd);
        return 1;
    }
    
    //Joins threads
    int exit_code = 0;
    for (int i = 0; i < N_THREADS; i++) {
        if ((ret_val = pthread_join(consumer_threads[i], NULL)) != 0) {
            fprintf(stderr, "pthread_join: %s\n", strerror(ret_val));
            exit_code = 1;
        }
    }

    //Frees mutex and condition variable
    if (connection_queue_free(&queue) == -1){
        fprintf(stderr, "Error freeing queue\n");
        close(sock_fd);
        return 1;
    }
    
    //Closes socket
    if (close(sock_fd) == -1) {
        perror("close");
        return 1;
    }

    return exit_code;

}
