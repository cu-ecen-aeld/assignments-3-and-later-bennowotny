#include <arpa/inet.h>
#include <asm-generic/socket.h>
#include <bits/pthreadtypes.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

#include "cleanup.h"
#include "server_behavior.h"

const char *SERVER_PORT = "9000";
const int LISTEN_BACKLOG = 20; // Listen for more connections simultaneously
const char *TMP_FILE = 
#if USE_AESD_CHAR_DEVICE
"/dev/aesdchar"
#else
"/var/tmp/aesdsocket"
#endif
;

/**
 * @brief Cleanup utilities only needed by the server management
 */

void cleanup_addrinfo(struct addrinfo **info) { freeaddrinfo(*info); }

void cleanup_tmpfile(FILE **fp) {
  #if USE_AESD_CHAR_DEVICE
  #else
  unlink(TMP_FILE);
  #endif
  fclose(*fp);
}

void cleanup_mutex(pthread_mutex_t *mutex) { pthread_mutex_destroy(mutex); }

struct thread_entry_t {
  pthread_t worker_thread;
  atomic_flag thread_complete;
  SLIST_ENTRY(thread_entry_t) _entry;
};

SLIST_HEAD(thread_list_head_t, thread_entry_t);

void cleanup_slist(struct thread_list_head_t *head) {
  while (!SLIST_EMPTY(head)) {
    struct thread_entry_t *entry = (struct thread_entry_t *)SLIST_FIRST(head);
    SLIST_REMOVE_HEAD(head, _entry);
    int *threadReturnCode;
    pthread_join(entry->worker_thread, (void **)&threadReturnCode);
    if (*threadReturnCode != EXIT_SUCCESS) {
      syslog(LOG_ERR, "Thread ID %lu failed during processing",
             entry->worker_thread);
    }
    free(threadReturnCode);
    free(entry);
  }
}

static sig_atomic_t signalCaught = 0;

void signal_handler(int signal) {
  (void)signal; // Assumed that proper signals are registered
  signalCaught = 1;
}

/// Program entrypoint
int main(int argc, char **argv) {

  // Open syslog
  openlog("aesdsocket", 0, LOG_USER);
  atexit(closelog); // Handle open logs at program exit

  // Parse daemon behavior from cmdline
  bool isDaemon = false;
  if (argc > 1 && strcmp(argv[1], "-d") == 0) {
    isDaemon = true;
  }

  // Create socket
  const int socketFd CLEANUP(cleanup_socket) = socket(AF_INET, SOCK_STREAM, 0);
  if (socketFd == -1) {
    syslog(LOG_ERR, "Could not open the socket");
    return EXIT_FAILURE;
  }

  // Allow port reuse, even if the TCP shutdown states aren't complete
  if (setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) !=
      0) {
    // Not a failure, keep going until we fail to bind the socket
    syslog(LOG_WARNING,
           "Could not configure address reuse, may cause spurious failures");
  }

  {
    struct addrinfo hints;
    // Scoped in to destroy when finished using this
    struct addrinfo *addrResult CLEANUP(cleanup_addrinfo);

    // Set up binding of socket to port
    bzero(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, SERVER_PORT, &hints, &addrResult) != 0) {
      syslog(LOG_ERR, "Could not get address information");
      return EXIT_FAILURE;
    }

    if (bind(socketFd, addrResult->ai_addr, sizeof(struct sockaddr)) != 0) {
      syslog(LOG_ERR, "Could not bind to port");
      return EXIT_FAILURE;
    }

    // Release the addrinfo
  }

  // If we are a deamon, enter daemon mode
  if (isDaemon) {
    if (daemon(0, 0) == -1) {
      syslog(LOG_ERR, "Needed to daemonize, but couldn't");
      return EXIT_FAILURE;
    }
  }

  // Register the socket for inbound connections
  if (listen(socketFd, LISTEN_BACKLOG)) {
    syslog(LOG_ERR, "Could not listen for connections");
    return EXIT_FAILURE;
  }

#if USE_AESD_CHAR_DEVICE
  FILE* tmpfile CLEANUP(cleanup_tmpfile) = NULL;
#else
  // Open/clear the storage file
  FILE *tmpfile CLEANUP(cleanup_tmpfile) = fopen(TMP_FILE, "w+");
  if (tmpfile == NULL) {
    syslog(LOG_ERR, "Error when waiting for a connection");
    return EXIT_FAILURE;
  }
