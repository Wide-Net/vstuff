/*
 * VoiSmart vGSM-II board driver
 *
 * Copyright (C) 2006-2007 Daniele Orlandi
 *
 * Authors: Daniele "Vihai" Orlandi <daniele@orlandi.com>
 *
 * This program is free software and may be modified and distributed
 * under the terms and conditions of the GNU General Public License.
 *
 */

#include <linux/autoconf.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/ctype.h>
#include <linux/fs.h>
#include <linux/bitops.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/serial.h>
#include <linux/termios.h>
#include <linux/serial_core.h>

#include "vgsm2.h"
#include "card.h"
#include "card_inline.h"
#include "regs.h"
#include "module.h"

struct list_head vgsm_cards_list = LIST_HEAD_INIT(vgsm_cards_list);
spinlock_t vgsm_cards_list_lock = SPIN_LOCK_UNLOCKED;

/* HW initialization */

static int vgsm_initialize_hw(struct vgsm_card *card)
{
	int i;

	/* Reset all subsystems */
	vgsm_outl(card, VGSM_R_SERVICE, VGSM_R_SERVICE_V_RESET);
	msleep(10); // FIXME!!!

#ifdef __LITTLE_ENDIAN
	vgsm_outl(card, VGSM_R_SERVICE, 0);
#elif __BIG_ENDIAN
	vgsm_outl(card, VGSM_R_SERVICE,
		VGSM_R_SERVICE_V_BIG_ENDIAN);
#else
#error Unsupported endianness
#endif

	vgsm_outl(card, VGSM_R_SIM_ROUTER,
			VGSM_R_SIM_ROUTER_V_ME_SOURCE(0, 0) |
			VGSM_R_SIM_ROUTER_V_ME_SOURCE(1, 1) |
			VGSM_R_SIM_ROUTER_V_ME_SOURCE(2, 2) |
			VGSM_R_SIM_ROUTER_V_ME_SOURCE(3, 3));

	for(i=0; i<card->sims_number; i++)
  	      vgsm_outl(card, VGSM_R_SIM_SETUP(i),
				VGSM_R_SIM_SETUP_V_VCC |
				VGSM_R_SIM_SETUP_V_3V |
				VGSM_R_SIM_SETUP_V_CLOCK_ME);

	/* Set LEDs */
	vgsm_outl(card, VGSM_R_LED_SRC,
		VGSM_R_LED_SRC_V_STATUS_G);

	vgsm_outl(card, VGSM_R_LED_USER,
		VGSM_R_LED_USER_V_STATUS_G);

	/* Enable interrupts */
	for(i=0; i<card->mes_number; i++) {
		if (card->modules[i]) {
			vgsm_outl(card, VGSM_R_ME_INT_ENABLE(i),
				VGSM_R_ME_INT_ENABLE_V_VDD |
				VGSM_R_ME_INT_ENABLE_V_VDDLP |
				VGSM_R_ME_INT_ENABLE_V_CCVCC |
				VGSM_R_ME_INT_ENABLE_V_DAI_RX_INT |
				VGSM_R_ME_INT_ENABLE_V_DAI_RX_END |
				VGSM_R_ME_INT_ENABLE_V_DAI_TX_INT |
				VGSM_R_ME_INT_ENABLE_V_DAI_TX_END |
				VGSM_R_ME_INT_ENABLE_V_UART_ASC0 |
				VGSM_R_ME_INT_ENABLE_V_UART_ASC1 |
				VGSM_R_ME_INT_ENABLE_V_UART_MESIM);

			vgsm_outl(card, VGSM_R_ME_FIFO_SETUP(i),
				VGSM_R_ME_FIFO_SETUP_V_RX_LINEAR |
				VGSM_R_ME_FIFO_SETUP_V_TX_LINEAR);
		}
	}

	for(i=0; i<card->sims_number; i++) {
		vgsm_outl(card, VGSM_R_SIM_INT_ENABLE(i),
			VGSM_R_SIM_INT_ENABLE_V_CCIN |
			VGSM_R_SIM_INT_ENABLE_V_UART);
	}

	vgsm_outl(card, VGSM_R_INT_ENABLE,
		(card->modules[0] ? VGSM_R_INT_ENABLE_V_ME(0) : 0) |
		(card->modules[1] ? VGSM_R_INT_ENABLE_V_ME(1) : 0) |
		(card->modules[2] ? VGSM_R_INT_ENABLE_V_ME(2) : 0) |
		(card->modules[3] ? VGSM_R_INT_ENABLE_V_ME(3) : 0) |
		VGSM_R_INT_ENABLE_V_SIM(0) |
		VGSM_R_INT_ENABLE_V_SIM(1) |
		VGSM_R_INT_ENABLE_V_SIM(2) |
		VGSM_R_INT_ENABLE_V_SIM(3));

	vgsm_msg(KERN_DEBUG, "VGSM card initialized\n");

	return 0;
}

