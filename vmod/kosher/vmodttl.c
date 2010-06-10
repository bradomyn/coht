#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <asm/uaccess.h>        /* copy_*_user */
#include "vmodttl.h"
#include "cio8536.h"
#include "modulbus_register.h"

struct message_list {
	struct list_head list;
	int dev;
	int val;
};

enum vmodttl_channel{
     VMOD_TTL_CHANNEL_A,
     VMOD_TTL_CHANNEL_B,
     VMOD_TTL_CHANNELS_AB
};

struct vmodttl_dev{
     int			created;        /* flag initialize      */
	int			dev;
	int			OpenCount;      /* open count */
	int			board_position;
	int			board_number;
	char			*cname;
	int			io_flag;
	int			us_pulse;	/* Time being up of the data strobe pulse */
	int			open_collector;
	spinlock_t		vmodttl_spinlock;
	int			is_big_endian;
	unsigned long		handle;
};


static int lun[VMODTTL_MAX_BOARDS];
static int num_lun;
module_param_array(lun, int, &num_lun, S_IRUGO);
MODULE_PARM_DESC(lun, "Logical Unit Number of the VMOD-TTL board");

static char *carrier[CARRIER_NAME * VMODTTL_MAX_BOARDS];
static int num_carrier;
module_param_array(carrier, charp, &num_carrier, S_IRUGO);
MODULE_PARM_DESC(carrier, "Name of the carrier in which the VMOD-TTL is plugged in.");

static int carrier_number[VMODTTL_MAX_BOARDS];
static int num_carrier_number;
module_param_array(carrier_number, int, &num_carrier_number, S_IRUGO);
MODULE_PARM_DESC(carrier_number, "Logical Unit Number of the carrier");

static int slot[VMODTTL_MAX_BOARDS];
static int num_slot;
module_param_array(slot, int, &num_slot, S_IRUGO);
MODULE_PARM_DESC(slot, "Slot of the carrier in which the VMOD-TTL board is placed.");

/* The One And Only Device (OAOD) */
struct cdev                     cdev;

/* module config tables */
static struct vmodttl    modules[VMODTTL_MAX_BOARDS];
static int                      lun_to_dev[VMODTTL_MAX_BOARDS];

/* The One And Only Device (OAOD) */
static dev_t devno;

static struct vmodttl_dev       *pvmodttlDv[VMODTTL_MAX_BOARDS];


/* 
 * I/O operations
 *
 */


static uint16_t vmodttl_read_word(struct vmodttl_dev *pd, int offset)
{

	unsigned long ioaddr = 0;
	uint16_t val = 0;

	ioaddr = pd->handle + offset;
	if (pd->is_big_endian)
		val = ioread16be((u16 *)(ioaddr));
	else
		val = ioread16((u16 *)(ioaddr));

	mb();
	udelay(DEFAULT_DELAY); /* Wait to give time to Z8536 to do the operations */

	return val;
}

	
static void vmodttl_write_word(struct vmodttl_dev *pd, int offset, uint16_t value)
{
	unsigned long ioaddr = pd->handle + offset;
	if(pd->is_big_endian)
		iowrite16be(value, (u16 *)ioaddr);
	else
		iowrite16(value, (u16 *)ioaddr);

	mb();
	udelay(DEFAULT_DELAY);
}

