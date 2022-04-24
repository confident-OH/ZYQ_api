/* 
 * virtio_htc_ioctl.c: 
       Create an input/output character device to 
       link kernel and user space
 */ 
 
#include <linux/cdev.h> 
#include <linux/delay.h> 
#include <linux/device.h> 
#include <linux/fs.h> 
#include <linux/init.h> 
#include <linux/irq.h> 
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/notifier.h>
#include "virtio_htc_ioctl.h"

#define SUCCESS 0 
#define DEVICE_NAME "ioctl_htc_dev" 
 
enum { 
    CDEV_NOT_USED = 0, 
    CDEV_EXCLUSIVE_OPEN = 1,
};

static RAW_NOTIFIER_HEAD(virtio_htc_ioctl_chain_head);
 
/* Is the device open right now? Used to prevent concurrent access into 
 * the same device 
 */ 
static atomic_t already_open = ATOMIC_INIT(CDEV_NOT_USED); 
 
/* The message the device will give when asked */
static union virtio_htc_ioctl_message message;
static DECLARE_RWSEM(message_rw_sem);
 
static struct class *cls; 
 
/* This is called whenever a process attempts to open the device file */ 
static int device_open(struct inode *inode, struct file *file) 
{ 
    pr_info("device_open(%p)\n", file); 
 
    try_module_get(THIS_MODULE); 
    return SUCCESS; 
} 
 
static int device_release(struct inode *inode, struct file *file) 
{ 
    pr_info("device_release(%p,%p)\n", inode, file); 
 
    module_put(THIS_MODULE); 
    return SUCCESS; 
} 
 
/* This function is called whenever a process which has already opened the 
 * device file attempts to read from it. 
 */ 
static ssize_t device_read(struct file *file, char __user *buffer,
                           size_t length, loff_t *offset) 
{ 
    int bytes_read = 0; 
    /* How far did the process reading the message get? Useful if the message 
     * is larger than the size of the buffer we get to fill in device_read. 
     */ 
    const char *message_ptr = message.message; 
 
    if (!*(message_ptr + *offset)) {
        *offset = 0; /* reset the offset */ 
        return 0; /* signify end of file */ 
    } 
 
    message_ptr += *offset; 
 
    down_read(&message_rw_sem);
    while (length) { 
        /* Because the buffer is in the user data segment, not the kernel 
         * data segment, assignment would not work. Instead, we have to 
         * use put_user which copies data from the kernel data segment to 
         * the user data segment. 
         */ 
        put_user(*(message_ptr++), buffer++); 
        length--; 
        bytes_read++; 
    }
    printk("[virtio_htc_ioctl] read test %s\n", message.command_message.htc_command.command_str);
    up_read(&message_rw_sem);
 
    pr_info("Read %d bytes, %ld left\n", bytes_read, length); 
 
    *offset += bytes_read; 

    return bytes_read; 
} 
 
/* called when somebody tries to write into our device file. */ 
static ssize_t device_write(struct file *file, const char __user *buffer, 
                            size_t length, loff_t *offset) 
{ 
    int i; 
 
    pr_info("device_write(%p,%p,%ld)", file, buffer, length);
    
    down_write(&message_rw_sem);
    for (i = 0; i < length; i++) 
        get_user(message.message[i], buffer + i);
    printk("[virtio_htc_ioctl] write test %s\n", message.command_message.htc_command.command_str);
    up_write(&message_rw_sem);
    /* Again, return the number of input characters used. */
    return i; 
}

int virtio_htc_ioctl_notifier_event(struct notifier_block *nb, unsigned long event, void *v)
{
    switch (event)
    {
    case RUN_LINE_COMMAND:
    {
        char *s = (char *)v;
        down_write(&message_rw_sem);
        strcpy(message.command_message.htc_command.command_str, s);
        message.command_message.status = 0;
        up_write(&message_rw_sem);
        printk("[virtio_htc_ioctl] %s\n", message.command_message.htc_command.command_str);
        break;
    }
    default:
        printk("[virtio_htc_ioctl] unkown event\n");
        break;
    }
    return NOTIFY_DONE;
}

// notifier block
static struct notifier_block virtio_htc_ioctl_notifier = {
    .notifier_call = virtio_htc_ioctl_notifier_event, 
};

