#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/timekeeping.h>
#include <linux/slab.h>

#define DRIVER_MAJOR 42
#define DRIVER_MAX_MINOR 1
#define HIGH 1
#define LOW 0
#define DATA_MASK_HIGH 0x1
#define DATA_MASK_LOW 0x0

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
static int dev_major;

static int detect_signal_from_device_with_timeout(bool expected_signal_state, int timeout_ns)
{
    int detected_signal_state;
    u64 start_time;
    u64 current_time;

    start_time = ktime_get();

    do
    {
        detected_signal_state = gpio_get_value(GPIO_DO);
        if(detected_signal_state == expected_signal_state)
        {
            return detected_signal_state;
        }
        current_time = ktime_get();
    }
    while(ktime_sub(current_time, start_time) <= timeout_ns);

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
    gpio_set_value(GPIO_DO, LOW);
    usleep_range(1000, 2000);

    /*
        MCU will pulls up and wait 20-40us for AM2302's response
    */
    gpio_set_value(GPIO_DO, HIGH);
    gpio_direction_input(GPIO_DO);
    value = detect_signal_from_device_with_timeout(LOW, 40000);
    if (value != LOW)
    {
        pr_err("[AM2302]: No LOW signal detected from device during init communication");
        return -EFAULT;
    }

    /*
        When AM2302 detect the start signal, AM2302 will pull low the bus 80us as response signal
    */
    value = detect_signal_from_device_with_timeout(HIGH, 80000);

    /*
        AM2302 pulls up 80us for preparation to send data
    */
    if (value != HIGH)
    {
        pr_err("[AM2302]: No HIGH signal detected from device");
        return -EFAULT;
    }
    
    // Let's sleep less, and then wait for LOW state during data reading
    usleep_range(50, 50);

    return 0;
}

static void am2302_format_data(u64 data)
{
    // We'll drop numbers after coma
    int humidity, temperature, checksum;
    int calculated_checksum;
    bool temperature_sign;

    pr_debug("[AM2302]: Data: %llu\n", data & 0xffffffffff);

    humidity = ((data & 0xffff000000) >> 24);
    temperature_sign = ((data & 0x800000) >> 23);
    temperature = ((data & 0x7fff00) >> 8);
    if(temperature_sign)
    {
        temperature = -temperature;
    }

    checksum = data & 0xff;
    calculated_checksum = (humidity & 0xff) + ((humidity >> 8) & 0xff) + (temperature & 0xff) + ((temperature >> 8) & 0xff);
    calculated_checksum = calculated_checksum & 0xff;

    if(checksum != calculated_checksum)
    {
        pr_err("[AM2302]: Checksum mismatch: %d - %d\n", checksum, calculated_checksum);
    }

    pr_info("[AM2302]: Humidity: %d\n", humidity/10);
    pr_info("[AM2302]: Temperature: %d\n", temperature/10);
}

static int detect_signal_from_device_and_get_duration(bool expected_signal_state, int timeout_ns)
{
    int detected_signal_state;
    u64 start_time;
    u64 current_time;

    start_time = ktime_get();
    current_time = ktime_get();
    detected_signal_state = gpio_get_value(GPIO_DO);
    while(detected_signal_state != expected_signal_state)
    {
        detected_signal_state = gpio_get_value(GPIO_DO);
        current_time = ktime_get();

        if(ktime_sub(current_time, start_time) > timeout_ns)
        {
            return -1;
        }
    }
    
    return ktime_sub(current_time, start_time);
}

static int am2302_get_data_from_device(void)
{
    
    int value;
    u64 data = 0;

    /*
        When AM2302 is sending data to MCU, every bit's transmission begin with low-voltage-level that last 50us, the
        following high-voltage-level signal's length decide the bit is "1" (70us) or "0" (26-28us)
    */

   // Wait some time for LOW state here as we had waited less in init function
   value = detect_signal_from_device_with_timeout(LOW, 30000);

    // 16 bit for humidity, 16 for temperature and 8 for checksum
    for(int i=0; i<40; ++i)
    {
        if(value == -1)
        {
            pr_err("[AM2302]: No LOW signal detected from device during data reading, iter: %d", i);
            return -EFAULT;
        }

        // Wait for 50us to get HIGH state as a confirmation of bit transmission start
        // We'll wait more, as practical experiments show the value can be much bigger
        value = detect_signal_from_device_with_timeout(HIGH, 80000);

        if(value == -1)
        {
            pr_err("[AM2302]: No HIGH signal detected from device during data reading, iter: %d", i);
            return -EFAULT;
        }

        value = detect_signal_from_device_and_get_duration(LOW, 90000);

        if(value > 40000)
        {
            data = ((data | DATA_MASK_HIGH) << 1);
        }
        else
        {
            data = ((data | DATA_MASK_LOW) << 1);
        }
    }

    data = data >> 1;

    am2302_format_data(data);
    
    return 0;
}

static int am2302_open(struct inode *inode, struct file *file)
{
    pr_debug("[AM2302]: Opening AM2302...\n");
    // Data-bus's free status is high voltage level
    gpio_set_value(GPIO_DO, HIGH);
    return 0;
}

static int am2302_read(struct file *file, char __user *user_buffer, size_t size, loff_t *offset)
{
    int value;
    pr_debug("[AM2302]: Reading from AM2302...\n");

    if(am2302_init_communication())
    {
        pr_err("[AM2302]: Couldn't communicate with the device");
        return 0;
    }

    value = am2302_get_data_from_device();
    if(value == -EFAULT)
    {
        pr_err("[AM2302]: Couldn't get data from the device");
        return 0;
    }

    // if(copy_to_user(user_buffer, &value, sizeof(value)))
    // {
    //     pr_err("[AM2302]: Couldn't send info to user");
    //     return 0;
    // }
    
    return 0;
}

static int am2302_release(struct inode *, struct file *)
{
    pr_debug("[AM2302]: Releasing AM2302...\n");
    gpio_direction_output(GPIO_DO, HIGH);
    return 0;
}

static int __init am2302_init(void)
{
    int err;
    dev_t dev;

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

    gpio_direction_output(GPIO_DO, HIGH);

    printk(KERN_INFO "[AM2302]: Initializing AM2302\n");
    err = alloc_chrdev_region(&dev, 0, DRIVER_MAX_MINOR, "am2302_sensor");
    dev_major = MAJOR(dev);
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
    device_destroy(am2302_class, MKDEV(DRIVER_MAJOR, 0));
	class_destroy(am2302_class);
    cdev_del(am2302_cdev);
    unregister_chrdev_region(MKDEV(dev_major, 0), DRIVER_MAX_MINOR);

    gpio_set_value(GPIO_DO, LOW);
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
