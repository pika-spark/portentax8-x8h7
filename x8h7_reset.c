#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/err.h>
#include <linux/delay.h>

/* This driver is used to handle in a clean way the reset of the STM32H7 during
 * start up.
 * During the probe a clean reset is performed on the STM32H7.
 *
 * This driver also creates a sysfs file to control the status of the reset pins 
 * of x8h7 (nrst and boot0) 
 * the file the driver creates is: /sys/devices/platform/x8h7rst/x8h7_reset 
 *
 * echo 0 > /sys/devices/platform/x8h7rst/x8h7_reset - put the device in reset
 * state 
 * echo 1 > /sys/devices/platform/x8h7rst/x8h7_reset - removes the device from
 * the reset state
 *
 * */

#define X8H7_RESET_KEPT_RESET 0
#define X8H7_RESET_NOT_RESET  1
#define X8H7_RESET_INVALID    2

/* driver device data */
struct x8h7_rst_data {
    struct gpio_desc *nrst_gpio_desc;
    struct gpio_desc *boot0_gpio_desc;
};

/* driver compatible list */
static const struct of_device_id x8h7_rst_of_match[] = {
    { .compatible = "portenta,x8h7rst", },
    { /* sentinel */ } // Required to terminate the array
};
MODULE_DEVICE_TABLE(of, x8h7_rst_of_match);

/* function that set the reset status for the x8h7 device
 * reset = true means that the device is kept under reset */
/* -------------------------------------------------------------------------- */
static void x8h7_reset_state(struct x8h7_rst_data *data, bool reset)
{
    if (!data || !data->nrst_gpio_desc || !data->boot0_gpio_desc)
        return;

    if (reset) {
       gpiod_set_value(data->nrst_gpio_desc, 0);
       gpiod_set_value(data->boot0_gpio_desc, 1);
    } else { 
       gpiod_set_value(data->nrst_gpio_desc, 1);
       gpiod_set_value(data->boot0_gpio_desc, 0);
    }
}

/* function to read the sys fs file that show the status of pins 
 * 0 the device is inactive (reset)
 * 1 the device is active (not reset) */
/* -------------------------------------------------------------------------- */
static ssize_t x8h7_reset_show(struct device *dev,
                                 struct device_attribute *attr, char *buf)
{
    struct x8h7_rst_data *data = dev_get_drvdata(dev);
    int nrst_current_phys_val;
    int boot0_current_phys_val;
    int reported_mode = -1; // Default to unknown

    if (!data || !data->nrst_gpio_desc || !data->boot0_gpio_desc)
        return -EINVAL;

    nrst_current_phys_val = gpiod_get_value(data->nrst_gpio_desc);
    boot0_current_phys_val = gpiod_get_value(data->boot0_gpio_desc);

    // Check for mode 0: nrst asserted, boot0 de-asserted
    if (nrst_current_phys_val == 0 && boot0_current_phys_val == 1 ) {
        reported_mode = X8H7_RESET_KEPT_RESET;
    }
    // Check for mode 1: nrst de-asserted, boot0 asserted
    else if (nrst_current_phys_val == 1 && boot0_current_phys_val == 0) {
        reported_mode = X8H7_RESET_NOT_RESET;
    }
    else {
        reported_mode = X8H7_RESET_INVALID;
    }

    return sysfs_emit(buf, "%d\n", reported_mode);
}

/* write the sysfs file */
/* -------------------------------------------------------------------------- */
static ssize_t x8h7_reset_store(struct device *dev,
                                  struct device_attribute *attr,
                                  const char *buf, size_t count)
{
    struct x8h7_rst_data *data = dev_get_drvdata(dev);
    long value;
    int ret;

    if (!data || !data->nrst_gpio_desc || !data->boot0_gpio_desc)
        return -EINVAL;

    // Parse the input string (expected "0" or "1")
    ret = kstrtol(buf, 10, &value);
    if (ret)
        return ret;

    if(value == X8H7_RESET_KEPT_RESET) {
        x8h7_reset_state(data,true);
    } else if(value == X8H7_RESET_NOT_RESET) {
        x8h7_reset_state(data,false);
    } else {
        dev_err(dev, "Invalid value for control_mode: %ld (expected 0 or 1)\n", value);
        return -EINVAL;
    }

    return count; // Return the number of bytes written
}

