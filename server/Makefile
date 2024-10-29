CC ?= gcc
USE_AESD_CHAR_DEVICE ?= 1
CFLAGS ?= -Wall -Werror -g
CFLAGS += -DUSE_AESD_CHAR_DEVICE=$(USE_AESD_CHAR_DEVICE)

all: aesdsocket

# For this executable, build all required objects and then link
aesdsocket: aesdsocket.o  server_behavior.o cleanup.o
	$(CROSS_COMPILE)$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# For any object file target, compile the source file with the same name
%.o: %.c
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -c -o $@ $*.c

clean:
	rm -rf aesdsocket
	rm -rf *.o