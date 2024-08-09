#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/timekeeping.h>
#include <linux/slab.h>

#include <linux/mod_devicetable.h>
#include <linux/property.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>


#define DRIVER_MAJOR 42
#define DRIVER_MAX_MINOR 1
#define HIGH 1
#define LOW 0
#define DATA_MASK_HIGH 0x1
#define DATA_MASK_LOW 0x0
#define BUFFER_SIZE 80

struct device_data {
    unsigned gpio_do;
};

/* Declate the probe and remove functions */
static int am2302_probe(struct platform_device *pdev);
static int am2302_remove(struct platform_device *pdev);
static struct file_operations am2302_sensor_ops;
struct device_data dev_data;
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
        detected_signal_state = gpio_get_value(dev_data.gpio_do);
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
    gpio_set_value(dev_data.gpio_do, LOW);
    usleep_range(1000, 2000);

    /*
        MCU will pulls up and wait 20-40us for AM2302's response
    */
    gpio_set_value(dev_data.gpio_do, HIGH);
    gpio_direction_input(dev_data.gpio_do);
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

static void am2302_format_data(u64 data, char* buffer)
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

	sprintf(buffer, "[AM2302]: Humidity: %d\n[AM2302]: Temperature: %d\n", humidity/10, temperature/10);
}

static int detect_signal_from_device_and_get_duration(bool expected_signal_state, int timeout_ns)
{
    int detected_signal_state;
    u64 start_time;
    u64 current_time;

    start_time = ktime_get();
    current_time = ktime_get();
    detected_signal_state = gpio_get_value(dev_data.gpio_do);
    while(detected_signal_state != expected_signal_state)
    {
        detected_signal_state = gpio_get_value(dev_data.gpio_do);
        current_time = ktime_get();

        if(ktime_sub(current_time, start_time) > timeout_ns)
        {
            return -1;
        }
    }
    
    return ktime_sub(current_time, start_time);
}

static u64 am2302_get_data_from_device(void)
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
    
    return data;
}

static int am2302_open(struct inode *inode, struct file *file)
{
    pr_debug("[AM2302]: Opening AM2302...\n");
    // Data-bus's free status is high voltage level
    gpio_set_value(dev_data.gpio_do, HIGH);
    return 0;
}

static int am2302_read(struct file *file, char __user *user_buffer, size_t size, loff_t *offset)
{
    u64 data;
	char buffer[BUFFER_SIZE] = {};
    pr_debug("[AM2302]: Reading from AM2302...\n");

    if (*offset > 0) {
        return 0;   //EOF
    }

    if(am2302_init_communication())
    {
        pr_err("[AM2302]: Couldn't communicate with the device");
        return 0;
    }

    data = am2302_get_data_from_device();
    if(data == -EFAULT)
    {
        pr_err("[AM2302]: Couldn't get data from the device");
        return 0;
    }

	am2302_format_data(data, buffer);

    if(copy_to_user(user_buffer, buffer, BUFFER_SIZE))
    {
        pr_err("[AM2302]: Couldn't send info to user");
        return 0;
    }

	*offset += BUFFER_SIZE;
    
    return BUFFER_SIZE;
}

static int am2302_release(struct inode *, struct file *)
{
    pr_debug("[AM2302]: Releasing AM2302...\n");
    gpio_direction_output((int)dev_data.gpio_do, HIGH);
    return 0;
}

MODULE_DEVICE_TABLE(of, am2302_sensor_match);

static struct file_operations am2302_sensor_ops = {
    .owner =     THIS_MODULE,
	.read =	     am2302_read,
	.open =	     am2302_open,
	.release =   am2302_release
};

MODULE_DESCRIPTION("AM2302 Temperature and Humidity Sensor driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Yevhenii Onishchenko");


static struct of_device_id am2302_driver_ids[] = {
	{
		.compatible = "aosong,am2302",
	}, {}
};
MODULE_DEVICE_TABLE(of, am2302_driver_ids);

static struct platform_driver am2302_driver = {
	.probe = am2302_probe,
	.remove = am2302_remove,
	.driver = {
		.name = "am2302_device_driver",
		.of_match_table = am2302_driver_ids,
	},
};

/**
 * @brief This function is called on loading the driver 
 */
static int am2302_probe(struct platform_device *pdev) {
	struct device *dev = &pdev->dev;
	int gpio_do, ret;
    dev_t devm;

	/* Check for device properties */
	if(!device_property_present(dev, "gpio_do")) {
		pr_err("[AM2302]: Device property 'gpio_do' not found!\n");
		return -1;
	}

	/* Read device properties */
	ret = device_property_read_u32(dev, "gpio_do", &gpio_do);
	if(ret) {
		pr_err("[AM2302]: Could not read 'gpio_do'\n");
		return -1;
	}

	dev_data.gpio_do = gpio_do;

    if (!gpio_is_valid(dev_data.gpio_do))
    {
        pr_err("[AM2302]: GPIO %d is not valid\n", dev_data.gpio_do);
        return -1;
    }

    if (gpio_request(dev_data.gpio_do, "gpio_do") < 0)
    {
        pr_err("[AM2302]: ERROR: GPIO %d request\n", dev_data.gpio_do);
        gpio_free(dev_data.gpio_do);
        return -1;
    }

    gpio_direction_output(dev_data.gpio_do, HIGH);

    ret = alloc_chrdev_region(&devm, 0, DRIVER_MAX_MINOR, "am2302_sensor");
    dev_major = MAJOR(devm);
    if (ret) {
		pr_err("[AM2302]: Couldn't allocate char dev region\n");
        return -1;
    }

    am2302_cdev = cdev_alloc();
    am2302_cdev->ops = &am2302_sensor_ops;
    ret = cdev_add(am2302_cdev, MKDEV(DRIVER_MAJOR, 0), DRIVER_MAX_MINOR);
	if(ret)
	{
		pr_err("[AM2302]: Couldn't add char dev\n");
		return -1;
	}

    // create device in /dev
    am2302_class = class_create(THIS_MODULE, "am2302_sensor");
    am2302_device = device_create(am2302_class, NULL, MKDEV(DRIVER_MAJOR, 0), NULL, "am2302");

    printk(KERN_INFO "[AM2302]: GPIO initialized\n");

    return 0;
}

static int am2302_remove(struct platform_device *pdev) {
	device_destroy(am2302_class, MKDEV(DRIVER_MAJOR, 0));
	class_destroy(am2302_class);
    cdev_del(am2302_cdev);
    unregister_chrdev_region(MKDEV(dev_major, 0), DRIVER_MAX_MINOR);

    gpio_set_value(dev_data.gpio_do, LOW);
    gpio_free(dev_data.gpio_do);
	return 0;
}

static int __init am2302_init(void) {
	if(platform_driver_register(&am2302_driver)) {
		pr_err("[AM2302]: Could not load driver\n");
		return -1;
	}
	return 0;
}

static void __exit am2302_exit(void) {
	platform_driver_unregister(&am2302_driver);
}

module_init(am2302_init);
module_exit(am2302_exit);

