#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/miscdevice.h> // misc dev
#include <linux/fs.h>         // file operations
#include <asm/uaccess.h>      // copy to/from user space
#include <linux/wait.h>       // waiting queue
#include <linux/sched.h>      // TASK_INTERRUMPIBLE
#include <linux/delay.h>      // udelay

#include <linux/interrupt.h>
#include <linux/gpio.h>

#define DRIVER_AUTHOR "Grupo Blanco Guay"
#define DRIVER_DESC   "Ejemplo interrupción para placa lab. DAC Rpi"

//GPIOS numbers as in BCM RPi

#define GPIO_BUTTON1 2
#define GPIO_BUTTON2 3

#define GPIO_SPEAKER 4

#define GPIO_GREEN1  27
#define GPIO_GREEN2  22
#define GPIO_YELLOW1 17
#define GPIO_YELLOW2 11
#define GPIO_RED1    10
#define GPIO_RED2    9

static int LED_GPIOS[]= {GPIO_GREEN1, GPIO_GREEN2, GPIO_YELLOW1, GPIO_YELLOW2, GPIO_RED1, GPIO_RED2} ;

static char *led_desc[]= {"GPIO_GREEN1","GPIO_GREEN2","GPIO_YELLOW1","GPIO_YELLOW2","GPIO_RED1","GPIO_RED2"} ;

/****************************************************************************/
/* LEDs write/read using gpio kernel API                                    */
/****************************************************************************/

static void byte2leds(char ch)
{
    int i;
    int val=(int)ch;

    for(i=0; i<6; i++) gpio_set_value(LED_GPIOS[i], (val >> i) & 1);
}

static char leds2byte(void)
{
    int val;
    char ch;
    int i;
    ch=0;

    for(i=0; i<6; i++)
    {
        val=gpio_get_value(LED_GPIOS[i]);
        ch= ch | (val << i);
    }
    return ch;
}


/****************************************************************************/
/* LEDs device file operations                                              */
/****************************************************************************/

static ssize_t leds_write(struct file *file, const char __user *buf,
                          size_t count, loff_t *ppos)
{

    char ch;

    if (copy_from_user( &ch, buf, 1 )) {
        return -EFAULT;
    }

    printk( KERN_INFO " (write) valor recibido: %d\n",(int)ch);

    byte2leds(ch);

    return 1;
}

static ssize_t leds_read(struct file *file, char __user *buf,
                         size_t count, loff_t *ppos)
{
    char ch;

    if(*ppos==0) *ppos+=1;
    else return 0;

    ch=leds2byte();

    printk( KERN_INFO " (read) valor entregado: %d\n",(int)ch);


    if(copy_to_user(buf,&ch,1)) return -EFAULT;

    return 1;
}


static const struct file_operations leds_fops = {
    .owner	= THIS_MODULE,
    .write	= leds_write,
    .read	= leds_read,
};

/****************************************************************************/
/* LEDs device struct                                                       */

static struct miscdevice leds_miscdev = {
    .minor	= MISC_DYNAMIC_MINOR,
    .name	= "leds",
    .fops	= &leds_fops,
};


/*******************************
 *  register device for leds
 *******************************/

static int r_dev_config(void)
{
    int ret=0;
    ret = misc_register(&leds_miscdev);
    if (ret < 0) {
        printk(KERN_ERR "misc_register failed\n");
    }
	else
		printk(KERN_NOTICE "misc_register OK... leds_miscdev.minor=%d\n", leds_miscdev.minor);
	return ret;
}


/****************************************************************************/
/* Interrupts variables block                                               */
/****************************************************************************/
static short int irq_BUTTON1    = 0;
static short int irq_BUTTON2    = 0;

// text below will be seen in 'cat /proc/interrupt' command
#define GPIO_BUTTON1_DESC           "Boton 1"
#define GPIO_BUTTON2_DESC			"Boton 2"

// below is optional, used in more complex code, in our case, this could be
#define GPIO_BUTTON1_DEVICE_DESC    "Placa lab. DAC"
#define GPIO_BUTTON2_DEVICE_DESC    "Placa lab. DAC"



/****************************************************************************/
/* IRQ handler - fired on interrupt                                         */
/****************************************************************************/
static irqreturn_t r_irq_handler1(int irq, void *dev_id, struct pt_regs *regs) {
    int ch;
    // we will increment value in leds with button push
    // due to switch bouncing this hadler will be fired few times for every putton push
    
    ch=leds2byte();
    ch++;
    if (ch>63) ch=0;
    byte2leds(ch);

    return IRQ_HANDLED;
}



