#include "cleanup.h"
#include <stdlib.h>
#include <unistd.h>

void cleanup_fd(const int *fd) {
  if (*fd != -1)
    close(*fd);
}

void cleanup_databuffer(char **ptr) { free(*ptr); }

void cleanup_socket(const int *socketFd) {
  if (*socketFd != -1) {
    shutdown(*socketFd, SHUT_RDWR);
    cleanup_fd(socketFd);
  }
}

void cleanup_malloc(void* mem){free(mem);}