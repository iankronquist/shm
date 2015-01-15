CC=gcc
CFLAGS=  -O0 -Wall -Wextra -g -std=c99

all: dbclient sockets

dbclient:
	$(CC) dbclient.c -o dbclient $(CFLAGS)

sockets:
	$(CC) sockets/server.c -o server $(CFLAGS)
	$(CC) sockets/client.c -o client $(CFLAGS)

clean:
	rm -f dbclient
	rm -f client
	rm -f server
	rm -rf *dSYM