/* Init the ports with the desired setup */
static void vmodttl_def_io(struct vmodttl_dev	*pd)
{
	unsigned char	tmp;

	vmodttl_write_word(pd, VMODTTL_CONTROL, MCCR);
	vmodttl_write_word(pd, VMODTTL_CONTROL, 0);

	/* port A		*/
	/* -------------------- */
	/* disable interrupt	*/
	vmodttl_write_word(pd, VMODTTL_CONTROL, PCSR_A);
	vmodttl_write_word(pd, VMODTTL_CONTROL, CLEAR_IE);
	/* clear ip/ius		*/
	vmodttl_write_word(pd, VMODTTL_CONTROL, PCSR_A);
	vmodttl_write_word(pd, VMODTTL_CONTROL, CLEAR_IP_IUS);

	vmodttl_write_word(pd, VMODTTL_CONTROL, PMSR_A);
	vmodttl_write_word(pd, VMODTTL_CONTROL, 0x14);		/* /LPM			*/


	if((pd->open_collector & A_CHAN_OPEN_COLLECTOR) == A_CHAN_OPEN_COLLECTOR) {
		/* open collector connection mode */
		if(pd->io_flag & A_CHAN_OUT) {
			vmodttl_write_word(pd, VMODTTL_CONTROL, DDR_A);
			vmodttl_write_word(pd, VMODTTL_CONTROL, 0x00);	 	/* output	*/
			vmodttl_write_word(pd, VMODTTL_CONTROL, DPPR_A);
			vmodttl_write_word(pd, VMODTTL_CONTROL, 0x00);
		} else {
			vmodttl_write_word(pd, VMODTTL_CONTROL, DDR_A);
			vmodttl_write_word(pd, VMODTTL_CONTROL, 0xff);		/* input 	*/
			vmodttl_write_word(pd, VMODTTL_CONTROL, DPPR_A);

			if(pd->io_flag & VMODTTL_O)
				vmodttl_write_word(pd, VMODTTL_CONTROL, 0xff);	/* invert! 	*/
			else
				vmodttl_write_word(pd, VMODTTL_CONTROL, 0x00);
		}
	} else {
		/* normal TTL connection mode */
		if(pd->io_flag & A_CHAN_OUT) {
			vmodttl_write_word(pd, VMODTTL_CONTROL, DDR_A);
			vmodttl_write_word(pd, VMODTTL_CONTROL, 0x00);		/* output	*/
		} else {
			vmodttl_write_word(pd, VMODTTL_CONTROL, DDR_A);
			vmodttl_write_word(pd, VMODTTL_CONTROL, 0xff);		/* input 	*/
		}

		vmodttl_write_word(pd, VMODTTL_CONTROL, DPPR_A);
		if(pd->io_flag & VMODTTL_O)
			vmodttl_write_word(pd, VMODTTL_CONTROL, 0xff);		/* invert! 	*/
		else
			vmodttl_write_word(pd, VMODTTL_CONTROL, 0x00);
	}

	vmodttl_write_word(pd, VMODTTL_CONTROL, SIOCR_A);
	vmodttl_write_word(pd, VMODTTL_CONTROL, 0x00);

	/* port B		*/
	/* -------------------- */
	/* disable interrupt	*/
	vmodttl_write_word(pd, VMODTTL_CONTROL, PCSR_B);
	vmodttl_write_word(pd, VMODTTL_CONTROL, CLEAR_IE);
	/* clear ip/ius		*/
	vmodttl_write_word(pd, VMODTTL_CONTROL, PCSR_B);
	vmodttl_write_word(pd, VMODTTL_CONTROL, CLEAR_IP_IUS);

	vmodttl_write_word(pd, VMODTTL_CONTROL ,PMSR_B);
	vmodttl_write_word(pd, VMODTTL_CONTROL, 0x14);		/* /LPM			*/

	if((pd->open_collector & B_CHAN_OPEN_COLLECTOR) == B_CHAN_OPEN_COLLECTOR) {
		/* open collector connection mode */
		if(pd->io_flag & B_CHAN_OUT) {
			vmodttl_write_word(pd, VMODTTL_CONTROL, DDR_B);
			vmodttl_write_word(pd, VMODTTL_CONTROL, 0x00);	/* output	 	*/
			vmodttl_write_word(pd, VMODTTL_CONTROL, DPPR_B);
			vmodttl_write_word(pd, VMODTTL_CONTROL, 0x00);
		} else {
			vmodttl_write_word(pd, VMODTTL_CONTROL, DDR_B);
			vmodttl_write_word(pd, VMODTTL_CONTROL, 0xff);	/* input 	 	*/
			vmodttl_write_word(pd, VMODTTL_CONTROL, DPPR_B);
			if(pd->io_flag & VMODTTL_O)
				vmodttl_write_word(pd, VMODTTL_CONTROL, 0xff);	/* invert! 	*/
			else
				vmodttl_write_word(pd, VMODTTL_CONTROL, 0x00);
		}
	} else {
		/* normal TTL connection mode */
		if(pd->io_flag & B_CHAN_OUT) {
			vmodttl_write_word(pd, VMODTTL_CONTROL, DDR_B);
			vmodttl_write_word(pd, VMODTTL_CONTROL, 0x00);		/* output 	*/
		} else {
			vmodttl_write_word(pd, VMODTTL_CONTROL, DDR_B);
			vmodttl_write_word(pd, VMODTTL_CONTROL, 0xff);		/* input  	*/
		}
		vmodttl_write_word(pd, VMODTTL_CONTROL, DPPR_B);
		if(pd->io_flag & VMODTTL_O)
			vmodttl_write_word(pd, VMODTTL_CONTROL, 0xff);		/* invert! 	*/
		else
			vmodttl_write_word(pd, VMODTTL_CONTROL, 0x00);
	}

	vmodttl_write_word(pd, VMODTTL_CONTROL, SIOCR_B);
	vmodttl_write_word(pd, VMODTTL_CONTROL, 0x00);

	/* port C	generates data strobe (Output) */
	if((pd->open_collector & C_CHAN_OPEN_COLLECTOR) == C_CHAN_OPEN_COLLECTOR) {
		/* open collector connection mode */
		vmodttl_write_word(pd, VMODTTL_CONTROL, DDR_C);
		vmodttl_write_word(pd, VMODTTL_CONTROL, 0x00);		/* output 	*/
		vmodttl_write_word(pd, VMODTTL_CONTROL, DPPR_C);
		vmodttl_write_word(pd, VMODTTL_CONTROL, 0x00);
	} else {
		/* normal TTL connection mode */
		vmodttl_write_word(pd, VMODTTL_CONTROL, DDR_C);
		vmodttl_write_word(pd, VMODTTL_CONTROL, 0x00);		/* output 	*/

		vmodttl_write_word(pd, VMODTTL_CONTROL, DPPR_C);
		if(pd->io_flag & VMODTTL_O)
			vmodttl_write_word(pd, VMODTTL_CONTROL, 0x0f);		/* invert!	*/
		else
			vmodttl_write_word(pd, VMODTTL_CONTROL, 0x00);
	}

	vmodttl_write_word(pd, VMODTTL_CONTROL, SIOCR_C);
	vmodttl_write_word(pd, VMODTTL_CONTROL, 0x00);

	vmodttl_write_word(pd, VMODTTL_CONTROL, MCCR);
	tmp  = vmodttl_read_word(pd, VMODTTL_CONTROL) | PAE | PBE | PCE; 
	vmodttl_write_word(pd, VMODTTL_CONTROL, MCCR);
	vmodttl_write_word(pd, VMODTTL_CONTROL, tmp);


}

