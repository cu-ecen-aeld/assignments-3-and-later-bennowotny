CC = gcc
CFLAGS = -Wall -Werror -g

all: aesdsocket

# For this executable, build all required objects and then link
aesdsocket: aesdsocket.o  server_behavior.o cleanup.o
	$(CROSS_COMPILE)$(CC) $(CFLAGS) $^ -o $@ 

# For any object file target, compile the source file with the same name
%.o: %.c
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -c -o $@ $*.c

clean:
	rm -rf aesdsocket
	rm -rf *.o