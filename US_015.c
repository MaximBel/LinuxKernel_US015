/*
 * Linux 2.6 and later 'parrot' sample device driver
 *
 * Copyright (c) 2011-2015, Pete Batard <pete@akeo.ie>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/interrupt.h>  // Required for the IRQ code
#include <linux/gpio.h>

#define DEVICE_NAME	"us-015"
#define DEVICE_CLASS "ultrasonic sensor"

static int majorNumber;
static short size_of_message;
static struct class* us_Class = NULL;
static struct device* us_Device = NULL;
static int irqNumber;                  // used to share the IRQ number

//static task_struct *immortalTask;

static unsigned int trigPin = 5;
static unsigned int echoPin = 6;

/* Module parameters that can be provided on insmod */
module_param( trigPin, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(trigPin, "Pin to make ultrasonic signal.");

module_param( echoPin, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(echoPin, "Pin to check ultrasonic responce.");

static inline int __init gpio_init(void);

static inline void __exit gpio_deinit(void);

static irq_handler_t gpio_us_echo_handler(unsigned int irq, void *dev_id, struct pt_regs *regs);

static int us_Task(void* Params);



// The prototype functions for the character driver -- must come before the struct definition
static int dev_open(struct inode *, struct file *);
static int dev_release(struct inode *, struct file *);
static ssize_t dev_read(struct file *, char *, size_t, loff_t *);
static ssize_t dev_write(struct file *, const char *, size_t, loff_t *);



/** @brief Devices are represented as file structure in the kernel. The file_operations structure from
 *  /linux/fs.h lists the callback functions that you wish to associated with your file operations
 *  using a C99 syntax structure. char devices usually implement open, read, write and release calls
 */
static struct file_operations fops = { .open = dev_open, .read = dev_read,
		.write = dev_write, .release = dev_release, };

/* Module initialization and release */
static int __init us_init(void)
{

	int gpioInitState = gpio_init();

	if(gpioInitState == ENOMEM ||
			gpioInitState == EINVAL ||
			gpioInitState == ENOSPC) {

		printk(KERN_ALERT "US-015: Gpio init fail with code %d \r\n", gpioInitState);

		gpio_deinit();

		return -gpioInitState;

	}

	// Try to dynamically allocate a major number for the device -- more difficult but worth it
	majorNumber = register_chrdev(0, DEVICE_NAME, &fops);
	if (majorNumber<0) {
		printk(KERN_ALERT "Failed to register a major number of US-015\n");
		return majorNumber;
	}

	// Register the device class
	us_Class = class_create(THIS_MODULE, DEVICE_CLASS);

	if (IS_ERR(us_Class)) { // Check for error and clean up if there is
		unregister_chrdev(majorNumber, DEVICE_NAME);
		printk(KERN_ALERT "Failed to register device class\n");
		return PTR_ERR(us_Class);// Correct way to return an error on a pointer
	}

	// Register the device driver
	us_Device = device_create(us_Class, NULL, MKDEV(majorNumber, 0), NULL, DEVICE_NAME);

	if (IS_ERR(us_Device)) {           // Clean up if there is an error
		class_destroy(us_Class);// Repeated code but the alternative is goto statements
		unregister_chrdev(majorNumber, DEVICE_NAME);
		printk(KERN_ALERT "Failed to create the device\n");
		return PTR_ERR(us_Device);
	}

	printk(KERN_INFO "US-015: module complitely initialized\n");

	return 0;
}

static void __exit us_exit(void)
{
	device_destroy(us_Class, MKDEV(majorNumber, 0));     // remove the device
	class_unregister(us_Class);// unregister the device class
	class_destroy(us_Class);// remove the device class
	unregister_chrdev(majorNumber, DEVICE_NAME);// unregister the major number

	gpio_deinit();

	printk(KERN_INFO "US-015: module complitely removed\n");
}

static inline int __init gpio_init(void) {
	unsigned long IRQflags;
	int result;

	gpio_request(trigPin, "sysfs");
	gpio_direction_output(trigPin, 0);// set in output mode
	gpio_export(trigPin, false);// appears in /sys/class/gpio/
	printk(KERN_INFO "US-015: gpio %d exported\n", trigPin);

	gpio_request(echoPin, "sysfs");// set up the gpioButton
	gpio_direction_input(echoPin);// set up as an input
	gpio_set_debounce(echoPin, 50);// ddebounce the button 50ms
	gpio_export(echoPin, false);// appears in /sys/class/gpio/
	printk(KERN_INFO "US-015: gpio %d exported\n", echoPin);

	irqNumber = gpio_to_irq(echoPin);

	IRQflags = IRQF_TRIGGER_RISING;

	// This next call requests an interrupt line
	result = request_irq(irqNumber,// the interrupt number
			(irq_handler_t) gpio_us_echo_handler,
			IRQflags,// use custom kernel param
			"us-015_handler",// used in /proc/interrupts
			NULL);// the *dev_id for shared lines

	return result;

}

static inline void __exit gpio_deinit(void) {

	free_irq(irqNumber, NULL);// free the IRQ number, no *dev_id required in this case
	gpio_unexport(echoPin);// unexport the Button GPIO
	gpio_free(echoPin);// free the LED GPIO

	gpio_set_value(trigPin, 0);      // turn the LED off, device was unloaded
	gpio_unexport(trigPin);// unexport the LED GPIO
	gpio_free(trigPin);// free the Button GPIO

	printk(KERN_INFO "US-015: gpio %d and %d unexported\n", trigPin, echoPin);

}

static irq_handler_t gpio_us_echo_handler(unsigned int irq, void *dev_id, struct pt_regs *regs){

	printk(KERN_INFO "US-015: Echo pin triggered\r\n");

	return (irq_handler_t) IRQ_HANDLED;  // announce IRQ was handled correctly

}

static int us_Task(void* Params) {


}

static int dev_open(struct inode *inodep, struct file *filep) {

// nothing to do here
printk(KERN_INFO "US-015: Device opened\r\n");

return 0;
}

static ssize_t dev_read(struct file *filep, char *buffer, size_t len,
	loff_t *offset) {
int error_count = 0;
// copy_to_user has the format ( * to, *from, size) and returns 0 on success
//error_count = copy_to_user(buffer, message, size_of_message); //TODO: do this function

printk(KERN_INFO "US-015: Nothing to read from LKM\r\n");

if (error_count == 0) {            // if true then have success
	printk(KERN_INFO "US-015: Sent distance to the user\n");
	return (size_of_message = 0); // clear the position to the start and return 0
} else {
	printk(KERN_INFO "US-015: Failed to send data to the user\n");
	return -EFAULT;         // Failed -- return a bad address message (i.e. -14)
}
}

static ssize_t dev_write(struct file *filep, const char *buffer, size_t len,
	loff_t *offset) {

printk(KERN_INFO "US-015: Nothing to write from user\r\n");

return 0;
}

static int dev_release(struct inode *inodep, struct file *filep) {

// nothing to do here
printk(KERN_INFO "US-015: Device released\r\n");

return 0;
}

/* Let the kernel know the calls for module init and exit */
module_init( us_init);
module_exit( us_exit);

/* Module information */
MODULE_AUTHOR("Beley Maxim");
MODULE_DESCRIPTION("Driver for US-015 ultrasonic distance sensor");
MODULE_VERSION("0.1");
MODULE_LICENSE("GPL");
