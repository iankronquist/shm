#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <pthread.h>
#include <fcntl.h>

#define IP_PROTOCOL 0

void server_loop(int);

void pwd(int socket_fd);
void put(int socket_fd);
void get(int socket_fd);
void cd(int socket_fd);
void dir(int socket_fd);
int get_fd_size(int fd);

const char HELP[] = "Helpful advice";
void *listen_loop(void *data);


int main (int argc, char * argv[]) {
    if (argc != 2) {
        printf("%s", HELP);
        exit(-1);
    }
    int port = strtol(argv[1], NULL, 10);
    server_loop(port);
    return 0;

}

void server_loop(int port) {
    int numthreads = 0;
    int capthreads = 4;
    pthread_t *threads = malloc(capthreads * sizeof(pthread_t));
    pthread_t *sockets = malloc(capthreads * sizeof(int));
        int socketfd, newsockfd, portno, clilen, ret;
        char buffer[256];
        struct sockaddr_in serv_addr, cli_addr;
        
        // Open a socket over the internet using TCP and the IP protocol
        socketfd = socket(AF_INET, SOCK_STREAM, IP_PROTOCOL);
        if (socketfd < 0) {
            perror("Couldn't open socket!");
            exit(1);
        }
        bzero((char*) &serv_addr, sizeof(serv_addr));
        // Set the address family
        serv_addr.sin_family = AF_INET;
        // Convert from host byte-order to network byte-order
        serv_addr.sin_port = htons(port);
        // Apparently INADDR_ANY is localhost?
        serv_addr.sin_addr.s_addr = INADDR_ANY;
        ret = bind(socketfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr));
        if (ret < 0) {
            perror("Failed to bind to the socket");
            exit(1);
        }
    while (1) {

        // Apparently the max backlog on most systems if 5
        ret = listen(socketfd, 5);
        if (ret < 0) {
            perror("Failed to listen on the socket");
            exit(1);
        }
        clilen = sizeof(cli_addr);
        newsockfd = accept(socketfd, (struct sockaddr *) &cli_addr, &clilen);
        if (newsockfd < 0) {
            perror("Failed to accept connection");
            exit(1);
        }
        if (numthreads == capthreads) {
            capthreads *= 2;
            threads = realloc(threads, sizeof(pthread_t) * capthreads);
        }
        pthread_create(&threads[numthreads], NULL, listen_loop,
                (void*)newsockfd);
        numthreads++;
    }
}

void *listen_loop(void *data) {
    puts("Thread spawned");
    const int fd = (int)data;
    int size;
    char *buffer;
    char *response;
    int response_size;
    while (read(fd, &size, sizeof(int)) > 0) {
        buffer = malloc(size);
        read(fd, buffer, size);

        if (strnstr(buffer, "cd", 2) != 0) {
            response_size = 12;
            response = malloc(response_size);
            memcpy(response, "server cd'd", response_size);
            write(fd, &response_size, sizeof(int));
            write(fd, response, response_size);
        } else if (strnstr(buffer, "put", 3) != 0) {
            puts("put");
            int name_size = strlen(&buffer[5]);
            char name[name_size];
            strncpy(name, &buffer[5], name_size);
            int in_fd = open(&buffer[5], O_CREAT | O_WRONLY,
                    S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);
            int file_size = size - (4 + name_size);
            if (file_size > 0) {
                write(in_fd, &buffer, file_size);
            }
            close(in_fd);
            response_size = 9;
            response = malloc(response_size);
            memcpy(response, "success", response_size);
            write(fd, response, response_size);
            write(fd, response, response_size);
        } else if (strnstr(buffer, "get", 3) != 0) {
            int name_size = strlen(&buffer[5]);
            char name[name_size];
            strncpy(name, &buffer[4], name_size);
            printf("Name %s:\n", name);
            if (access(name, R_OK) != -1) {
                int in_fd = open(&buffer[5], O_RDONLY,
                        S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);
                int file_size = get_fd_size(in_fd);
                write(fd, &file_size, sizeof(int));
                char *file_buffer = malloc(file_size);
                read(in_fd, file_buffer, file_size);
                write(fd, file_buffer, file_size);
                free(file_buffer);
                free(buffer);
                continue;
            } else {
                response_size = 15;
                response = malloc(response_size);
                memcpy(response, "File not found", response_size);
                write(fd, &response_size, sizeof(int));
                write(fd, response, response_size);
            }
        } else {
            response_size = 16;
            response = malloc(response_size);
            memcpy(response, "Invalid command", response_size);
            write(fd, &response_size, sizeof(int));
            write(fd, response, response_size);
        }
        free(buffer);
        free(response);
    }
    close(fd);
    puts("thread exited");
    pthread_exit(NULL);
}


int get_fd_size(int fd) {
    struct stat statfd;
    int err = fstat(fd, &statfd);
    if (err == -1) {
        perror("fstat");
        exit(-1);
    }
    return statfd.st_size;
}
