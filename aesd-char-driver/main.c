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
    PDEBUG("open");
    /**
     * TODO: handle open
     */
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -EINVAL;
    struct aesd_buffer_entry *datablk = NULL;
    size_t strOffset = 0;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle read
     */

    if(!access_ok(buf, count)){
        retval = -EINVAL;
        goto exit;
    }

    if(mutex_lock_interruptible(&aesd_device.buffer_mutex) != 0){
        retval = -EINTR;
        goto exit;
    }

    datablk = aesd_circular_buffer_find_entry_offset_for_fpos(&aesd_device.buffer, *f_pos, &strOffset);
    if(datablk == NULL){
        retval = 0;
        goto unlock_bufferMtx;
    }

    retval = (datablk->size - strOffset) > count ? count : (datablk->size - strOffset);
    *f_pos += retval;

    if(copy_to_user(buf, datablk->buffptr + strOffset, retval) != 0){
        retval = -EINVAL;
        goto unlock_bufferMtx;
    }

    goto unlock_bufferMtx;

unlock_bufferMtx:
    mutex_unlock(&aesd_device.buffer_mutex);
exit:
    return retval;
}

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

    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    
    if(!access_ok(buf, count)){
        retval = -EINVAL;
        goto exit;
    }

    strBuf = kmalloc(count, GFP_KERNEL);
    if(strBuf == NULL){
        retval = -ENOMEM;
        goto exit;
    }

    if(copy_from_user(strBuf, buf, count) != 0){
        retval = -EINVAL;
        goto cleanup_tmpbuffer;
    }

    if(mutex_lock_interruptible(&aesd_device.nextLine_mutex) != 0){
        retval = -EINTR;
        goto cleanup_tmpbuffer;
    }

    eolPtr = memmem(strBuf, count, "\n", 1);
    count = eolPtr == NULL ? count : eolPtr - strBuf + 1;
    if(eolPtr == NULL && aesd_device.nextLine == NULL){
        PDEBUG("op 1");
        // We don't have a newline or an existing line
        // start a new one
        aesd_device.nextLine = strBuf;
        aesd_device.nextLineLength = count;    
        // exit early, don't deallocate
        retval = count;
        *f_pos += count;
        goto unlock_nxtLineMutex;
    }else if(eolPtr == NULL && aesd_device.nextLine != NULL){
        PDEBUG("op 2");
        // We don't have a newline, but we already had a line going
        // append
        reallocPtr = krealloc(aesd_device.nextLine, aesd_device.nextLineLength + count, GFP_KERNEL);
        if(reallocPtr == NULL){
            // keep the command, return failure
            retval = -ENOMEM;
            goto unlock_nxtLineMutex;
        }
        aesd_device.nextLine = reallocPtr;
        memcpy(aesd_device.nextLine + aesd_device.nextLineLength, strBuf, count);
        aesd_device.nextLineLength += count;
        retval = count;
        *f_pos += count;
    }else if(eolPtr != NULL && aesd_device.nextLine == NULL){
        PDEBUG("op 3");
        // We have a newline and no previous data pending
        // skip the nextLine buffer, write straight to the buffer
        if(mutex_lock_interruptible(&aesd_device.buffer_mutex) != 0){
            retval = -EINTR;
            goto cleanup_tmpbuffer;
        }

        newEntry.buffptr = strBuf;
        newEntry.size = count;
        removedEntry = aesd_circular_buffer_add_entry(&aesd_device.buffer, &newEntry);
        kfree(removedEntry);

        mutex_unlock(&aesd_device.buffer_mutex);
        // exit early, don't free the buffer
        retval = count;
        *f_pos += count;
        goto unlock_nxtLineMutex;
    }else{
        PDEBUG("op 4");
        // We have a newline and previous data
        // append the string and then steal the appended string
        reallocPtr = krealloc(aesd_device.nextLine, aesd_device.nextLineLength + count, GFP_KERNEL);
        if(reallocPtr == NULL){
            // keep the command, return failure
            retval = -ENOMEM;
            goto unlock_nxtLineMutex;
        }
        aesd_device.nextLine = reallocPtr;
        memcpy(aesd_device.nextLine + aesd_device.nextLineLength, strBuf, count);
        aesd_device.nextLineLength += count;

        if(mutex_lock_interruptible(&aesd_device.buffer_mutex) != 0){
            retval = -EINTR;
            goto cleanup_tmpbuffer;
        }

        newEntry.buffptr = aesd_device.nextLine;
        newEntry.size = aesd_device.nextLineLength;
        removedEntry = aesd_circular_buffer_add_entry(&aesd_device.buffer, &newEntry);
        kfree(removedEntry);

        mutex_unlock(&aesd_device.buffer_mutex);
        retval = count;
        *f_pos += count;
    }

    goto cleanup_tmpbuffer; // no-op, signifying intention

cleanup_tmpbuffer:
    kfree(strBuf);
unlock_nxtLineMutex:
    mutex_unlock(&aesd_device.nextLine_mutex);
exit:    
    return retval;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
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
