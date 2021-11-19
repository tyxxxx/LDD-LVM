#ifndef    _SCULL_H_
#define    _SCULL_H_

#ifndef    SCULL_MAJOR
#define    SCULL_MAJOR 0    
#endif    /* SCULL_MAJOR */

#ifndef SCULL_QUANTUM
#define SCULL_QUANTUM 4000
#endif /* SCULL_QUANTUM */

#ifndef SCULL_QSET
#define SCULL_QSET 1000
#endif /* SCULL_QSET */

#define SCULL_DEBUG   

#undef PDEBUG
#ifdef SCULL_DEBUG
#ifdef __KERNEL__
#define PDEBUG(fmt, args...) printk( KERN_DEBUG "scull: " fmt, ## args)
#else
#define PDEBUG(fmt, args...) fprintf(stderr, fmt, ## args)
#endif /* __KERNEL__ */
#else 
#define PDEBUG(fmt, args...)
#endif /* SCULL_DEBUG */

#undef PDEBUGG
#define PDEBUGG(fmt, args...)

struct scull_qset {
    void **data;
    struct scull_qset *next;
};

struct scull_dev {
    struct scull_qset *data;
    int quantum;
    int qset;
    unsigned long size;
    unsigned int access_key;
    struct semaphore sem;
    struct cdev cdev;
};


extern int scull_major;
extern int scull_quantum;
extern int scull_qset;

#endif    /* _SCULL_H_ */