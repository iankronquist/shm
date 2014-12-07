#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <assert.h>

#define IP_PROTOCOL 0

int init_connection(char *address, int port);
int read_loop(int fd);
int local_parse(char *line, ssize_t line_len);
int get_fd_size(int fd);

int main(int argc, char *argv[]) {

    if (argc < 3) {
        fprintf(stderr, "No port provided!\n");
        exit(1);
    }
    int port = (int)strtol(argv[2], NULL, 10);
    int fd = init_connection(argv[1], port);
    read_loop(fd);
    close(fd);
    return 0;
}

int init_connection(char *readable_address, int port) {
    int socketfd, ret;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    socketfd = socket(AF_INET, SOCK_STREAM, IP_PROTOCOL);
    if (socketfd < 0) {
        perror("Couldn't open socket!");
        exit(1);
    }
    bzero((char*) &serv_addr, sizeof(serv_addr));
    int address = inet_pton(AF_INET, readable_address, &serv_addr.sin_addr);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    ret = connect(socketfd, &serv_addr, sizeof(serv_addr));
    if (ret < 0) {
        perror("Couldn't connect to the socket");
        exit(1);
    }
    return socketfd;
}
    
int read_loop(const int fd) {
    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    int line_size;
    char *response = NULL;
    printf("\n>>> ");
    while ((linelen = getline(&line, &linecap, stdin)) > 0) {
        line[linelen-1] = '\0';
        if (line[0] == 'l') {
            local_parse(line, linelen);
            printf("\n>>> ");
            continue;
        }
        if (strnstr(line, "put", 3) != 0) {
            int name_size = strlen(&line[4]);
            char name[name_size];
            strncpy(name, &line[4], name_size);
            assert(name[name_size-1] == 0);
            printf("Name '%s', %i:\n", name, name_size);
            if (access(name, R_OK) != -1) {
                int in_fd = open(name, O_RDONLY,
                        S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH);
                int file_size = get_fd_size(in_fd);
                write(fd, &file_size, sizeof(int));
                char *file_line = malloc(file_size);
                read(in_fd, file_line, file_size);
                write(fd, file_line, file_size);
                free(file_line);
                free(line);
                continue;
            } else {
                puts("File couldn't be read");
                printf("\n>>> ");
                continue;
            }
        }
        line_size = linelen;
        write(fd, &line_size, sizeof(int));
        write(fd, line, line_size);
        if (read(fd, &line_size, sizeof(int)) == -1) {
            perror("reading size");
            exit(-1);
        }
        response = malloc(line_size);
        if (read(fd, response, line_size) == -1) {
            perror("reading message");
            exit(-1);
        }
        printf("%s\n", response);
        if (strcmp(response, "exit") == 0) {
            break;
        }
        free(response);
        printf("\n>>> ");
    }
    close(fd);
    return 0;
}

int local_parse(char *line, ssize_t line_len) {
    puts ("cd locally, etc.");
    printf("%s\n", line);
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
