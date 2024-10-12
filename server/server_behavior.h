#ifndef SERVER_BEHAVIOR_H
#define SERVER_BEHAVIOR_H

#include <netinet/in.h>
#include <stdio.h>
#include <pthread.h>

/**
 * @brief Perform the server action of reading in a packet, appending it to the
 * tempfile, and sending back the tempfile.
 *
 * @param connectionFd A file descriptor for the active connection
 * @param tmpFile A FILE pointer to the tempfile (needs RW access)
 * @param tmpFileMutex A mutex to protect I/O to the @ref tmpFile
 * @param clientAddr The client address, reported on connection closure
 * @param out_thread A pthread that can be joined when work is complete.  
 * Joining returns EXIT_SUCCESS on packet handled or else EXIT_FAILURE (as int*, memory must be freed by the joiner)
 * @return EXIT_SUCCESS if the thread was started, EXIT_FAILURE on error.  The
 * program should clean up and close on EXIT_FAILURE
 */
int on_server_connection(int connectionFd, FILE *tmpFile, pthread_mutex_t* tmpFileMutex,  char clientAddr[INET_ADDRSTRLEN], pthread_t* out_thread);

/**
 * @brief Setup the server's multithreading implementation
 * 
 * @param out_tmpFileMutex a mutex to protect the temp file for the server.  Used on thread creation
 * @param out_timestampThread a thread recording timestamps to the tempFile.  Should be joined separately before general resource cleanup
 * @param tmpFile the temporary file for logging (required by timestamping thread)
 * @param endTimestamping a flag to signal the timestamp thread to stop.  Should be cleared before joining
 * @return EXIT_SUCCESS if success, EXIT_FAILURE on error.  The
 * program should clean up and close on EXIT_FAILURE
 */
int on_server_initialize(pthread_mutex_t *out_tmpFileMutex, pthread_t *out_timestampThread, FILE* tmpFile, pthread_mutex_t* endTimestamping);

#endif
