#include <linux/init.h>           // Macros used to mark up functions e.g. __init __exit
#include <linux/module.h>         // Core header for loading LKMs into the kernel
#include <linux/device.h>         // Header to support the kernel Driver Model
#include <linux/kernel.h>         // Contains types, macros, functions for the kernel
#include <linux/fs.h>             // Header for the Linux file system support
#include <linux/uaccess.h>          // Required for the copy to user function
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/io.h>
#include <linux/delay.h>
#define  DEVICE_NAME "imxqUART"    ///< The device will appear at /dev/ebbchar using this value
#define  CLASS_NAME  "UART"        ///< The device class -- this is a character device driver
#define UART1_URXD 0x30880000       //32 bit read only
#define UART1_UTXD 0x30880040       //32 bit write only
#define UART1_UCR1 0x30880080       //32bit r/w control register
#define UART1_UFCR 0x30880090       //32bit r/w FIFO control register
#define UART1_UTS  0x308800B4       //UART testing register

char __iomem *txRegister, *rxRegister, *ctrlRegister, *testRegister;
char __iomem *ctrlReg, *testReg;
static char rxBuffer[1024]={"\0"};
static char txBuffer[1024]={"\0"};
static int rxIndex=0;
int rxflag=0; //semaphore
int txflag=0; //semaphore
static int txLen=0;
struct task_struct *task1, *task2;
static DEFINE_MUTEX(my_mutex);
//struct mutex my_mutex;
//mutex_init(&my_mutex);
#pragma interrupt_handler ISR
void ISR(int x)        //set/reset ISR
{
   rxflag=x;
}
#pragma interrupt_handler ISR2
void ISR2(int x)        //set/reset ISR2
{
   txflag=x;
}







MODULE_LICENSE("GPL");            ///< The license type -- this affects available functionality
MODULE_AUTHOR("Derek Molloy");    ///< The author -- visible when you use modinfo
MODULE_DESCRIPTION("A simple Linux char driver for the char-dev");  ///< The description -- see modinfo
MODULE_VERSION("0.1");            ///< A version number to inform users
 
static int    majorNumber;                 
static int    numberOpens = 0;              ///< Counts the number of times the device is opened
static struct class*  ebbcharClass  = NULL; ///< The device-driver class struct pointer
static struct device* ebbcharDevice = NULL; ///< The device-driver device struct pointer
 
static int     dev_open(struct inode *, struct file *);
static int     dev_release(struct inode *, struct file *);
static ssize_t dev_read(struct file *, char *, size_t, loff_t *);
static ssize_t dev_write(struct file *, const char *, size_t, loff_t *);
static int thread_rx(void *data);
static int thread_tx(void *data);

static struct file_operations fops =
{
   .open = dev_open,
   .read = dev_read,
   .write = dev_write,
   .release = dev_release,
};

static struct kobject *register_kobj;
static int lines = 0;
static int kill_thread=0;
// function for many symbol data enter
static ssize_t __used store_value(struct kobject *kobj, struct kobj_attribute *attr, const int buf, size_t count){ 
    lines = buf;
    return count;
}

// register function to attribute
static struct kobj_attribute store_val_attribute = __ATTR( put_parameters, 0220, NULL, NULL);

// put attribute to attribute group
static struct attribute *register_attrs[] = {
    &store_val_attribute.attr,
    NULL,   /* NULL terminate the list*/
};
static struct attribute_group  reg_attr_group = {
    .attrs = register_attrs
};

static int thread_tx(void *data)
{
   int i;
   while(1)
   {
      mutex_lock(&my_mutex);
      if(kill_thread){return 1;}
      mutex_unlock(&my_mutex);
      for ( i=0;i<txLen;i++)
      {
            
         mutex_lock(&my_mutex);
         if(kill_thread){return 1;}
         mutex_unlock(&my_mutex);  
         if (txflag==0)
         {
            i--;
            /* do nothing - wait */
         }
         mutex_lock(&my_mutex);
         if (txflag == 1)
         {
          // writel(0x0000,txRegister);
           // txRegister[0] = txBuffer[i];
            ISR2(0);
         
         }
         mutex_unlock(&my_mutex);
      }
   }
   return 0;
}

static int thread_rx(void *data)
{
   while(1)
   {
      //printk(KERN_INFO "recv rx char: %c", readl(rxRegister));
      mutex_lock(&my_mutex);
      if(kill_thread){return 1;}
      if (rxflag == 1)
      {

         if(rxIndex>1023) //drop incoming packets if buffer full
         {rxIndex=1023;}
         else 
         {
         rxBuffer[rxIndex]=(char)readl(rxRegister);
	 //rxBuffer[rxIndex] = rxRegister[0];
         rxIndex+=1;
         }
         //rxRegister[0] = (char)0;
         ISR(0);
       
      }
      mutex_unlock(&my_mutex);
   }
   return 0;
}

