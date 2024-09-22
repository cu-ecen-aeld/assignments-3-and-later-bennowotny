#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#include <syslog.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <netdb.h>

const char* SERVER_PORT = "9000";
const int LISTEN_BACKLOG = 0;

int main(){

  openlog("aesdsocket", 0, LOG_USER);

  const int socketFd = socket(AF_INET, SOCK_STREAM, 0);
  if(socketFd == -1){
    syslog(LOG_ERR, "Could not open the socket");
    closelog();
    return EXIT_FAILURE;
  }
  
  struct addrinfo hints;
  struct addrinfo *addrResult;

  bzero(&hints, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  
  if(getaddrinfo(NULL, SERVER_PORT, &hints, &addrResult) != 0){
    syslog(LOG_ERR, "Could not get address information");
    shutdown(socketFd, SHUT_RDWR);
    close(socketFd);
    closelog();
    return EXIT_FAILURE;
  }

  if (bind(socketFd, addrResult->ai_addr, sizeof(struct sockaddr)) != 0){
    syslog(LOG_ERR, "Could not bind to port");
    freeaddrinfo(addrResult);
    shutdown(socketFd, SHUT_RDWR);
    close(socketFd);
    closelog();
    return EXIT_FAILURE;
  }

  freeaddrinfo(addrResult);

  if(listen(socketFd, LISTEN_BACKLOG)){
    syslog(LOG_ERR, "Could not listen for connections");
    shutdown(socketFd, SHUT_RDWR);
    close(socketFd);
    closelog();
    return EXIT_FAILURE;
  }

  const int connectionSocketFd = accept(socketFd, NULL, NULL);
  if(connectionSocketFd == -1){
    syslog(LOG_ERR, "Error when waiting for a connection");
    shutdown(socketFd, SHUT_RDWR);
    close(socketFd);
    closelog();
    return EXIT_FAILURE;
  }

  if(send(connectionSocketFd, "Hello, world!\n", 15, 0) != 14){
    syslog(LOG_ERR, "Error when sending data");
    shutdown(socketFd, SHUT_RDWR);
    close(connectionSocketFd);
    close(socketFd);
    closelog();
    return EXIT_FAILURE;
  }

  shutdown(connectionSocketFd, SHUT_RDWR);
  shutdown(socketFd, SHUT_RDWR);
  close(connectionSocketFd);
  close(socketFd);
  closelog();

  return EXIT_SUCCESS;
}