/* Configure the Zilog Z8536 CIO */
static void vmodttl_default(struct vmodttl_dev  *pd)
{
	unsigned int	dummy;
	int		i = 0;
	
	spin_lock(&pd->vmodttl_spinlock);

	/* Prepare the Zilog Z8536 CIO to be configured */
	dummy = vmodttl_read_word(pd, VMODTTL_CONTROL);	
	vmodttl_write_word(pd, VMODTTL_CONTROL, MCCR);
	vmodttl_write_word(pd, VMODTTL_CONTROL, 0);
	vmodttl_write_word(pd, VMODTTL_CONTROL, MICR);	
	dummy = vmodttl_read_word(pd, VMODTTL_CONTROL);	
	vmodttl_write_word(pd, VMODTTL_CONTROL, MICR);	
	vmodttl_write_word(pd, VMODTTL_CONTROL, 0);		

	vmodttl_write_word(pd, VMODTTL_CONTROL, MICR);	/* reset cio			*/
	vmodttl_write_word(pd, VMODTTL_CONTROL, RESET);
	vmodttl_write_word(pd, VMODTTL_CONTROL,0);
	dummy = vmodttl_read_word(pd, VMODTTL_CONTROL);	

	vmodttl_def_io(pd);

	/* enable master interrupts	*/
	vmodttl_write_word(pd, VMODTTL_CONTROL, MICR);
	i = (vmodttl_read_word(pd, VMODTTL_CONTROL) | MIE | NV);
	vmodttl_write_word(pd, VMODTTL_CONTROL, i);

	spin_unlock(&pd->vmodttl_spinlock);
	printk(KERN_INFO "VMODTTL: board initialized\n");
	

}

