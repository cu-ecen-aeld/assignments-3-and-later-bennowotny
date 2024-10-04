#include "cleanup.h"
#include <stdlib.h>
#include <unistd.h>

void cleanup_fd(const int *fd) {
  if (*fd != -1)
    close(*fd);
}

void cleanup_databuffer(char **ptr) { free(*ptr); }