#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/timekeeping.h>

#define DRIVER_MAJOR 42
#define DRIVER_MAX_MINOR 1

//GPIO digital output
#define GPIO_DO 11

struct device_data {
    struct cdev cdev;
    char *buffer;
    int buffer_len;
};

static struct file_operations am2302_sensor_ops;
struct device_data devs[1];
struct cdev *am2302_cdev;

static struct device *am2302_device;
static struct class *am2302_class;

static int detect_signal_from_device_with_timeout(int gpio, bool expected_signal_state, int timeout_us)
{
    int detected_signal;
    u64 start_time;
    u64 current_time;

    start_time = ktime_get();
    pr_debug("[AM2302]: start_time %llu\n", start_time);

    do
    {
        detected_signal = gpio_get_value(gpio);
        if(detected_signal == expected_signal_state)
        {
            return detected_signal;
        }
        current_time = ktime_get();
        pr_debug("[AM2302]: current_time in loop %llu\n", current_time);
    }
    while(ktime_sub(current_time, start_time) <= timeout_us);

    pr_debug("[AM2302]: current_time last %llu\n", current_time);

    return -1;
}

static int am2302_init_communication(void)
{
    int value;
    
    /* 
        Communication between MCU and AM2302 begins. MCU will
        pull low data-bus and this process must beyond at least 1~10ms 
        to ensure AM2302 could detect MCU's signal
    */
    gpio_set_value(GPIO_DO, 0);
    msleep(1);

    /*
        MCU will pulls up and wait 20-40us for AM2302's response
    */
    gpio_set_value(GPIO_DO, 1);
    
    // Use range function according to https://docs.kernel.org/timers/timers-howto.html
    usleep_range(20, 40);

    /*
        When AM2302 detect the start signal, AM2302 will pull low the bus 80us as response signal
    */
    gpio_direction_input(GPIO_DO);
    value = detect_signal_from_device_with_timeout(GPIO_DO, 0, 20000);
    if (value != 0)
    {
        pr_err("[AM2302]: No LOW signal detected from device");
        return -EFAULT;
    }
    usleep_range(80, 80);

    /*
        AM2302 pulls up 80us for preparation to send data
    */
    value = gpio_get_value(GPIO_DO);
    if (value != 1)
    {
        pr_err("[AM2302]: No HIGH signal detected from device");
        return -EFAULT;
    }
    usleep_range(80, 80);

    return 0;
}

static int am2302_open(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "[AM2302]: Opening AM2302...\n");
    // Data-bus's free status is high voltage level
    gpio_set_value(GPIO_DO, 1);
    return 0;
}

static int am2302_read(struct file *file, char __user *user_buffer, size_t size, loff_t *offset)
{
    int value;
    printk(KERN_INFO "[AM2302]: Reading from AM2302...\n");

    if(am2302_init_communication())
    {
        pr_err("[AM2302]: Couldn't communicate with the device");
        return -EFAULT;
    }
    /*
        When AM2302 is sending data to MCU, every bit's transmission begin with low-voltage-level that last 50us, the
        following high-voltage-level signal's length decide the bit is "1" (70us) or "0" (26-28us)
    */

    if(copy_to_user(user_buffer, &value, sizeof(value)))
    {
        pr_err("[AM2302]: Couldn't send info to user");
        return -EFAULT;
    }

    return 0;
}

static int am2302_release(struct inode *, struct file *)
{
    printk(KERN_INFO "[AM2302]: Releasing AM2302...\n");
    return 0;
}

static int __init am2302_init(void)
{
    int err;
    dev_t dev = MKDEV(DRIVER_MAJOR, 0);

    if (!gpio_is_valid(GPIO_DO))
    {
        pr_err("[AM2302]: GPIO %d is not valid\n", GPIO_DO);
        return -1;
    }

    if (gpio_request(GPIO_DO, "GPIO_DO") < 0)
    {
        pr_err("[AM2302]: ERROR: GPIO %d request\n", GPIO_DO);
        gpio_free(GPIO_DO);
        return -1;
    }

    gpio_direction_output(GPIO_DO, 0);
    gpio_set_value(GPIO_DO, 1);

    printk(KERN_INFO "[AM2302]: Initializing AM2302\n");
    err = alloc_chrdev_region(&dev, 0, DRIVER_MAX_MINOR, "am2302_sensor");
    if (err != 0) {
        return err;
    }

    am2302_cdev = cdev_alloc();
    am2302_cdev->ops = &am2302_sensor_ops;
    err = cdev_add(am2302_cdev, MKDEV(DRIVER_MAJOR, 0), DRIVER_MAX_MINOR);

    // create device in /dev
    am2302_class = class_create(THIS_MODULE, "am2302_sensor");
    am2302_device = device_create(am2302_class, NULL, MKDEV(DRIVER_MAJOR, 0), NULL, "am2302");

    printk(KERN_INFO "[AM2302]: GPIO initialized\n");

    return err;
}
module_init(am2302_init);

static void __exit am2302_exit(void)
{
    cdev_del(am2302_cdev);
    device_destroy(am2302_class, MKDEV(DRIVER_MAJOR, 0));
	class_destroy(am2302_class);
    unregister_chrdev_region(MKDEV(DRIVER_MAJOR, 0), DRIVER_MAX_MINOR);

    gpio_set_value(GPIO_DO, 0);
    gpio_free(GPIO_DO);
}
module_exit(am2302_exit);

MODULE_DEVICE_TABLE(of, am2302_sensor_match);

static struct file_operations am2302_sensor_ops = {
    .owner =     THIS_MODULE,
	.read =	     am2302_read,
	.open =	     am2302_open,
	.release =   am2302_release
};

MODULE_DESCRIPTION("AM2302 Gas Sensor driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Yevhenii Onishchenko");
