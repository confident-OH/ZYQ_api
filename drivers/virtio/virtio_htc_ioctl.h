#ifndef VIRTIO_HTC_IOCTL
#define VIRTIO_HTC_IOCTL 
 
#include <linux/ioctl.h>
#include <linux/virtio_htc.h>
#include <linux/rwsem.h>
 
/* The major device number. We can not rely on dynamic registration 
 * any more, because ioctls need to know it. 
 */ 
#define MAJOR_NUM 100 
 
/* Set the message of the device driver */ 
#define IOCTL_SET_MSG _IOW(MAJOR_NUM, 0, char *) 
/* _IOW means that we are creating an ioctl command number for passing 
 * information from a user process to the kernel module. 
 * 
 * The first arguments, MAJOR_NUM, is the major device number we are using. 
 * 
 * The second argument is the number of the command (there could be several 
 * with different meanings). 
 * 
 * The third argument is the type we want to get from the process to the 
 * kernel. 
 */ 
 
/* Get the message of the device driver */ 
#define IOCTL_GET_MSG _IOR(MAJOR_NUM, 1, char *) 
/* This IOCTL is used for output, to get the message of the device driver. 
 * However, we still need the buffer to place the message in to be input, 
 * as it is allocated by the process. 
 */ 
 
/* Get the n'th byte of the message */ 
#define IOCTL_GET_NTH_BYTE _IOWR(MAJOR_NUM, 2, int) 
/* The IOCTL is used for both input and output. It receives from the user 
 * a number, n, and returns message[n]. 
 */ 
 
/* The name of the device file */ 
#define DEVICE_FILE_NAME "ioctl_htc_dev" 
#define DEVICE_PATH "/dev/ioctl_htc_dev"
#define BUF_LEN 1024

#define RUN_LINE_COMMAND 1

typedef struct htc_ioctl_message_info
{
    int status;
    char command_str[256];
}htc_ioctl_message_info;

typedef union virtio_htc_ioctl_message
{
    char message[BUF_LEN];
    htc_ioctl_message_info command_message;
}virtio_htc_ioctl_message;
 
#endif