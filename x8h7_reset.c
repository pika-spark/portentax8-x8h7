#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h> // For device tree GPIO functions
#include <linux/gpio.h>    // For general GPIO functions
#include <linux/gpio/consumer.h> // New header for gpiod_ functions
#include <linux/err.h>           // For IS_ERR

struct x8h7_rst_data {
    struct gpio_desc *nrst_gpio_desc; // Pointer to the GPIO descriptor
    struct gpio_desc *boot0_gpio_desc; // Pointer to the GPIO descriptor
};

// This array links our driver to devices via the "compatible" string
static const struct of_device_id x8h7_rst_of_match[] = {
    { .compatible = "portenta,x8h7rst", },
    { /* sentinel */ } // Required to terminate the array
};
MODULE_DEVICE_TABLE(of, x8h7_rst_of_match); // Exports the table for modular autoloading

// Helper function to set both GPIOs based on desired mode
// mode: 0 for nrst asserted, boot0 de-asserted
// mode: 1 for nrst de-asserted, boot0 asserted
/* -------------------------------------------------------------------------- */
static void x8h7_reset_state(struct x8h7_rst_data *data, bool reset)
{
    if (!data || !data->nrst_gpio_desc || !data->boot0_gpio_desc)
        return;

    if (reset) {
       printk("DAIM: request reset of x8h7");
       gpiod_set_value(data->nrst_gpio_desc, 0);
       gpiod_set_value(data->boot0_gpio_desc, 1);
    } else { 
       printk("DAIM: x8h7 exit from reset ");
       gpiod_set_value(data->nrst_gpio_desc, 1);
       gpiod_set_value(data->boot0_gpio_desc, 0);
    }
}


// --- SysFS Functions for control_mode ---

// show function for control_mode (read)
// Reports 0 if (nrst=asserted, boot0=de-asserted)
// Reports 1 if (nrst=de-asserted, boot0=asserted)
// Reports -1 for unknown/invalid state
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
        reported_mode = 0;
    }
    // Check for mode 1: nrst de-asserted, boot0 asserted
    else if (nrst_current_phys_val == 1 && boot0_current_phys_val == 0) {
        reported_mode = 1;
    }
    else {
        reported_mode = 2;
    }

    return sysfs_emit(buf, "%d\n", reported_mode);
}

// store function for control_mode (write)
// Accepts 0 or 1
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

    if(value == 0) {
        x8h7_reset_state(data,true);
    } else if(value == 1) {
        x8h7_reset_state(data,false);
    } else {
        dev_err(dev, "Invalid value for control_mode: %ld (expected 0 or 1)\n", value);
        return -EINVAL;
    }

    return count; // Return the number of bytes written
}

// Define the control_mode SysFS attribute
static DEVICE_ATTR_RW(x8h7_reset); // Creates device_attr_control_mode


// Array of all attributes to create for this device
static struct attribute *x8h7_rst_attrs[] = {
    &dev_attr_x8h7_reset.attr,
    NULL, // Sentinel to mark the end of the array
};

// Attribute group for convenient creation/removal
static const struct attribute_group x8h7_rst_group = {
    .attrs = x8h7_rst_attrs,
};
static int x8h7_rst_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    struct x8h7_rst_data *data;
    int ret;

    printk("DAIM: probe function - START");
    dev_info(dev, "My Custom LED driver probe function called!\n");

    // Allocate memory for our device data
    data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;

