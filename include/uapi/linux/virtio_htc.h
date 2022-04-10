#ifndef _LINUX_VIRTIO_TEST_H
#define _LINUX_VIRTIO_TEST_H
#include <linux/types.h>
#include <linux/virtio.h>
#include <linux/virtio_types.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_config.h>
#include <linux/workqueue.h>

/* The feature bitmap for virtio test */
#define VIRTIO_TEST_F_CAN_PRINT 0

typedef struct htc_command_config
{
    int64_t id;
    char command_str[256];
}htc_command_config;

typedef struct htc_return_host
{
    union
    {
        htc_command_config htc_command;
    };
}

typedef struct virtio_htc {
    struct virtio_device *vdev;
    struct virtqueue *print_vq, *htc_command_vq;

    struct work_struct htc_work;
    struct work_struct htc_handle;
    bool stop_update;
    atomic_t stop_once;

    /* Waiting for host to send the command. */
    wait_queue_head_t acked;

    htc_command_config htc_data;
    htc_return_host htc_ret;
}virtio_htc;

#endif /* _LINUX_VIRTIO_TEST_H */