static void vgsm_me_interrupt(struct vgsm_card *card, int id)
{
	struct vgsm_module *module = card->modules[id];
	u32 me_int_status = vgsm_inl(card, VGSM_R_ME_INT_STATUS(id));
	u32 me_status = vgsm_inl(card, VGSM_R_ME_STATUS(id));

	if (!module)
		return;

	if (me_int_status & VGSM_R_ME_INT_STATUS_V_UART_ASC0)
		vgsm_uart_interrupt(&module->asc0);

	if (me_int_status & VGSM_R_ME_INT_STATUS_V_UART_ASC1)
		vgsm_uart_interrupt(&module->asc1);

	if (me_int_status & VGSM_R_ME_INT_STATUS_V_UART_MESIM)
		vgsm_uart_interrupt(&module->mesim);

	if (me_int_status & VGSM_R_ME_INT_STATUS_V_VDD)
		vgsm_debug_card(card, 1,
			"ME VDD changed from: %d to %d\n",
			!(me_status & VGSM_R_ME_STATUS_V_VDD),
			!!(me_status & VGSM_R_ME_STATUS_V_VDD));

	if (me_int_status & VGSM_R_ME_INT_STATUS_V_VDDLP)
		vgsm_debug_card(card, 1,
			"ME VDDLP changed from: %d to %d\n",
			!(me_status & VGSM_R_ME_STATUS_V_VDDLP),
			!!(me_status & VGSM_R_ME_STATUS_V_VDDLP));

	if (me_int_status & VGSM_R_ME_INT_STATUS_V_CCVCC)
		vgsm_debug_card(card, 1,
			"ME CCVCC changed from: %d to %d\n",
			!(me_status & VGSM_R_ME_STATUS_V_VDD),
			!!(me_status & VGSM_R_ME_STATUS_V_VDD));

	if (me_int_status & VGSM_R_ME_INT_STATUS_V_DAI_RX_INT)
		vgsm_debug_card(card, 3, "DAI RX INT\n");

	if (me_int_status & VGSM_R_ME_INT_STATUS_V_DAI_RX_END)
		vgsm_debug_card(card, 3, "DAI RX END\n");

	if (me_int_status & VGSM_R_ME_INT_STATUS_V_DAI_TX_INT)
		vgsm_debug_card(card, 3, "DAI TX INT\n");

	if (me_int_status & VGSM_R_ME_INT_STATUS_V_DAI_TX_END)
		vgsm_debug_card(card, 3, "DAI TX END\n");
}

static void vgsm_sim_interrupt(struct vgsm_sim *sim)
{
	struct vgsm_card *card = sim->card;

	u32 sim_int_status = vgsm_inl(card, VGSM_R_SIM_INT_STATUS(sim->id));
	u32 sim_status = vgsm_inl(card, VGSM_R_SIM_STATUS(sim->id));

	if (sim_int_status & VGSM_R_SIM_INT_STATUS_V_UART)
		vgsm_uart_interrupt(&sim->uart);

	if (sim_int_status & VGSM_R_SIM_INT_STATUS_V_CCIN)
		vgsm_debug_card(card, 1,
			"SIM CCIN changed from: %d to %d\n",
			!(sim_status & VGSM_R_SIM_STATUS_V_CCIN),
			!!(sim_status & VGSM_R_SIM_STATUS_V_CCIN));
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19)
static irqreturn_t vgsm_interrupt(int irq, void *dev_id, struct pt_regs *regs)
#else
static irqreturn_t vgsm_interrupt(int irq, void *dev_id)
#endif
{
	struct vgsm_card *card = dev_id;
	u32 int_status;
	int i;

	if (unlikely(!card)) {
		vgsm_msg(KERN_CRIT,
			"spurious interrupt (IRQ %d)\n",
			irq);
		return IRQ_NONE;
	}

	int_status = vgsm_inl(card, VGSM_R_INT_STATUS);
	if (!int_status)
		return IRQ_NONE;

	for(i=0; i<card->mes_number; i++) {
		if (int_status & VGSM_R_INT_STATUS_V_ME(i))
			vgsm_me_interrupt(card, i);
	}

	for(i=0; i<card->sims_number; i++) {
		if (int_status & VGSM_R_INT_STATUS_V_SIM(i))
			vgsm_sim_interrupt(&card->sims[i]);
	}

	return IRQ_HANDLED;
}

