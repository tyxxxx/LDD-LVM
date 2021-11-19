#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/kernel.h>    /* printk() */
#include <linux/slab.h>        /* kmalloc() */
#include <linux/fs.h>        /* everything... */
#include <linux/errno.h>    /* error codes */
#include <linux/types.h>    /* size_t */

#include <linux/fcntl.h>    /* O_ACCMODE */
#include <linux/cdev.h>

#include <asm/uaccess.h>    /* copy_*_user */

#include "scull.h"        /* local definitions */


int scull_major = SCULL_MAJOR;
int scull_minor = 0;
int scull_quantum = SCULL_QUANTUM;
int scull_qset = SCULL_QSET;


module_param(scull_major, int, S_IRUGO);
module_param(scull_minor, int, S_IRUGO);
module_param(scull_quantum, int, S_IRUGO);
module_param(scull_qset, int, S_IRUGO);






int
scull_trim(struct scull_dev *dev)
{
    struct scull_qset *next, *dptr;
    int qset = dev->qset;
    int i;
    for (dptr = dev->data; dptr; dptr = next) {
        if (dptr->data) {
            for (i = 0; i < qset; i++) {
                kfree(dptr->data[i]);
            }
            kfree(dptr->data);
            dptr->data = NULL;
        }
        
        next = dptr->next;
        kfree(dptr);
    }
    dev->size = 0;
    dev->quantum = scull_quantum;
    dev->qset = scull_qset;
    dev->data = NULL;
    
    return 0;
}

struct scull_qset 
*scull_follow(struct scull_dev *dev, int n)
{
    struct scull_qset *qs = dev->data;
    

    if (!qs) {
        qs = dev->data = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);

        if (qs == NULL) {
            PDEBUG("scull_follow_if_fail\n");
            return NULL;
        }
        memset(qs, 0, sizeof(struct scull_qset));
    }
    

    while (n--) {
        if (!qs->next) {
            qs->next = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
            if (qs->next == NULL) {
                PDEBUG("scull_follow_n_%d\n", n);
                return NULL;
            }
            memset(qs->next, 0, sizeof(struct scull_qset));
        }
        qs = qs->next;
    }
    
    return qs;
}


ssize_t
scull_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    struct scull_dev *dev = filp->private_data;
    struct scull_qset *dptr;
    int quantum = dev->quantum, qset = dev->qset;
    int itemsize = quantum * qset;
    int item, rest, s_pos, q_pos;
    ssize_t retval = 0;
    
    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;
    if (*f_pos >= dev->size)
        goto out;
    if (*f_pos + count > dev->size)
        count = dev->size - *f_pos;
    

    item = (long)*f_pos / itemsize;    
    rest = (long)*f_pos % itemsize;
    s_pos = rest / quantum;
    q_pos = rest % quantum;

    dptr = scull_follow(dev, item);
    if (dptr == NULL || !dptr->data || !dptr->data[s_pos])
        goto out;

       
    if (count > quantum - q_pos)
        count = quantum - q_pos;
    
    if (copy_to_user(buf, dptr->data[s_pos] + q_pos, count)) {
        retval = -EFAULT;
        goto out;
    }
    *f_pos += count;
    retval = count;
    
out:
    up(&dev->sem);
    return retval;
}