/* Define the control_mode SysFS attribute */
static DEVICE_ATTR_RW(x8h7_reset);


/* Array of all attributes to create for this device */
static struct attribute *x8h7_rst_attrs[] = {
    &dev_attr_x8h7_reset.attr,
    NULL, // Sentinel to mark the end of the array
};

/* Attribute group for convenient creation/removal */
static const struct attribute_group x8h7_rst_group = {
    .attrs = x8h7_rst_attrs,
};

/* -------------------------------------------------------------------------- */
static int x8h7_rst_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    struct x8h7_rst_data *data;
    int ret;

    dev_info(dev, "X8H7 reset probe function called\n");

    // Allocate memory for our device data
    data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;

    // --- Get NRST_STM32 GPIO Descriptor ---
    data->nrst_gpio_desc = devm_gpiod_get(dev, "nrst", GPIOD_OUT_HIGH); // Initialize to inactive (high by default)
    if (IS_ERR(data->nrst_gpio_desc)) {
        ret = PTR_ERR(data->nrst_gpio_desc);
        dev_err(dev, "Failed to get nrst-gpios descriptor: %d\n", ret);
        return ret;
    }

    // --- Get BOOT0_STM32 GPIO Descriptor ---
    data->boot0_gpio_desc = devm_gpiod_get(dev, "boot0", GPIOD_OUT_HIGH); // Initialize to inactive (high by default)
    if (IS_ERR(data->boot0_gpio_desc)) {
        ret = PTR_ERR(data->boot0_gpio_desc);
        dev_err(dev, "Failed to get boot0-gpios descriptor: %d\n", ret);
        return ret;
    }

    dev_info(dev, "X8H7 reset sequence started\n");

    dev_info(dev, " (0) nrst 0, boot0 1\n");
    gpiod_set_value(data->nrst_gpio_desc, 0);
    gpiod_set_value(data->boot0_gpio_desc, 1);
    dev_info(dev, " (1) wait ~50 ms\n");
    usleep_range(50000, 51000); 
    dev_info(dev, " (0) nrst 1, boot0 0\n");
    gpiod_set_value(data->nrst_gpio_desc, 1);
    gpiod_set_value(data->boot0_gpio_desc, 0);

    dev_info(dev, "X8H7 reset sequence finisced... device can now be used\n");

    platform_set_drvdata(pdev, data);

    // --- Create SysFS attributes ---
    ret = sysfs_create_group(&dev->kobj, &x8h7_rst_group);
    if (ret) {
        dev_err(dev, "Failed to create sysfs group for x8h7_rst: %d\n", ret);
        return ret;
    }

    dev_info(dev, "X8H7 reset probe OK! /sys/devices/platform/x8h7rst/x8h7_reset created!  \n");

    return 0;
}

/* -------------------------------------------------------------------------- */
static int x8h7_rst_remove(struct platform_device *pdev)
{
    struct x8h7_rst_data *data = platform_get_drvdata(pdev);
    struct device *dev = &pdev->dev;
    dev_info(&pdev->dev, "x8h7_reset driver remove called\n");

    sysfs_remove_group(&dev->kobj, &x8h7_rst_group);

    if (data->nrst_gpio_desc) {
        gpiod_set_value(data->nrst_gpio_desc, 0);
    }
    if (data->boot0_gpio_desc) {
        gpiod_set_value(data->boot0_gpio_desc, 1);
    }

    dev_info(dev, "X8H7 is now under reset and cannot be used\n");
    return 0;
}

// Define the platform driver structure
static struct platform_driver x8h7_rst_driver = {
    .probe      = x8h7_rst_probe,
    .remove     = x8h7_rst_remove,
    .driver     = {
        .name           = "x8h7_rst", // Name of the driver
        .of_match_table = x8h7_rst_of_match, // Link to our compatible table
    },
};

module_platform_driver(x8h7_rst_driver); // Helper macro to register/unregister the driver

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Daniele Aimo <d.aimo@arduino.cc>");
MODULE_DESCRIPTION("Arduino X8H7 reset manager driver");