void vgsm_card_update_router(struct vgsm_card *card)
{
	u32 sim_router = 0;
	int i;

	for(i=0; i<card->mes_number; i++) {
		if (card->modules[i])
			sim_router |= card->modules[i]->route_to_sim << (i*4);
	}

	vgsm_outl(card, VGSM_R_SIM_ROUTER, sim_router);
}

static void vgsm_card_release(struct kref *kref)
{
	struct vgsm_card *card = container_of(kref, struct vgsm_card, kref);

	kfree(card);
}

struct vgsm_card *vgsm_card_get(struct vgsm_card *card)
{
	if (card)
		kref_get(&card->kref);

	return card;
}

void vgsm_card_put(struct vgsm_card *card)
{
	kref_put(&card->kref, vgsm_card_release);
}

/*----------------------------------------------------------------------------*/

static ssize_t vgsm_show_serial_number(
	struct device *device,
	DEVICE_ATTR_COMPAT
	char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct vgsm_card *card = pci_get_drvdata(pci_dev);

	return snprintf(buf, PAGE_SIZE, "%08d\n", card->serial_number);
}

static DEVICE_ATTR(serial_number, S_IRUGO,
		vgsm_show_serial_number,
		NULL);

/*----------------------------------------------------------------------------*/

static ssize_t vgsm_show_mes_number(
	struct device *device,
	DEVICE_ATTR_COMPAT
	char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct vgsm_card *card = pci_get_drvdata(pci_dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", card->mes_number);
}

static DEVICE_ATTR(mes_number, S_IRUGO,
		vgsm_show_mes_number,
		NULL);

/*----------------------------------------------------------------------------*/

static ssize_t vgsm_show_sims_number(
	struct device *device,
	DEVICE_ATTR_COMPAT
	char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct vgsm_card *card = pci_get_drvdata(pci_dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", card->sims_number);
}

static DEVICE_ATTR(sims_number, S_IRUGO,
		vgsm_show_sims_number,
		NULL);

/*----------------------------------------------------------------------------*/

static ssize_t vgsm_show_hw_version(
	struct device *device,
	DEVICE_ATTR_COMPAT
	char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct vgsm_card *card = pci_get_drvdata(pci_dev);

	return snprintf(buf, PAGE_SIZE,
		"%d.%d.%d\n",
		(card->hw_version & 0x00ff0000) >> 16,
		(card->hw_version & 0x0000ff00) >>  8,
		(card->hw_version & 0x000000ff) >>  0);
}

static DEVICE_ATTR(hw_version, S_IRUGO,
		vgsm_show_hw_version,
		NULL);

/*----------------------------------------------------------------------------*/

static ssize_t vgsm_card_identify_attr_show(
	struct device *device,
	DEVICE_ATTR_COMPAT
	char *buf)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct vgsm_card *card = pci_get_drvdata(pci_dev);

	return snprintf(buf, PAGE_SIZE,
		"%d\n",
		test_bit(VGSM_CARD_FLAGS_IDENTIFY, &card->flags) ? 1 : 0);
}

static ssize_t vgsm_card_identify_attr_store(
	struct device *device,
	const char *buf,
	size_t count)
{
	struct pci_dev *pci_dev = to_pci_dev(device);
	struct vgsm_card *card = pci_get_drvdata(pci_dev);

	unsigned int value;
	if (sscanf(buf, "%02x", &value) < 1)
		return -EINVAL;

	if (value)
		set_bit(VGSM_CARD_FLAGS_IDENTIFY, &card->flags);
	else
		clear_bit(VGSM_CARD_FLAGS_IDENTIFY, &card->flags);

	schedule_delayed_work(&vgsm_identify_work, 0);

	return count;
}

static DEVICE_ATTR(identify, S_IRUGO | S_IWUSR,
		vgsm_card_identify_attr_show,
		vgsm_card_identify_attr_store);

/*----------------------------------------------------------------------------*/

static struct device_attribute *vgsm_card_attributes[] =
{
        &dev_attr_serial_number,
        &dev_attr_mes_number,
        &dev_attr_sims_number,
        &dev_attr_hw_version,
        &dev_attr_identify,
	NULL,
};

int vgsm_card_sysfs_create_files(
	struct vgsm_card *card)
{
	struct device_attribute **attr = vgsm_card_attributes;
	int err;

	while(*attr) {
		err = device_create_file(
			&card->pci_dev->dev,
			*attr);

		if (err < 0)
			goto err_create_file;

		attr++;
	}

	return 0;

err_create_file:

	while(*attr != *vgsm_card_attributes) {
		device_remove_file(
			&card->pci_dev->dev,
			*attr);

		attr--;
	}

	return err;
}

void vgsm_card_sysfs_delete_files(
	struct vgsm_card *card)
{
	struct device_attribute **attr = vgsm_card_attributes;

	while(*attr) {
		device_remove_file(
			&card->pci_dev->dev,
			*attr);

		attr++;
	}
}

struct vgsm_card *vgsm_card_create(
	struct vgsm_card *card,
	struct pci_dev *pci_dev,
	int id)
{
	BUG_ON(card); /* Static allocation not supported */

	if (!card) {
		card = kmalloc(sizeof(*card), GFP_KERNEL);
		if (!card)
			return NULL;
	}

	memset(card, 0, sizeof(*card));

	kref_init(&card->kref);

	card->pci_dev = pci_dev;
	card->id = id;

	spin_lock_init(&card->lock);

	return card;
}

void vgsm_card_destroy(struct vgsm_card *card)
{
	int i;

	for(i=card->mes_number-1; i>=0; i--) {
		if (card->modules[i])
			vgsm_module_destroy(card->modules[i]);
	}

	for(i=card->sims_number-1; i>=0; i--)
		vgsm_sim_destroy(&card->sims[i]);

	vgsm_card_put(card);
}

static int vgsm_card_asmi_waitbusy(struct vgsm_card *card)
{
	int i;

	if (vgsm_inl(card, VGSM_R_ASMI_STA) & VGSM_R_ASMI_STA_V_RUNNING) {

		for(i=0; i<100 && (vgsm_inl(card, VGSM_R_ASMI_STA) &
				VGSM_R_ASMI_STA_V_RUNNING); i++)
			udelay(5);

		for(i=0; i<500 && (vgsm_inl(card, VGSM_R_ASMI_STA) &
				VGSM_R_ASMI_STA_V_RUNNING); i++)
			msleep(10);

		if (i == 500)
			return -ETIMEDOUT;
	}

	return 0;
}

int vgsm_card_ioctl_fw_version(
	struct vgsm_card *card,
	unsigned int cmd,
	unsigned long arg)
{
	u32 version = vgsm_inl(card, VGSM_R_VERSION);

	return put_user(version, (int __user *)arg);
}

int vgsm_card_ioctl_fw_upgrade(
	struct vgsm_card *card,
	unsigned int cmd,
	unsigned long arg)
{
	struct vgsm2_fw_header fwh;
	int err;
	int i;

	if (copy_from_user(&fwh, (void *)arg, sizeof(fwh))) {
		err = -EFAULT;
		goto err_copy_from_user;
	}

	vgsm_msg_card(card, KERN_INFO,
		"Firmware programming started (%d bytes)...\n",
		fwh.size);

	for(i=0; i<0x60000; i+=0x10000) {
		vgsm_msg_card(card, KERN_INFO,
			"Erasing sector 0x%05x\n", i);

		vgsm_outl(card, VGSM_R_ASMI_ADDR, i);
		vgsm_outl(card, VGSM_R_ASMI_CTL,
				VGSM_R_ASMI_CTL_V_WREN |
				VGSM_R_ASMI_CTL_V_SECTOR_ERASE |
				VGSM_R_ASMI_CTL_V_START);

		err = vgsm_card_asmi_waitbusy(card);
		if (err < 0)
			goto err_timeout;
	}

	for(i=0; i<fwh.size; i++) {
		u8 b;

		if (copy_from_user(&b,
				(void *)(arg + sizeof(fwh) + i),
				sizeof(b))) {
			err = -EFAULT;
			goto err_copy_from_user_payload;
		}

		vgsm_outl(card, VGSM_R_ASMI_ADDR, i);
		vgsm_outl(card, VGSM_R_ASMI_CTL,
				VGSM_R_ASMI_CTL_V_WREN |
				VGSM_R_ASMI_CTL_V_WRITE |
				VGSM_R_ASMI_CTL_V_START |
				VGSM_R_ASMI_CTL_V_DATAIN(b));

		err = vgsm_card_asmi_waitbusy(card);
		if (err < 0)
			goto err_timeout;
	}

	vgsm_msg_card(card, KERN_INFO,
		"Firmware programming completed\n");

	set_bit(VGSM_CARD_FLAGS_RECONFIG_PENDING, &card->flags);

	return 0; 

err_timeout:
err_copy_from_user_payload:
err_copy_from_user:

	return err;
}

int vgsm_card_ioctl_fw_read(
	struct vgsm_card *card,
	unsigned int cmd,
	unsigned long arg)
{
	int err;
	int i;

	for(i=0; i<0x60000; i++) {
		u8 b;

		vgsm_outl(card, VGSM_R_ASMI_ADDR, i);
		vgsm_outl(card, VGSM_R_ASMI_CTL,
			VGSM_R_ASMI_CTL_V_RDEN |
			VGSM_R_ASMI_CTL_V_READ |
			VGSM_R_ASMI_CTL_V_START);

		vgsm_card_asmi_waitbusy(card);

		b = VGSM_R_ASMI_STA_V_DATAOUT(
				vgsm_inl(card, VGSM_R_ASMI_STA));

		if (copy_to_user((void __user *)(arg + i),
				&b, sizeof(b))) {
			err = -EFAULT;
			goto err_copy_to_user_payload;
		}
	}

	return i; 

err_copy_to_user_payload:

	return err;
}

int vgsm_card_probe(struct vgsm_card *card)
{
	int err;
	int i;

	union {
		__u32 serial;
		__u8 octets[4];
	} s;

	/* From here on vgsm_msg_card may be used */

	err = pci_enable_device(card->pci_dev);
	if (err < 0)
		goto err_pci_enable_device;

	pci_write_config_word(card->pci_dev, PCI_COMMAND, PCI_COMMAND_MEMORY);

	err = pci_request_regions(card->pci_dev, vgsm_DRIVER_NAME);
	if (err < 0) {
		vgsm_msg_card(card, KERN_CRIT,
			     "cannot request I/O memory region\n");
		goto err_pci_request_regions;
	}

	if (!card->pci_dev->irq) {
		vgsm_msg_card(card, KERN_CRIT,
			     "No IRQ assigned to card!\n");
		err = -ENODEV;
		goto err_noirq;
	}

	card->regs_bus_mem = pci_resource_start(card->pci_dev, 0);
	if (!card->regs_bus_mem) {
		vgsm_msg_card(card, KERN_CRIT,
			     "No IO memory assigned to card\n");
		err = -ENODEV;
		goto err_no_regs_base;
	}

	/* Note: rember to not directly access address returned by ioremap */
	card->regs_mem = ioremap(card->regs_bus_mem, 0x10000);

	if(!card->regs_mem) {
		vgsm_msg_card(card, KERN_CRIT,
			     "Cannot ioremap I/O memory\n");
		err = -ENODEV;
		goto err_ioremap_regs;
	}

	card->fifo_bus_mem = pci_resource_start(card->pci_dev, 1);
	if (!card->fifo_bus_mem) {
		vgsm_msg_card(card, KERN_CRIT,
			     "No FIFO memory assigned to card\n");
		err = -ENODEV;
		goto err_no_fifo_base;
	}

	/* Note: rember to not directly access address returned by ioremap */
	card->fifo_mem = ioremap(card->fifo_bus_mem, 0x10000);

	if(!card->fifo_mem) {
		vgsm_msg_card(card, KERN_CRIT,
			     "Cannot ioremap FIFO memory\n");
		err = -ENODEV;
		goto err_ioremap_fifo;
	}

	/* Requesting IRQ */
	err = request_irq(card->pci_dev->irq, &vgsm_interrupt,
			  SA_SHIRQ, vgsm_DRIVER_NAME, card);
	if (err < 0) {
		vgsm_msg_card(card, KERN_CRIT,
			     "unable to register irq\n");
		goto err_request_irq;
	}

	{
	__u32 r_info = vgsm_inl(card, VGSM_R_INFO);
	card->sims_number = (r_info & 0x000000f0) >> 4;
	card->mes_number = (r_info & 0x0000000f) >> 0;
	}

	card->hw_version = vgsm_inl(card, VGSM_R_VERSION);

	if (card->sims_number > 8) {
		err = -EINVAL;
		goto err_sims_number_invalid;
	}

	if (card->mes_number > 8) {
		err = -EINVAL;
		goto err_mes_number_invalid;
	}

	vgsm_msg_card(card, KERN_INFO,
		"vGSM-II card found at %#0lx\n",
		card->regs_bus_mem);

	vgsm_msg_card(card, KERN_INFO,
		"HW version: %d.%d.%d\n",
		(card->hw_version & 0x00ff0000) >> 16,
		(card->hw_version & 0x0000ff00) >>  8,
		(card->hw_version & 0x000000ff) >>  0);

	for(i=0; i<sizeof(s);i++) {
		vgsm_outl(card, VGSM_R_ASMI_ADDR, 0x70000 + i);
		vgsm_outl(card, VGSM_R_ASMI_CTL,
			VGSM_R_ASMI_CTL_V_RDEN |
			VGSM_R_ASMI_CTL_V_READ |
			VGSM_R_ASMI_CTL_V_START);

		vgsm_card_asmi_waitbusy(card);

		s.octets[i] = VGSM_R_ASMI_STA_V_DATAOUT(
				vgsm_inl(card, VGSM_R_ASMI_STA));

	}

	card->serial_number = s.serial;

	if (card->serial_number)
		vgsm_msg_card(card, KERN_INFO,
			"Serial number: %08d\n", card->serial_number);

	vgsm_msg_card(card, KERN_INFO,
		"GSM module sockets: %d\n",
		card->mes_number);

	vgsm_msg_card(card, KERN_INFO,
		"SIM sockets: %d\n",
		card->sims_number);

	for(i=0; i<card->mes_number; i++) {

		u32 me_status = vgsm_inl(card, VGSM_R_ME_STATUS(i));

		if (me_status & VGSM_R_ME_STATUS_V_VDDLP) {

			u32 fifo_size = vgsm_inl(card, VGSM_R_ME_FIFO_SIZE(i));

			char tmpstr[8];
			snprintf(tmpstr, sizeof(tmpstr), "gsm%d", i);

			card->modules[i] = vgsm_module_create(
						NULL, card, i, tmpstr,
						VGSM_FIFO_RX_BASE(i),
						VGSM_R_ME_FIFO_SIZE_V_RX_SIZE(fifo_size),
						VGSM_FIFO_TX_BASE(i),
						VGSM_R_ME_FIFO_SIZE_V_TX_SIZE(fifo_size),
						VGSM_ME_ASC0_BASE(i),
						VGSM_ME_ASC1_BASE(i),
						VGSM_ME_SIM_BASE(i));
			if (!card->modules[i]) {
				err = -ENOMEM;
				goto err_module_create;
			}

			vgsm_msg_card(card, KERN_INFO,
				"Module %d is installed and powered %s\n", i,
				vgsm_module_power_get(card->modules[i]) ? "ON" : "OFF");

			vgsm_msg_card(card, KERN_INFO,
				"Module %d RX_FIFO=0x%04x TX_FIFO=%04x\n", i,
				VGSM_R_ME_FIFO_SIZE_V_RX_SIZE(fifo_size),
				VGSM_R_ME_FIFO_SIZE_V_TX_SIZE(fifo_size));
		} else {
			vgsm_msg_card(card, KERN_INFO,
				"Module %d is not installed\n", i);
		}
	}

	for(i=0; i<card->sims_number; i++)
		vgsm_sim_create(&card->sims[i], card, i, VGSM_SIM_UART_BASE(i));

	vgsm_initialize_hw(card);

	set_bit(VGSM_CARD_FLAGS_READY, &card->flags);

	return 0;

err_module_create:
	for(i=card->mes_number-1; i>=0; i--) {
		if (card->modules[i])
			vgsm_module_destroy(card->modules[i]);
	}
err_mes_number_invalid:
err_sims_number_invalid:
	free_irq(card->pci_dev->irq, card);
err_request_irq:
	iounmap(card->fifo_mem);
err_ioremap_fifo:
err_no_fifo_base:
	iounmap(card->regs_mem);
err_ioremap_regs:
err_no_regs_base:
err_noirq:
	pci_release_regions(card->pci_dev);
err_pci_request_regions:
err_pci_enable_device:

	return err;
}

void vgsm_card_remove(struct vgsm_card *card)
{
//	int i;
	int shutting_down = FALSE;

	/* Clean up any allocated resources and stuff here */

	vgsm_msg_card(card, KERN_INFO,
		"shutting down card at %p.\n", card->regs_mem);

	set_bit(VGSM_CARD_FLAGS_SHUTTING_DOWN, &card->flags);

#if 0
		vgsm_card_lock(card);
		vgsm_module_send_power_get(card->modules[i]);
		vgsm_card_unlock(card);

		wait_for_completion_timeout(
			&card->modules[i].read_status_completion, 2 * HZ);

		if (test_bit(VGSM_MODULE_STATUS_ON,
						&card->modules[i].status)) {

			/* Force an emergency shutdown if the application did
			 * not do its duty
			 */

			vgsm_card_lock(card);
			vgsm_module_send_onoff(&card->modules[i],
				VGSM_CMD_MAINT_ONOFF_EMERG_OFF);
			vgsm_card_unlock(card);

			shutting_down = TRUE;

			vgsm_msg_card(card, KERN_NOTICE,
				"Module %d has not been shut down, forcing"
				" emergency shutdown\n",
				card->modules[i].id);
		}
	}
#endif

	if (shutting_down) {
#if 0

		msleep(3200);

		for(i=0; i<card->num_modules; i++) {
			vgsm_card_lock(card);
			vgsm_module_send_onoff(&card->modules[i], 0);
			vgsm_card_unlock(card);
		}
#endif
	}

	clear_bit(VGSM_CARD_FLAGS_READY, &card->flags);

	/* Disable IRQs */
	vgsm_outl(card, VGSM_R_INT_ENABLE, 0);

	if (test_bit(VGSM_CARD_FLAGS_RECONFIG_PENDING, &card->flags)) {

		vgsm_msg_card(card, KERN_INFO,
			"Reconfiguring FPGA. PCI rescan or reboot required\n");

		vgsm_outl(card,
			VGSM_R_SERVICE,
			VGSM_R_SERVICE_V_RECONFIG);
	}

	pci_write_config_word(card->pci_dev, PCI_COMMAND, 0);

	/* Free IRQ */
	free_irq(card->pci_dev->irq, card);

	/* Unmap */
	iounmap(card->fifo_mem);
	iounmap(card->regs_mem);

	pci_release_regions(card->pci_dev);

	pci_disable_device(card->pci_dev);
}

