CC = gcc
CFLAGS = -Wall -Werror -g

all: aesdsocket

aesdsocket: aesdsocket.c
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -o aesdsocket aesdsocket.c

clean:
	rm -rf aesdsocket
	rm -rf *.o