// --- Get NRST_STM32 GPIO Descriptor ---
    // devm_gpiod_get retrieves the GPIO descriptor for a named GPIO property.
    // It also automatically requests the GPIO and sets its direction to output
    // and initial state based on DT flags (e.g., GPIO_ACTIVE_HIGH/LOW).
    data->nrst_gpio_desc = devm_gpiod_get(dev, "nrst", GPIOD_OUT_HIGH); // Initialize to inactive (high by default)
    if (IS_ERR(data->nrst_gpio_desc)) {
        ret = PTR_ERR(data->nrst_gpio_desc);
        dev_err(dev, "Failed to get nrst-gpios descriptor: %d\n", ret);
        return ret;
    }

    // We explicitly set the value here to ensure it's in the correct *inactive*
    // state. The GPIOD_OUT_HIGH/LOW in gpiod_get *suggests* an initial state,
    // but it's good to be explicit regarding the DT's active_low/high property.
    // The gpiod_get function automatically reads the OF_GPIO_ACTIVE_LOW flag.
    // If the flag is set (active low), then GPIOD_HIGH means inactive.
    // If the flag is NOT set (active high), then GPIOD_LOW means inactive.

    /*
    gpiod_set_value(data->nrst_gpio_desc, gpiod_is_active_low(data->nrst_gpio_desc) ? 1 : 0);
    dev_info(dev, "NRST GPIO descriptor obtained. Active low: %s. Initial value set to %d (inactive).\n",
             gpiod_is_active_low(data->nrst_gpio_desc) ? "yes" : "no",
             gpiod_is_active_low(data->nrst_gpio_desc) ? 1 : 0);
    */
    printk("DAIM: nrst 1");
    gpiod_set_value(data->nrst_gpio_desc, 1);
    
    // --- Get BOOT0_STM32 GPIO Descriptor ---
    data->boot0_gpio_desc = devm_gpiod_get(dev, "boot0", GPIOD_OUT_HIGH); // Initialize to inactive (high by default)
    if (IS_ERR(data->boot0_gpio_desc)) {
        ret = PTR_ERR(data->boot0_gpio_desc);
        dev_err(dev, "Failed to get boot0-gpios descriptor: %d\n", ret);
        return ret;
    }
    /* 
    gpiod_set_value(data->boot0_gpio_desc, gpiod_is_active_low(data->boot0_gpio_desc) ? 1 : 0);
    dev_info(dev, "BOOT0 GPIO descriptor obtained. Active low: %s. Initial value set to %d (inactive).\n",
             gpiod_is_active_low(data->boot0_gpio_desc) ? "yes" : "no",
             gpiod_is_active_low(data->boot0_gpio_desc) ? 1 : 0);
    */ 

    printk("DAIM: boot 0");
    gpiod_set_value(data->boot0_gpio_desc, 0);
    


    platform_set_drvdata(pdev, data);

    // --- Create SysFS attributes ---
    ret = sysfs_create_group(&dev->kobj, &x8h7_rst_group);
    if (ret) {
        dev_err(dev, "Failed to create sysfs group for x8h7_rst: %d\n", ret);
        return ret;
    }
    else {
        printk("DAIM: sysFs created!\n");
    }

    dev_info(dev, "Successfully probed  \n");

    // Now you can control the LED, e.g., turn it on:
    //gpio_set_value(data->gpio_pin, 1); // Assuming active-high logic for simplicity,
                                     // but actual logic comes from DT or driver
    return 0;
}

static int x8h7_rst_remove(struct platform_device *pdev)
{
    struct x8h7_rst_data *data = platform_get_drvdata(pdev);
    struct device *dev = &pdev->dev;
    dev_info(&pdev->dev, "x8h7_reset driver remove called\n");
    // Remove SysFS attributes
    sysfs_remove_group(&dev->kobj, &x8h7_rst_group);
    if (data->nrst_gpio_desc) {
         gpiod_set_value(data->nrst_gpio_desc, gpiod_is_active_low(data->nrst_gpio_desc) ? 1 : 0);
    }
    if (data->boot0_gpio_desc) {
        gpiod_set_value(data->boot0_gpio_desc, gpiod_is_active_low(data->boot0_gpio_desc) ? 1 : 0);
    }
    // devm_gpio_request_one handles automatic release, so no explicit gpio_free() needed
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
MODULE_AUTHOR("Daniele Aimo");
MODULE_DESCRIPTION("Driver");
