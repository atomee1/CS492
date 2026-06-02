/*
 * scull.c -- the bare scull char module
 *
 * Copyright (C) 2001 Alessandro Rubini and Jonathan Corbet
 * Copyright (C) 2001 O'Reilly & Associates
 *
 * The source code in this file can be freely used, adapted,
 * and redistributed in source or binary form, so long as an
 * acknowledgment appears in derived source files.  The citation
 * should list that the code comes from the book "Linux Device
 * Drivers" by Alessandro Rubini and Jonathan Corbet, published
 * by O'Reilly & Associates.   No warranty is attached;
 * we cannot take responsibility for errors or fitness for use.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/kernel.h>	/* printk() */
#include <linux/slab.h>		/* kmalloc() */
#include <linux/fs.h>		/* everything... */
#include <linux/errno.h>	/* error codes */
#include <linux/types.h>	/* size_t */
#include <linux/cdev.h>

#include <linux/uaccess.h>	/* copy_*_user */

#include "scull.h"		/* local definitions */

#include <linux/semaphore.h> /* Added to use semaphore */
#include <linux/mutex.h>     /* Addes to use mutex */

/*
 * Our parameters which can be set at load time.
 */

static int scull_major =   SCULL_MAJOR;
static int scull_minor =   0;
static int scull_fifo_elemsz = SCULL_FIFO_ELEMSZ_DEFAULT; /* ELEMSZ */
static int scull_fifo_size   = SCULL_FIFO_SIZE_DEFAULT;   /* N      */

module_param(scull_major, int, S_IRUGO);
module_param(scull_minor, int, S_IRUGO);
module_param(scull_fifo_size, int, S_IRUGO);
module_param(scull_fifo_elemsz, int, S_IRUGO);

MODULE_AUTHOR("wee");
MODULE_LICENSE("Dual BSD/GPL");

/* FIFO buffer and synchronization primitives */

static char *fifo_buf;              // Flat buffer for all slots
static char *fifo_start;            // Pointer to next element (read)
static char *fifo_end;              // Pointer to next element (write)
static size_t bytes_slot;           // Bytes per slot (len + data)

static struct semaphore sem_items;  // Counts available messages
static struct semaphore sem_spaces; // Counts available slots
static struct mutex mut_fifo;       // Mutex for buffer access

static struct cdev scull_cdev;		/* Char device structure */

/*
 * Open and close
 */

static int scull_open(struct inode *inode, struct file *filp)
{
	printk(KERN_INFO "scull open\n");
	return 0;          /* success */
}

static int scull_release(struct inode *inode, struct file *filp)
{
	printk(KERN_INFO "scull close\n");
	return 0;
}

/*
 * Read and Write
 */
static ssize_t scull_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	size_t actual, to_copy;
    ssize_t ret;
    char *lenp, *datap;

    // Waits until there's at least 1 item
    if (down_interruptible(&sem_items))
        return -ERESTARTSYS;

    // Locks buffer for exclusive access
    if (mutex_lock_interruptible(&mut_fifo)) {
        up(&sem_items);

        return -ERESTARTSYS;
    }

    // Reads stored length + pointer to data
    lenp   = fifo_start;
    actual = *(size_t *)lenp;
    datap  = lenp + sizeof(size_t);

    // Copies min(actual, count) bytes to user
    to_copy = min(actual, count);
    if (copy_to_user(buf, datap, to_copy)) {
        ret = -EFAULT;
        goto out;
    }
    ret = to_copy;

    // Advances circular pointer
    fifo_start += bytes_slot;

    if (fifo_start >= fifo_buf + scull_fifo_size * bytes_slot)
        fifo_start = fifo_buf;

out:
    // Unlocks + opens up a slot
    mutex_unlock(&mut_fifo);
    up(&sem_spaces);

    return ret;
}


