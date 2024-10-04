#include "server_behavior.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/syslog.h>
#include "cleanup.h"

const size_t BUFFER_SIZE_INCREMENT = 1024;

int on_server_connection(int connectionFd, FILE* tmpFile){
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
      if ((recvDataSize = recv(connectionFd, dataBuf + dataBufferSize,
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
        bzero(dataBuf + dataBufferSize,
              dataBufferAllocationSize - dataBufferSize);
      } else {
        // We found the packet end!  Progress to the next step
        dataBufferSize = endOfPacket - dataBuf + 1; // Incl. newline
        packetComplete = true;
      }
    }

    // write data to file
    fseek(tmpFile, 0, SEEK_END);
    if (fwrite(dataBuf, sizeof(char), dataBufferSize, tmpFile) !=
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
    fseek(tmpFile, 0, SEEK_SET);
    while ((bytesRead = fread(fileBuf, sizeof(char), BUFFER_SIZE_INCREMENT,
                              tmpFile)) != 0) {
      // More data to read
      if (send(connectionFd, fileBuf, bytesRead, 0) != bytesRead) {
        syslog(LOG_ERR, "Could not send data to client");
        return EXIT_FAILURE;
      }
    }

    return EXIT_SUCCESS;
}