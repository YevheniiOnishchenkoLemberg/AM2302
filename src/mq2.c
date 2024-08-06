#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/device.h>

#define DRIVER_MAJOR 42
#define DRIVER_MAX_MINOR 1

struct device_data {
    struct cdev cdev;
};

static struct file_operations mq2_gas_sensor_ops;
struct device_data devs[1];
struct cdev *mq2_cdev;

static struct device *mq2_device;
static struct class *mq2_class;

static int mq2_open(struct inode *inode, struct file *file)
{
    return 0;
}

static int mq2_read(struct file *file, char __user *user_buffer, size_t size, loff_t *offset)
{
    return 0;
}

static int mq2_release(struct inode *, struct file *)
{
    return 0;
}

static int __init mq2_init(void)
{
    int err;
    dev_t dev = MKDEV(DRIVER_MAJOR, 0);

    printk(KERN_INFO "[MQ-2]: Initializing MQ-2\n");
    err = alloc_chrdev_region(&dev, 0, DRIVER_MAX_MINOR, "mq2_gas_sensor");
    if (err != 0) {
        return err;
    }

    mq2_cdev = cdev_alloc();
    mq2_cdev->ops = &mq2_gas_sensor_ops;
    err = cdev_add(mq2_cdev, MKDEV(DRIVER_MAJOR, 0), DRIVER_MAX_MINOR);

    // create device in /dev
    mq2_class = class_create(THIS_MODULE, "mq2_gas_sensor");
    mq2_device = device_create(mq2_class, NULL, MKDEV(DRIVER_MAJOR, 0), NULL, "mq2");

    return err;
}
module_init(mq2_init);

static void __exit mq2_exit(void)
{
    cdev_del(mq2_cdev);
    device_destroy(mq2_class, MKDEV(DRIVER_MAJOR, 0));
	class_destroy(mq2_class);
    unregister_chrdev_region(MKDEV(DRIVER_MAJOR, 0), DRIVER_MAX_MINOR);
}
module_exit(mq2_exit);

MODULE_DEVICE_TABLE(of, mq2_gas_sensor_match);

static struct file_operations mq2_gas_sensor_ops = {
    .owner =     THIS_MODULE,
	.read =	     mq2_read,
	.open =	     mq2_open,
	.release =   mq2_release
};

MODULE_DESCRIPTION("MQ-2 Gas Sensor driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Yevhenii Onishchenko");
