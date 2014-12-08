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
    void *lock;
};


int get_info(char *hostname, struct ip_row* row);
struct ip_row *get_row(char *hostname, struct db_info *db);
void repl();
void print_row(struct ip_row* row);
char *init_shm();
void append_row(struct ip_row *row);
void append_if_not_present(char *host_name);
int get_fd_size(int fd);
void clear();
void show();
void errorexit(char *message);
int open_db(struct db_info *db);
void close_db(struct db_info *db);
int cmd_exit();
void unlock(char *hostname);
void lock(char *hostname);
void querylock(char *hostname);
int writeall();
void save(char *file_name);
void load(char *file_name);
void lock_table();
void unlock_table();
void querylock_table();

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
        // remove \n
        line[linelen-1] = '\0';
        if (strstr(line, "fetch") > 0)  {
            append_if_not_present(&line[6]);
        } else if(strstr(line, "clear")) {
           clear(); 
        } else if(strstr(line, "show")) {
            show();
        } else if(strstr(line, "exit")) {
            cmd_exit();
        } else if(strstr(line, "lock_table")) {
            //TODO fix design flaw: open_db locks whole table
            puts("broken");
            lock_table();
        } else if(strstr(line, "unlock_table")) {
            //TODO fix design flaw: open_db locks whole table
            puts("broken");
            unlock_table();
        } else if(strstr(line, "query_table")) {
            puts("broken?");
            //TODO fix design flaw: open_db locks whole table
            querylock_table();
        } else if(strstr(line, "unlock")) {
            unlock(&line[7]);
        }  else if(strstr(line, "lock")) {
            lock(&line[5]);
        } else if(strstr(line, "query")) {
            querylock(&line[6]);
        } else if(strstr(line, "write")) {
            writeall();
        } else if(strstr(line, "load")) {
            load(&line[5]);
        } else if(strstr(line, "save")) {
            save(&line[5]);
        } else {
            puts("Invalid command!");
        }
        printf("\n%s", PROMPT);
    }
    return;
}

