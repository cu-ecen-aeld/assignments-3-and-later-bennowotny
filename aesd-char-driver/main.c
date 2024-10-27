/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "aesd-circular-buffer.h"
#include "aesd_ioctl.h"
#include "aesdchar.h"
#include "asm/string.h"
#include "asm/uaccess.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Ben Nowotny"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    struct aesd_dev* devicePtr;
    PDEBUG("open");
    /**
     * TODO: handle open
     */
    // setup the file data to use the device pointer
    devicePtr = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = devicePtr;
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */
    // there is only 1 global device, no deallocation needs to be done per. file
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -EINVAL;
    struct aesd_buffer_entry *datablk = NULL;
    size_t strOffset = 0;
    struct aesd_dev* device;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle read
     */

    // extract device from the file pointer
    device = (struct aesd_dev*) (filp->private_data);

    // exit early if invalid memory is used
    if(!access_ok(buf, count)){
        retval = -EINVAL;
        goto exit;
    }

    // reading from buffer - lock buffer mutex
    if(mutex_lock_interruptible(&device->buffer_mutex) != 0){
        retval = -EINTR;
        goto exit;
    }

    // read the datablock from the buffer
    datablk = aesd_circular_buffer_find_entry_offset_for_fpos(&device->buffer, *f_pos, &strOffset);
    PDEBUG("blk is %p", datablk);
    if(datablk == NULL){
        // not enough data in the buffer for this read
        retval = 0;
        goto unlock_bufferMtx;
    }

    // calculate the size of the read based off the count and the data available
    retval = (datablk->size - strOffset) > count ? count : (datablk->size - strOffset);
    *f_pos += retval;
    PDEBUG("reading out %zu bytes", retval);

    // give the data to the user
    if(copy_to_user(buf, datablk->buffptr + strOffset, retval) != 0){
        retval = -EINVAL;
        goto unlock_bufferMtx;
    }

    goto unlock_bufferMtx; // noop, signaling intent

unlock_bufferMtx:
    mutex_unlock(&device->buffer_mutex);
exit:
    return retval;
}

// helper function, like strstr but without 0-termination assumptions
// based off of memmem(3)
static void* memmem(const void* haystack, size_t haystackSize, const void* needle, size_t needleSize){
    const char* haystackData = haystack;
    const char* needleData = needle;
    size_t index = 0;
    size_t matchSize = 0;

    while(index < (haystackSize - needleSize + 1)){
        while(matchSize < needleSize && (*(haystackData + index + matchSize) == *(needleData + matchSize))){
            ++matchSize;
        }
        if(matchSize == needleSize){
            return (void*)(haystackData + index);
        }
        matchSize = 0;
        ++index;
    }
    return NULL;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    char* strBuf = NULL;
    const char* eolPtr = NULL;
    char* reallocPtr = NULL;
    struct aesd_buffer_entry newEntry;
    const char *removedEntry;
    struct aesd_dev* device;

    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);

    // extrace the device from the file data
    device = (struct aesd_dev*) (filp->private_data);
    
    // exit early if given a bad pointer
    if(!access_ok(buf, count)){
        retval = -EINVAL;
        goto exit;
    }

    // take ownership of the given data
    strBuf = kmalloc(count, GFP_KERNEL);
    if(strBuf == NULL){
        retval = -ENOMEM;
        goto exit;
    }

    if(copy_from_user(strBuf, buf, count) != 0){
        retval = -EINVAL;
        goto cleanup_tmpbuffer;
    }

    // mutate the 'nextLine' - lock the mutex
    if(mutex_lock_interruptible(&device->nextLine_mutex) != 0){
        retval = -EINTR;
        goto cleanup_tmpbuffer;
    }

    // look for a newline in the input data, adjust the size if a newline is found
    eolPtr = memmem(strBuf, count, "\n", 1);
    count = eolPtr == NULL ? count : eolPtr - strBuf + 1;
    if(eolPtr == NULL && device->nextLine == NULL){
        // We don't have a newline or an existing line
        // start a new one
        device->nextLine = strBuf;
        device->nextLineLength = count;    
        // exit early, don't deallocate
        retval = count;
        *f_pos += count;
        goto unlock_nxtLineMutex;
    }else if(eolPtr == NULL && device->nextLine != NULL){
        // We don't have a newline, but we already had a line going
        // append
        reallocPtr = krealloc(device->nextLine, device->nextLineLength + count, GFP_KERNEL);
        if(reallocPtr == NULL){
            // keep the command, return failure
            retval = -ENOMEM;
            goto unlock_nxtLineMutex;
        }
        device->nextLine = reallocPtr;
        memcpy(device->nextLine + device->nextLineLength, strBuf, count);
        device->nextLineLength += count;
        retval = count;
        *f_pos += count;
    }else if(eolPtr != NULL && device->nextLine == NULL){
        // We have a newline and no previous data pending
        // skip the nextLine buffer, write straight to the buffer
        if(mutex_lock_interruptible(&device->buffer_mutex) != 0){
            retval = -EINTR;
            goto cleanup_tmpbuffer;
        }

        newEntry.buffptr = strBuf;
        newEntry.size = count;
        removedEntry = aesd_circular_buffer_add_entry(&device->buffer, &newEntry);
        kfree(removedEntry);

        mutex_unlock(&device->buffer_mutex);
        // exit early, don't free the buffer
        retval = count;
        *f_pos += count;
        goto unlock_nxtLineMutex;
    }else{
        // We have a newline and previous data
        // append the string and then steal the appended string
        reallocPtr = krealloc(device->nextLine, device->nextLineLength + count, GFP_KERNEL);
        if(reallocPtr == NULL){
            // keep the command, return failure
            retval = -ENOMEM;
            goto unlock_nxtLineMutex;
        }
        device->nextLine = reallocPtr;
        memcpy(device->nextLine + device->nextLineLength, strBuf, count);
        device->nextLineLength += count;

        if(mutex_lock_interruptible(&device->buffer_mutex) != 0){
            retval = -EINTR;
            goto cleanup_tmpbuffer;
        }

        newEntry.buffptr = device->nextLine;
        newEntry.size = device->nextLineLength;
        removedEntry = aesd_circular_buffer_add_entry(&device->buffer, &newEntry);
        kfree(removedEntry);

        mutex_unlock(&device->buffer_mutex);
        device->nextLine = NULL;
        device->nextLineLength = 0;
        retval = count;
        *f_pos += count;
    }

    goto cleanup_tmpbuffer; // noop, signifying intention

