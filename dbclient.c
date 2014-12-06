#define _GNU_SOURCE
#define _SVID_SOURCE
#define _XOPEN_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <errno.h>
#include <semaphore.h>
#include <arpa/inet.h>

#include "ipdb.h"

#define SHM_SIZE sizeof(int) + MAX_ROWS * sizeof(struct ip_row)
#define DATA_OFFSET sizeof(int)

struct db_info {
    size_t size;
    int fd;
    char *data;
};


int get_info(char *hostname, struct ip_row* row);
void repl();
void print_row(struct ip_row* row);
char *init_shm();
void append_row(struct ip_row *row);
int get_fd_size(int fd);
void clear();
void show();
void errorexit(char *message);
void open_db(struct db_info *db);
void close_db(struct db_info *db);

int main() {
    repl();
    return 0;
}

void repl() {
    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    printf("%s", PROMPT);
    while ((linelen = getline(&line, &linecap, stdin)) > 0) {
        if (strstr(line, "fetch") > 0)  {
            struct ip_row row;
            line[linelen-1] = '\0';
            get_info(&line[6], &row); 
            print_row(&row);
            append_row(&row);
        } else if(strstr(line, "clear")) {
           clear(); 
        } else if(strstr(line, "show")) {
            show();
        } else {
            puts("Invalid command!");
        }
        printf("\n%s", PROMPT);
    }
    return;
}

int get_info(char *hostname, struct ip_row* row) {
    int status;
    struct addrinfo hints, *results;
    char ip_str[INET6_ADDRSTRLEN];
    memset(&hints, 0, sizeof(hints));
    memset(row, 0, sizeof(struct ip_row));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if ((status = getaddrinfo(hostname, NULL, &hints, &results)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        return -1;
    }
    strncpy(row->row_name, hostname, NAME_SIZE);
    for(struct addrinfo *p = results; p != NULL; p = p->ai_next) {
        void *addr;
        if (p->ai_family == AF_INET) { // IPv4
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)p->ai_addr;
            addr = &(ipv4->sin_addr);
            inet_ntop(p->ai_family, addr, ip_str, INET_ADDRSTRLEN);
            strncpy(row->row_address4, ip_str, INET_ADDRSTRLEN);
        } else { //IPv6
            struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)p->ai_addr;
            addr = &(ipv6->sin6_addr);
            inet_ntop(p->ai_family, addr, ip_str, sizeof ip_str);
            strncpy(row->row_address6, ip_str, INET6_ADDRSTRLEN);
        }
    }
    freeaddrinfo(results);
    return 0;
}

void print_row(struct ip_row* row) {
    printf("%s\n", row->row_name);
    printf("%s\n", row->row_address4);
    printf("%s", row->row_address6);
}

void append_row(struct ip_row *row) {
    char *addr;
    int shmfd;
    char *name = malloc(NAME_SIZE);
    SHARED_MEM_NAME(name);
    //row->row_lock = sem_open(row->row_name, O_CREAT, S_IWUSR | S_IRUSR, 0);
    if (sem_init(&row->row_lock, 1, 1) == -1) {
        errorexit("Error initializing semaphore");
    }
 
    shmfd = shm_open(name, O_CREAT | O_RDWR, S_IWUSR | S_IRUSR);
    if (shmfd == -1) {
       errorexit("shm_open");
    }

    int db_original_size = get_fd_size(shmfd);
    int db_size = db_original_size + sizeof(struct ip_row);

    if (ftruncate(shmfd, db_size) == -1) {
        errorexit("ftruncate");
    }
    addr = mmap(NULL, db_size, PROT_READ | PROT_WRITE, MAP_SHARED, shmfd, 0);
    if (addr == -1) {
        errorexit("mmap failed");
    }
    memcpy(addr + db_original_size, row, sizeof(struct ip_row));
    munmap(addr, db_size);
    close(shmfd);
}


void open_db(struct db_info *db) {
    char *name = malloc(NAME_SIZE);
    SHARED_MEM_NAME(name);
 
    db->fd = shm_open(name, O_CREAT | O_RDWR, S_IWUSR | S_IRUSR);
    if (db->fd == -1) {
       errorexit("shm_open");
    }

    db->size = get_fd_size(db->fd);

    if (ftruncate(db->fd, db->size) == -1) {
        errorexit("ftruncate");
    }
    db->data = mmap(NULL, db->size, PROT_READ | PROT_WRITE, MAP_SHARED, db->fd, 0);
    if (db->data == -1) {
        errorexit("mmap failed");
    }
}

