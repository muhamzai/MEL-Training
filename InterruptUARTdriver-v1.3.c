#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/atomic.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/console.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_core.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/wait.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/string.h>

#define  DEVICE_NAME "imxqUART"    ///< The device will appear at /dev/ebbchar using this value
#define  CLASS_NAME  "UART"        ///< The device class -- this is a character device driver
#define UART1_URXD 0x30880000       //32 bit read only
#define UART1_UTXD 0x30880040       //32 bit write only
#define UART1_UCR1 0x30880080       //32bit r/w control register
#define UART1_UCR2 0x30880084       //32bit r/w control register
#define UART1_UCR3 0x30880088       //32bit r/w control register
#define UART1_UCR4 0x3088008C       //32bit r/w control register
#define UART1_UFCR 0x30880090       //32bit r/w FIFO control register
#define UART1_UTS  0x308800B4       //UART testing register
#define UART1_USR1 0x30880094
#define UART1_USR2 0x30880098
#define UART1_UBIR 0x308800A4
#define UART1_UBMR 0x308800A8
#define UART1_UBRC 0x308800AC
#define UART1_UMCR 0x308800B8

MODULE_LICENSE("GPL");            ///< The license type -- this affects available functionality
MODULE_AUTHOR("Hamza");    ///< The author -- visible when you use modinfo
MODULE_DESCRIPTION("Linux interrupt based UART driver");  ///< The description -- see modinfo
MODULE_VERSION("1.3");

typedef struct UART_vars
{
	char rxBuffer[1024];
	struct clk* c1 , *c2;
	int  rxIndex;
	int  majorNumber;
	int  lines;
	int  retIrq;
	struct device_node* dev;
	int  numberOpens;              ///< Counts the number of times the device is opened
	char __iomem* txRegister, * rxRegister;
} UART_vars;

static struct class* ebbcharClass = NULL; ///< The device-driver class struct pointer
static struct device* ebbcharDevice = NULL; ///< The device-driver device struct pointer
static struct kobject* register_kobj;
struct UART_vars *vars;

static irqreturn_t ISR(int irq, void* dev_id)      // IRQ handler
{
	if (vars->rxIndex > 1023) //drop incoming packets if buffer full
	{
		vars->rxIndex = 1023;
	}
	else
	{
		vars->rxBuffer[vars->rxIndex] = (char)readl(vars->rxRegister);
		vars->rxIndex += 1;
	}
	return IRQ_HANDLED;
}

static int     dev_open(struct inode*, struct file*);
static int     dev_release(struct inode*, struct file*);
static ssize_t dev_read(struct file*, char*, size_t, loff_t*);
static ssize_t dev_write(struct file*, const char*, size_t, loff_t*);

static struct file_operations fops =
{
   .open = dev_open,
   .read = dev_read,
   .write = dev_write,
   .release = dev_release,
};


// function for many symbol data enter
static ssize_t __used store_value(struct kobject* kobj, struct kobj_attribute* attr, const int buf, size_t count) {
	vars->lines = buf;
	return count;
}

// register function to attribute
static struct kobj_attribute store_val_attribute = __ATTR(put_parameters, 0220, NULL, NULL);

// put attribute to attribute group
static struct attribute* register_attrs[] = {
	&store_val_attribute.attr,
	NULL,   /* NULL terminate the list*/
};
static struct attribute_group  reg_attr_group = {
	.attrs = register_attrs
};

