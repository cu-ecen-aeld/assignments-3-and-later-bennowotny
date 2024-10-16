#include "server_behavior.h"
#include "cleanup.h"
#include <bits/pthreadtypes.h>
#include <bits/time.h>
#include <bits/types/sigset_t.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/syslog.h>
#include <time.h>
#include <unistd.h>

const size_t BUFFER_SIZE_INCREMENT = 1024;

typedef struct {
  int connectionFd;                 // unique per connection - no locking
  FILE *tmpFile;                    // shared across connections - LOCK
  pthread_mutex_t *tmpFileMutex;    // locks above variable
  char clientAddr[INET_ADDRSTRLEN]; // copied per connection - no locking
  atomic_flag *completeFlag;        // unique per connection - no locking
  int *returnCode;                  // unique per connection - no locking
} server_thread_param_t;

#define _THREAD_RETURN(x)                                                      \
  do {                                                                         \
    *returnCode = x;                                                           \
    return returnCode;                                                         \
  } while (false)

#define THREAD_RETURN_FAILURE _THREAD_RETURN(EXIT_FAILURE)
#define THREAD_RETURN_SUCCESS _THREAD_RETURN(EXIT_SUCCESS)

void cleanup_client_addr(char **addr) {
  syslog(LOG_INFO, "Closed connection from %s", *addr);
}

void cleanup_completion_flag(atomic_flag **flag) { atomic_flag_clear(*flag); }

/**
 * @brief Write to the end of a file, guarded by a mutex
 *
 * @param data character buffer to write
 * @param dataSize number of characters to wrilte
 * @param file FILE* to the output file
 * @param write_guard guard mutex (locked/unlocked around write)
 * @return int EXIT_SUCCESS on pass, else EXIT_FAILURE
 */
static int write_safe_to_file_end(char *data, size_t dataSize, FILE *file,
                                  pthread_mutex_t *write_guard) {
  // guard use of the file
  pthread_mutex_lock(write_guard);
  // write buffer to file
  fseek(file, 0, SEEK_END);
  if (fwrite(data, sizeof(char), dataSize, file) != dataSize) {
    syslog(LOG_ERR, "Could not write data to file");
    pthread_mutex_unlock(write_guard);
    return EXIT_FAILURE;
  }
  // flush data to disc, avoid relying on OS synchronization for the FS
  fflush(file);
  const int tmpFileFd = fileno(file);
  fsync(tmpFileFd);
  pthread_mutex_unlock(write_guard);
  return EXIT_SUCCESS;
}