static irqreturn_t r_irq_handler2(int irq, void *dev_id, struct pt_regs *regs) {

    int ch;
    // we will increment value in leds with button push
    // due to switch bouncing this hadler will be fired few times for every putton push
    
    ch=leds2byte();
    ch--;
    if (ch<0) ch=63;
    byte2leds(ch);

    return IRQ_HANDLED;
}



/*****************************************************************************/
/* This functions requests GPIOs and configures interrupts */
/*****************************************************************************/

/*******************************
 *  request and init gpios for leds
 *******************************/

static int r_GPIO_config(void)
{
    int i;
    int res=0;
    for(i=0; i<6; i++)
    {
        if ((res=gpio_request_one(LED_GPIOS[i], GPIOF_INIT_LOW, led_desc[i])))
        {
            printk(KERN_ERR "GPIO request faiure: led GPIO %d %s\n",LED_GPIOS[i], led_desc[i]);
            return res;
        }
        gpio_direction_output(LED_GPIOS[i],0);
	}
	
	return res;
}


/*******************************
 *  set interrup for button 1
 *******************************/

static int r_int_config(void)
{
	int res=0;
    if ((res=gpio_request(GPIO_BUTTON1, GPIO_BUTTON1_DESC))) {
        printk(KERN_ERR "GPIO request faiure: %s\n", GPIO_BUTTON1_DESC);
        return res;
    }
    
    if ((res=gpio_request(GPIO_BUTTON2, GPIO_BUTTON2_DESC))) {
        printk(KERN_ERR "GPIO request faiure: %s\n", GPIO_BUTTON2_DESC);
        return res;
    }

    if ( (irq_BUTTON1 = gpio_to_irq(GPIO_BUTTON1)) < 0 ) {
        printk(KERN_ERR "GPIO to IRQ mapping faiure %s\n", GPIO_BUTTON1_DESC);
        return irq_BUTTON1;
    }
    
    if ( (irq_BUTTON2 = gpio_to_irq(GPIO_BUTTON2)) < 0 ) {
        printk(KERN_ERR "GPIO to IRQ mapping faiure %s\n", GPIO_BUTTON2_DESC);
        return irq_BUTTON2;
    }


    printk(KERN_NOTICE "  Mapped int %d for button1 in gpio %d\n", irq_BUTTON1, GPIO_BUTTON1);
	printk(KERN_NOTICE "  Mapped int %d for button1 in gpio %d\n", irq_BUTTON2, GPIO_BUTTON2);

    if ((res=request_irq(irq_BUTTON1,
                    (irq_handler_t ) r_irq_handler1,
                    IRQF_TRIGGER_FALLING,
                    GPIO_BUTTON1_DESC,
                    GPIO_BUTTON1_DEVICE_DESC))) {
        printk(KERN_ERR "Irq Request failure\n");
        return res;
    }
    
    
     if ((res=request_irq(irq_BUTTON2,
                    (irq_handler_t ) r_irq_handler2,
                    IRQF_TRIGGER_FALLING,
                    GPIO_BUTTON2_DESC,
                    GPIO_BUTTON2_DEVICE_DESC))) {
        printk(KERN_ERR "Irq Request failure\n");
        return res;
    }



    return res;
}


/****************************************************************************/
/* Module init / cleanup block.                                             */
/****************************************************************************/

static void r_cleanup(void) {
    int i;
    printk(KERN_NOTICE "%s module cleaning up...\n", KBUILD_MODNAME);
    for(i=0; i<6; i++){
		gpio_free(LED_GPIOS[i]);
	}
	if (leds_miscdev.this_device) misc_deregister(&leds_miscdev);
	if(irq_BUTTON1) free_irq(irq_BUTTON1, GPIO_BUTTON1_DEVICE_DESC);   //libera irq
    gpio_free(GPIO_BUTTON1);  // libera GPIO
	printk(KERN_NOTICE "Done. Bye from %s module\n", KBUILD_MODNAME);
    return;
}


static int r_init(void) {
	int res=0;
    printk(KERN_NOTICE "Hello, loading %s module!\n", KBUILD_MODNAME);
    printk(KERN_NOTICE "%s - devices config...\n", KBUILD_MODNAME);

    if((res = r_dev_config())||(res = r_int_config())){
		r_cleanup();
		return res;
	}
	printk(KERN_NOTICE "%s - GPIO config...\n", KBUILD_MODNAME);
	
	if((res = r_GPIO_config())){
		r_cleanup();
		return res;
	}
	return res;
}

module_init(r_init);
module_exit(r_cleanup);


/****************************************************************************/
/* Module licensing/description block.                                      */
/****************************************************************************/
MODULE_LICENSE("GPL");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);

