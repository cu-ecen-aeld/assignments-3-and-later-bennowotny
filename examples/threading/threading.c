#include "threading.h"
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

#define MS_PER_SEC  (1000)
#define NS_PER_MS   (1000000)

struct timespec ms_to_timespec(uint32_t ms){
    uint32_t sec = 0;
    while(ms >= MS_PER_SEC){
        ++sec;
        ms -= 1000;
    }
    return (struct timespec) {.tv_sec = sec, .tv_nsec = ms * NS_PER_MS};
}

int sleep_ms(uint32_t ms){
    struct timespec waiting_time = ms_to_timespec(ms);
    struct timespec remaining_time;
    while(nanosleep(&waiting_time, &remaining_time) == -1){
        if(errno == EINTR){
            waiting_time = remaining_time;
        }else{
            return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}

void* threadfunc(void* thread_param)
{

    // TODO: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    //struct thread_data* thread_func_args = (struct thread_data *) thread_param;

    struct thread_data* thread_func_args = (struct thread_data*) thread_param;
    // Assume success, any test can overwrite with failure
    thread_func_args->thread_complete_success = true;
    if(sleep_ms(thread_func_args->wait_to_obtain_ms)){
        thread_func_args->thread_complete_success = false;
    }
    if(pthread_mutex_lock(thread_func_args->mutex)){
        thread_func_args->thread_complete_success = false;
    }
    if(sleep_ms(thread_func_args->wait_to_release_ms)){
        pthread_mutex_unlock(thread_func_args->mutex);
        thread_func_args->thread_complete_success = false;
    }
    if(pthread_mutex_unlock(thread_func_args->mutex)){
        thread_func_args->thread_complete_success = false;
    }

    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * TODO: allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */

     
     // Check that args are valid, since these should be storable as unsigned
     if(wait_to_obtain_ms < 0 || wait_to_release_ms < 0){
        return false;
     }

     struct thread_data* args = malloc(sizeof(struct thread_data));
     if(args == NULL){
        return false;
     }
     args->mutex = mutex;
     args->wait_to_obtain_ms = wait_to_obtain_ms;
     args->wait_to_release_ms = wait_to_release_ms;

    if(pthread_create(thread, NULL, threadfunc, args)){
        return false;
    }

    return true;
}

