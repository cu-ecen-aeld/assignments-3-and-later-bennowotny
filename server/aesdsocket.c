#include <arpa/inet.h>
#include <asm-generic/socket.h>
#include <signal.h>
#include <netdb.h>
#include <netinet/in.h>
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

const char *SERVER_PORT = "9000";
const int LISTEN_BACKLOG = 0;
const char *TMP_FILE = "/var/tmp/aesdsocket";
const size_t BUFFER_SIZE_INCREMENT = 1024;

void cleanup_fd(const int *fd) { close(*fd); }

void cleanup_socket(const int *socketFd) {
  if(*socketFd != -1){
    shutdown(*socketFd, SHUT_RDWR);
    cleanup_fd(socketFd);
  }
}

void cleanup_addrinfo(struct addrinfo **info) { freeaddrinfo(*info); }

void cleanup_tmpfile(FILE **fp) {
  unlink(TMP_FILE);
  fclose(*fp);
}

void cleanup_databuffer(char **ptr) { free(*ptr); }

#define CLEANUP(x) __attribute__((__cleanup__(x)))

static sig_atomic_t signalCaught = 0;

void signal_handler(int /* signal */){
  signalCaught = 1;
}

int main(int argc, char** argv) {

  openlog("aesdsocket", 0, LOG_USER);
  atexit(closelog); // Handle open logs at program exit

  bool isDaemon = false;
  if(argc > 1 && strcmp(argv[1], "-d") == 0){
    isDaemon = true;
  }

  const int socketFd CLEANUP(cleanup_socket) = socket(AF_INET, SOCK_STREAM, 0);
  if (socketFd == -1) {
    syslog(LOG_ERR, "Could not open the socket");
    return EXIT_FAILURE;
  }

  setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));

  {
    struct addrinfo hints;
    struct addrinfo *addrResult CLEANUP(
        cleanup_addrinfo); // Scoped in to destroy when finished using this

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
  }

  if(isDaemon){
    if(daemon(0, 0) == -1){
      syslog(LOG_ERR, "Needed to daemonize, but couldn't");
      return EXIT_FAILURE;
    }
  }

  if (listen(socketFd, LISTEN_BACKLOG)) {
    syslog(LOG_ERR, "Could not listen for connections");
    return EXIT_FAILURE;
  }
  
  FILE *tmpfile CLEANUP(cleanup_tmpfile) = fopen(TMP_FILE, "w+");
  if (tmpfile == NULL) {
    syslog(LOG_ERR, "Error when waiting for a connection");
    return EXIT_FAILURE;
  }

  struct sigaction signalBehavior = {0};
  signalBehavior.sa_flags = SA_SIGINFO;
  signalBehavior.sa_handler = signal_handler;
  sigaction(SIGTERM, &signalBehavior, NULL);
  sigaction(SIGINT, &signalBehavior, NULL);

  while(signalCaught != 1)
  {
    struct sockaddr connectedAddr;
    socklen_t addrLen = sizeof(connectedAddr);
    const int connectionSocketFd CLEANUP(cleanup_socket) =
        accept(socketFd, &connectedAddr, &addrLen);
    if (connectionSocketFd == -1 && signalCaught == 0) {
      syslog(LOG_ERR, "Error when waiting for a connection");
      return EXIT_FAILURE;
    }else if(signalCaught == 1){
      break;
    }

    char addrString[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(((struct sockaddr_in *)&connectedAddr)->sin_addr),
              addrString, sizeof(connectedAddr));
    syslog(LOG_INFO, "Connection from address: %s", addrString);

    char *dataBuf CLEANUP(cleanup_databuffer) = malloc(BUFFER_SIZE_INCREMENT);
    if (dataBuf == NULL) {
      syslog(LOG_ERR, "Could not malloc initial buffer resource");
      return EXIT_FAILURE;
    }
    size_t dataBufferAllocationSize = BUFFER_SIZE_INCREMENT;
    size_t dataBufferSize = 0;
    bzero(dataBuf, BUFFER_SIZE_INCREMENT);
    bool packetComplete = false;

    while (!packetComplete) {
      ssize_t recvDataSize = 0;
      // Leave space for a null-termination character
      if ((recvDataSize = recv(connectionSocketFd, dataBuf + dataBufferSize,
                               BUFFER_SIZE_INCREMENT - 1, 0)) < 0) {
        syslog(LOG_ERR, "Failed to get received data from socket!");
        return EXIT_FAILURE;
      }
      dataBufferSize += recvDataSize;
      const char *const endOfPacket = strstr(dataBuf, "\n");
      if (endOfPacket == NULL) {
        // Did not hear a newline yet; keep searching
        dataBuf =
            realloc(dataBuf, dataBufferAllocationSize + BUFFER_SIZE_INCREMENT);
        if (dataBuf == NULL) {
          syslog(LOG_ERR, "Reallocating packet buffer failed");
          return EXIT_FAILURE;
        }
        dataBufferAllocationSize += BUFFER_SIZE_INCREMENT;
        bzero(dataBuf + dataBufferSize, dataBufferAllocationSize - dataBufferSize);
      } else {
        // We found the packet end!  Progress to the next step
        dataBufferSize = endOfPacket - dataBuf + 1; // Incl. newline
        packetComplete = true;
      }
    }

    // write data to file
    fseek(tmpfile, 0, SEEK_END);
    if (fwrite(dataBuf, sizeof(char), dataBufferSize, tmpfile) !=
        dataBufferSize) {
      syslog(LOG_ERR, "Could not write packet data to file");
      return EXIT_FAILURE;
    }

    // write file to socket
    char *fileBuf CLEANUP(cleanup_databuffer) = malloc(BUFFER_SIZE_INCREMENT);
    if (fileBuf == NULL) {
      syslog(LOG_ERR, "Could not malloc initial buffer resource");
      return EXIT_FAILURE;
    }
    size_t bytesRead = 0;
    fseek(tmpfile, 0, SEEK_SET);
    while((bytesRead = fread(fileBuf, sizeof(char), BUFFER_SIZE_INCREMENT, tmpfile)) != 0){
      // More data to read
      if(send(connectionSocketFd, fileBuf, bytesRead, 0) != bytesRead){
        syslog(LOG_ERR, "Could not send data to client");
        return EXIT_FAILURE;
      }
    }

    syslog(LOG_INFO, "Connection closed for address: %s", addrString);
  }

  // Variables that are stack-allocated will have a 'destructor' called
  // automatically
  return EXIT_SUCCESS;
}