static void *server_work_thread(void *param) {
  // Parse the thread params
  const server_thread_param_t *parsedParams = (server_thread_param_t *)param;
  int connectionFd CLEANUP(cleanup_socket) =
      parsedParams->connectionFd; // Have the thread own the connection lifetime
  FILE *tmpFile = parsedParams->tmpFile;
  char clientAddr[INET_ADDRSTRLEN];
  memcpy(clientAddr, parsedParams->clientAddr, INET_ADDRSTRLEN);
  char *clientAddrBegin CLEANUP(cleanup_client_addr) =
      clientAddr; // Token variable to print out the client address on
                  // closing the connection
  int *returnCode = parsedParams->returnCode;
  pthread_mutex_t *tmpFileMutex = parsedParams->tmpFileMutex;
  // autoclear flag on exit
  atomic_flag *completionFlag CLEANUP(cleanup_completion_flag) =
      parsedParams->completeFlag;
  // All parameter data copied, we can free the parameter block
  free(param);

  // Scoped block to free resources for reading the packet
  // packet will be smaller than ram, but might not be small enough for the
  // writeback buffering also
  {
    char *dataBuf CLEANUP(cleanup_databuffer) = malloc(BUFFER_SIZE_INCREMENT);
    if (dataBuf == NULL) {
      syslog(LOG_ERR, "Could not malloc read buffer resource");
      THREAD_RETURN_FAILURE;
    }
    size_t dataBufferAllocationSize = BUFFER_SIZE_INCREMENT;
    size_t dataBufferSize = 0;
    bzero(dataBuf, BUFFER_SIZE_INCREMENT);
    bool packetComplete = false;

    // Read loop: read network packets until we find a newline.  Keep the buffer
    // in-memory
    while (!packetComplete) {
      ssize_t recvDataSize = 0;
      // Leave space for a null-termination character
      if ((recvDataSize = recv(connectionFd, dataBuf + dataBufferSize,
                               BUFFER_SIZE_INCREMENT - 1, 0)) < 0) {
        syslog(LOG_ERR, "Failed to get received data from socket!");
        THREAD_RETURN_FAILURE;
      }
      dataBufferSize += recvDataSize;
      const char *const endOfPacket = strstr(dataBuf, "\n");
      if (endOfPacket == NULL) {
        // Did not hear a newline yet; keep searching
        dataBuf =
            realloc(dataBuf, dataBufferAllocationSize + BUFFER_SIZE_INCREMENT);
        if (dataBuf == NULL) {
          syslog(LOG_ERR, "Reallocating packet buffer failed");
          THREAD_RETURN_FAILURE;
        }
        dataBufferAllocationSize += BUFFER_SIZE_INCREMENT;
        bzero(dataBuf + dataBufferSize,
              dataBufferAllocationSize - dataBufferSize);
      } else {
        // We found the packet end!  Progress to the next step
        dataBufferSize = endOfPacket - dataBuf + 1; // Incl. newline
        packetComplete = true;
      }
    }

    if (write_safe_to_file_end(dataBuf, dataBufferSize, tmpFile,
                               tmpFileMutex) != EXIT_SUCCESS) {
      syslog(LOG_ERR, "Could not write packet data to file");
      THREAD_RETURN_FAILURE;
    }
  } // Clean up inbound packet resources

  // guard use of the file
  pthread_mutex_lock(tmpFileMutex);
  // write file to socket
  char *fileBuf CLEANUP(cleanup_databuffer) = malloc(BUFFER_SIZE_INCREMENT);
  if (fileBuf == NULL) {
    syslog(LOG_ERR, "Could not malloc initial buffer resource");
    pthread_mutex_unlock(tmpFileMutex);
    THREAD_RETURN_FAILURE;
  }
  size_t bytesRead = 0;
  fseek(tmpFile, 0, SEEK_SET);
  while ((bytesRead = fread(fileBuf, sizeof(char), BUFFER_SIZE_INCREMENT,
                            tmpFile)) != 0) {
    // More data to read
    if (send(connectionFd, fileBuf, bytesRead, 0) != bytesRead) {
      syslog(LOG_ERR, "Could not send data to client");
      pthread_mutex_unlock(tmpFileMutex);
      THREAD_RETURN_FAILURE;
    }
  }

  pthread_mutex_unlock(tmpFileMutex);
  // Clean up the writeback buffer on function scope end

  THREAD_RETURN_SUCCESS;
}

typedef struct {
  pthread_mutex_t *tmpFileMutex;
  FILE *tmpFile;
  pthread_mutex_t *endTimestamping;
} timestamp_params_t;

#define MS_PER_SEC (1000)
#define NS_PER_MS (1000000)

// Sleep for 10s
#define SLEEP_TIME_MS (10000)

