#include <linux/virtio_htc.h>
#include <linux/swap.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/oom.h>
#include <linux/wait.h>
#include <linux/mm.h>
#include <linux/mount.h>
#include <linux/magic.h>
#include <linux/notifier.h>
#include "virtio_htc_ioctl.h"


static struct virtio_device_id id_table[] = {
    { VIRTIO_ID_HTC, VIRTIO_DEV_ANY_ID },
    { 0 },
};

static struct virtio_htc *vb_dev;

struct htc_return_host htc_return_list[256];

int htc_return_start, htc_return_end;

static RAW_NOTIFIER_HEAD(virtio_htc_chain_head);

static void htczyq_ack(struct virtqueue *vq)
{
    struct virtio_htc *vb = vq->vdev->priv;
    printk("virthtc get ack\n");

    wake_up(&vb->acked);
}

static int init_vqs(struct virtio_htc *vb)
{
    struct virtqueue *vqs[2];
    vq_callback_t *callbacks[] = { htczyq_ack, htczyq_ack };
    static const char * const names[] = { "get htc command", "return guest result" };
    int err, nvqs;

    nvqs = virtio_has_feature(vb->vdev, VIRTIO_TEST_F_CAN_PRINT) ? 1 : 0;
    err = vb->vdev->config->find_vqs(vb->vdev, 2,
					 vqs, callbacks, names, NULL, NULL);
    if (err)
        return err;

    vb->htc_command_vq = vqs[0];
    vb->htc_return_vq = vqs[1];

    return 0;
}

static void remove_common(struct virtio_htc *vb)
{
    /* Now we reset the device so we can clean up the queues. */
    vb->vdev->config->reset(vb->vdev);

    vb->vdev->config->del_vqs(vb->vdev);
}

static void virttest_remove(struct virtio_device *vdev)
{
    struct virtio_htc *vb = vdev->priv;

    remove_common(vb);
    cancel_work_sync(&vb->htc_work);
    cancel_work_sync(&vb->htc_handle);
    kfree(vb);
    vb_dev = NULL;
}

static int virtio_htc_validate(struct virtio_device *vdev)
{
    return 0;
}

static void htc_work_func(struct work_struct *work)
{
    struct virtio_htc *vb;
    struct scatterlist sg;
    unsigned int unused;

    vb = container_of(work, struct virtio_htc, htc_work);

    struct virtqueue *vq = vb->htc_command_vq;
    sg_init_one(&sg, &vb->htc_data, sizeof(vb->htc_data));

    /* We should always be able to add one buffer to an empty queue. */
    // virtqueue_add_outbuf(vq, &sg, 1, vb, GFP_KERNEL);
    virtqueue_add_inbuf(vq, &sg, 1, vb, GFP_KERNEL);
    virtqueue_kick(vq);

    wait_event(vb->acked, virtqueue_get_buf(vq, &unused));
    queue_work(system_freezable_wq, &vb->htc_handle);
}

static void htc_work_handle(struct work_struct *work)
{
    struct virtio_htc *vb;
    struct scatterlist sg;
    struct htc_command_config *conf = NULL;
    unsigned int unused;

    vb = container_of(work, struct virtio_htc, htc_handle);
    conf = &(vb->htc_data);
    struct virtqueue *vq = vb->htc_return_vq;
    
    switch (conf->id)
    {
    case 1:
    {
        /* return the memory info */
        printk("zyq debug mem_scan\n");
        struct sysinfo mem_info;
        si_meminfo(&mem_info);
        vb->htc_ret.id = 1;
        vb->htc_ret.guest_mem_info.totalram = mem_info.totalram * mem_info.mem_unit;
        printk("zyq debug totalram: %d__%ld", sizeof(mem_info.totalram), mem_info.totalram);
        vb->htc_ret.guest_mem_info.freeram = mem_info.freeram * mem_info.mem_unit;
        printk("zyq debug freeram: %d__%ld", sizeof(mem_info.freeram), mem_info.freeram);
        vb->htc_ret.guest_mem_info.sharedram = mem_info.sharedram * mem_info.mem_unit;
        printk("zyq debug sharedram: %d__%ld", sizeof(mem_info.sharedram), mem_info.sharedram);
        vb->htc_ret.guest_mem_info.bufferram = mem_info.bufferram * mem_info.mem_unit;
        printk("zyq debug bufferram: %d__%ld", sizeof(mem_info.bufferram), mem_info.bufferram);
        vb->htc_ret.guest_mem_info.totalhigh = mem_info.totalhigh * mem_info.mem_unit;
        printk("zyq debug totalhigh: %d__%ld", sizeof(mem_info.totalhigh), mem_info.totalhigh);
        vb->htc_ret.guest_mem_info.freehigh = mem_info.freehigh * mem_info.mem_unit;
        printk("zyq debug freehigh: %d__%ld", sizeof(mem_info.freehigh), mem_info.freehigh);
        vb->htc_ret.guest_mem_info.mem_unit = mem_info.mem_unit;
        printk("zyq debug mem_unit: %d__%ld", sizeof(mem_info.mem_unit), mem_info.mem_unit);
        break;
    }
    case 2:
        /* load and exec a program */
        virtio_htc_ioctl_notifier_call(RUN_LINE_COMMAND, conf->command_str);
        vb->htc_ret.id = conf->id;
        strcpy(vb->htc_ret.htc_command.command_str, conf->command_str);
        break;
    case 3:
        /* exec status */
        vb->htc_ret.id = conf->id;
        strcpy(vb->htc_ret.htc_command.command_str, conf->command_str);
        break;
    default:
        break;
    }

    printk("htc handle work, id: %lld, str: %s\n", conf->id, 
                                                 conf->command_str);
    sg_init_one(&sg, &vb->htc_ret, sizeof(vb->htc_ret));
    virtqueue_add_outbuf(vq, &sg, 1, vb, GFP_KERNEL);
    virtqueue_kick(vq);
    wait_event(vb->acked, virtqueue_get_buf(vq, &unused));
}