int writeall() {
     struct db_info db;
    if (open_db(&db) == -1) {
        puts("The database is empty");
        return 1;
    }
    write(STDOUT_FILENO, db.data, db.size);
    close_db(&db);
    return 0;
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

void append_if_not_present(char *host_name) {
    struct db_info db;
    struct ip_row row;
    // Will return -1 if the db is empty. If it's empty, append the row
    if (open_db(&db) != -1) {
       // Will return a pointer to the row if it exists in the database,
       // else NULL
       struct ip_row *row_in_db = get_row(host_name, &db);
       if (row_in_db != NULL) {
            // If the row exists, print it and return, otherwise add it.
            print_row(row_in_db);
            close_db(&db);
            return;
       }
       close_db(&db);
    }
    // Since the row does not exist, get its name, print it, and append it
    get_info(host_name, &row); 
    print_row(&row);
    append_row(&row);
}

void append_row(struct ip_row *row) {
    struct db_info db;
    char *name = malloc(NAME_SIZE);
    SHARED_MEM_NAME(name);
    if (sem_init(&row->row_lock, 1, 1) == -1) {
        errorexit("Error initializing semaphore");
    }
 
    db.fd = shm_open(name, O_CREAT | O_RDWR, S_IWUSR | S_IRUSR);
    if (db.fd == -1) {
       errorexit("shm_open");
    }

    int db_original_size = get_fd_size(db.fd);
    if (db_original_size == 0) {
        puts("db is empty");
        db.size = sizeof(sem_t) + sizeof(struct ip_row);
    } else {
        db.size = db_original_size + sizeof(struct ip_row);
    }
    db.size = db_original_size + sizeof(struct ip_row);

    if (ftruncate(db.fd, db.size) == -1) {
        errorexit("ftruncate");
    }
    db.lock = mmap(NULL, db.size, PROT_READ | PROT_WRITE, MAP_SHARED, db.fd, 0);
    db.data = db.lock + sizeof(sem_t);
    if (db.data == (char *)-1) {
        errorexit("mmap failed");
    }
    if (db_original_size == 0) {
        if (sem_init(db.lock, 1, 1) == -1) {
            errorexit("Error initializing semaphore");
        }
    }
    if(sem_wait(db.lock) == -1) {
        errorexit("Error locking");
    }
    memcpy(db.data + db_original_size, row, sizeof(struct ip_row));
    if (sem_post(db.lock) == -1) {
        errorexit("Error unlocking");
    }
    munmap(db.lock, db.size);
    close(db.fd);
    free(name);
}


int open_db(struct db_info *db) {
    char *name = malloc(NAME_SIZE);
    SHARED_MEM_NAME(name);
 
    db->fd = shm_open(name, O_CREAT | O_RDWR, S_IWUSR | S_IRUSR);
    if (db->fd == -1) {
       errorexit("shm_open");
    }

    db->size = get_fd_size(db->fd);

    if (db->size == 0) {
       close(db->fd);
       return -1;
    }
    if (ftruncate(db->fd, db->size) == -1) {
        errorexit("ftruncate");
    }
    db->lock = mmap(NULL, db->size, PROT_READ | PROT_WRITE, MAP_SHARED, db->fd, 0);
    db->data = db->lock + sizeof(sem_t);
    if (db->data == (char *)-1) {
        errorexit("mmap failed");
    }
    free(name);
    if(sem_wait(db->lock) == -1) {
        errorexit("Error locking");
    }
    return 0;
}

void close_db(struct db_info *db) {
    if (sem_post(db->lock) == -1) {
        errorexit("Error unlocking");
    }
    munmap(db->lock, db->size);
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
    if(open_db(&db) == -1) {
        puts("The databse is empty!");
        return;
    }
    char *addr_copy = db.data;
    struct ip_row *row;
    for (int i = 0; i < db.size/sizeof(struct ip_row); i++) {
        row = (struct ip_row *)addr_copy;
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
    if (open_db(&db) == -1)
        return 0;
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
    close_db(&db);
    return 0;
}

void lock(char *hostname) {
    struct db_info db;
    if (open_db(&db) == -1) {
        puts("The database is empty");
        return;
    }
    struct ip_row *row = get_row(hostname, &db);
    if (row == NULL) {
        printf("%s is not in the database\n", hostname);
        close_db(&db);
        return;
    }
    if (sem_wait(&row->row_lock) == -1) {
            errorexit("Could not open lock");
    }
    close_db(&db);
}

void unlock(char *hostname) {
    struct db_info db;
    if (open_db(&db) == -1) {
        puts("The database is empty");
        return;
    }
    struct ip_row *row = get_row(hostname, &db);
    if (row == NULL) {
        printf("%s is not in the database\n", hostname);
        close_db(&db);
        return;
    }
    if (sem_post(&row->row_lock) == -1) {
            errorexit("Could not open lock");
    }
    close_db(&db);
}

void querylock(char *hostname) {
    struct db_info db;
    if (open_db(&db) == -1) {
        puts("The database is empty");
        return;
    }
    struct ip_row *row = get_row(hostname, &db);
    if (row == NULL) {
        printf("%s is not in the database\n", hostname);
        close_db(&db);
        return;
    }
    int semval;
    if (sem_getvalue(&row->row_lock, &semval) == -1) {
            errorexit("Could not open lock");
    }
    printf("The semaphore for %s is %s\n",
            hostname, semval ? "unlocked" : "locked");
    close_db(&db);
}

void querylock_table() {
    struct db_info db;
    if (open_db(&db) == -1) {
        puts("The database is empty");
        return;
    }
    int semval;
    if (sem_getvalue(db.lock, &semval) == -1) {
            errorexit("Could not open lock");
    }
    printf("The semaphore for the table is %s\n", 
            semval ? "unlocked" : "locked");
    close_db(&db);
}

struct ip_row *get_row(char *hostname, struct db_info *db) {
    struct ip_row *row = (struct ip_row *)db->data;
    for (size_t i = 0; i < db->size/sizeof(struct ip_row); i++) {
        write(STDOUT_FILENO, row, sizeof(struct db_info));
        if(strncmp(hostname, row[i].row_name, NAME_SIZE) == 0) {
            return &row[i];
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

void save(char *file_name) {
    struct db_info db;
    if (open_db(&db) == -1) {
        puts("Database is empty!");
        return;
    }
    printf("%s", file_name);
    int savefd = open(file_name, O_CREAT | O_WRONLY, S_IWUSR | S_IRUSR);
    if (savefd == -1) {
        perror("Couldn't open file");
        close_db(&db);
        return;
    }
    struct ip_row *row = (struct ip_row *)db.data;
    for (size_t i = 0; i < db.size/sizeof(struct ip_row); i++) {
        if (sem_wait(&row[i].row_lock) == -1) {
            errorexit("Could not open lock");
        }
        if (write(savefd, &row[i], sizeof(struct ip_row)) == -1) {
            errorexit("Could not write to savefile");
        }
        if (sem_post(&row[i].row_lock) == -1) {
            errorexit("Could not close lock");
        }
    }
    close(savefd);
    close_db(&db);
}

void load(char *file_name) {
    struct db_info db;
    if (open_db(&db) != -1) {
        puts("Database is not empty!");
        close_db(&db);
        return;
    }
    int savefd = open(file_name, O_RDONLY);
    struct ip_row row;
    ssize_t read_bytes;
    while ((read_bytes = read(savefd, &row,
                    sizeof(struct ip_row))) == sizeof(struct ip_row)) {
        append_row(&row);
    }
    close(savefd);
}

void lock_table() {
    struct db_info db;
    if (open_db(&db) == -1) {
        puts("Database is empty!");
        return;
    }
    if (sem_wait(db.lock) == -1) {
        errorexit("Could not lock the db");
    }
    close_db(&db);
}

void unlock_table() {
    struct db_info db;
    if (open_db(&db) == -1) {
        puts("Database is empty!");
        return;
    }
    if (sem_post(db.lock) == -1) {
        errorexit("Could not lock the db");
    }
    close_db(&db);   
}

int cmd_exit() {
    struct db_info db;
    // destroy semaphores
    if (open_db(&db) != -1) {
        struct ip_row *row = (struct ip_row *)db.data;
        for (size_t i = 0; i < db.size/sizeof(struct ip_row); i++) {
            if (sem_destroy(&row[i].row_lock) == -1) {
                errorexit("sem_destroy");
            }
        }
        close_db(&db);
    }
    char name[NAME_SIZE];
    SHARED_MEM_NAME(name);
    if (shm_unlink(name) == -1) {
        errorexit("unlink");
        exit(-1);
    }
    exit(0);
}

void errorexit(char *message) {
    perror(message);
    exit(-1);
}