int vmodttl_open(struct inode *inode, struct file *filp)
{
	unsigned int		minor = iminor(inode);
    	struct vmodttl_dev		*pd =  (struct vmodttl_dev *)pvmodttlDv[minor];

	pd->OpenCount++;
	return 0; 
}

int vmodttl_release(struct inode *inode, struct file *filp)
{
	unsigned int		minor = iminor(inode);
    	struct vmodttl_dev		*pd =  (struct vmodttl_dev *)pvmodttlDv[minor];

	pd->OpenCount--;
	return 0;

}


static int vmodttl_ioctl(struct inode *inode, struct file *fp, unsigned op, unsigned long arg)
{
	unsigned int		minor = iminor(inode);
	struct vmodttl_dev		*pd =  (struct vmodttl_dev *)pvmodttlDv[minor];

	switch(op){

	case VMODTTL_CONFIG:
	{
		struct vmodttlconfig conf;

		spin_lock(&pd->vmodttl_spinlock);
		if(copy_from_user((char *)&conf, (char *)arg, sizeof(struct vmodttlconfig))) 
			return -EFAULT;

		if((conf.io_flag > (A_CHAN_OUT + B_CHAN_OUT + C_CHAN_OUT)) ||
		   conf.io_flag < 0){
			spin_unlock(&pd->vmodttl_spinlock);
			return -EINVAL;
		}
		else
			pd->io_flag = conf.io_flag;

		/* there is a sleep of DEFAULT_DELAY us in vmodttl_write|read_word() */
		if (conf.us_pulse < DEFAULT_DELAY)
			pd->us_pulse = 0; 
		else
			pd->us_pulse = conf.us_pulse - DEFAULT_DELAY;
	
		if(conf.invert)
			pd->io_flag |= VMODTTL_O;

		pd->open_collector = conf.open_collector;
		vmodttl_def_io(pd); 
		spin_unlock(&pd->vmodttl_spinlock);
	}
		break;
	case VMODTTL_READ_CHAN:
	{
		int val = 0;
		int dev = 0;
		int strobe_value = 0;
		struct vmodttlarg buf;

		if(copy_from_user((char *)&buf, (char *)arg,sizeof(int)))
			return -EFAULT;

		dev = buf.dev & 0x00ff; /* Get channel */	
		if(dev < VMOD_TTL_CHANNEL_A || dev > VMOD_TTL_CHANNEL_B)
			return -EINVAL; /* invalid channel */

		spin_lock(&pd->vmodttl_spinlock);

		switch(dev){
		case VMOD_TTL_CHANNEL_A:
			val = (int) vmodttl_read_word(pd, VMODTTL_PORTA);
			strobe_value = 0x04;
			break;

		case VMOD_TTL_CHANNEL_B:
			val = (int) vmodttl_read_word(pd, VMODTTL_PORTB);		
			strobe_value = 0x08;
			break;
			
		case VMOD_TTL_CHANNELS_AB:
			val = (int) vmodttl_read_word(pd, VMODTTL_PORTA);
			val += ((int) vmodttl_read_word(pd, VMODTTL_PORTB)) << 8;

		default:
		     break;
		}
				
		/* produce the strobe on C channel (pulse) */
		vmodttl_write_word(pd, VMODTTL_PORTC, strobe_value);
		udelay(pd->us_pulse);
		vmodttl_write_word(pd, VMODTTL_PORTC, 0);

		spin_unlock(&pd->vmodttl_spinlock);
		buf.val = val;

		if(copy_to_user((char *)arg, (char *)&buf, sizeof(struct vmodttlarg)))
			return -EFAULT;
				
		break;
	}
	case VMODTTL_WRITE_CHAN:
	{
		struct vmodttlarg buf;
		int dev;
		uint16_t data;
		uint16_t tmp;
		int strobe_value = 0;

		if(copy_from_user((char *)&buf, (char *)arg, sizeof(struct vmodttlarg)))
			return -EIO; 

		dev = buf.dev;
		data = buf.val;

		if(dev < VMOD_TTL_CHANNEL_A || dev > VMOD_TTL_CHANNELS_AB)
			return -EINVAL; /* invalid channel */

		spin_lock(&pd->vmodttl_spinlock);
		switch(dev){
		case VMOD_TTL_CHANNEL_A:

			if((pd->io_flag & A_CHAN_OUT) == A_CHAN_IN){
				spin_unlock(&pd->vmodttl_spinlock);
				return -EIO;
			}

			vmodttl_write_word(pd, VMODTTL_PORTA, data);
			vmodttl_write_word(pd, VMODTTL_CONTROL, MCCR);
			tmp = vmodttl_read_word(pd, VMODTTL_CONTROL);

			vmodttl_write_word(pd, VMODTTL_CONTROL, MCCR);
			vmodttl_write_word(pd, VMODTTL_CONTROL, (tmp | PAE));
			strobe_value = 0x01;
			break;

		case VMOD_TTL_CHANNEL_B:

			if((pd->io_flag & B_CHAN_OUT) == B_CHAN_IN){
				spin_unlock(&pd->vmodttl_spinlock);
				return -EIO;
			}

			vmodttl_write_word(pd, VMODTTL_PORTB, data);
			vmodttl_write_word(pd, VMODTTL_CONTROL, MCCR);
			tmp = vmodttl_read_word(pd, VMODTTL_CONTROL);

			vmodttl_write_word(pd, VMODTTL_CONTROL, MCCR);
			vmodttl_write_word(pd, VMODTTL_CONTROL, (tmp | PBE));
			strobe_value = 0x02;
			break;

		case VMOD_TTL_CHANNELS_AB:

			if(((pd->io_flag & B_CHAN_OUT) == B_CHAN_IN) ||
			   (pd->io_flag & A_CHAN_OUT) == A_CHAN_IN) {
				spin_unlock(&pd->vmodttl_spinlock);
				return -EIO;
			}
						
			vmodttl_write_word(pd, VMODTTL_PORTA, data & 0xff);
			vmodttl_write_word(pd, VMODTTL_CONTROL, MCCR);
			tmp = vmodttl_read_word(pd, VMODTTL_CONTROL);

			vmodttl_write_word(pd, VMODTTL_PORTB, (data >> 8) & 0xff); 	
			vmodttl_write_word(pd, VMODTTL_CONTROL, MCCR);	
			tmp = vmodttl_read_word(pd, VMODTTL_CONTROL);

			vmodttl_write_word(pd, VMODTTL_CONTROL, MCCR);
			vmodttl_write_word(pd, VMODTTL_CONTROL, (tmp | PAE | PBE));
			strobe_value = 0x03;
						

		default:
			break;

		}

		/* produce the strobe on C channel (pulse) */
		vmodttl_write_word(pd, VMODTTL_PORTC, strobe_value);
		udelay(pd->us_pulse);
		vmodttl_write_word(pd, VMODTTL_PORTC, 0);
		spin_unlock(&pd->vmodttl_spinlock);
		break;
	}
	default:
		printk(KERN_INFO "There is no default option");
		return -EINVAL;

	}


	return 0;

}
struct file_operations vmodttl_fops = {
        .owner =	THIS_MODULE,
	.read =		NULL,
        .ioctl =	vmodttl_ioctl,
        .open =	vmodttl_open,
        .release =	vmodttl_release
};



