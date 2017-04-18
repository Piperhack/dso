#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/miscdevice.h> // misc dev
#include <linux/fs.h>         // file operations
#include <asm/uaccess.h>      // copy to/from user space
#include <linux/wait.h>       // waiting queue
#include <linux/sched.h>      // TASK_INTERRUMPIBLE
#include <linux/delay.h>      // udelay
#include <linux/timer.h> 
#include <linux/jiffies.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>

#define DRIVER_AUTHOR "Grupo Blanco Guay"
#define DRIVER_DESC   "Practica Final (leds/speaker/buttons)"

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


//****************************************************
//   TIMERs
//****************************************************

//Definimos una función "timer" para el botón 1
static void funcion_timer1(unsigned long n){
		enable_irq(irq_BUTTON1);
	}
// Definimos una función "timer" para el botón 2
static void funcion_timer2(unsigned long n){
		enable_irq(irq_BUTTON2);
	}
DEFINE_TIMER(timer_rebote1, funcion_timer1, 0, 0);
DEFINE_TIMER(timer_rebote2, funcion_timer2, 0, 0);


static int reboteticks;
static int rebotems = 300;

module_param(rebotems, int, S_IRUGO);


//****************************************************
///   TASKLET
//****************************************************

static void boton1_int(unsigned long n);
static void boton2_int(unsigned long n);

DECLARE_TASKLET(handler_boton1,boton1_int, 0);
DECLARE_TASKLET(handler_boton2, boton2_int,0);



/****************************************************************************/
/* LEDs write/read using gpio kernel API                                    */
/****************************************************************************/

static void byte2leds(char ch)
{
    int i;
    int val=(int)ch; //"val" representa la posición del array 
    
	if(((val>>6 & 1) == 0) && ((val>>7 & 1) == 0)){
		for(i=0; i<6; i++){
                    gpio_set_value(LED_GPIOS[i], (val >> i) & 1);
		}
	}else if(((val>>6 & 1) == 1) && ((val>>7 & 1) == 0)){
		for(i=0; i<6; i++){
			if(gpio_get_value(LED_GPIOS[i]) == 0 && ((val >> i & 1) == 1))
                            gpio_set_value(LED_GPIOS[i], 1);
			}
		}else if(((val>>6 & 1) == 0) && ((val>>7 & 1) == 1)){
                    for(i=0; i<6; i++){
                            if(gpio_get_value(LED_GPIOS[i]) == 1 && ((val >> i & 1) == 1))
                                    gpio_set_value(LED_GPIOS[i], 0);
			}
		}
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



/****************************************************************************/
/* "SPEAKER"                                           */
/****************************************************************************/

static ssize_t speaker_write (struct file *file, const char __user *buf,
                          size_t count, loff_t *ppos){
    char ch;

    if (copy_from_user( &ch, buf, 1 )) {
        return -EFAULT;
    }

    printk( KERN_INFO " (write) valor recibido: %d\n",(int)ch);

    if(ch == '0'){
		gpio_set_value(SPEAKER_GPIO, 0);
	} else {
		gpio_set_value(SPEAKER_GPIO, 1);
	}

    return 1;
}

static const struct file_operations speaker_fops ={
	.owner	= THIS_MODULE,
	.write	= speaker_write,
};

static struct miscdevice speaker_miscdev ={
	.minor 	= MISC_DYNAMIC_MINOR,
	.name 	= "speaker",
	.fops	= &speaker_fops,
};

/****************************************************************************/




/****************************************************************************/
/* IRQ handler - fired on interrupt                                         */
/****************************************************************************/
static irqreturn_t r_irq_handler1(int irq, void *dev_id, struct pt_regs *regs) {
	disable_irq_nosync(irq_BUTTON1);
	mod_timer(&timer_rebote1, jiffies + reboteticks);
    tasklet_schedule(&handler_boton1);
	return IRQ_HANDLED;
}

static irqreturn_t r_irq_handler2(int irq, void *dev_id, struct pt_regs *regs) {
	disable_irq_nosync(irq_BUTTON2);
	mod_timer(&timer_rebote2, jiffies + reboteticks);
    tasklet_schedule(&handler_boton2);
    return IRQ_HANDLED;
}

//INCREMENTAMOS el valor que representan los leds gracias al pulsador
static void boton1_int(unsigned long n) {
	int ch;
	
	ch = leds2byte();
	ch++;
	if(ch>63) ch=0;
	byte2leds(ch);
}

// DECREMENTAMOS el valor que representan los leds gracias al pulsador
static void boton2_int(unsigned long n) {
	int ch;

	ch = leds2byte();
	ch--;
	if(ch<0) ch=63;
	byte2leds(ch);
}





/*******************************
 *  REGISTER DEVS
 *******************************/

static int r_dev_config(void)
{
    int ret=0;
	
	/* Leds */
    ret = misc_register(&leds_miscdev);
    if (ret < 0) {
        printk(KERN_ERR "misc_register failed\n");
    }
	else
		printk(KERN_NOTICE "misc_register OK... leds_miscdev.minor=%d\n", leds_miscdev.minor);
	return ret;
	
	/* Speaker */
    ret = misc_register(&speaker_miscdev);
    if (ret < 0) {
        printk(KERN_ERR "misc_register failed\n");
    }
	else
		printk(KERN_NOTICE "misc_register OK... speaker_miscdev.minor=%d\n", speaker_miscdev.minor);
	return ret;	
	
}


/*****************************************************************************/
/* This functions requests GPIOs and configures interrupts */
/*****************************************************************************/

/*******************************
 *  request and init gpios
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
	
	if (res=gpio_request_one(SPEAKER_GPIO, GPIOF_INIT_LOW, "El speaker")){
		printk("Ha fallado la inicializacion del speaker \n");
		return res;
	}
	gpio_direction_output(SPEAKER_GPIO,0);
	
	return res;
}


/*******************************
 *  set interrup
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
	printk(KERN_NOTICE "  Mapped int %d for button2 in gpio %d\n", irq_BUTTON2, GPIO_BUTTON2);

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
	
	//Calculo de tiempo, calcula el tiempo de los ticks
	reboteticks = msecs_to_jiffies(rebotems);
		
	return res;
}


static void r_cleanup(void) {
    int i;
    printk(KERN_NOTICE "%s module cleaning up...\n", KBUILD_MODNAME);
	
    for(i=0; i<6; i++){
		gpio_set_value(LED_GPIOS[i], 0);
		gpio_free(LED_GPIOS[i]);
	}
	
	if (leds_miscdev.this_device) misc_deregister(&leds_miscdev);
	if (speaker_miscdev.this_device) misc_deregister(&speaker_miscdev);
	
	if(irq_BUTTON1) free_irq(irq_BUTTON1, GPIO_BUTTON1_DEVICE_DESC);   //libera irq
	if(irq_BUTTON2) free_irq(irq_BUTTON2, GPIO_BUTTON2_DEVICE_DESC);   //libera irq
	
    gpio_free(GPIO_BUTTON1);  // libera GPIO
	gpio_free(GPIO_BUTTON2);  // libera GPIO
	
	printk(KERN_NOTICE "Done. Bye from %s module\n", KBUILD_MODNAME);
    return;
}


module_init(r_init);
module_exit(r_cleanup);


/****************************************************************************/
/* Module licensing/description block.                                      */
/****************************************************************************/

MODULE_LICENSE("GPL");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);

