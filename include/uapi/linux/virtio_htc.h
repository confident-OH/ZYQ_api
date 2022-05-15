#ifndef _LINUX_VIRTIO_TEST_H
#define _LINUX_VIRTIO_TEST_H
#include <linux/types.h>
#include <linux/virtio.h>
#include <linux/virtio_types.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_config.h>
#include <linux/workqueue.h>
#include <linux/fs.h>
#include <linux/rwsem.h>
#include <linux/mm.h>
#include <linux/page_reporting.h>

/* The feature bitmap for virtio test */
#define VIRTIO_TEST_F_CAN_PRINT 0

#define EVENT_RUN_SUCCESS 0
#define EVENT_RUN_INFO 1

#define RETURN_LIST_FULL 2

typedef struct htc_command_config
{
    long int id;
    char command_str[1024];
}htc_command_config;

typedef struct htc_mem_status
{
	unsigned long totalram;	/* Total usable main memory size */
	unsigned long freeram;	/* Available memory size */
	unsigned long sharedram;	/* Amount of shared memory */
	unsigned long bufferram;	/* Memory used by buffers */
	unsigned long totalhigh;	/* Total high memory size */
	unsigned long freehigh;	/* Available high memory size */
	__u32 mem_unit;			/* Memory unit size in bytes */
}htc_mem_status;

typedef struct htc_return_host
{
    long int id;
    union
    {
        htc_command_config htc_command;
        struct htc_mem_status guest_mem_info;
    };
}htc_return_host;

typedef struct virtio_htc {
    struct virtio_device *vdev;
    struct virtqueue *htc_return_vq, *htc_command_vq;

    struct work_struct htc_work;
    struct work_struct htc_handle;
    bool stop_update;
    atomic_t stop_once;

    /* Waiting for host to send the command. */
    wait_queue_head_t acked;

    htc_command_config htc_data;
    htc_return_host htc_ret;
}virtio_htc;

extern int virtio_htc_ioctl_notifier_call(unsigned long val, void *v);

#endif /* _LINUX_VIRTIO_TEST_H */