static int __init ebbchar_init(void) {

	int retclk = 0;

	char __iomem* * UCR1, * UCR2, * UCR3, * UCR4, * testRegister, * USR1, * USR2, * UBIR, * UBMR, * UBRC, * UMCR, * UFCR;

	vars = kmalloc(sizeof(UART_vars),GFP_KERNEL); //allocate space to global struct pointer
	memset(vars->rxBuffer,'\0',1024);
        vars->rxIndex = 0;
        vars->majorNumber=0;
        vars->lines = 0;
	vars->retIrq = 0;
        vars->numberOpens = 0;

	printk(KERN_INFO "EBBChar: Initializing the EBBChar LKM\n");

	// Try to dynamically allocate a major number for the device -- more difficult but worth it
	vars->majorNumber = register_chrdev(0, DEVICE_NAME, &fops);
	if (vars->majorNumber < 0) {
		printk(KERN_ALERT "EBBChar failed to register a major number\n");
		return vars->majorNumber;
	}
	
	printk(KERN_INFO "EBBChar: registered correctly with major number %d\n", vars->majorNumber);

	// Register the device class
	ebbcharClass = class_create(THIS_MODULE, CLASS_NAME);
	if (IS_ERR(ebbcharClass)) {                // Check for error and clean up if there is
		unregister_chrdev(vars->majorNumber, DEVICE_NAME);
		printk(KERN_ALERT "Failed to register device class\n");
		return PTR_ERR(ebbcharClass);          // Correct way to return an error on a pointer
	}
	printk(KERN_INFO "EBBChar: device class registered correctly\n");

	// Register the device driver
	ebbcharDevice = device_create(ebbcharClass, NULL, MKDEV(vars->majorNumber, 0), NULL, DEVICE_NAME);
	if (IS_ERR(ebbcharDevice)) {               // Clean up if there is an error
		class_destroy(ebbcharClass);           // Repeated code but the alternative is goto statements
		unregister_chrdev(vars->majorNumber, DEVICE_NAME);
		printk(KERN_ALERT "Failed to create the device\n");
		return PTR_ERR(ebbcharDevice);
	}
	register_kobj = kobject_create_and_add("test_1025_sym", kernel_kobj);
	if (!register_kobj)
		return -ENOMEM;

	//create attributes (files)
	if (sysfs_create_group(register_kobj, &reg_attr_group)) {
		kobject_put(register_kobj);
		return -ENOMEM;
	}

	vars->txRegister = ioremap_nocache(UART1_UTXD, 4);
	vars->rxRegister = ioremap_nocache(UART1_URXD, 4);
	testRegister = ioremap_nocache(UART1_UTS, 4);
	UCR1 = ioremap_nocache(UART1_UCR1, 4);
	UCR2 = ioremap_nocache(UART1_UCR2, 4);
	UCR3 = ioremap_nocache(UART1_UCR3, 4);
	UCR4 = ioremap_nocache(UART1_UCR4, 4);
	USR1 = ioremap_nocache(UART1_USR1, 4);
	USR2 = ioremap_nocache(UART1_USR2, 4);
	UBIR = ioremap_nocache(UART1_UBIR, 4);
	UBMR = ioremap_nocache(UART1_UBMR, 4);
	UBRC = ioremap_nocache(UART1_UBRC, 4);
	UMCR = ioremap_nocache(UART1_UMCR, 4);
	UFCR = ioremap_nocache(UART1_UFCR, 4);

	/* The device node's clocks need to be initialized to write to the hwRegisters */
	vars->dev = of_find_node_by_path("/serial@30880000"); // get device node from dtb
	vars->c1 = of_clk_get_by_name(vars->dev, "per");				// get "per" clock of our target device from dtb
	if (IS_ERR(vars->c1))
	{
		printk(KERN_INFO "NO CLOCK FOUND");
	}
	retclk = clk_prepare(vars->c1);						//prepare clock to be initialized
	if (retclk < 0) { printk(KERN_INFO "Clock prepare Failed : %d\n", retclk); }
	retclk = clk_enable(vars->c1);							//initialize the clock
	if (retclk < 0) { printk(KERN_INFO "Clock enable Failed : %d\n", retclk); }

	vars->c2 = of_clk_get_by_name(vars->dev, "ipg");				// get "ipg" clock of our target device from dtb
	if (IS_ERR(vars->c2))
	{
		printk(KERN_INFO "NO CLOCK FOUND");
	}
	retclk = clk_prepare(vars->c2);						//prepare clock to be initialized
	if (retclk < 0) { printk(KERN_INFO "Clock prepare Failed : %d\n", retclk); }
	retclk = clk_enable(vars->c2);							//initialize the clock
	if (retclk < 0) { printk(KERN_INFO "Clock enable Failed : %d\n", retclk); }

	writel(0x0001, UCR1);
	writel(0x702F, UCR2);
	writel(0x38C , UCR3);
	writel(0X4002, UCR4);
	writel(0xB01 , UFCR);
	writel(0x1F7F, UBIR);
	writel(0x3D08, UBMR);
	writel(0x201 , UCR1);
	writel(0x00  , UMCR);
	writel(0x2040, USR1);
	writel(0X5088, USR2);
	
	writel(0x1000, testRegister);                                  // Set Rx Tx loopback mode for testing
	printk(KERN_INFO "EBBChar: device class created correctly\n"); // Made it! device was initialized

	vars->retIrq = irq_of_parse_and_map(vars->dev, 0);                          // map UART hwirq of UART to kernel virq space
	if (!vars->retIrq) { printk(KERN_INFO "debug info: map irq failed"); }
	else 
	{printk(KERN_INFO "irq mapped...\n");}
	if (unlikely(request_irq(vars->retIrq, ISR, 0x00, "UART@0x30880000", (void*)(ISR))))
	{
		printk(KERN_INFO "debug info: request irq failed");
	}
	else {printk(KERN_INFO "irq registered...\n");}
	return 0;
}

static void __exit ebbchar_exit(void) {

	free_irq(vars->retIrq,(void *) ISR);
	if (!free_irq(0,vars->dev)) //remove irq handler associated with device node
        {printk(KERN_INFO "IRQ free!!!\n");}
	device_destroy(ebbcharClass, MKDEV(vars->majorNumber, 0));     // remove the device
	class_unregister(ebbcharClass);                          // unregister the device class
	class_destroy(ebbcharClass);                             // remove the device class
	unregister_chrdev(vars->majorNumber, DEVICE_NAME);             // unregister the major number
	kobject_put(register_kobj);

        clk_disable_unprepare(vars->c1);           //disable and unprepare register clocks

        clk_disable_unprepare(vars->c2);
        kfree (vars);
	printk(KERN_INFO "EBBChar: Goodbye from the LKM!\n");
}

static int dev_open(struct inode* inodep, struct file* filep) {
	vars->numberOpens++;
	printk(KERN_INFO "EBBChar: Device has been opened %d time(s)\n", vars->numberOpens);
	return 0;
}

static ssize_t dev_read(struct file* filep, char* buffer, size_t len, loff_t* offset) {

	vars->rxIndex = 0;
	memset(vars->rxBuffer, '\0', 1024);
	return len;
}

static ssize_t dev_write(struct file* filep, const char* buffer, size_t len, loff_t* offset)
{
	int i = 0;
	vars->lines += 1;
	vars->rxIndex = 0;
	printk(KERN_INFO "recv tx pin: %s", buffer);
	memset(vars->rxBuffer, '\0', 1024);
	for (i = 0; i < strlen(buffer); i++)
	{
		writel(buffer[i], vars->txRegister);   //write character by character the line to tx
	}
	msleep(5);
	printk(KERN_INFO "recv rx: %s", vars->rxBuffer);
	return len;
}

static int dev_release(struct inode* inodep, struct file* filep) {
        printk(KERN_INFO "EBBChar: Device successfully closed\n");
	return 0;
}

module_init(ebbchar_init);
module_exit(ebbchar_exit);