static ssize_t scull_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	size_t to_copy;
    ssize_t ret;
    char *lenp, *datap;

    // Waits until there's at least 1 open slot
    if (down_interruptible(&sem_spaces))
        return -ERESTARTSYS;

    // Locks buffer for exclusive access
    if (mutex_lock_interruptible(&mut_fifo)) {
        up(&sem_spaces);

        return -ERESTARTSYS;
    }

    // Computes pointer + how many bytes to store
    lenp = fifo_end;
    datap = lenp + sizeof(size_t);
    to_copy = min((size_t)scull_fifo_elemsz, count);

    // Copies from user space
    if (copy_from_user(datap, buf, to_copy)) {
        ret = -EFAULT;

        goto out;
    }

    // Stores length prefix
    *(size_t *)lenp = to_copy;
    ret = to_copy;

    // Advances circular pointer
    fifo_end += bytes_slot;

    if (fifo_end >= fifo_buf + scull_fifo_size * bytes_slot)
        fifo_end = fifo_buf;

out:
    // Unlocks + signals a new item
    mutex_unlock(&mut_fifo);
    up(&sem_items);

    return ret;
}

/*
 * The ioctl() implementation
 */
static long scull_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{

	int err = 0;
	int retval = 0;
    
	/*
	 * extract the type and number bitfields, and don't decode
	 * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
	 */
	if (_IOC_TYPE(cmd) != SCULL_IOC_MAGIC) return -ENOTTY;
	if (_IOC_NR(cmd) > SCULL_IOC_MAXNR) return -ENOTTY;

	err = !access_ok((void __user *)arg, _IOC_SIZE(cmd));
	if (err) return -EFAULT;

	switch(cmd) {
	case SCULL_IOCGETELEMSZ:
		return scull_fifo_elemsz;

	default:  /* redundant, as cmd was checked against MAXNR */
		return -ENOTTY;
	}
	return retval;

}

struct file_operations scull_fops = {
	.owner 		= THIS_MODULE,
	.unlocked_ioctl = scull_ioctl,
	.open 		= scull_open,
	.release	= scull_release,
	.read 		= scull_read,
	.write 		= scull_write,
};

/*
 * Finally, the module stuff
 */

/*
 * The cleanup function is used to handle initialization failures as well.
 * Thefore, it must be careful to work correctly even if some of the items
 * have not been initialized
 */
void scull_cleanup_module(void)
{
	dev_t devno = MKDEV(scull_major, scull_minor);

	if (fifo_buf) {
		kfree(fifo_buf);
	}

	/* Get rid of the char dev entry */
	cdev_del(&scull_cdev);

	/* cleanup_module is never called if registering failed */
	unregister_chrdev_region(devno, 1);
}

int scull_init_module(void)
{
	int result;
	dev_t dev = 0;

	/*
	 * Get a range of minor numbers to work with, asking for a dynamic
	 * major unless directed otherwise at load time.
	 */
	if (scull_major) {
		dev = MKDEV(scull_major, scull_minor);
		result = register_chrdev_region(dev, 1, "scull");
	} else {
		result = alloc_chrdev_region(&dev, scull_minor, 1, "scull");
		scull_major = MAJOR(dev);
	}
	if (result < 0) {
		printk(KERN_WARNING "scull: can't get major %d\n", scull_major);
		return result;
	}

	cdev_init(&scull_cdev, &scull_fops);
	scull_cdev.owner = THIS_MODULE;
	result = cdev_add (&scull_cdev, dev, 1);
	/* Fail gracefully if need be */
	if (result) {
		printk(KERN_NOTICE "Error %d adding scull character device", result);
		goto fail;
	}

	// Finds bytes per slot + allocates flat buffer
    bytes_slot = scull_fifo_elemsz + sizeof(size_t);
    fifo_buf   = kmalloc(scull_fifo_size * bytes_slot, GFP_KERNEL);
	
    if (!fifo_buf) {
        printk(KERN_WARNING "scull: can't allocate FIFO buffer\n");
        result = -ENOMEM;

        goto fail;
    }

    // Initialize circular pointers + sync primitives
    fifo_start = fifo_end = fifo_buf;           // Both at buffer start
    sema_init(&sem_items, 0);                   // No items yet
    sema_init(&sem_spaces, scull_fifo_size);    // All slots free
    mutex_init(&mut_fifo);                      // Unlock

	printk(KERN_INFO "scull: FIFO SIZE=%u, ELEMSZ=%u\n", scull_fifo_size, scull_fifo_elemsz);

	return 0;

  fail:
	scull_cleanup_module();
	return result;
}

module_init(scull_init_module);
module_exit(scull_cleanup_module);