static int __init ebbchar_init(void){
   int loopback = 4096;
   printk(KERN_INFO "EBBChar: Initializing the EBBChar LKM\n");
 
   // Try to dynamically allocate a major number for the device -- more difficult but worth it
   majorNumber = register_chrdev(0, DEVICE_NAME, &fops);
   if (majorNumber<0){
      printk(KERN_ALERT "EBBChar failed to register a major number\n");
      return majorNumber;
   }
   printk(KERN_INFO "EBBChar: registered correctly with major number %d\n", majorNumber);
 
   // Register the device class
   ebbcharClass = class_create(THIS_MODULE, CLASS_NAME);
   if (IS_ERR(ebbcharClass)){                // Check for error and clean up if there is
      unregister_chrdev(majorNumber, DEVICE_NAME);
      printk(KERN_ALERT "Failed to register device class\n");
      return PTR_ERR(ebbcharClass);          // Correct way to return an error on a pointer
   }
   printk(KERN_INFO "EBBChar: device class registered correctly\n");
 
   // Register the device driver
   ebbcharDevice = device_create(ebbcharClass, NULL, MKDEV(majorNumber, 0), NULL, DEVICE_NAME);
   if (IS_ERR(ebbcharDevice)){               // Clean up if there is an error
      class_destroy(ebbcharClass);           // Repeated code but the alternative is goto statements
      unregister_chrdev(majorNumber, DEVICE_NAME);
      printk(KERN_ALERT "Failed to create the device\n");
      return PTR_ERR(ebbcharDevice);
   }
   register_kobj = kobject_create_and_add("test_1025_sym", kernel_kobj);
    if (!register_kobj)
    return -ENOMEM;

    //create attributes (files)
    if(sysfs_create_group(register_kobj, &reg_attr_group)){
        kobject_put(register_kobj);
        return -ENOMEM;
    }
   
   txRegister = ioremap_nocache(UART1_UTXD, 4);
   rxRegister = ioremap_nocache(UART1_URXD, 4);
   ctrlRegister = ioremap_nocache(UART1_UCR1, 4);
   testRegister = ioremap_nocache(UART1_UTS, 4);
  

   writel(0x1000,testRegister);  
   printk(KERN_INFO "EBBChar: device class created correctly\n"); // Made it! device was initialized
   task1 = kthread_run(thread_rx, NULL, "thread_func_rx");
   task2 = kthread_run(thread_tx, NULL, "thread_func_tx");
   return 0;
}

static void __exit ebbchar_exit(void){
   kill_thread=1;
   device_destroy(ebbcharClass, MKDEV(majorNumber, 0));     // remove the device
   class_unregister(ebbcharClass);                          // unregister the device class
   class_destroy(ebbcharClass);                             // remove the device class
   unregister_chrdev(majorNumber, DEVICE_NAME);             // unregister the major number
   kobject_put(register_kobj);
   //kthread_stop(task1);
   //kthread_stop(task2);
   printk(KERN_INFO "EBBChar: Goodbye from the LKM!\n");
}

static int dev_open(struct inode *inodep, struct file *filep){
   numberOpens++;
   printk(KERN_INFO "EBBChar: Device has been opened %d time(s)\n", numberOpens);
   return 0;
}
 
static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset){

   int i=0;
   txLen=(int)len;
   rxIndex=0;
   rxflag=0;
   memset(rxBuffer,'\0',1024);
   //printk(KERN_INFO "debug info nan : %s", txBuffer);
   for ( i=0;i<len;i++)
   {
      ISR(1);        //Interrupt to set rxthread buffer copy 
      //while(txflag){} //wait till thread reads register
      //buffer[i] = txRegister[0]; //just a debug option to get what is in tx else data written to txregister will be set on tx line
      // printk(KERN_INFO "debug info: %c", rxBuffer[rxIndex-1]);
   }  //release comment when rx available
   return len;
}

static ssize_t dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset){
   //sprintf(message, "%s (%zu letters)", buffer, len);   // appending received string with its length
  // size_of_message = strlen(message);                 // store the length of the stored message
   int i=0;
   lines+=1;
   rxIndex=0;
   printk(KERN_INFO "recv tx pin: %s", buffer);
   memset (rxBuffer,'\0',1024);
   for (i=0;i<strlen(buffer);i++)
   {
      writel(buffer[i],txRegister);
      //txRegister[0] = buffer[i];
      //txRegister[0] = buffer[i]; //in future remove this read from buffer as rxRegister will be set by UART port when data received and change interrupt to whenever data is available in rxregister
      //printk(KERN_INFO "debug info: %c", txRegister[0]);
      msleep(1);
      ISR(1);
      msleep(1);
//      ISR2(1);        //Interrupt to set rxthread buffer copy 
//      msleep(1);
      //   if (buffer[i]=='\0' || buffer[i]=='\n'){i=strlen(buffer)-50;}
      //while(rxflag){} //wait till thread reads register
   }   

   printk(KERN_INFO "recv rx: %s", rxBuffer);
   return len;
}

static int dev_release(struct inode *inodep, struct file *filep){
   printk(KERN_INFO "EBBChar: Device successfully closed\n");
   return 0;
}

module_init(ebbchar_init);
module_exit(ebbchar_exit);