static void *record_timestamp(void *param) {
  const timestamp_params_t *parsedParams = (timestamp_params_t *)param;
  FILE *tmpFile = parsedParams->tmpFile;
  pthread_mutex_t *tmpFileMutex = parsedParams->tmpFileMutex;
  pthread_mutex_t *endTimestamping = parsedParams->endTimestamping;
  free(param);

  while (true) {
    struct timespec currTime;
    clock_gettime(CLOCK_REALTIME, &currTime);
    currTime.tv_sec += 10;
    if (pthread_mutex_timedlock(endTimestamping, &currTime) != ETIMEDOUT) {
      break;
    }

    time_t wallTime = time(NULL);
    struct tm *timestamp = localtime(&wallTime);
    char timeString[1024] = "timestamp: "; // buffer for time string
    // parse the time into a string, after the 'timestamp:' label
    if (strftime(timeString + 11, 1024, "%a, %d %b %Y %T %z", timestamp) == 0) {
      syslog(LOG_ERR, "Could not log time, try again later...");
      continue;
    }
    const size_t timeString_len = strlen(timeString) + 1;
    timeString[timeString_len - 1] =
        '\n'; // replace null-terminator with newline

    if (write_safe_to_file_end(timeString, timeString_len, tmpFile,
                               tmpFileMutex) != EXIT_SUCCESS) {
      syslog(LOG_ERR,
             "Could not write timestamp data to file, try again later...");
      continue;
    }
  }

  return NULL;
}

int on_server_initialize(pthread_mutex_t *out_tmpFileMutex,
                         pthread_t *out_timestampThread, FILE *tmpFile,
                         pthread_mutex_t *endTimestamping) {
  if (pthread_mutex_init(out_tmpFileMutex, NULL) != 0) {
    syslog(LOG_ERR, "Could not initialize the mutex for the tempFile");
    return EXIT_FAILURE;
  }

  if (pthread_mutex_init(endTimestamping, NULL) != 0) {
    syslog(LOG_ERR,
           "Could not initialize the mutex for the ending the timestamping");
    return EXIT_FAILURE;
  }
  pthread_mutex_lock(endTimestamping);

  timestamp_params_t *param = malloc(sizeof(timestamp_params_t));
  if (param == NULL) {
    syslog(LOG_ERR, "Could not allocate space for thread parameters");
    return EXIT_FAILURE;
  }

  param->tmpFile = tmpFile;
  param->tmpFileMutex = out_tmpFileMutex;
  param->endTimestamping = endTimestamping;

  if (pthread_create(out_timestampThread, NULL, record_timestamp, param) != 0) {
    syslog(LOG_ERR, "Could not start timestamping thread");
    free(param);
    return EXIT_FAILURE;
  }

  // From here, the thread must free the parameters

  return EXIT_SUCCESS;
}

int on_server_connection(int connectionFd, FILE *tmpFile,
                         pthread_mutex_t *tmpFileMutex,
                         char clientAddr[INET_ADDRSTRLEN],
                         pthread_t *out_thread, atomic_flag *out_flag) {

  server_thread_param_t *param = malloc(sizeof(server_thread_param_t));
  if (param == NULL) {
    syslog(LOG_ERR, "Could not allocate space for thread parameters");
    return EXIT_FAILURE;
  }

  param->connectionFd = connectionFd;
  param->tmpFile = tmpFile;
  param->tmpFileMutex = tmpFileMutex;
  bzero(param->clientAddr, INET_ADDRSTRLEN);
  if (snprintf(param->clientAddr, INET_ADDRSTRLEN, "%s", clientAddr) < 0) {
    syslog(LOG_ERR, "Could not write client addr to thread");
    return EXIT_FAILURE;
  }
  param->returnCode = malloc(sizeof(*(param->returnCode)));
  if (param->returnCode == NULL) {
    syslog(LOG_ERR, "Could not allocate space for thread return code");
    free(param);
    return EXIT_FAILURE;
  }
  *(param->returnCode) = EXIT_FAILURE;      // assume failure
  (void)atomic_flag_test_and_set(out_flag); // assume incomplete
  param->completeFlag = out_flag;

  if (pthread_create(out_thread, NULL, server_work_thread, (void *)param) !=
      0) {
    syslog(LOG_ERR, "Could not start the worker thread");
    free(param->returnCode);
    free(param);
    return EXIT_FAILURE;
  }

  // From here, the thread must free the parameters

  return EXIT_SUCCESS;
}