static int check_lun_args(void)
{
	int i;

	if((num_lun != num_carrier) || (num_lun != num_carrier_number) || (num_lun != num_slot) ||
		(num_lun > VMODTTL_MAX_BOARDS)) {
		printk(KERN_ERR "VMODTTL: the number of parameters doesn't match or is invalid\n");
		return -1;
	}
	
	for(i=0; i < num_lun ; i++){
		int lun_d 		= (int) lun[i];
		char *cname 		= carrier[i];
		int carrier_num 	= (int)carrier_number[i];
		int slot_board 		= (int)slot[i];

		struct vmodttl 	*module = &(modules[i]);
		struct carrier_as 	as, *asp  = &as;
		gas_t 			get_address_space;

		if (lun_d < 0 || lun_d > VMODTTL_MAX_BOARDS){
			printk(KERN_ERR "VMODTTL Invalid lun: %d\n", lun_d);
			module->lun = -1;
			continue;
		}
		
		get_address_space = modulbus_carrier_as_entry(cname);

		if (get_address_space == NULL) {
			printk(KERN_ERR "No carrier %s\n", cname);
			return -1;
		} else
			printk(KERN_INFO "Carrier %s a.s. entry point at %p\n",
					cname, get_address_space);
		if (get_address_space(asp, carrier_num, slot_board, 1) <= 0){
			printk(KERN_ERR "VMODTTL Invalid carrier number: %d or slot: %d\n",
				  carrier_num, slot_board);
			module->lun = -1;
			continue;
		}

		module->lun           = lun_d;
		module->cname         = cname;
		module->carrier       = carrier_num;
		module->slot          = slot_board;
		module->address       = asp->address;

		module->is_big_endian = asp->is_big_endian;
		lun_to_dev[lun_d]       = i;

		printk(KERN_INFO "VMODTTL handle is %lx. Big Endian %d\n", module->address, module->is_big_endian);
		printk(KERN_INFO
				"    module<%d>: lun %d with "
				"carrier %s carrier_number = %d slot = %d\n",
				i, lun_d, cname, carrier_num, slot_board);
	}
        
        return 0;
}

