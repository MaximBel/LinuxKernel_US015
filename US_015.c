#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
//#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/kthread.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <asm/uaccess.h>
#include <linux/timekeeping.h>
#include <asm/div64.h>

#define DEVICE_NAME					"us-015"
#define DEVICE_CLASS 				"Ultrasonic sensor"
#define OUTPUT_MESSAGE_MAX_LENGTH 	7


//-----------------
static struct class* us_Class = NULL;
static struct device* us_Device = NULL;
static int majorNumber;
//-----------------
static struct task_struct * usThread = NULL;
static int irqNumber;
//-----------------
static char outputMessage[OUTPUT_MESSAGE_MAX_LENGTH];
static unsigned int readOffset = 0;
static unsigned int lengthOfData = 0;
static unsigned int distance_in_sm = 999;
static bool echoPinTriggered = false;
static u64 echoPinTrigOnTime = 0;
//-------PARAMS-------------
static unsigned int trigPin = 5;
static unsigned int echoPin = 6;
//-----
module_param( trigPin, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(trigPin, "Pin to make ultrasonic signal.");
//-----
module_param( echoPin, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(echoPin, "Pin to check ultrasonic responce.");

static inline int __init gpio_init(void);
static inline void __exit gpio_deinit(void);
static irq_handler_t gpio_us_echo_handler(unsigned int irq, void *dev_id, struct pt_regs *regs);
static int us_Task(void* Params);

// File operation functions for /dev fs
static int dev_open(struct inode *, struct file *);
static int dev_release(struct inode *, struct file *);
static ssize_t dev_read(struct file *, char *, size_t, loff_t *);
static ssize_t dev_write(struct file *, const char *, size_t, loff_t *);

static struct file_operations fops = {
		.open = dev_open,
		.read = dev_read,
		.write = dev_write,
		.release = dev_release
};


static int __init us_init(void)
{

	//------gpio init-------------

	int gpioInitState = gpio_init();

	if(gpioInitState == ENOMEM ||
			gpioInitState == EINVAL ||
			gpioInitState == ENOSPC) {

		printk(KERN_ALERT "US-015: Gpio init fail with code %d.\r\n", gpioInitState);
		gpio_deinit();
		return -gpioInitState;

	}

	//---------thread init-----------------

	usThread = kthread_run(us_Task, NULL, "US-015 thread");

	if(!usThread || usThread == ERR_PTR(-ENOMEM)) {
	 printk(KERN_ALERT "US-015: Thread init fail. \r\n");
	 gpio_deinit();
	 return -((int)usThread);
	 }
	printk(KERN_ALERT "US-015: thread was started.\r\n");

	//---------Char. device init------------

	majorNumber = register_chrdev(0, DEVICE_NAME, &fops);
	if (majorNumber<0) {
		printk(KERN_ALERT "Failed to register a major number of US-015\n");
		return majorNumber;
	}

	us_Class = class_create(THIS_MODULE, DEVICE_CLASS);
	if (IS_ERR(us_Class)) {
		unregister_chrdev(majorNumber, DEVICE_NAME);
		printk(KERN_ALERT "Failed to register device class\n");
		return PTR_ERR(us_Class);
	}

	us_Device = device_create(us_Class, NULL, MKDEV(majorNumber, 0), NULL, DEVICE_NAME);
	if (IS_ERR(us_Device)) {
		class_destroy(us_Class);
		unregister_chrdev(majorNumber, DEVICE_NAME);
		printk(KERN_ALERT "Failed to create the device\n");
		return PTR_ERR(us_Device);
	}

	printk(KERN_INFO "US-015: device initialized, module complitely loaded!\r\n");

	return 0;
}

static void __exit us_exit(void)
{
	int unregResult = -1;

	//----------------------
	device_destroy(us_Class, MKDEV(majorNumber, 0));
	class_unregister(us_Class);
	class_destroy(us_Class);
	unregister_chrdev(majorNumber, DEVICE_NAME);
	printk(KERN_INFO "US-015: device released.\r\n");
	//-----------------------

	gpio_deinit();

	//----------------------
	if(usThread) {
		unregResult = kthread_stop(usThread);
		printk(KERN_INFO "US-015: thread removed with state: %d\r\n", unregResult);
	}

	printk(KERN_INFO "US-015: module complitely removed\r\n");
}

static inline int __init gpio_init(void) {
	unsigned long IRQflags;
	int result;

	//----export trig
	gpio_request(trigPin, "sysfs");
	gpio_direction_output(trigPin, 0);
	gpio_export(trigPin, false);
	gpio_set_value(trigPin, 0);
	printk(KERN_INFO "US-015: gpio %d exported\n", trigPin);

	//----export echo
	gpio_request(echoPin, "sysfs");
	gpio_direction_input(echoPin);
	gpio_set_debounce(echoPin, 50);
	gpio_export(echoPin, false);
	printk(KERN_INFO "US-015: gpio %d exported\n", echoPin);

	//---register interrupt
	irqNumber = gpio_to_irq(echoPin);
	IRQflags = IRQF_TRIGGER_FALLING;
	result = request_irq(irqNumber,	(irq_handler_t) gpio_us_echo_handler, IRQflags, "us-015_handler", NULL);

	return result;

}

static inline void __exit gpio_deinit(void) {

	//----unexport echo interrupt and actualy echo pin
	free_irq(irqNumber, NULL);
	gpio_unexport(echoPin);
	gpio_free(echoPin);

	//---
	gpio_set_value(trigPin, 0);
	gpio_unexport(trigPin);
	gpio_free(trigPin);

	printk(KERN_INFO "US-015: gpio %d and %d unexported\n", trigPin, echoPin);

}

static irq_handler_t gpio_us_echo_handler(unsigned int irq, void *dev_id, struct pt_regs *regs) {

	// ----processing echo pin interrupt
	if(echoPinTriggered == false) {
		echoPinTrigOnTime = ktime_get_ns();
		echoPinTriggered = true;
	}

	return (irq_handler_t) IRQ_HANDLED;

}

static int us_Task(void* Params) {

	u64 measurementStart = 0;

	while (!kthread_should_stop()) {

		echoPinTriggered = false;

		// trig pin to start measurement
		gpio_set_value(trigPin, 1);
		udelay(10);
		gpio_set_value(trigPin, 0);

		// wait for HIGH level on echo pin
		while(gpio_get_value(echoPin) == 0);

		// registrate measurement start
		measurementStart = ktime_get_ns();
		// specific division for 64-bit number
		do_div(measurementStart, 1000);

		// wait for reflation from barier
		msleep(50);

		// if interrupt was triggered
		if(echoPinTriggered == true) {

			// specific division for 64-bit number
			do_div(echoPinTrigOnTime, 1000);
			// calc distance for object(in santimeters)
			distance_in_sm = ((int)(echoPinTrigOnTime - measurementStart) * 34029) / 4000000 ;

		} else {

			// else we have infinity distance
			distance_in_sm = 999;

		}

	}

	return 0;

}

static int dev_open(struct inode *inodep, struct file *filep) {

	printk(KERN_INFO "US-015: Device opened\r\n");

	sprintf(outputMessage, "%03u\r\n", distance_in_sm);
	lengthOfData = strlen(outputMessage);
	readOffset = 0;

	return 0;
}

static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset) {
	int error_count = 1;
	int outputLength = 0;

	outputLength = len < (lengthOfData - readOffset) ? len : (lengthOfData - readOffset);

	// copy_to_user has the format ( * to, *from, size) and returns 0 on success
	error_count = copy_to_user(buffer, &outputMessage[readOffset], outputLength);

	if (error_count == 0) {

		printk(KERN_INFO "US-015: Distance was sended to user.\n");
		readOffset += outputLength;
		return outputLength;

	} else {

		printk(KERN_INFO "US-015: Fail to write data to user\r\n");
		return -EFAULT;

	}

}

static ssize_t dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset) {

	printk(KERN_INFO "US-015: Nothing to write from user\r\n");

	return 0;
}

static int dev_release(struct inode *inodep, struct file *filep) {

// nothing to do here
	printk(KERN_INFO "US-015: Device released\r\n");

	return 0;
}


module_init( us_init);
module_exit( us_exit);

/* Module information */
MODULE_AUTHOR("Beley Maxim");
MODULE_DESCRIPTION("Driver for US-015 ultrasonic distance sensor");
MODULE_VERSION("1.1");
MODULE_LICENSE("GPL");
