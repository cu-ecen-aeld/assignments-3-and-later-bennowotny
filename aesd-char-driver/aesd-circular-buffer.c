/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

#include "aesd-circular-buffer.h"

#define AESD_BUFFER_INCREMENT(x) ((x) = (((x) + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED))

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *  aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn )
{
    size_t currBlockOffset = buffer->out_offs;
    const size_t numBlocksToSearch = buffer->full ? 
        AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED : 
        (buffer->in_offs + AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED - buffer->out_offs) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; // avoid negative modulus shenannigans
    size_t blocksSearched = 0;
    while(char_offset >= buffer->entry[currBlockOffset].size && blocksSearched < numBlocksToSearch){
        char_offset -= buffer->entry[currBlockOffset].size;
        AESD_BUFFER_INCREMENT(currBlockOffset);
        ++blocksSearched;
    }

    if(blocksSearched >= numBlocksToSearch){
        return NULL; // Exhausted filled-in blocks
    }

    *entry_offset_byte_rtn = char_offset; // Amount of counting into the current block 
    return &(buffer->entry[currBlockOffset]);
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
*/
const char* aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    const char* possibly_erased_data = buffer->entry[buffer->in_offs].buffptr;
    buffer->entry[buffer->in_offs] = *add_entry;
    AESD_BUFFER_INCREMENT(buffer->in_offs);
    if(buffer->in_offs == buffer->out_offs && !(buffer->full)){
        buffer->full = true;
        // we just became full, but didn't overwrite yet
    }else if(buffer->full){
        AESD_BUFFER_INCREMENT(buffer->out_offs);
        // we just overwrote data
        return possibly_erased_data;
    }
    // no overwrite occured
    return NULL;
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}
