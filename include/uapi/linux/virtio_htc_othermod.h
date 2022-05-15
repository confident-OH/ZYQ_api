#ifndef _LINUX_VIRTIO_HTC_OTHERMOD
#define _LINUX_VIRTIO_HTC_OTHERMOD
#include <linux/notifier.h>

#define OTHERMODMAX 10

struct raw_notifier_head virtio_htc_othermod[OTHERMODMAX];

#endif /* _LINUX_VIRTIO_HTC_OTHERMOD */