ssize_t
scull_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    struct scull_dev *dev = filp->private_data;
    struct scull_qset *dptr;
    int quantum = dev->quantum, qset = dev->qset;
    int itemsize = quantum * qset;
    int item, s_pos, q_pos, rest;
    ssize_t retval = -ENOMEM;    
    
    if (down_interruptible(&dev->sem))
        return -ERESTARTSYS;
    

    item = (long)*f_pos / itemsize;    
    rest = (long)*f_pos % itemsize;
    s_pos = rest / quantum;
    q_pos = rest % quantum;
    
    dptr = scull_follow(dev, item);
    if (dptr == NULL) {
        PDEBUG("scull_follow_fail\n");
        goto out;
    }
    if (!dptr->data) {
        dptr->data = kmalloc(qset * sizeof(char *), GFP_KERNEL);
        if (!dptr->data) {
            PDEBUG("km_dptr->data_fail\n");
            goto out;
        }
        memset(dptr->data, 0, qset * sizeof(char *));
    }
    if (!dptr->data[s_pos]) {
        dptr->data[s_pos] = kmalloc(quantum, GFP_KERNEL);
        if (!dptr->data[s_pos]) {
            PDEBUG("km_dptr->data[s_pos]_fail\n");
            goto out;
        }
    }
    

    if (count > quantum - q_pos)
        count = quantum - q_pos;
    
    if (copy_from_user(dptr->data[s_pos] + q_pos, buf, count)) {
        retval = -EFAULT;
        PDEBUG("copy_fail\n");
        goto out;
    }
    *f_pos += count;
    retval = count;
    PDEBUG("%d", count);
    

    if (dev->size < *f_pos)
        dev->size = *f_pos;

out:    
    up(&dev->sem);
    return retval;
}


loff_t
scull_llseek(struct file *filp, loff_t off, int whence)
{
    struct scull_dev *dev = filp->private_data;
    loff_t newpos;
    
    switch(whence) {
        case SEEK_SET:
            newpos = off;
            break;
        case SEEK_CUR:
            newpos = filp->f_pos + off;
            break;
        case SEEK_END:
            newpos = dev->size + off;
            break;
        default:
            return -EINVAL;
    }
    if (newpos < 0) 
        return -EINVAL;
    filp->f_pos = newpos;
    return newpos;
}

int
scull_open(struct inode *inode, struct file *filp)
{
    struct scull_dev *dev;
    dev = container_of(inode->i_cdev, struct scull_dev, cdev);
    filp->private_data = dev;
    

    if ( (filp->f_flags & O_ACCMODE) == O_WRONLY) {
        if (down_interruptible(&dev->sem))
            return -ERESTARTSYS;
        scull_trim(dev);    
        up(&dev->sem);
    }
    
    return 0;
}

int
scull_release(struct inode *inode, struct file *filp)
{
    return 0;
}


struct file_operations scull_fops = {
    .owner =    THIS_MODULE,
    .llseek =   scull_llseek,
    .read =     scull_read,
    .write =    scull_write,
    .open =     scull_open,
    .release =  scull_release,
};


struct scull_dev *scull_devices;



static void
scull_setup_cdev(struct scull_dev *dev, int index)
{
    int err,devno = MKDEV(scull_major, scull_minor + index);
    
    cdev_init(&dev->cdev, &scull_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &scull_fops;
    err = cdev_add(&dev->cdev, devno, 1);
    
    if (err)
        printk(KERN_NOTICE "Error %d adding scull%d", err, index);
}

void
scull_cleanup_module(void)
{
    dev_t devno = MKDEV(scull_major, scull_minor);
    
    /* 去掉我们字符设备的入口 */
    if (scull_devices) {
        scull_trim(scull_devices);
        cdev_del(&scull_devices->cdev);
        kfree(scull_devices);
    }
    
    unregister_chrdev_region(devno, 1);
}

int
scull_init_module(void)
{
    int result;
    dev_t dev = 0;
    

    if(scull_major) {
        dev = MKDEV(scull_major, scull_minor);
        result = register_chrdev_region(dev, 1, "scull");
    } else {
        result = alloc_chrdev_region(&dev, scull_minor, 1, "scull");
        scull_major = MAJOR(dev);
    }
    if (result <0) {
        printk(KERN_WARNING "scull: can't get major %d\n", scull_major);
        return result;
    }
    

    scull_devices = kmalloc(sizeof(struct scull_dev), GFP_KERNEL);
    if (!scull_devices) {
        result = -ENOMEM;
        goto fail;
    }
    memset(scull_devices, 0, sizeof(struct scull_dev));
    

    scull_devices->quantum = scull_quantum;
    scull_devices->qset = scull_qset;
    sema_init(&scull_devices->sem, 1);
    scull_setup_cdev(scull_devices, 0);
    
    return 0;
    
fail:
    scull_cleanup_module();
    return result;
}


module_init(scull_init_module);
module_exit(scull_cleanup_module);