cleanup_tmpbuffer:
    kfree(strBuf);
unlock_nxtLineMutex:
    mutex_unlock(&aesd_device.nextLine_mutex);
exit:    
    return retval;
}

loff_t aesd_llseek (struct file *fp, loff_t offset, int whence){
    int retval = -EINVAL;
    struct aesd_buffer_entry* entryptr = NULL;
    struct aesd_dev* device = NULL;
    int i;
    loff_t fileSize = 0;
    PDEBUG("seeking to %lld with whence %d", offset, whence);

    // grab the device from the file information
    device = (struct aesd_dev*)(fp->private_data);

    if(mutex_lock_interruptible(&device->buffer_mutex) != 0){
        return -EINTR;
    }

    AESD_CIRCULAR_BUFFER_FOREACH(entryptr, &device->buffer, i){
        fileSize += entryptr->size;
    }

    mutex_unlock(&device->buffer_mutex);

    if(whence == SEEK_END || whence == SEEK_SET || whence == SEEK_CUR){
        switch(whence){
        case SEEK_END:
            // writes to the end go to end-relative offset
            retval = fileSize + offset;
            break;
        case SEEK_SET:
            // offsets from the beginning of the file go to their requested offset
            retval = offset;
            break;
        case SEEK_CUR:
            // current offsets are wrt the current position
            retval = fp->f_pos + offset;
            break;
        default:
            break;
        }

        // bound the return value to be within file bounds
        retval = retval > fileSize ? fileSize : retval;
        retval = retval < 0 ? 0 : retval;
    }

    // update the file pointer so it remembers its new offset (if there's not an error)
    if(retval >= 0)
        fp->f_pos = retval;
    return retval;
}


int aesd_fsync(struct file *fp, loff_t unused1, loff_t unused2, int datasync){
    // data in memory, always synced
    return 0;
}

long aesd_ioctl (struct file *fp, unsigned int opcode, unsigned long param){
    struct aesd_seekto command_data;
    loff_t offset = 0;
    size_t buffer_offset = 0;
    struct aesd_buffer_entry* entryptr;
    int i;
    struct aesd_dev* device;
    PDEBUG("ioctl called with code %d", opcode);

    device = (struct aesd_dev*)(fp->private_data);

    if(!access_ok((void*) param, sizeof(struct aesd_seekto))){
        return -EINVAL;
    }

    if(opcode != AESDCHAR_IOCSEEKTO){
        return -EINVAL;
    }

    if(copy_from_user(&command_data, (void*)param, sizeof(struct aesd_seekto)) != 0){
        return -EINVAL;
    }

    if(mutex_lock_interruptible(&device->buffer_mutex) != 0){
        return -EINTR;
    }
    mutex_unlock(&device->buffer_mutex);

    AESD_CIRCULAR_BUFFER_FOREACH(entryptr, &device->buffer, i){
        if(buffer_offset == command_data.write_cmd) goto post_loop;
        offset += entryptr->size;
        ++buffer_offset;
    }
    // searched the whole loop without finding the entry: not enough entries
    return -EINVAL;

post_loop:
    if(entryptr->size < command_data.write_cmd_offset){
        // not enough bytes in requested command
        return -EINVAL;
    }

    offset += command_data.write_cmd_offset;

    fp->f_pos = offset;

    return 0;
}

struct file_operations aesd_fops = {
    .owner =            THIS_MODULE,
    .read =             aesd_read,
    .write =            aesd_write,
    .open =             aesd_open,
    .release =          aesd_release,
    .llseek =           aesd_llseek,
    .fsync =            aesd_fsync,
    .unlocked_ioctl =   aesd_ioctl
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}



int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    /**
     * TODO: initialize the AESD specific portion of the device
     */

    aesd_circular_buffer_init(&aesd_device.buffer);
    mutex_init(&aesd_device.buffer_mutex);
    mutex_init(&aesd_device.nextLine_mutex);
    aesd_device.nextLine = NULL;
    aesd_device.nextLineLength = 0;

    result = aesd_setup_cdev(&aesd_device);

    if( result ) 
        goto cleanup_chrdev;

    goto exit;

cleanup_chrdev:
    unregister_chrdev_region(dev, 1);
exit:
    return result;
}

void aesd_cleanup_module(void)
{
    int _;
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */

    for(_ = 0; _ < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; ++_){
        struct aesd_buffer_entry nullEntry = {.buffptr = NULL, .size = 0};
        const char* data = aesd_circular_buffer_add_entry(&aesd_device.buffer, &nullEntry);
        kfree(data);
    }
    kfree(aesd_device.nextLine);
    mutex_destroy(&aesd_device.buffer_mutex);
    mutex_destroy(&aesd_device.nextLine_mutex);

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