static int __init vmodttl_init(void)
{
	int err;
	int i;
	struct vmodttl_dev *pd;

        printk(KERN_INFO "Initializing module with lun=%d/%d\n", num_lun, VMODTTL_MAX_BOARDS);

        err = check_lun_args();
        if (err != 0)
                return -1;

        err = alloc_chrdev_region(&devno, 0, VMODTTL_MAX_BOARDS, "vmodttl");
        if (err != 0) 
		goto fail_chrdev;

        printk(KERN_INFO "Allocated devno %0x\n",devno);

        cdev_init(&cdev, &vmodttl_fops);
        cdev.owner = THIS_MODULE;
        err = cdev_add(&cdev, devno, VMODTTL_MAX_BOARDS);
        if (err){
        	printk(KERN_ERR "Added cdev with err = %d\n", err);
		goto fail_cdev;
	}

	for(i = 0; i < num_lun ; i++){ 
		if(modules[i].lun == -1)
			continue;

		pd = kzalloc(sizeof(struct vmodttl_dev), GFP_KERNEL);

		if (pd == 0)
			goto fail_cdev;

		pvmodttlDv[i] = (void *)pd;
		pd->handle = modules[i].address;
		pd->dev = i;
		pd->OpenCount = 0;
		pd->io_flag = 0;
		pd->open_collector = 0; /* All of channels are open drain by default */
		pd->created = 1;
		pd->is_big_endian = modules[i].is_big_endian;
		pd->board_number = modules[i].carrier;
		pd->board_position = modules[i].slot;
		pd->cname = modules[i].cname;

		spin_lock_init(&pd->vmodttl_spinlock);
		vmodttl_default(pd);
	}

	return 0;

fail_cdev:      unregister_chrdev_region(devno, VMODTTL_MAX_BOARDS);
fail_chrdev:    return -1;

}

static void __exit vmodttl_exit(void)
{
	int i;

	cdev_del(&cdev);
        unregister_chrdev_region(devno, VMODTTL_MAX_BOARDS);
	
	for(i =0; i < num_lun; i++)
	{
		if(pvmodttlDv[i] != 0)
		{
			kfree(pvmodttlDv[i]);
		}
	}

	printk(KERN_INFO "VMODTTL: exit module.\n");
}

module_init(vmodttl_init);
module_exit(vmodttl_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Samuel Iglesias Gonsalvez");
MODULE_DESCRIPTION("VMOD-TTL device driver");
MODULE_VERSION("1.0");