int vgsm_card_register(struct vgsm_card *card)
{
	int err;
	int i;

	spin_lock(&vgsm_cards_list_lock);
	list_add_tail(&card->cards_list_node, &vgsm_cards_list);
	spin_unlock(&vgsm_cards_list_lock);

	for (i=0; i<card->sims_number; i++) {
		err = vgsm_sim_register(&card->sims[i]);
		if (err < 0)
			goto err_register_sim;
	}

	for (i=0; i<card->mes_number; i++) {
		if (card->modules[i]) {
			err = vgsm_module_register(card->modules[i]);
			if (err < 0)
				goto err_module_register;
		}
	}

	err = vgsm_card_sysfs_create_files(card);
	if (err < 0)
		goto err_card_sysfs_create_files;

	return 0;

	vgsm_card_sysfs_delete_files(card);
err_card_sysfs_create_files:
err_register_sim:
	for(--i; i>=0; i--)
		vgsm_sim_unregister(&card->sims[i]);
err_module_register:
	for(--i; i>=0; i--) {
		if (card->modules[i])
			vgsm_module_unregister(card->modules[i]);
	}

	return err;
}

void vgsm_card_unregister(struct vgsm_card *card)
{
	int i;

	vgsm_card_sysfs_delete_files(card);

	spin_lock(&vgsm_cards_list_lock);
	list_del(&card->cards_list_node);
	spin_unlock(&vgsm_cards_list_lock);

	for(i=card->sims_number-1; i>=0; i--)
		vgsm_sim_unregister(&card->sims[i]);

	for(i=card->mes_number-1; i>=0; i--) {
		if (card->modules[i])
			vgsm_module_unregister(card->modules[i]);
	}

}

int __init vgsm_card_modinit(void)
{
	return 1;
}

void __exit vgsm_card_modexit(void)
{
}

