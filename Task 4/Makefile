CC      = gcc
CFLAGS  = -Wall -Wextra -Werror -std=c11 -D_POSIX_C_SOURCE=200809L -pthread
LDFLAGS = -pthread

.PHONY: all clean

all: server client

server: server.o common.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

client: client.o common.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c common.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o server client
