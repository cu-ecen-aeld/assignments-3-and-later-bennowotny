#ifndef SERVER_BEHAVIOR_H
#define SERVER_BEHAVIOR_H

#include <stdio.h>

/**
 * @brief Perform the server action of reading in a packet, appending it to the tempfile, 
 * and sending back the tempfile.
 * 
 * @param connectionFd A file descriptor for the active connection
 * @param tmpFile A FILE pointer to the tempfile (needs RW access)
 * @return EXIT_SUCCESS if the packet was handled, EXIT_FAILURE on error.  The program should clean up and close on EXIT_FAILURE
 */
int on_server_connection(int connectionFd, FILE* tmpFile);

#endif