void close_db(struct db_info *db) {
    munmap(db->data, db->size);
    close(db->fd);
}

void clear() {
    char *name = malloc(NAME_SIZE);
    SHARED_MEM_NAME(name);
    if(shm_unlink(name) == -1) {
        errorexit("Unlinking shared memory");
    }
}

void show() {
    struct db_info db;
    open_db(&db);
    char *addr_copy = db.data;
    struct ip_row *row;
    for (int i = 0; i < db.size/sizeof(struct ip_row); i++) {
        row = addr_copy;
        if(sem_wait(&row->row_lock) == -1) {
            errorexit("Error locking");
        }
        printf("%s\n%s\n%s\n", row->row_name, row->row_address4,
                row->row_address6);
        addr_copy += sizeof(struct ip_row);
        if (sem_post(&row->row_lock) == -1) {
            errorexit("Error unlocking");
        }
    }
    close_db(&db);
}

int get_fd_size(int fd) {
    struct stat statfd;
    int err = fstat(fd, &statfd);
    if (err == -1) {
        errorexit("Could not stat file");
    }
    return statfd.st_size;
}

int check(char *hostname) {
    struct db_info db;
    open_db(&db);
    struct ip_row *row = (struct ip_row *)db.data;
    for (size_t i = 0; i < db.size; i++) {
        if(strncmp(hostname, row[i].row_name, NAME_SIZE) == 0) {
            if (sem_wait(&row->row_lock) == -1) {
                errorexit("Could not open lock");
            }
            print_row(row);
            if (sem_post(&row->row_lock) == -1) {
                errorexit("Could not close lock");
            }
            return 1;
        }
    }
    printf("No host named %s was found\n", hostname);
    return 0;
}

struct ip_row *get_row(char *hostname, struct db_info *db) {
    struct ip_row *row = (struct ip_row *)db->data;
    for (size_t i = 0; i < db->size; i++) {
        if(strncmp(hostname, row[i].row_name, NAME_SIZE) == 0) {
            return row;
            return 1;
        }
    }
    return NULL;
}

void db_map(struct db_info *db, void (func)(struct ip_row *)) {
    struct ip_row *row = (struct ip_row *)db->data;
    for (size_t i = 0; i < db->size; i++) {
        if (sem_wait(&row->row_lock) == -1) {
            errorexit("Could not open lock");
        }
        func(row);
        if (sem_post(&row->row_lock) == -1) {
            errorexit("Could not close lock");
        }
    }
}

void save(struct db_info *db, char *file_name) {
    int savefd = open(file_name, O_WRONLY);
    struct ip_row *row = (struct ip_row *)db->data;
    for (size_t i = 0; i < db->size; i++) {
        if (sem_wait(&row->row_lock) == -1) {
            errorexit("Could not open lock");
        }
        if (write(savefd, row, sizeof(struct ip_row)) == -1) {
            errorexit("Could not write to savefile");
        }
        if (sem_post(&row->row_lock) == -1) {
            errorexit("Could not close lock");
        }
    }
    close(savefd);
}



/*

void show(char *hostname, char* shm, size_t shm_len) {
    if (shm_len == 0) {
        puts("Database is empty");
        return;
    }
    struct ip_row *row = (struct ip_row *)shm;
    for (size_t i = 0; i < shm_len; i++) {
        //acquire lock
        //print
    }
}

void lock_row(char *hostname, char *shm, size_t shm_len) {
    struct ip_row *row = (struct ip_row *)shm;
    for (size_t i = 0; i < shm_len; i++) {
        if(strncmp(hostname, row[i].row_name, NAME_SIZE) == 0) {
            //acquire lock
            //return
        }
    }
    printf("No host named %s was found\n", hostname);
    return -1;
}

void unlock_row(char *hostname, char *shm, size_t shm_len) {
    struct ip_row *row = (struct ip_row *)shm;
    for (size_t i = 0; i < shm_len; i++) {
        if(strncmp(hostname, row[i].row_name, NAME_SIZE) == 0) {
            //acquire lock
            //return
        }
    }
    printf("No host named %s was found\n", hostname);
    return -1;
}

int exit(char* shm_name) {
    if (shm_unlink(shm_name) == -1) {
        errorexit("unlink");
        exit(-1);
    }
    exit(0);
}
*/

void errorexit(char *message) {
    perror(message);
    exit(-1);
}