#endif

  // setup server multithreading
  pthread_t timestampThread;
  pthread_mutex_t endTimestamping;
  pthread_mutex_t tmp; // No cleanup, temporary copy of the mutex
#if USE_AESD_CHAR_DEVICE
  if (on_server_initialize(&tmp, &timestampThread, tmpfile, &endTimestamping) !=
      EXIT_SUCCESS) {
    syslog(LOG_ERR, "Could not initialize the server");
    return EXIT_FAILURE;
  }
#endif
  pthread_mutex_t tmpFileMutex CLEANUP(cleanup_mutex) =
      tmp; // Longterm mutex storage, requires cleanup

  // thread storage for worker threads
  struct thread_list_head_t threadList CLEANUP(cleanup_slist);
  SLIST_INIT(&threadList);

  // Register signal handling on SIGINT and SIGTERM
  struct sigaction signalBehavior = {0};
  signalBehavior.sa_flags = SA_SIGINFO;
  signalBehavior.sa_handler = signal_handler;
  sigaction(SIGTERM, &signalBehavior, NULL);
  sigaction(SIGINT, &signalBehavior, NULL);

  // signalCaught is set on signal reception
  while (signalCaught != 1) {
    // Wait for a connection
    struct sockaddr connectedAddr;
    socklen_t addrLen = sizeof(connectedAddr);
    // DON'T clean up the socket at this scope.  The thread lives longer, so it
    // should do cleanup
    const int connectionSocketFd = accept(socketFd, &connectedAddr, &addrLen);
    // Handle the case where we catch our signal while waiting to accept a
    // connection
    if (connectionSocketFd == -1 && signalCaught == 0) {
      syslog(LOG_ERR, "Error when waiting for a connection");
      return EXIT_FAILURE;
    } else if (signalCaught == 1) {
      break;
    }

    // Get client address
    char addrString[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(((struct sockaddr_in *)&connectedAddr)->sin_addr),
              addrString, sizeof(connectedAddr));
    syslog(LOG_INFO, "Accepted connection from %s", addrString);

    // DON'T clean up the memory here, since we would destroy it too early
    struct thread_entry_t *threadTrackingData =
        malloc(sizeof(struct thread_entry_t));
    if (threadTrackingData == NULL) {
      syslog(LOG_ERR, "Could not allocate thread tracking structure");
      return EXIT_FAILURE;
    }

#if USE_AESD_CHAR_DEVICE
    if(tmpfile == NULL){
      tmpfile = fopen(TMP_FILE, "w+");
      if (tmpfile == NULL) {
        syslog(LOG_ERR, "Error when waiting for a connection");
        return EXIT_FAILURE;
      }
      setbuf(tmpfile, NULL);
    }
#endif

    // Run the server behavior (read a line, write the file)
    if (on_server_connection(connectionSocketFd, tmpfile, &tmpFileMutex,
                             addrString, &(threadTrackingData->worker_thread),
                             &(threadTrackingData->thread_complete)) !=
        EXIT_SUCCESS) {
      syslog(LOG_ERR, "Server processing failed");
      free(threadTrackingData);
      return EXIT_FAILURE;
    }

    SLIST_INSERT_HEAD(&threadList, threadTrackingData, _entry);

    // Clean up existing threads when we make a new one
    struct thread_entry_t *threadEntry;
    struct thread_entry_t *lastEntry = NULL;
    SLIST_FOREACH(threadEntry, &threadList, _entry){
      if(!atomic_flag_test_and_set(&(threadEntry->thread_complete))){
        // flag cleared on complete
        int *threadReturnCode;
        pthread_join(threadEntry->worker_thread, (void **)&threadReturnCode);
        if (*threadReturnCode != EXIT_SUCCESS) {
          syslog(LOG_ERR, "Thread ID %lu failed during processing",
                threadEntry->worker_thread);
        }
        free(threadReturnCode);
        
        SLIST_REMOVE(&threadList, threadEntry, thread_entry_t, _entry);
        free(threadEntry);
        if(lastEntry != NULL){
          threadEntry = lastEntry; // Restore iteration
        }
      }
      lastEntry = threadEntry;
    }

  }

  if (signalCaught) {
    syslog(LOG_INFO, "Caught signal, exiting");
  }

  // stop the timestamping thread's sleep
  pthread_mutex_unlock(&endTimestamping);
  pthread_join(timestampThread, NULL);

  // Variables that are stack-allocated will have a 'destructor' called
  // automatically
  return EXIT_SUCCESS;
}