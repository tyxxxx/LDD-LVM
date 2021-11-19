#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/hdreg.h>
#include <linux/kdev_t.h>
#include <linux/vmalloc.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/bio.h>

#include "sbull.h"

static int sbull_major = SBULL_MAJOR;
static int hardsect_size = SBULL_HARDSECT;
static int nsectors = 2;
module_param(sbull_major, int, 0);
module_param(hardsect_size, int, 0);
module_param(nsectors, int, 0);

struct sbull_dev {
    int size;
    u8 *data;
    short users;
    spinlock_t lock;
    struct request_queue *queue;
    struct gendisk *gd;
 };

 static struct sbull_dev *device = NULL;
 
 static void sbull_transfer(struct sbull_dev *dev, unsigned long sector, unsigned long nsect, char *buffer, int write)
 {
     unsigned long offset = sector*KERNEL_SECTOR_SIZE;
     unsigned long nbytes = nsect*KERNEL_SECTOR_SIZE;
 
    if ((offset+nbytes) > dev->size)
    {
         return;
    }

      if (write)
    {
         memcpy(dev->data+offset, buffer, nbytes);
    }
   else
    {
         memcpy(buffer, dev->data+offset, nbytes);
     }
  }
 
 static int sbull_xfer_bio(struct sbull_dev *dev, struct bio *bio)
  {
    int i;
     struct bio_vec *bvec;
     sector_t sector = bio->bi_sector;

     bio_for_each_segment(bvec, bio, i)
     {
        char *buffer = __bio_kmap_atomic(bio, i, KM_USER0);
        sbull_transfer(dev, sector, bio_cur_bytes(bio)>>9, buffer, bio_data_dir(bio) == WRITE);
        sector += bio_cur_bytes(bio)>>9;
         __bio_kunmap_atomic(bio, KM_USER0);
    }
    return 0;
  }
 
   static int sbull_xfer_request(struct sbull_dev *dev, struct request *req)
  {
    struct bio *bio;
     int nsect = 0;
 
    __rq_for_each_bio(bio, req) {
        sbull_xfer_bio(dev, bio);
        nsect += bio->bi_size/KERNEL_SECTOR_SIZE;
    }
    return nsect;
}
 
static void sbull_full_request(struct request_queue *q)
{
    struct request *req;
    int sectors_xferred;
    struct sbull_dev *dev = q->queuedata;

    while((req = blk_fetch_request(q)) != NULL)
     {
        if (req->cmd_type != REQ_TYPE_FS)
       {
            __blk_end_request_all(req, -EIO);
            continue;
        }

        sectors_xferred = sbull_xfer_request(dev, req);

        __blk_end_request_cur(req, 0);
    }
}

 static int sbull_open(struct block_device *device, fmode_t mode)
 {
    struct sbull_dev *dev = device->bd_disk->private_data;

    spin_lock(&dev->lock);
    dev->users++;
    spin_unlock(&dev->lock);
    return 0;
 }

 static int sbull_release(struct gendisk *disk, fmode_t mode)
{
    struct sbull_dev *dev = disk->private_data;

     spin_lock(&dev->lock);
    dev->users--;
     spin_unlock(&dev->lock);

     return 0;
 }

 static struct block_device_operations sbull_ops = {
    .owner = THIS_MODULE,
    .open = sbull_open,
    .release = sbull_release,
 };

static void setup_device(struct sbull_dev *dev)
{
    memset(dev, 0, sizeof(struct sbull_dev));
    dev->size = nsectors*hardsect_size;
    dev->data = vmalloc(dev->size);
    if (dev->data == NULL)
     {
        return;
    }

    spin_lock_init(&dev->lock);

    dev->queue = blk_init_queue(sbull_full_request, &dev->lock);
    if (dev->queue == NULL)
    {
        goto out_vfree;
    }

    blk_queue_logical_block_size(dev->queue, hardsect_size);
    dev->queue->queuedata = dev;

    dev->gd = alloc_disk(1);
    if (!dev->gd)
    {
        goto out_vfree;
    }

    dev->gd->major = sbull_major;
    dev->gd->first_minor = 0;
    dev->gd->fops = &sbull_ops;
    dev->gd->queue = dev->queue;
    dev->gd->private_data = dev;
    snprintf(dev->gd->disk_name, 6, "sbull");    
    set_capacity(dev->gd, nsectors*(hardsect_size/KERNEL_SECTOR_SIZE));
    add_disk(dev->gd);

    return;

    out_vfree:
        if (dev->data)
        {
            vfree(dev->data);
        }
}

static int __init sbull_init(void)
 {
    sbull_major = register_blkdev(sbull_major, "sbull");
     if (sbull_major <= 0)
    {
        return -EBUSY;    
   }

     device = kmalloc(sizeof(struct sbull_dev), GFP_KERNEL);
    if (device == NULL)
    {
        goto out_unregister;
    }

    setup_device(device);

    return 0;

    out_unregister:
        unregister_blkdev(sbull_major, "sbull");
        return -ENOMEM;
}

static void sbull_exit(void)
{
    struct sbull_dev *dev = device;

    if (dev->gd)
    {
        del_gendisk(dev->gd);
        put_disk(dev->gd);
    }

    if (dev->queue)
    {
        blk_cleanup_queue(dev->queue);
   }

    if (dev->data)
     {
         vfree(dev->data);
    }

    unregister_blkdev(sbull_major, "sbull");
    kfree(device);
 }

 module_init(sbull_init);
 module_exit(sbull_exit);
 MODULE_LICENSE("GPL");