int virtio_htc_ioctl_notifier_call(unsigned long val, void *v)
{
    printk("start virtio_htc_ioctl_notifier_call\n");
    return raw_notifier_call_chain(&virtio_htc_ioctl_chain_head, val, v);
}

/* This function is called whenever a process tries to do an ioctl on our 
 * device file. We get two extra parameters (additional to the inode and file 
 * structures, which all device functions get): the number of the ioctl called 
 * and the parameter given to the ioctl function. 
 * 
 * If the ioctl is write or read/write (meaning output is returned to the 
 * calling process), the ioctl call returns the output of this function. 
 */ 
static long 
device_ioctl(struct file *file, /* ditto */ 
             unsigned int ioctl_num, /* number and param for ioctl */ 
             unsigned long ioctl_param) 
{ 
    int i; 
    long ret = SUCCESS; 
 
    /* We don't want to talk to two processes at the same time. */ 
    if (atomic_cmpxchg(&already_open, CDEV_NOT_USED, CDEV_EXCLUSIVE_OPEN)) 
        return -EBUSY; 
 
    /* Switch according to the ioctl called */ 
    switch (ioctl_num) { 
    case IOCTL_SET_MSG: { 
        /* Receive a pointer to a message (in user space) and set that to 
         * be the device's message. Get the parameter given to ioctl by 
         * the process. 
         */ 
        device_write(file, (char __user *)ioctl_param, sizeof(virtio_htc_ioctl_message), NULL); 
        break; 
    } 
    case IOCTL_GET_MSG: { 
        loff_t offset = 0; 
 
        /* Give the current message to the calling process - the parameter 
         * we got is a pointer, fill it. 
         */ 
        i = device_read(file, (char __user *)ioctl_param, sizeof(virtio_htc_ioctl_message), &offset);
        break; 
    }
    } 
 
    /* We're now ready for our next caller */ 
    atomic_set(&already_open, CDEV_NOT_USED); 
 
    return ret; 
} 
 
/* Module Declarations */ 
 
/* This structure will hold the functions to be called when a process does 
 * something to the device we created. Since a pointer to this structure 
 * is kept in the devices table, it can't be local to init_module. NULL is 
 * for unimplemented functions. 
 */ 
static struct file_operations fops = { 
    .read = device_read, 
    .write = device_write, 
    .unlocked_ioctl = device_ioctl, 
    .open = device_open, 
    .release = device_release, /* a.k.a. close */ 
}; 
 
/* Initialize the module - Register the character device */ 
static int __init chardev2_init(void) 
{ 
    /* Register the character device (atleast try) */ 
    int ret_val = register_chrdev(MAJOR_NUM, DEVICE_NAME, &fops); 
 
    /* Negative values signify an error */ 
    if (ret_val < 0) { 
        pr_alert("%s failed with %d\n", 
                 "Sorry, registering the character device ", ret_val); 
        return ret_val; 
    } 
 
    cls = class_create(THIS_MODULE, DEVICE_FILE_NAME); 
    device_create(cls, NULL, MKDEV(MAJOR_NUM, 0), NULL, DEVICE_FILE_NAME); 
 
    pr_info("Device created on /dev/%s\n", DEVICE_FILE_NAME); 

    ret_val = raw_notifier_chain_register(&virtio_htc_ioctl_chain_head, &virtio_htc_ioctl_notifier);
    if (ret_val)
    {
        printk("error raw_notifier_chain_register failed!\n");
        return -1;
    }
 
    return ret_val; 
} 
 
/* Cleanup - unregister the appropriate file from /proc */ 
static void __exit chardev2_exit(void) 
{ 
    device_destroy(cls, MKDEV(MAJOR_NUM, 0)); 
    class_destroy(cls); 
 
    /* Unregister the device */ 
    unregister_chrdev(MAJOR_NUM, DEVICE_NAME); 
    raw_notifier_chain_unregister(&virtio_htc_ioctl_chain_head, &virtio_htc_ioctl_notifier);
} 
 
module_init(chardev2_init); 
module_exit(chardev2_exit);
EXPORT_SYMBOL(virtio_htc_ioctl_notifier_call);
 
MODULE_DESCRIPTION("Virtio htc ioctl");
MODULE_LICENSE("GPL");