int virtio_htc_notifier_event(struct notifier_block *nb, unsigned long event, void *v)
{
    switch (event)
    {
    case EVENT_RUN_SUCCESS:
    {
        htc_return_host *item = (htc_return_host *)v;
        queue_work(system_freezable_wq, &(vb_dev->htc_handle));
        break;
    }
    default:
        printk("[virtio_htc] unkown event\n");
        break;
    }
    return NOTIFY_DONE;
}

//define notifier block
static struct notifier_block virtio_htc_notifier = {
    .notifier_call = virtio_htc_notifier_event, 
};

static void virtio_htc_changed(struct virtio_device *vdev)
{
    struct virtio_htc *vb = vdev->priv;
    vb_dev = vb;
    if (!vb->stop_update) {
        //atomic_set(&vb->stop_once, 0);
        queue_work(system_freezable_wq, &vb->htc_work);
    }
}

static int virttest_probe(struct virtio_device *vdev)
{
    struct virtio_htc *vb;
    int err;

    printk("********* htc zyq probe\n");
    if (!vdev->config->get) {
        return -EINVAL;
    }

    vdev->priv = vb = kmalloc(sizeof(*vb), GFP_KERNEL);
    if (!vb) {
        err = -ENOMEM;
        goto out;
    }
    vb->htc_data.id = 1;
    strcpy(vb->htc_data.command_str, "zyq htc init\n");
    vb->htc_ret.id = 0;
    vb->htc_ret.htc_command.command_str[0] = '\0';
    vb->vdev = vdev;
    INIT_WORK(&vb->htc_work, htc_work_func);
    INIT_WORK(&vb->htc_handle, htc_work_handle);

    vb->stop_update = false;

    init_waitqueue_head(&vb->acked);
    err = init_vqs(vb);
    if (err)
        goto out_free_vb;

    virtio_device_ready(vdev);

    atomic_set(&vb->stop_once, 0);
    vb_dev = vb;

    htc_return_start = 0;
    htc_return_end = 0;
    memset(htc_return_list, 0, sizeof(htc_return_list));

    return 0;

out_free_vb:
    kfree(vb);
out:
    return err;
}

static unsigned int features[] = {
    VIRTIO_TEST_F_CAN_PRINT,
};

static struct virtio_driver virtio_htc_driver = {
    .feature_table = features,
    .feature_table_size = ARRAY_SIZE(features),
    .driver.name =  KBUILD_MODNAME,
    .driver.owner = THIS_MODULE,
    .id_table = id_table,
    .validate = virtio_htc_validate,
    .probe =    virttest_probe,
    .remove =   virttest_remove,
    .config_changed = virtio_htc_changed,
};

// module_virtio_driver(virtio_htc_driver);

static int __init virtio_htc_driver_init(void)
{
    int ret;
    ret = raw_notifier_chain_register(&virtio_htc_chain_head, &virtio_htc_notifier);
    ret = register_virtio_driver(&(virtio_htc_driver));
    return ret;
}
module_init(virtio_htc_driver_init);

static void __exit virtio_htc_driver_exit(void) 
{
    raw_notifier_chain_unregister(&virtio_htc_chain_head, &virtio_htc_notifier);
    unregister_virtio_driver(&(virtio_htc_driver));
}
module_exit(virtio_htc_driver_exit);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio htc driver");
MODULE_LICENSE("GPL");