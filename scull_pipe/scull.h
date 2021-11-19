

#ifndef _SCULL_H
#define _SCULL_H


#undef PDEBUG           
#ifdef SCULL_DEBUG
#   ifdef __KERNEL__
        
#       define PDEBUG(fmt, args...) printk( KERN_DEBUG "scull: " fmt, ## args )
#   else
       
#       define PDEBUG(fmt, args...) fprintf(stderr, fmt, ## args)
#   endif
#else
#   define PDEBUG(fmt, args...) 
#endif

#undef PDEBUGG
#define PDEBUGG(fmt, args...)

#ifndef SCULL_MAJOR
#define SCULL_MAJOR 0
#endif

#ifndef SCULL_NR_DEVS
#define SCULL_NR_DEVS 4     /* scull0 through scull3 */
#endif

#ifndef SCULL_P_NR_DEVS
#define SCULL_P_NR_DEVS 4   /* scullpipe0 through scullpipe3 */
#endif 


#ifndef SCULL_P_BUFFER
#define SCULL_P_BUFFER 4000 
#endif

#ifndef SCULL_QUANTUM
#define SCULL_QUANTUM 4000
#endif

#ifndef SCULL_QSET 
#define SCULL_QSET 1000
#endif

/*
 * Representation of scull quantum sets.
 */
 struct scull_qset {
     void **data;
     struct scull_qset *next;
 };

 struct scull_dev {
     struct scull_qset *data;   /* Pointer to first quantum set */
     int quantum;               /* the current quantum size */
     int qset;                  /* the current array size */
     unsigned long size;        /* amount of data stored here */
     unsigned int access_key;   /* used by sculluid and scullpriv */
     struct mutex mutex;        /* mutual exclusion semaphore */
     struct cdev cdev;          /* Char device structure */
 };




/* Use 'k' as magic number */
#define SCULL_IOC_MAGIC 'k'



#define SCULL_IOCRESET  _IO(SCULL_IOC_MAGIC, 0)



#define SCULL_IOCSQUANTUM    _IOW(SCULL_IOC_MAGIC, 1, int)
#define SCULL_IOCSQSET      _IOW(SCULL_IOC_MAGIC, 2, int)
#define SCULL_IOCTQUANTUM   _IO(SCULL_IOC_MAGIC, 3)
#define SCULL_IOCTQSET      _IO(SCULL_IOC_MAGIC, 4)
#define SCULL_IOCGQUANTUM   _IOR(SCULL_IOC_MAGIC, 5, int)
#define SCULL_IOCGQSET      _IOR(SCULL_IOC_MAGIC, 6, int)
#define SCULL_IOCQQUANTUM   _IO(SCULL_IOC_MAGIC, 7)
#define SCULL_IOCQQSET      _IO(SCULL_IOC_MAGIC, 8)
#define SCULL_IOCXQUANTUM   _IOWR(SCULL_IOC_MAGIC, 9, int)
#define SCULL_IOCXQSET      _IOWR(SCULL_IOC_MAGIC, 10, int)
#define SCULL_IOCHQUANTUM   _IO(SCULL_IOC_MAGIC, 11)
#define SCULL_IOCHQSET      _IO(SCULL_IOC_MAGIC, 12)


 #define SCULL_P_IOCTSIZE _IO(SCULL_IOC_MAGIC, 13)
 #define SCULL_P_IOCQSIZE _IO(SCULL_IOC_MAGIC, 14)


#define SCULL_IOC_MAXNR 14


int     scull_p_init(dev_t dev);
void    scull_p_cleanup(void);

#endif /* SCULL_H */
