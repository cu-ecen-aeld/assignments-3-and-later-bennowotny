#include <arpa/inet.h>
#include <asm-generic/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

#include "server_behavior.h"
#include "cleanup.h"

const char *SERVER_PORT = "9000";
const int LISTEN_BACKLOG = 1;
const char *TMP_FILE = "/var/tmp/aesdsocket";

/**
 * @brief Cleanup utilities only needed by the server management
 */


void cleanup_socket(const int *socketFd) {
  if (*socketFd != -1) {
    shutdown(*socketFd, SHUT_RDWR);
    cleanup_fd(socketFd);
  }
}

void cleanup_addrinfo(struct addrinfo **info) { freeaddrinfo(*info); }

void cleanup_tmpfile(FILE **fp) {
  unlink(TMP_FILE);
  fclose(*fp);
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
  if(setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) != 0){
    // Not a failure, keep going until we fail to bind the socket
    syslog(LOG_WARNING, "Could not configure address reuse, may cause spurious failures");
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

  // Open/clear the storage file
  FILE *tmpfile CLEANUP(cleanup_tmpfile) = fopen(TMP_FILE, "w+");
  if (tmpfile == NULL) {
    syslog(LOG_ERR, "Error when waiting for a connection");
    return EXIT_FAILURE;
  }

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
    const int connectionSocketFd CLEANUP(cleanup_socket) =
        accept(socketFd, &connectedAddr, &addrLen);
    // Handle the case where we catch our signal while waiting to accept a connection
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

    // Run the server behavior (read a line, write the file)
    if(on_server_connection(connectionSocketFd, tmpfile) != EXIT_SUCCESS){
      syslog(LOG_ERR, "Server processing failed");
      return EXIT_FAILURE;
    }

    // Connection closed automatically due to scoped cleanup of connectionSocketFd
    syslog(LOG_INFO, "Closed connection from %s", addrString);
  }
  
  if(signalCaught){
    syslog(LOG_INFO, "Caught signal, exiting");
  }

  // Variables that are stack-allocated will have a 'destructor' called
  // automatically
  return EXIT_SUCCESS;
}