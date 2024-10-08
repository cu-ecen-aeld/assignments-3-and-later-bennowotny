#ifndef CLEANUP_H
#define CLEANUP_H

#include <stdio.h>
#include <netdb.h>

/**
 * @brief Common cleanup utilities
 * 
 */

/**
 * @brief Attach a cleanup function to a variable  
 */
#define CLEANUP(x) __attribute__((__cleanup__(x)))

/**
 * @brief Close a file descriptor
 * 
 * @param fd file descriptor
 */
void cleanup_fd(const int *fd);

/**
 * @brief Free a buffer
 * 
 * @param ptr buffer
 */
void cleanup_databuffer(char **ptr);

#endif
