
 
#include <linux/module.h>
#include <linux/moduleparam.h>

#include <linux/kernel.h>	/* printk(), min() */
#include <linux/slab.h>		/* kmalloc() */
#include <linux/fs.h>		/* everything... */
#include <linux/proc_fs.h>
#include <linux/errno.h>	/* error codes */
#include <linux/types.h>	/* size_t */
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>
#include <linux/sched.h>
#include <linux/seq_file.h>

#include "scull.h"		/* local definitions */


struct scull_pipe {
    wait_queue_head_t inq, outq;        /* read and write queues */
    char *buffer, *end;                 /* begin of buf, end of buf */
    int buffersize;                     /* used in pointer arithmetic */
    char *rp, *wp;                      /* where to read, where to write */
    int nreaders, nwriters;              /* number of openings for r/w */
    struct fasync_struct *async_queue;  /* asynchronous readers */
    struct mutex mutex;                 /* mutual exclusion semaphore */
    struct cdev cdev;
};

static int scull_p_nr_devs = SCULL_P_NR_DEVS;   /* number of pipe devices */
int scull_p_buffer = SCULL_P_BUFFER;    /* buffer size */
dev_t scull_p_devno;    /* Our first device number */

static struct scull_pipe *scull_p_devices;


static int spacefree(struct scull_pipe *dev);


static int scull_p_open(struct inode *inode, struct file *filp)
{
    struct scull_pipe *dev;

    dev = container_of(inode->i_cdev, struct scull_pipe, cdev);
    filp->private_data = dev;

    if (mutex_lock_interruptible(&dev->mutex))
        return -ERESTARTSYS;

    if (!dev->buffer) {
       
        dev->buffer = kmalloc(scull_p_buffer, GFP_KERNEL);
        if (!dev->buffer) {
            mutex_unlock(&dev->mutex);
            return -ENOMEM;
        }
    }
    dev->buffersize = scull_p_buffer;
    dev->end = dev->buffer + dev->buffersize;
    dev->rp = dev->wp = dev->buffer;    

 
    if (filp->f_mode & FMODE_READ)
        dev->nreaders++;
    if (filp->f_mode & FMODE_WRITE)
        dev->nwriters++;
    mutex_unlock(&dev->mutex);

    return nonseekable_open(inode, filp);
}

static int scull_p_release(struct inode *inode, struct file *filp)
{
    struct scull_pipe *dev = filp->private_data;


    mutex_lock_interruptible(&dev->mutex);
    if (filp->f_mode & FMODE_READ)
        dev->nreaders--;
    if (filp->f_mode & FMODE_WRITE)
        dev->nwriters--;
    if (dev->nreaders + dev->nwriters == 0) {
        kfree(dev->buffer);
        dev->buffer = NULL;  
    }
    mutex_unlock(&dev->mutex);
    return 0;
}


static ssize_t scull_p_read(struct file *filp, char __user *buf, size_t count, loff_t *f_ops)
{
    struct scull_pipe *dev = filp->private_data;

    if (mutex_lock_interruptible(&dev->mutex))
        return -ERESTARTSYS;

    while (dev->rp == dev->wp) { 
        mutex_unlock(&dev->mutex); /
        if (filp->f_flags & O_NONBLOCK)
            return -EAGAIN;
        PDEBUG("\"%s\" reading: going to sleep\n", current->comm);
        if (wait_event_interruptible(dev->inq, (dev->rp != dev->wp)))
            return -ERESTARTSYS; 
    
        if (mutex_lock_interruptible(&dev->mutex))
            return -ERESTARTSYS;
    }
    
    if (dev->wp > dev->rp)
        count = min(count, (size_t)(dev->wp - dev->rp));
    else 
        count = min(count, (size_t)(dev->end - dev->rp));
    if (copy_to_user(buf, dev->rp, count)) {
        mutex_unlock(&dev->mutex);
        return -EFAULT;
    }
    dev->rp += count;
    if (dev->rp == dev->end)
        dev->rp = dev->buffer; 
    mutex_unlock(&dev->mutex);


    wake_up_interruptible(&dev->outq);
    PDEBUG("\"%s\" did read %li bytes\n", current->comm, (long)count);
    return count;
}

static int scull_getwritespace(struct scull_pipe *dev, struct file *filp)
{
    while (spacefree(dev) == 0) {
        DEFINE_WAIT(wait);

        mutex_unlock(&dev->mutex);
        if (filp->f_flags & O_NONBLOCK)
            return -EAGAIN;
        PDEBUG("\"%s\" writing: going to sleep\n", current->comm);
        prepare_to_wait(&dev->outq, &wait, TASK_INTERRUPTIBLE);
        if (spacefree(dev) == 0)
            schedule();
        finish_wait(&dev->outq, &wait);
        if (signal_pending(current))
            return -ERESTARTSYS;    
        if (mutex_lock_interruptible(&dev->mutex))
            return -ERESTARTSYS;
    }
    return 0;
}


static int spacefree(struct scull_pipe *dev)
{
    if (dev->rp == dev->wp)
        return dev->buffersize - 1;
    return ((dev->rp + dev->buffersize - dev->wp) % dev->buffersize) - 1;
}

static ssize_t scull_p_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_ops)
{
    struct scull_pipe *dev = filp->private_data;
    int result;

    if (mutex_lock_interruptible(&dev->mutex))
        return -ERESTARTSYS;
    
   
    result = scull_getwritespace(dev, filp);
    if (result)
        return result;  

   
    count = min(count, (size_t)spacefree(dev));
    if (dev->wp >= dev->rp)
        count = min(count, (size_t)(dev->end - dev->wp));   /* to end-of-buf */
    else 
        count = min(count, (size_t)(dev->rp - dev->wp -1));
    PDEBUG("Going to accept %li bytes to %p from %p\n", (long)count, dev->wp, buf);
    if (copy_from_user(dev->wp, buf, count)){
        mutex_unlock(&dev->mutex);
        return -EFAULT;
    }
    dev->wp += count;
    if (dev->wp == dev->end)
        dev->wp = dev->buffer; 
    mutex_unlock(&dev->mutex);

    
    wake_up_interruptible(&dev->inq);   

   
    if (dev->async_queue)
        kill_fasync(&dev->async_queue, SIGIO, POLL_IN);
    PDEBUG("\"%s\" did write %li bytes\n", current->comm, (long)count);
    return count;
}


#ifdef SCULL_DEBUG


#endif 


struct file_operations scull_pipe_fops = {
    .owner =    THIS_MODULE,

    .read =     scull_p_read,
    .write =    scull_p_write,

    .open =     scull_p_open,
    .release =  scull_p_release,

};


static void scull_p_setup_cdev(struct scull_pipe *dev, int index)
{
    int err, devno = scull_p_devno + index;
   
    cdev_init(&dev->cdev, &scull_pipe_fops);
    dev->cdev.owner = THIS_MODULE;
    err = cdev_add(&dev->cdev, devno, 1);
    
    if (err)
        printk(KERN_NOTICE "Error %d adding scullpipe%d", err, index);
}


int scull_p_init(dev_t firstdev)
{
    int i, result;

    result = register_chrdev_region(firstdev, scull_p_nr_devs, "scullp");
    if (result < 0) {
        printk(KERN_NOTICE "Unable to get scullp region, error %d\n", result);
        return 0;
    }
    scull_p_devno = firstdev;
    scull_p_devices = kmalloc(scull_p_nr_devs * sizeof(struct scull_pipe), GFP_KERNEL);
    if (scull_p_devices == NULL) {
        unregister_chrdev_region(firstdev, scull_p_nr_devs);
        return 0;
    }
    memset(scull_p_devices, 0, scull_p_nr_devs * sizeof(struct scull_pipe));
    for (i = 0; i < scull_p_nr_devs; i++) {
        init_waitqueue_head(&(scull_p_devices[i].inq));
        init_waitqueue_head(&(scull_p_devices[i].outq));
        mutex_init(&scull_p_devices[i].mutex);
        scull_p_setup_cdev(scull_p_devices + i, i);
    }

#ifdef SCULL_DEBUG

#endif 
    return scull_p_nr_devs;
}


void scull_p_cleanup(void)
{
    int i;

#ifdef SCULL_DEBUG
    remove_proc_entry("scullpipe", NULL);
#endif

    if (!scull_p_devices)
        return;     
    
    for (i = 0; i < scull_p_nr_devs; i++) {
        cdev_del(&scull_p_devices[i].cdev);
        kfree(scull_p_devices[i].buffer);
    }
    kfree(scull_p_devices);
    unregister_chrdev_region(scull_p_devno, scull_p_nr_devs);
    scull_p_devices = NULL; 
}
