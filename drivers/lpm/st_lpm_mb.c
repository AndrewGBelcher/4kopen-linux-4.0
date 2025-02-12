/*
 * This driver implements communication with internal Standby Controller
 * over mailbox interface in some STMicroelectronics devices.
 *
 * Copyright (C) 2014 STMicroelectronics Limited.
 *
 * Contributor:Francesco Virlinzi <francesco.virlinzi@st.com>
 * Author:Pooja Agarwal <pooja.agarwal@st.com>
 * Author:Udit Kumar <udit-dlh.kumar@st.com>
 * Author:Sudeep Biswas <sudeep.biswas@st.com>
 *
 * May be copied or modified under the terms of the GNU General Public
 * License Version 2.0 only.  See linux/COPYING for more information.
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/firmware.h>
#include <linux/elf.h>
#include <asm/unaligned.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/power/st_lpm.h>
#include <linux/power/st_lpm_def.h>
#include <linux/of_platform.h>
#include <linux/debugfs.h>

static const struct st_lpm_wkup_dev_name wkup_dev_name_tab[] = {
	{ST_LPM_WAKEUP_IR, "ir"},
	{ST_LPM_WAKEUP_CEC, "cec"},
	{ST_LPM_WAKEUP_FRP, "frp"},
	{ST_LPM_WAKEUP_WOL, "wol"},
	{ST_LPM_WAKEUP_RTC, "rtc"},
	{ST_LPM_WAKEUP_ASC, "asc"},
	{ST_LPM_WAKEUP_NMI, "nmi"},
	{ST_LPM_WAKEUP_HPD, "hpd"},
	{ST_LPM_WAKEUP_PIO, "pio"},
	{ST_LPM_WAKEUP_EXT, "ext"},
	{ST_LPM_WAKEUP_CUST, "cust"}
};

static const struct st_lpm_trace_module_names
			trace_module_tab[MAX_TRACE_MODULE_NUM] = {
	{ST_LPM_TRACE_IR, "IR"},
	{ST_LPM_TRACE_CEC, "CEC"},
	{ST_LPM_TRACE_PWM, "PWM"},
	{ST_LPM_TRACE_WOL, "WOL"},
	{ST_LPM_TRACE_RTC, "RTC"},
	{ST_LPM_TRACE_ASC, "ASC"},
	{ST_LPM_TRACE_TIMER, "TIMER"},
	{ST_LPM_TRACE_CUSTOM, "CUSTOM"},
	{ST_LPM_TRACE_PIO, "PIO"},
	{ST_LPM_TRACE_I2C, "I2C"},
	{ST_LPM_TRACE_EDID, "EDID"},
	{ST_LPM_TRACE_KEYSCN, "KEYSCN"},
	{ST_LPM_TRACE_SPI, "SPI"},
	{ST_LPM_TRACE_GENERAL, "GENERAL"},
};

enum lpm_services {
	LPM_FW_RELOAD,
	LPM_CUST_FEAT,
	LPM_EDID,
	LPM_SBC_TRACES,
	LPM_SBC_TRACES_IN_SUSPEND
};

static struct st_lpm_version lpm_fw_ver_vs_services[] = {
	 /* Since LPM Fw v1.2.0 */
	[LPM_FW_RELOAD] = {.major_soft = 1, .minor_soft = 2, .patch_soft = 0},
	 /* Since LPM Fw v1.4.0 */
	[LPM_CUST_FEAT] = {.major_soft = 1, .minor_soft = 4, .patch_soft = 0},
	/* Since LPM Fw v1.4.2 */
	[LPM_EDID] = {.major_soft = 1, .minor_soft = 4,	.patch_soft = 2},
	/* Since LPM Fw v1.8.0 */
	[LPM_SBC_TRACES] = {.major_soft = 1, .minor_soft = 8, .patch_soft = 0},
	/* Since LPM Fw v1.8.1 */
	[LPM_SBC_TRACES_IN_SUSPEND] = {
		.major_soft = 1, .minor_soft = 8, .patch_soft = 1},
};

static bool lpm_check_fw_version(struct st_lpm_version *fw_ver,
				 enum lpm_services service)
{
	struct st_lpm_version *service_version;

	service_version = &lpm_fw_ver_vs_services[service];

	if (fw_ver->major_soft > service_version->major_soft)
		return true;
	else if (fw_ver->major_soft < service_version->major_soft)
		return false;
	else
		if (fw_ver->minor_soft > service_version->minor_soft)
			return true;
		else if (fw_ver->minor_soft < service_version->minor_soft)
			return false;
		else
			if (fw_ver->patch_soft > service_version->patch_soft)
				return true;
			else if (fw_ver->patch_soft <
					service_version->patch_soft)
				return false;
			else
				return true;
}

/**
 * st_lpm_driver_data - Local struct of driver
 * @iomem_base[2]:		memory region mapped by driver
 *				iomem_base[0] is SBC program and data
 *				base address
 *				size of iomem_base[0] is 0xA0000
 *				iomem_base[1] is SBC mailbox address
 *				size of iomem_base[1] is 0x400
 *				iomem_base[2] is SBC config reg mapped in
 *				sysconf
 *
 * @fw_reply_msg:		reply message from firmware
 * @fw_request_msg:		firmware request message
 * @fw_name:			Name of firmware
 * @reply_from_sbc:		reply from SBC, true in case reply received
 * @wait_queue:			wait queue
 * @mutex:			message protection mutex
 * @glbtransaction_id:		global transaction id used in communication
 * @sbc_state:			State of SBC firmware
 * @power_on_gpio:		the gpio pin number assinged by gpio lib for
 *				power on/off pin
 * @trigger_level:		the trigger level for power on/off pin
 * @pmem_offset:		the progam section of the f/w to be loaded here
 *				this is different for different chip.
 *				Read from DT
 * @dev:			the device object. Required to store here for
 *				using in dev_*
 */
struct st_lpm_driver_data {
	void * __iomem iomem_base[3];
	struct lpm_message fw_reply_msg;
	struct lpm_message fw_request_msg;
	char fw_name[30];
	int reply_from_sbc;
	wait_queue_head_t wait_queue;
	struct mutex mutex;
	unsigned char glbtransaction_id;
	enum st_lpm_sbc_state sbc_state;
	unsigned int power_on_gpio;
	unsigned int trigger_level;
	unsigned int pmem_offset;
	unsigned int const *data_ptr;
	struct notifier_block wakeup_cb;
	u16 wakeup_bitmap;
	struct mutex fwlock;
	bool fwstatus;
	struct device *dev;
	struct st_lpm_version fw_ver;
	const struct firmware *fw;
	u16 trace_data_value;
};


/* To write 32 bit data into LPM */
#define lpm_write32(drv, index, offset, value)	iowrite32(value,	\
			(drv)->iomem_base[index] + offset)

/* To read 32 bit data into LPM */
#define lpm_read32(drv, idx, offset)	ioread32(			\
			(drv)->iomem_base[idx] + offset)

/*
 * For internal standby controller
 * Various offset of LPM IP, These offsets are w.r.t LPM memory resources.
 * There are three LPM memory resources used
 * first is for SBC PMEM,
 * second is for SBC mailbox,
 * third is for SBC configuration registers.
 */
/* Marker in elf file to indicate writing to program area */
#define SBC_PRG_MEMORY_ELF_MARKER	0x00400000

/* SBC mailbox offset as seen by Host w.r.t mem source 2 */
#define SBC_MBX_OFFSET		0x0

/* SBC configuration register offset as seen on Host w.r.t mem source 3 */
#define SBC_CONFIG_OFFSET	0x0

/*
 * Mailbox registers to be written by host,
 * SBC firmware will read below registers to get host message.
 * There are four such registers in mailbox each of 4 bytes.
 */
#define MBX_WRITE_STATUS(i)	(SBC_MBX_OFFSET + (i) * 0x4)

/*
 * Mailbox registers to be read by host,
 * SBC firmware will write below registers to send message.
 * There are four such registers in mailbox each of 4 bytes.
 */
#define MBX_READ_STATUS(i)	(SBC_MBX_OFFSET + 0x100 + (i) * 0x4)

/* To clear mailbox interrupt status */
#define MBX_READ_CLR_STATUS1	(SBC_MBX_OFFSET + 0x144)

/* To enable/disable mailbox interrupt on Host :RW */
#define MBX_INT_ENABLE		(SBC_MBX_OFFSET + 0x164)
/* To enable mailbox interrupt on Host : WO only set allowed */
#define MBX_INT_SET_ENABLE	(SBC_MBX_OFFSET + 0x184)

/* To disable mailbox interrupt on Host : WO only clear allowed */
#define MBX_INT_CLR_ENABLE	(SBC_MBX_OFFSET + 0x1A4)

/*
 * From host there are three type of message can be send to SBC
 * No reply expected from SBC i.e. reset SBC, Passive standby
 * Reply is expected from SBC i.e. get version etc.
 * Reply is expected but interrupts are disabled
 */
#define SBC_REPLY_NO		0x0
#define SBC_REPLY_YES		0x1
#define SBC_REPLY_NO_IRQ	0x2

/* Value to be set by SBC-Fw in EXIT_CPS register when exiting form CPS */
#define CPS_EXIT_EXIT_CPS	0x9b

/* Value to be set by SBC-Fw in EXIT_CPS register when exiting other than CPS */
#define DEFAULT_EXIT_CPS	0xb8

static int lpm_config_reboot(enum st_lpm_config_reboot_type type, void *data)
{
	struct st_lpm_adv_feature feature;

	feature.feature_name = ST_LPM_SET_EXIT_CPS;

	switch (type) {
	case ST_LPM_REBOOT_WITH_DDR_SELF_REFRESH:
	/*
	 * Exit_CPS:
	 * [7:1] == 7b1001101:
	 *	DDRs in self-refresh
	 * [0] == 1b1:
	 *	DDRs in self-refresh (back compatibility)
	 */
		feature.params.set_params[0] = CPS_EXIT_EXIT_CPS;
		break;

	case ST_LPM_REBOOT_WITH_DDR_OFF:
	/*
	 * Exit_CPS:
	 * [9:8] == 2b11:
	 *	DDRs are powered off
	 */
		feature.params.set_params[0] = DEFAULT_EXIT_CPS;
		break;
	}

	st_lpm_set_adv_feature(1, &feature);

	return 0;
}

static void lpm_configure_power_on_gpio(struct st_lpm_driver_data *lpm_drv)
{
	int err = 0;

	if (lpm_drv->power_on_gpio >= 0) {
		struct st_lpm_pio_setting pio_setting = {
			.pio_bank = lpm_drv->power_on_gpio / 8,
			.pio_pin = lpm_drv->power_on_gpio % 8,
			.pio_level = lpm_drv->trigger_level ?
				ST_LPM_PIO_LOW : ST_LPM_PIO_HIGH,
			.pio_use = ST_LPM_PIO_POWER,
			.pio_direction = 1,
			.interrupt_enabled = 0,
			.pio_disable = 0,
			.stdbyorwake = 0,
			.ctrlpiolevel = 0,
		};

		err = st_lpm_setup_pio(&pio_setting);
		if (err < 0)
			dev_warn(lpm_drv->dev,
				"Setup power_on gpio failed: err = %d\n", err);
	}
}

static int lpm_exchange_msg(struct lpm_message *command,
	struct lpm_message *response, void *private_data);

/*
 * lpm_setup_tracedata - Set trace data parameters
 * @trace_modules - peripherals for which trace data to be enabled
 *
 * Return - 0 on success
 * Return - negative error code on failure.
 */
static int lpm_setup_tracedata(u16 trace_modules, void *data)
{
	struct st_lpm_driver_data *lpm_drv = data;
	struct lpm_message command = {0};
	struct lpm_message response = {0};
	struct st_lpm_version *service_version;

	if (!lpm_check_fw_version(&lpm_drv->fw_ver, LPM_SBC_TRACES)) {
		service_version = &lpm_fw_ver_vs_services[LPM_SBC_TRACES];

		dev_warn(lpm_drv->dev,
			 "This LPM Fw does not support SBC traces service\n");
		dev_warn(lpm_drv->dev,
			 "SBC traces service available from LPM Fw v%d.%d.%d\n",
			 service_version->major_soft,
			 service_version->minor_soft,
			 service_version->patch_soft);

		return -EINVAL;
	}

	lpm_drv->trace_data_value = trace_modules;

	command.command_id = LPM_MSG_TRACE_DATA;
	command.buf[0] = trace_modules & 0xFF;
	command.buf[1] = (trace_modules >> 8) & 0xFF;

	return lpm_exchange_msg(&command, &response, lpm_drv);
}

static void lpm_sec_init(struct st_lpm_driver_data *lpm_drv)
{
	struct st_lpm_version driver_ver;
	int err = 0;
	const char *acg_calib;

	dev_dbg(lpm_drv->dev, "SBC Firmware is running\n");

	/* Print firmware version information */
	err = st_lpm_get_version(&driver_ver, &lpm_drv->fw_ver);

	if (err >= 0) {
		dev_info(lpm_drv->dev, "LPM: firmware ver %d.%d.%d",
			 lpm_drv->fw_ver.major_soft,
			 lpm_drv->fw_ver.minor_soft,
			 lpm_drv->fw_ver.patch_soft);
		dev_info(lpm_drv->dev,
			 "(%02d-%02d-20%02d)\n", lpm_drv->fw_ver.day,
			 lpm_drv->fw_ver.month, lpm_drv->fw_ver.year);
	}

	/*
	 * Request default value of EXIT_CPS, to be set while
	 * exiting from non-CPS warm reset cases, such as
	 * manual reset exit, WDT exit, shutdown exit.
	 */
	lpm_config_reboot(ST_LPM_REBOOT_WITH_DDR_OFF, lpm_drv);

	lpm_configure_power_on_gpio(lpm_drv);

	if (!of_property_read_string(lpm_drv->dev->of_node, "st,acg_calib",
			&acg_calib) && !strcmp("off", acg_calib))
		st_lpm_setup_rtc_calibration_time(0);

#ifdef CONFIG_GENERAL_SBC_TRACES
	lpm_setup_tracedata(ST_LPM_TRACE_GENERAL, lpm_drv);
#endif
}


/**
 * lpm_isr() - Mailbox ISR
 * @this_irq:	irq
 * @params:	Parameters
 *
 * This ISR is invoked when there is some message from SBC.
 * Message could be reply or some request.
 * If this a request then such message will be posted to a work queue.
 */
static irqreturn_t lpm_isr(int this_irq, void *params)
{
	struct st_lpm_driver_data *lpm_drv =
		(struct st_lpm_driver_data *)params;
	u32 msg_read[4], i;
	struct lpm_message *msg, *msg_p;

	/*
	 * Read the data from mailbox
	 * SBC will always be in little endian mode
	 * if host is in big endian then reverse int
	 */
	lpm_drv->reply_from_sbc = 0;

	for (i = 0; i < 4; i++) {
		msg_read[i] = lpm_read32(lpm_drv, 1, MBX_READ_STATUS(1 + i));
		msg_read[i] = cpu_to_le32(msg_read[i]);
		}
	/* Copy first message to check if it's reply from SBC or request */
	msg_p = (struct lpm_message *)msg_read;

	if (msg_p->command_id == LPM_MSG_INFORM_HOST) {
		if (msg_p->buf[0] == ST_LPM_ALARM_TIMER) {
			st_lpm_notify(ST_LPM_RTC);

			/* Clear mail box */
			lpm_write32(lpm_drv, 1, MBX_READ_CLR_STATUS1,
					0xFFFFFFFF);

			/* No more action needed in this case
			 * SBC is not waiting or requesting anything
			 */
			return IRQ_HANDLED;
		}
	}

	/* Check if reply from SBC or request from SBC */
	if ((msg_p->command_id & LPM_MSG_REPLY) ||
			(msg_p->command_id == LPM_MSG_BKBD_READ)) {
		msg = &lpm_drv->fw_reply_msg;
		lpm_drv->reply_from_sbc = 1;
	} else {
		msg = &lpm_drv->fw_request_msg;
	}

	/* Copy mailbox data into local structure */
	memcpy(msg, &msg_read, 16);

	/* Clear mail box */
	lpm_write32(lpm_drv, 1, MBX_READ_CLR_STATUS1, 0xFFFFFFFF);

	if (lpm_drv->reply_from_sbc != 1)
		return IRQ_WAKE_THREAD;

	wake_up_interruptible(&lpm_drv->wait_queue);

	return IRQ_HANDLED;
}

static irqreturn_t lpm_threaded_isr(int this_irq, void *params)
{
	struct st_lpm_driver_data *lpm_drv =
		(struct st_lpm_driver_data *)params;
	int err = 0;
	struct lpm_message command;
	struct lpm_message *msg_p;
	char *buf = command.buf;
	u8 length;
	u32 offset;
	char data[128];

	msg_p = &lpm_drv->fw_request_msg;
	dev_dbg(lpm_drv->dev, "Send reply to firmware\nrecd command id %x\n",
		msg_p->command_id);

	switch (msg_p->command_id) {
	case LPM_MSG_VER:
		/* In case firmware requested driver version*/
		buf[0] = (LPM_MAJOR_PROTO_VER << 4) | LPM_MINOR_PROTO_VER;
		buf[1] = (LPM_MAJOR_SOFT_VER << 4) | LPM_MINOR_SOFT_VER;
		buf[2] = (LPM_PATCH_SOFT_VER << 4) | LPM_BUILD_MONTH;
		buf[3] = LPM_BUILD_DAY;
		buf[4] = LPM_BUILD_YEAR;
		command.command_id = LPM_MSG_VER | LPM_MSG_REPLY;
		break;
	case LPM_MSG_INFORM_HOST:
		if (msg_p->buf[0] == ST_LPM_LONG_PIO_PRESS) {
			err = st_lpm_notify(ST_LPM_FP_PIO_PRESS);
			if (err >= 0) {
				memcpy(&command.buf[2], &err, 4);
				command.command_id = LPM_MSG_INFORM_HOST |
					LPM_MSG_REPLY;
				break;
			} else {
				dev_dbg(lpm_drv->dev,
					"PIO Reset callback error reported (%d)\n",
					err);
			}
		}

		if (msg_p->buf[0] == ST_LPM_ALARM_TIMER)
			return IRQ_HANDLED;

		if (msg_p->buf[0] == ST_LPM_TRACE_DATA) {
			length = msg_p->buf[1];
			offset = get_unaligned_le32(&(msg_p->buf[2]));
			memset(data, 0, 128);
			memcpy_fromio(data, lpm_drv->iomem_base[0] +
					DATA_OFFSET + offset, length);
			dev_info(lpm_drv->dev, "SBC f/w traces:%s\n", data);
			return IRQ_HANDLED;
		}
	default:
		/* Send reply to SBC as error*/
		buf[0] = msg_p->command_id;
		buf[1] = -EINVAL;
		command.command_id = LPM_MSG_ERR;
		break;
	}

	command.transaction_id = msg_p->transaction_id;

	lpm_exchange_msg(&command, NULL, lpm_drv);
	msg_p->command_id = 0;
	return IRQ_HANDLED;
}

/**
 * lpm_send_msg() - Send mailbox message
 * @msg:	message pointer
 * @msg_size:	message size
 *
 * Return - 0 if firmware is running
 * Return - -EREMOTEIO either firmware is not loaded or not running
 */
static int lpm_send_msg(struct st_lpm_driver_data *lpm_drv,
	struct lpm_message *msg, unsigned char msg_size)
{
	int err = 0, count;
	u32 *tmp_i = (u32 *)msg;

	/* Check if firmware is loaded or not */
	if (!(lpm_drv->sbc_state == ST_LPM_SBC_RUNNING ||
	      lpm_drv->sbc_state == ST_LPM_SBC_BOOT))
		return -EREMOTEIO;

	/*
	 * Write data to mailbox, covert data into LE format.
	 * also mailbox is 4 byte deep, we need to write 4 byte always
	 *
	 * First byte of message is used to generate interrupt as well as
	 * serve as command id.
	 * Therefore first four byte of message part are written at last.
	 */
	for (count = (msg_size + 1) / 4; count >= 0; count--) {
		*(tmp_i + count) = cpu_to_le32(*(tmp_i + count));
		lpm_write32(lpm_drv, 1, (MBX_WRITE_STATUS(1 + count)),
				    *(tmp_i + count));
	}

	return err;
}

/**
 * lpm_get_response() - To get SBC response
 * @response:	response of SBC
 *
 * This function is to get SBC response in polling mode
 * This will be called when interrupts are disabled and we
 * still need to get response from SBC.
 *
 * Return - 0 on success
 * Return - 1 when SBC firmware is not responding
 */
static int lpm_get_response(struct st_lpm_driver_data *lpm_drv,
	struct lpm_message *response)
{
	int count, i;
	u32 msg_read1[4];

	/* Poll time of 1 Second is good enough to see SBC reply */
	for (count = 0; count < 100; count++) {
		msg_read1[0] = lpm_read32(lpm_drv, 1, MBX_READ_STATUS(1));
		msg_read1[0] = cpu_to_le32(msg_read1[0]);
		/* If we received a reply then break the loop */
		if (msg_read1[0] & 0xFF)
			break;
		mdelay(10);
	}

	/* If no reply within 1 second then firmware is not responding */
	if (count == 100) {
		dev_err(lpm_drv->dev, "count %d value %x\n",
				count, msg_read1[0]);
		return 1;
	}

	/* Get other data from mailbox */
	for (i = 1; i < 4; i++) {
		msg_read1[i] = lpm_read32(lpm_drv, 1, MBX_READ_STATUS(1 + i));
		msg_read1[i] = cpu_to_le32(msg_read1[i]);
	}
	/* Copy data received from mailbox*/
	memcpy(&lpm_drv->fw_reply_msg, (void *)msg_read1, 16);
	lpm_write32(lpm_drv, 1, MBX_READ_CLR_STATUS1, 0xFFFFFFFF);

	return 0;
}

/**
 * lpm_exchange_msg() - Internal function  used for message exchange with SBC
 * @send_msg:	message to send
 * @response:	response from SBC firmware
 *
 * This function can be called in three contexts
 * One when reply from SBC is expected for this command
 * Second when reply from SBC is not expected
 * Third when called from interrupt disabled but reply is expected
 *
 * Return - 0 on success
 * Return - negative error code on failure.
*/
static int lpm_exchange_msg(struct lpm_message *command,
	struct lpm_message *response, void *data)
{
	struct st_lpm_driver_data *lpm_drv = data;
	struct lpm_message lpm_msg = {0};
	int err = 0, data_size;
	int reply_type =  SBC_REPLY_YES;

	dev_dbg(lpm_drv->dev, "%s\n", __func__);

	/*
	 * Lock the mailbox, prevent other caller to access MB write
	 * In case API is called with interrupt disabled from Linux PM
	 * try to lock mutex.
	 */
	if (irqs_disabled()) {
		err = mutex_trylock(&lpm_drv->mutex);
		reply_type = SBC_REPLY_NO_IRQ;
		if (!err)
			return -EAGAIN;
	} else {
		mutex_lock(&lpm_drv->mutex);
		if (response == NULL)
			reply_type = SBC_REPLY_NO;
	}

	lpm_msg.command_id = command->command_id;

	if (lpm_msg.command_id & LPM_MSG_REPLY || command->transaction_id)
		lpm_msg.transaction_id = command->transaction_id;
	else
		lpm_msg.transaction_id = lpm_drv->glbtransaction_id++;

	/* Copy data into mailbox message */
	data_size = st_lpm_get_command_data_size(command->command_id);
	if (data_size)
		memcpy(lpm_msg.buf, command->buf, data_size);

	/* Print message information for debug purpose */
	dev_dbg(lpm_drv->dev, "Sending msg {%x, %x}\n", lpm_msg.command_id,
		lpm_msg.transaction_id);

	lpm_drv->reply_from_sbc = 0;

	/* Send message to mailbox write */
	err = lpm_send_msg(lpm_drv, &lpm_msg, data_size);

	if (unlikely(err < 0)) {
		dev_err(lpm_drv->dev, "firmware not loaded\n");
		goto exit_fun;
	}

	switch (reply_type) {
	case SBC_REPLY_NO_IRQ:
		err = lpm_get_response(lpm_drv, response);
		if (err) /* f/w timed out to give a response */
			err = -ETIMEDOUT;
		break;
	case  SBC_REPLY_YES:
		/*
		 * wait for response here
		 * In case of signal, we can get negative value
		 * In such case wait till timeout or response from SBC
		 */
		do {
			err = wait_event_interruptible_timeout(
				lpm_drv->wait_queue,
				lpm_drv->reply_from_sbc == 1,
				msecs_to_jiffies(100));
		} while (err < 0);

		if (err == 0) /* means, wait ended because of timeout */
			err = -ETIMEDOUT;
		/*
		 * if err is major than 0, this means condition evaluated to
		 * true before timeout occurred. But that means there is no
		 * error.
		 */
		if (err > 0)
			err = 0;
		break;
	case SBC_REPLY_NO:
		goto exit_fun;
		break;
	}

	dev_dbg(lpm_drv->dev, "recd reply  %x {%x, %x }\n", err,
		lpm_drv->fw_reply_msg.command_id,
	lpm_drv->fw_reply_msg.transaction_id);

	if (unlikely(err ==  -ETIMEDOUT)) {
		dev_err(lpm_drv->dev, "f/w is not responding\n");
		err = -EAGAIN;
		goto exit_fun;
	}

	BUG_ON(!(lpm_drv->fw_reply_msg.command_id & LPM_MSG_REPLY));

	memcpy(response, &lpm_drv->fw_reply_msg,
	       sizeof(struct lpm_message));

	if (lpm_msg.transaction_id == lpm_drv->fw_reply_msg.transaction_id) {
		if (response->command_id == LPM_MSG_ERR) {
			dev_err(lpm_drv->dev,
				"Firmware error code %d\n", response->buf[1]);
			/*
			 * Firmware does not support this command
			 * therefore firmware gave error.
			 * In such cases, return EREMOTEIO as firmware error
			 * code is not yet decided.
			 * To Do
			 * conversion of firmware error code into Linux world
			 */
			err = -EREMOTEIO;
		}
		/* There is possibility we might get response for large msg. */
		if (response->command_id == LPM_MSG_BKBD_READ)
			dev_dbg(lpm_drv->dev, "Got in reply a big message\n");
	} else {
		/*
		 * Different trans id is expected from SBC as big messages are
		 * encapsulated into LPM_MSG_BKBD_WRIRE message.
		 */
		dev_dbg(lpm_drv->dev,
			"Received ID %x\n", response->transaction_id);
	}

exit_fun:
	mutex_unlock(&lpm_drv->mutex);

	return err;
}

/**
 * lpm_send_big_message() - To send big message over SBC DMEM
 * @size:	size of message
 * @offset:	offset
 * @sbc_msg:	buffer pointer
 *
 * This function is used to send large messages(>LPM_MAX_MSG_DATA)
 * using SBC DMEM.
 *
 * Return - 0 on success
 * Return - negative error code on failure.
 */
static int lpm_write_bulk(u16 size, const char *sbc_msg, void *data,
			  int *offset_dmem)
{
	struct st_lpm_driver_data *lpm_drv = data;
	int err = 0;
	unsigned int offset;
	struct lpm_message command = {
		.command_id = LPM_MSG_LGWR_OFFSET,
	};
	struct lpm_message response = {0};

	if (!offset_dmem) {
		put_unaligned_le16(size, command.buf);

		err = lpm_exchange_msg(&command, &response, lpm_drv);

		if (err)
			return err;

		/* Get the offset in SBC memory */
		offset = get_unaligned_le32(&response.buf[2]);
	} else {
		offset = *offset_dmem;
	}

	/* Copy message in SBC memory */
	if (size == sizeof(unsigned long))
		writel(*((unsigned long *)sbc_msg),
		       lpm_drv->iomem_base[0] + DATA_OFFSET + offset);
	else
		memcpy_toio(lpm_drv->iomem_base[0] + DATA_OFFSET + offset,
			    sbc_msg, size);

	if (!offset_dmem) {
		/* Send this big message */
		put_unaligned_le16(size, command.buf);
		put_unaligned_le32(offset, &command.buf[2]);
		command.command_id = LPM_MSG_BKBD_WRITE;
		return lpm_exchange_msg(&command, &response, lpm_drv);
	}

	return 0;
}

static int lpm_read_bulk(u16 size, u16 offset, char *msg, void *data)
{
	struct st_lpm_driver_data *lpm_drv = data;

	memcpy_fromio(msg,
		offset + DATA_OFFSET + lpm_drv->iomem_base[0], size);

	return 0;
}

/**
 * lpm_write_edid_info() - To send edid info over SBC DMEM
 * @edid_buf:	EDID info buffer pointer
 * @block_num:	Block number to read
 * @data:	pointer the the st_lpm_driver_data structure
 *
 * This function is used to send EDID info message to LPM
 * using SBC DMEM.
 *
 * Return - 0 on success
 * Return - negative error code on failure.
*/
static int lpm_write_edid_info(u8 *edid_buf, u8 block_num, void *data)
{
	struct st_lpm_driver_data *lpm_drv = data;
	int err = 0;
	u32 offset;
	struct lpm_message command = {0};
	struct lpm_message response = {0};

	if (!lpm_check_fw_version(&lpm_drv->fw_ver, LPM_EDID)) {
		dev_warn(lpm_drv->dev,
			 "This LPM Fw does not support EDID service\n");
		return -EINVAL;
	}

	if (block_num > ST_LPM_EDID_MAX_BLK_NUM) {
		dev_err(lpm_drv->dev, "Block number is out of range\n");
		return -EINVAL;
	}

	/* Request an offset to copy EDID info to SBC */
	command.command_id = LPM_MSG_LGWR_OFFSET;
	put_unaligned_le16(ST_LPM_EDID_BLK_SIZE, command.buf);

	err = lpm_exchange_msg(&command, &response, lpm_drv);
	if (err)
		return err;

	offset = get_unaligned_le32(&response.buf[2]);

	/* Copy message in SBC memory */
	memcpy_toio(lpm_drv->iomem_base[0] + DATA_OFFSET + offset,
		    (const void *)edid_buf, ST_LPM_EDID_BLK_SIZE);

	/* Inform SBC EDID Info are available for copy
	 * to EDID Info SBC DMEM dedicated space
	 */
	command.command_id = LPM_MSG_EDID_INFO,
	command.buf[0] = ST_LPM_EDID_BLK_SIZE;
	command.buf[1] = block_num;
	put_unaligned_le32(offset, &command.buf[2]);
	err = lpm_exchange_msg(&command, &response, lpm_drv);
	if (err || response.buf[0] != 0 ||
	    response.buf[1] != command.buf[1])
		return -EIO;

	/* Send the 0xFF block number to indicates end of EDID info */
	command.command_id = LPM_MSG_EDID_INFO,
	command.buf[0] = ST_LPM_EDID_BLK_SIZE;
	command.buf[1] = ST_LPM_EDID_BLK_END;
	command.buf[2] = 0;
	err = lpm_exchange_msg(&command, &response, lpm_drv);
	if (err || response.buf[0] != 0 ||
	    response.buf[1] != ST_LPM_EDID_BLK_END)
		return -EIO;

	return 0;
}

/**
 * lpm_read_edid_info() - To send edid info over SBC DMEM
 * @edid_buf:	EDID info buffer pointer
 * @block_num:	Block number to read
 * @data:	Pointer to the st_lpm_driver_data structure
 *
 * This function is used to request EDID info message from LPM
 * REMARK: As EDID read will return 4 * 128Byte of data,
 * edid_buf should point on a memory zone big enough to store
 * these EDID info data.
 *
 * Return - 0 on success
 * Return - negative error code on failure.
*/
static int lpm_read_edid_info(u8 *edid_buf, u8 block_num, void *data)
{
	struct st_lpm_driver_data *lpm_drv = data;
	int err = 0;
	u32 offset;
	struct lpm_message command = {0};
	struct lpm_message response = {0};

	if (!lpm_check_fw_version(&lpm_drv->fw_ver, LPM_EDID)) {
		dev_warn(lpm_drv->dev,
			 "This LPM Fw does not support EDID service\n");
		return -EINVAL;
	}

	if (block_num > ST_LPM_EDID_MAX_BLK_NUM) {
		dev_err(lpm_drv->dev, "Block number is out of range\n");
		return -EINVAL;
	}

	/* Send the EDID message to inform SBC EDID info are available */
	command.command_id = LPM_MSG_EDID_INFO,
	command.buf[0] = 0;
	command.buf[1] = block_num;
	err = lpm_exchange_msg(&command, &response, lpm_drv);
	if (err || response.buf[0] != ST_LPM_EDID_BLK_SIZE ||
	    response.buf[1] != command.buf[1])
		return -EIO;
	offset = get_unaligned_le32(&response.buf[2]);
	memcpy_fromio(edid_buf, lpm_drv->iomem_base[0] + DATA_OFFSET + offset,
		      ST_LPM_EDID_BLK_SIZE);

	return 0;
}

/**
 * lpm_reload_fw_prepare() - To prepare SBC for Fw reload
 * @data:	Pointer to the st_lpm_driver_data structure
 *
 * This function is used to prepare SBC in order to reload its Fw
 *
 * Return - 0 on success
 * Return - negative error code on failure.
*/
static int lpm_reload_fw_prepare(void *data)
{
	struct st_lpm_driver_data *lpm_drv = data;
	struct st_lpm_adv_feature feature;
	int err = 0, i = 0;
	u32 power_status;

	if (!lpm_check_fw_version(&lpm_drv->fw_ver, LPM_FW_RELOAD)) {
		dev_warn(lpm_drv->dev, "FW reload not supported\n");
		return -EOPNOTSUPP;
	}

	dev_dbg(lpm_drv->dev, "Go for SBC IDLE\n");

	mutex_lock(&lpm_drv->fwlock);

	/* Put SBC in Idle mode for reload operation */
	feature.feature_name = ST_LPM_SBC_IDLE;
	feature.params.set_params[0] = 0x55;
	feature.params.set_params[1] = 0xAA;

	st_lpm_set_adv_feature(1, &feature);

	/* Wait till SBC is in IDLE */
	do {
		power_status = readl(lpm_drv->iomem_base[2] + LPM_POWER_STATUS);
	} while (((power_status & XP70_IDLE_MODE) == 0) && (i++ < 1000));

	if (power_status == 0) {
		dev_info(lpm_drv->dev, "Failed to put SBC in Idle\n");
		mutex_unlock(&lpm_drv->fwlock);
		return -ETIMEDOUT;
	}

	dev_dbg(lpm_drv->dev, "SBC is in IDLE\n");

	/* Exit SBC from Idle mode to load new Fw data */
	writel(BIT(1), lpm_drv->iomem_base[2]);

	i = 0;

	/* Wait SBC comes out of Idle */
	do {
		power_status = readl(lpm_drv->iomem_base[2] + LPM_POWER_STATUS);
	} while (((power_status & XP70_IDLE_MODE) != 0) && (i++ < 1000));

	if (power_status != 0) {
		dev_info(lpm_drv->dev, "SBC not ready for new Fw\n");
		mutex_unlock(&lpm_drv->fwlock);
		return -ETIMEDOUT;
	}

	lpm_drv->fwstatus = false;

	mutex_unlock(&lpm_drv->fwlock);

	dev_dbg(lpm_drv->dev, "SBC is ready for new Fw\n");

	return err;
}

/**
 * lpm_start_loaded_fw() - To start SBC for loaded Fw
 * @data:	Pointer to the st_lpm_driver_data structure
 *
 * This function is used to start SBC after a Fw has been loaded
 *
 * Return - 0 on success
 * Return - negative error code on failure.
*/
static int lpm_start_loaded_fw(void *data)
{
	struct st_lpm_driver_data *lpm_drv = data;
	int err, i;

	dev_info(lpm_drv->dev, "Booting SBC Firmware...\n");

	mutex_lock(&lpm_drv->fwlock);

	writel(3, lpm_drv->iomem_base[2]);
	mdelay(10);
	writel(1, lpm_drv->iomem_base[2]);

	lpm_drv->sbc_state = ST_LPM_SBC_BOOT;
	lpm_write32(lpm_drv, 1, MBX_INT_SET_ENABLE, 0xFF);

	/* get response from firmware */
	for (i = 0; i < 10; i++) {
		mdelay(100);
		err = st_lpm_get_fw_state(&lpm_drv->sbc_state);
		if (err == 0 && lpm_drv->sbc_state == ST_LPM_SBC_RUNNING)
			break;
	}

	if (err) {
		dev_err(lpm_drv->dev, "Failed to start SBC Fw\n");
		mutex_unlock(&lpm_drv->fwlock);
		return err;
	}

	lpm_drv->fwstatus = true;

	lpm_sec_init(lpm_drv);

	st_lpm_notify(ST_LPM_GPIO_WAKEUP);

	mutex_unlock(&lpm_drv->fwlock);

	dev_info(lpm_drv->dev, "SBC Fw booted.\n");

	return 0;
}

static int lpm_offset_dmem(enum st_lpm_sbc_dmem_offset_type type, void *data)
{
	struct st_lpm_driver_data *lpm_drv = data;

	if (!lpm_drv->data_ptr)
		return -EINVAL;

	return  lpm_drv->data_ptr[type];
}

static void lpm_ir_enable(bool enable, void *data)
{
	struct st_lpm_driver_data *lpm_drv = data;
	u32 lpm_config_1 = lpm_read32(lpm_drv, 2, LPM_CONFIG_1_OFFSET);

	if (enable)
		lpm_config_1 |= IRB_ENABLE;
	else
		lpm_config_1 &= ~IRB_ENABLE;

	lpm_write32(lpm_drv, 2, LPM_CONFIG_1_OFFSET, lpm_config_1);
}

static struct st_lpm_ops st_lpm_mb_ops = {
	.exchange_msg = lpm_exchange_msg,
	.write_bulk = lpm_write_bulk,
	.read_bulk = lpm_read_bulk,
	.get_dmem_offset = lpm_offset_dmem,
	.config_reboot = lpm_config_reboot,
	.ir_enable = lpm_ir_enable,
	.write_edid = lpm_write_edid_info,
	.read_edid = lpm_read_edid_info,
	.reload_fw_prepare = lpm_reload_fw_prepare,
	.start_loaded_fw = lpm_start_loaded_fw,
	.setup_tracedata = lpm_setup_tracedata,
};

static struct of_device_id lpm_child_match_table[] = {
	{ .compatible = "st,rtc-sbc", },
	{ .compatible = "st,wakeup-pins", },
	{ }
};

#ifndef CONFIG_SBC_FW_LOADED_BY_PBL
static int lpm_load_fw(struct st_lpm_driver_data *lpm_drv)
{
	int i;
	struct elf64_hdr *ehdr;
	struct elf64_phdr *phdr;
	const u8 *elf_data;

	signed long offset;
	unsigned long size, add;
	int dmem_offset;

	const struct firmware *fw = lpm_drv->fw;

	if (unlikely(!lpm_drv->fw)) {
		dev_info(lpm_drv->dev, "LPM Firmware not found.");
		dev_info(lpm_drv->dev, "Controller Passive Standby not operational.");
		dev_info(lpm_drv->dev, "Use sysfs FW trigger to load and activate LPM Firmware\n");
		return -EINVAL;
	}

	dev_info(lpm_drv->dev, "SBC Fw %s Found, Loading ...\n",
		 lpm_drv->fw_name);

	mutex_lock(&lpm_drv->fwlock);

	/**
	 * Now load the different sections of the elf to the appropriate device
	 * addresses.
	 */
	elf_data = fw->data;
	ehdr = (struct elf64_hdr *)elf_data;
	phdr = (struct elf64_phdr *)(elf_data + ehdr->e_phoff);

	dmem_offset = lpm_drv->data_ptr[ST_SBC_DMEM_PEN_HOLDING_VAR];

	for (i = 0; i < ehdr->e_phnum; i++, phdr++) {
		if (phdr->p_type != PT_LOAD)
			continue;

		if (phdr->p_paddr != SBC_PRG_MEMORY_ELF_MARKER) {
			size =  phdr->p_memsz;
			add = phdr->p_paddr;
			if (add < dmem_offset && (dmem_offset < (add + size))) {
				dmem_offset += DMEM_PEN_HOLDING_CODE_SZ;
				offset = DATA_OFFSET + dmem_offset;
				memcpy_toio(lpm_drv->iomem_base[0] + offset,
					elf_data + phdr->p_offset + dmem_offset,
					size - dmem_offset);
				continue;
			}
			offset = DATA_OFFSET + phdr->p_paddr;
		} else
			offset = lpm_drv->pmem_offset;

		size = phdr->p_memsz;

		memcpy_toio(lpm_drv->iomem_base[0] + offset,
			    elf_data + phdr->p_offset, size);
	}

	mutex_unlock(&lpm_drv->fwlock);

	dev_info(lpm_drv->dev, "SBC Fw %s loaded.\n", lpm_drv->fw_name);
	return 0;
}

static int lpm_firmware_cb(const struct firmware *fw,
		struct st_lpm_driver_data *lpm_drv)
{
	int err = 1;

	lpm_drv->fw = fw;

	err = lpm_load_fw(lpm_drv);
	if (err >= 0)
		err = lpm_start_loaded_fw(lpm_drv);

	if (err >= 0)
		of_platform_populate(lpm_drv->dev->of_node,
				     lpm_child_match_table,
				     NULL, lpm_drv->dev);

	release_firmware(lpm_drv->fw);
	return err;
}
#endif

static ssize_t st_lpm_show_version(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct st_lpm_version driver_ver;
	struct st_lpm_version fw_ver;
	int err = 0;

	dev_dbg(dev, "SBC st_lpm_show_version\n");

	/* Print firmware version information */
	err = st_lpm_get_version(&driver_ver, &fw_ver);

	if (err < 0)
		return 0;

	return snprintf(buf, PAGE_SIZE, "%d.%d.%d (%02d-%02d-20%02d)\n",
			 fw_ver.major_soft,
			 fw_ver.minor_soft,
			 fw_ver.patch_soft,
			 fw_ver.day,
			 fw_ver.month,
			 fw_ver.year);
}


static ssize_t st_lpm_show_wakeup(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int i;
	char data[2] = {0, 0};
	char wkup_dev_name[50] = "";
	enum st_lpm_wakeup_devices wakeup_device = 0;
	s16 ValidSize;

	if (st_lpm_get_wakeup_device(&wakeup_device) < 0)
		dev_err(dev, "<%s> firmware not responding\n", __func__);

	if (ST_LPM_WAKEUP_PIO & wakeup_device)
		if (st_lpm_get_wakeup_info(ST_LPM_WAKEUP_PIO, &ValidSize,
					   2, data) < 0)
			dev_err(dev, "<%s> trigger data message failed\n",
					__func__);

	for (i = 0; i < sizeof(wkup_dev_name_tab) /
			sizeof(struct st_lpm_wkup_dev_name); i++) {
		if (wkup_dev_name_tab[i].index & wakeup_device) {
			snprintf(wkup_dev_name, sizeof(wkup_dev_name) - 1, "%s",
				 wkup_dev_name_tab[i].name);
		}
	}

	return snprintf(buf, PAGE_SIZE, "{%s} 0x%x 0x%x\n", wkup_dev_name,
			data[0], data[1]);
}

#ifndef CONFIG_SBC_FW_LOADED_BY_PBL
static ssize_t st_lpm_load_fw(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct st_lpm_driver_data *lpm_drv = dev_get_drvdata(dev);
	int ret;
	bool first_load = true;

	ret = snprintf(lpm_drv->fw_name, count, "%s", buf);

	BUG_ON(ret >= sizeof(lpm_drv->fw_name));

	dev_info(lpm_drv->dev, "About to load new firmware (%s)\n",
			 lpm_drv->fw_name);

	/* Check if fw exits before switching SBC to Idle Mode */
	ret = request_firmware(&lpm_drv->fw, lpm_drv->fw_name, lpm_drv->dev);
	if (ret) {
		dev_warn(lpm_drv->dev, "LPM Fw %s not found, abort\n",
			lpm_drv->fw_name);
		goto exit;
	}

	if (lpm_drv->fwstatus) {
		ret = lpm_reload_fw_prepare(lpm_drv);
		if (ret < 0)
			goto exit;

		first_load = false;
	}

	ret = lpm_load_fw(lpm_drv);
	if (ret >= 0)
		ret = lpm_start_loaded_fw(lpm_drv);

	if (ret >= 0) {
		if (first_load)
			of_platform_populate(lpm_drv->dev->of_node,
					     lpm_child_match_table,
					     NULL, lpm_drv->dev);

		ret = count;
	}

exit:
	release_firmware(lpm_drv->fw);
	return ret;
}

static DEVICE_ATTR(loadfw, S_IWUSR, NULL, st_lpm_load_fw);
#endif

#ifdef CONFIG_DEBUG_FS
static int lpm_trace_data_config_show(struct seq_file *s, void *unused)
{
	struct st_lpm_driver_data *lpm_drv = s->private;
	int i;

	dev_info(lpm_drv->dev, "Current trace config:\n");
	for (i = 0; i < 16; i++) {
		if (lpm_drv->trace_data_value & trace_module_tab[i].index)
			dev_info(lpm_drv->dev, "%s\n",
				 trace_module_tab[i].name);
	}

	return 0;
}

static int lpm_trace_data_config_open(struct inode *inode, struct file *file)
{
	return single_open(file, lpm_trace_data_config_show, inode->i_private);
}

static ssize_t lpm_trace_data_config_write(struct file *file,
					   const char __user *ubuf,
					   size_t count, loff_t *ppos)
{
	struct seq_file	*s = file->private_data;
	struct st_lpm_driver_data *lpm_drv = s->private;
	int ret;
	unsigned int trace_module;

	ret = sscanf(ubuf, "0x%X", &trace_module);

	if (!ret)
		dev_info(lpm_drv->dev, "Format should be 0xABCD\n");

	/* Mask TIMER traces as it produces SBC Fw not responding */
	lpm_setup_tracedata(trace_module & ~ST_LPM_TRACE_TIMER, lpm_drv);

	return count;
}

static const struct file_operations lpm_trace_data_config_fops = {
	.open			= lpm_trace_data_config_open,
	.write			= lpm_trace_data_config_write,
	.read			= seq_read,
	.llseek			= seq_lseek,
	.release		= single_release,
};
#endif

static DEVICE_ATTR(wakeup, S_IRUGO, st_lpm_show_wakeup, NULL);
static DEVICE_ATTR(version, S_IRUGO, st_lpm_show_version, NULL);

static int st_sbc_get_devtree_pdata(struct device *dev,
				    struct st_sbc_platform_data *pdata,
				    struct st_lpm_driver_data *lpm_drv)
{
	int result;
	struct device_node *node;
	enum of_gpio_flags flags;
	const char *fw_name;

	node = dev->of_node;
	if (node == NULL)
		return -ENODEV;

	memset(pdata, 0, sizeof(*pdata));

	if (!of_find_property(node, "gpios", NULL)) {
		dev_err(dev, "LPM without power-on-gpio\n");
		return -ENODEV;
	}

	result = of_get_gpio_flags(node, 0, &flags);
	if (result < 0)
		return result;

	pdata->gpio = (unsigned int)result;
	pdata->trigger_level = !(flags & OF_GPIO_ACTIVE_LOW);

	if (of_property_read_string(node, "st,fw_name", &fw_name))
		return -ENODEV;

	result = snprintf(lpm_drv->fw_name, sizeof(lpm_drv->fw_name),
			  "%s", fw_name);

	BUG_ON(result >= sizeof(lpm_drv->fw_name));

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static void st_lpm_check_wakeup_device(
	struct st_lpm_driver_data *lpm_drv,
	struct device *dev, unsigned int enable)
{
	int bit = 0;

	pr_info("sti: -> device %s as wakeup %s\n", dev_name(dev),
		(enable ? "enabled" : "disabled"));

	if (strstr(dev_name(dev), "rc"))
		bit = ST_LPM_WAKEUP_IR;
	else if (strstr(dev_name(dev), "dwmac"))
		bit = ST_LPM_WAKEUP_WOL;
	else if (strstr(dev_name(dev), "st-keyscan"))
		bit = ST_LPM_WAKEUP_FRP;
	else if (strstr(dev_name(dev), "ttyAS"))
		bit = ST_LPM_WAKEUP_ASC;
	else if (strstr(dev_name(dev), "rtc_sbc"))
		bit = ST_LPM_WAKEUP_RTC;
	else if (strstr(dev_name(dev), "stm_cec"))
		bit = ST_LPM_WAKEUP_CEC;
	else if (strstr(dev_name(dev), "gpio_keys"))
		bit = ST_LPM_WAKEUP_PIO;
	else if (strstr(dev_name(dev), "st_wakeup_pin.pio"))
		bit = ST_LPM_WAKEUP_PIO;
	else if (strstr(dev_name(dev), "st_wakeup_pin.ext_it"))
		bit = ST_LPM_WAKEUP_EXT;
	else if (strstr(dev_name(dev), "hdmi"))
		bit = ST_LPM_WAKEUP_HPD;
	else {
		dev_dbg(lpm_drv->dev,
			"%s: device wakeup not managed via lpm\n",
			dev_name(dev));
		return;
	}

	if (enable)
		lpm_drv->wakeup_bitmap |= bit;
	else
		lpm_drv->wakeup_bitmap &= ~bit;
}

static int sti_wakeup_notifier(struct notifier_block *nb,
	unsigned long action, void *data)
{
	struct st_lpm_driver_data *lpm_drv =
		container_of(nb, struct st_lpm_driver_data, wakeup_cb);
	struct device *dev = data;

	switch (action) {
	case WAKEUP_SOURCE_ADDED:
	case WAKEUP_SOURCE_REMOVED:
		st_lpm_check_wakeup_device(lpm_drv, dev,
			action == WAKEUP_SOURCE_ADDED ? 1UL : 0UL);
		break;
	}

	return NOTIFY_OK;
}
#else
static inline int sti_wakeup_notifier(struct notifier_block *nb,
	unsigned long action, void *data)
{
	return NOTIFY_OK;
}
#endif

static int st_lpm_probe(struct platform_device *pdev)
{
	struct st_lpm_driver_data *lpm_drv;
	const struct st_sbc_platform_data *pdata = pdev->dev.platform_data;
	struct st_sbc_platform_data alt_pdata;
	struct resource *res;
	int err = 0;
	int count = 0;
	u32 confreg;
	unsigned long offset_dtu, offset_penholding;
#ifdef CONFIG_DEBUG_FS
	struct dentry *root;
#endif

	dev_dbg(&pdev->dev, "st lpm probe\n");

	lpm_drv = devm_kzalloc(&pdev->dev, sizeof(struct st_lpm_driver_data),
				GFP_KERNEL);
	if (unlikely(lpm_drv == NULL)) {
		dev_err(lpm_drv->dev, "%s: Request memory failed\n", __func__);
		return -ENOMEM;
	}

	lpm_drv->dev = &pdev->dev;
	if (!pdata) {
		err = st_sbc_get_devtree_pdata(&pdev->dev, &alt_pdata, lpm_drv);
		if (err)
			return err;
		pdata = &alt_pdata;
	}

	lpm_drv->data_ptr = of_match_node(pdev->dev.driver->of_match_table,
			 pdev->dev.of_node)->data;

	lpm_drv->power_on_gpio = pdata->gpio;
	lpm_drv->trigger_level = pdata->trigger_level;

	err = devm_gpio_request(&pdev->dev, lpm_drv->power_on_gpio,
				"lpm_power_on_gpio");
	if (err) {
		dev_err(lpm_drv->dev, "%s: gpio_request failed\n", __func__);
		return err;
	}

	err = gpio_direction_output(lpm_drv->power_on_gpio,
			lpm_drv->trigger_level);
	if (err) {
		dev_err(lpm_drv->dev, "%s: gpio_direction_output failed\n",
				__func__);
		return err;
	}

	for (count = 0; count < 3; count++) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, count);
		if (!res)
			return -ENODEV;

		dev_dbg(lpm_drv->dev,
			"mem:SBC res->start %x %x\n", res->start, res->end);
		if (!devm_request_mem_region(&pdev->dev, res->start,
					     resource_size(res), "st-lpm")) {
			dev_err(lpm_drv->dev, "%s: Request mem 0x%x region failed\n",
			       __func__, res->start);
			return -ENOMEM;
		}

		lpm_drv->iomem_base[count] = devm_ioremap_nocache(&pdev->dev,
							res->start,
							resource_size(res));
		if (!lpm_drv->iomem_base[count]) {
			dev_err(lpm_drv->dev, "%s: Request iomem 0x%x region failed\n",
			       __func__, (unsigned int)res->start);
			return -ENOMEM;
		}
		dev_dbg(lpm_drv->dev,
			"lpm_add %x\n", (u32)lpm_drv->iomem_base[count]);
	}

	/* Read the pmem_offset from DT */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "pmem");
	if (!res)
		return -ENODEV;

	lpm_drv->pmem_offset = res->start;

	/* Irq request */
	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(lpm_drv->dev, "%s irq resource %x not provided\n",
			__func__, res->start);
		return -ENODEV;
	}

	/* Semaphore initialization */
	init_waitqueue_head(&lpm_drv->wait_queue);
	mutex_init(&lpm_drv->mutex);

	mutex_init(&lpm_drv->fwlock);

	/*
	 * Work struct init
	 * lpm does not need dedicate work queue
	 * use default queue
	 */
	if (devm_request_threaded_irq(&pdev->dev, res->start, lpm_isr,
		lpm_threaded_isr, IRQF_NO_SUSPEND,
		"st-lpm", (void *)lpm_drv) < 0) {
		dev_err(lpm_drv->dev, "%s: Request stlpm irq not done\n",
			__func__);
		return -ENODEV;
	}

	/*set driver specific data */
	platform_set_drvdata(pdev, lpm_drv);

	/*
	 * Program Mailbox for interrupt enable
	 */
	lpm_write32(lpm_drv, 1, MBX_INT_SET_ENABLE, 0xFF);
	lpm_drv->sbc_state = ST_LPM_SBC_BOOT;

	/* register wakeup notifier */
	lpm_drv->wakeup_cb.notifier_call = sti_wakeup_notifier;
	wakeup_source_notifier_register(&lpm_drv->wakeup_cb);

	st_lpm_set_ops(&st_lpm_mb_ops, (void *)lpm_drv);

	/* Check whether the f/w is  running */
	confreg = readl(lpm_drv->iomem_base[2]);
	if (confreg & BIT(0)) { /* SBC Fw is booted */
		err = st_lpm_get_fw_state(&lpm_drv->sbc_state);

		if (!err && lpm_drv->sbc_state == ST_LPM_SBC_RUNNING) {
			lpm_drv->fwstatus = true;
			lpm_sec_init(lpm_drv);
			of_platform_populate(lpm_drv->dev->of_node,
					     lpm_child_match_table,
					     NULL, lpm_drv->dev);
		} else {
			dev_err(lpm_drv->dev,
				"SBC Fw is not responding... Abort\n");
			dev_info(lpm_drv->dev,
				"Controller Passive Standby not operational.");
			dev_info(lpm_drv->dev,
				"Use sysfs FW trigger to load and activate LPM Firmware\n");
		}
	} else {
#ifdef CONFIG_SBC_FW_LOADED_BY_PBL
		dev_dbg(lpm_drv->dev, "SBC Firmware loaded but not booted\n");
		err = lpm_start_loaded_fw(lpm_drv);
		if (err) {
			dev_err(lpm_drv->dev, "Failed to start SBC Fw\n");
			return err;
		}

		of_platform_populate(lpm_drv->dev->of_node,
				     lpm_child_match_table,
				     NULL, lpm_drv->dev);

#else
		dev_dbg(lpm_drv->dev, "SBC Firmware loading requested\n");
		/* Initiate the loading of the device f/w */
		err = request_firmware_nowait(THIS_MODULE, 1, lpm_drv->fw_name,
					      &pdev->dev, GFP_KERNEL,
					      (struct st_lpm_driver_data *)
					      lpm_drv,
					      (void *)lpm_firmware_cb);
		if (err)
			return err;
#endif
	}

	err = device_create_file(&pdev->dev, &dev_attr_wakeup);
	if (err) {
		dev_err(&pdev->dev, "Cannot create wakeup sysfs entry\n");
		return err;
	}

	err = device_create_file(&pdev->dev, &dev_attr_version);
	if (err) {
		dev_err(&pdev->dev, "Cannot create version sysfs entry\n");
		return err;
	}

#ifndef CONFIG_SBC_FW_LOADED_BY_PBL
	err = device_create_file(&pdev->dev, &dev_attr_loadfw);
	if (err) {
		dev_err(&pdev->dev, "Cannot create loadfw sysfs entry\n");
		return err;
	}
#endif

#ifdef CONFIG_DEBUG_FS
	root = debugfs_create_dir(dev_name(&pdev->dev), NULL);
	if (!root)
		return -ENOMEM;

	if (!debugfs_create_file("trace_config", S_IRUGO | S_IWUSR, root,
				 lpm_drv, &lpm_trace_data_config_fops))
		return -ENOMEM;
#endif
	offset_dtu = st_lpm_get_dmem_offset(ST_SBC_DMEM_DTU);
	offset_penholding = st_lpm_get_dmem_offset(ST_SBC_DMEM_PEN_HOLDING_VAR);
	if (!(offset_dtu < 0 || offset_penholding < 0
		|| offset_penholding < offset_dtu))
		memset(lpm_drv->iomem_base[0] + DATA_OFFSET + offset_dtu,
			0,
			offset_penholding - offset_dtu);

	return 0;
}

/**
 * st_lpm_remove() - To free used resources
 * @pdev:	device pointer
 * Return code 0
 */

static int  st_lpm_remove(struct platform_device *pdev)
{
	struct st_lpm_driver_data *lpm_drv =
		platform_get_drvdata(pdev);

	dev_dbg(lpm_drv->dev, "st_lpm_remove\n");

	return 0;
}

/*
 * data array storing chip specific SBC-DMEM offsets
 * currently defined only for 407 family SoCs
 * [0] = offset of dtu buffer address in SBC-DMEM
 * [1] = offset of CPS Tag in SBC-DMEM
 * [2] = offset of pen holding variable in SBC-DMEM
 */
unsigned int stih407_family_lpm_data[] = {0x94, 0x84, 0xa4};
unsigned int stih418_lpm_data[] = {0x98, 0x88, 0xb8};

static struct of_device_id st_lpm_match[] = {
	{
		.compatible = "st,stih418-lpm",
		.data = stih418_lpm_data,
	},
	{
		.compatible = "st,stih407-family-lpm",
		.data = stih407_family_lpm_data,
	},
	{
		.compatible = "st,lpm",
		.data = NULL,
	},
	{},
};
MODULE_DEVICE_TABLE(of, st_lpm_match);

#ifdef IR_WAKEUP_KEY_SET
static void st_lpm_setup_ir_init(void)
{
	static struct st_lpm_ir_keyinfo ir_key[2] = {
	[0] = {
		.ir_id = 8,
		.time_period = 560,
		.tolerance = 30,
		.ir_key.key_index = 0x0,
		.ir_key.num_patterns = 33,
		.ir_key.fifo = {
			[0] = {
				.mark = 8960,
				.symbol = 13440,
			},
			[1 ... 8] = {
				.mark = 560,
				.symbol = 1120,
			},
			[9 ... 15] = {
				.mark = 560,
				.symbol = 560 * 4,
			},
			[16 ... 24] = {
				.mark = 560,
				.symbol = 1120,
			},
			[25 ... 32] = {
				.mark = 560,
				.symbol = 560 * 4,
			},
			[33] = {
				.mark = 560,
				.symbol = 560 * 2,
			},
		},
	},

	[1] = {
		.ir_id = 0x7,
		.time_period = 900,
		.tolerance = 30,
		.ir_key.key_index = 0x2,
		.ir_key.num_patterns = 10,
		.ir_key.fifo = {
			[0] = {
				.mark = 900,
				.symbol = 1800,
			},
			[1] = {
				.mark = 1800,
				.symbol = 2700,
			},
			[2] = {
				.mark = 900,
				.symbol = 2700,
			},
			[3] = {
				.mark = 1800,
				.symbol = 2700,
			},
			[4 ... 6] = {
				.mark = 900,
				.symbol = 1800,
			},
			[7] = {
				.mark = 900,
				.symbol = 2700,
			},
			[8] = {
				.mark = 900,
				.symbol = 1800,
			},
			[9] = {
				.mark = 1800,
				.symbol = 2700,
			},
		},
		},
	};
	static int st_lpm_setup_ir_done_once;

	if (st_lpm_setup_ir_done_once)
		return;
	/*
	 * Work around for IR Red key wakeup
	 * This workaround will be removed, once
	 * proper IR module is written and loaded
	 */
	st_lpm_setup_ir(ARRAY_SIZE(ir_key), ir_key);
	st_lpm_setup_ir_done_once = 1;
}
#else
static void st_lpm_setup_ir_init(void)
{
}
#endif

#ifdef CONFIG_PM_SLEEP
static int st_lpm_pm_suspend(struct device *dev)
{
	struct st_lpm_driver_data *lpm_drv = dev_get_drvdata(dev);
	struct st_lpm_version *version;
	u16 current_traces;

	st_lpm_setup_ir_init();

	if (!lpm_drv->wakeup_bitmap) {
		/* don't suspend without wakeup device */
		pr_err("No device is able to wakeup\n");
		return -EINVAL;
	}
	st_lpm_set_wakeup_device(lpm_drv->wakeup_bitmap);

	if (lpm_drv->trace_data_value &&
	    !lpm_check_fw_version(&lpm_drv->fw_ver, LPM_SBC_TRACES_IN_SUSPEND)) {
		version = &lpm_fw_ver_vs_services[LPM_SBC_TRACES_IN_SUSPEND];

		dev_warn(lpm_drv->dev,
			 "This LPM Fw does not support SBC traces during suspend\n");
		dev_warn(lpm_drv->dev,
			 "SBC traces during suspend available from LPM Fw v%d.%d.%d\n",
			 version->major_soft, version->minor_soft,
			 version->patch_soft);

		current_traces = lpm_drv->trace_data_value;
		lpm_setup_tracedata(0x0, lpm_drv);
		lpm_drv->trace_data_value = current_traces;
	}

	lpm_config_reboot(ST_LPM_REBOOT_WITH_DDR_SELF_REFRESH, lpm_drv);

	return 0;
}

static int st_lpm_pm_resume(struct device *dev)
{
	struct st_lpm_driver_data *lpm_drv = dev_get_drvdata(dev);

	if (lpm_drv->trace_data_value &&
	    !lpm_check_fw_version(&lpm_drv->fw_ver, LPM_SBC_TRACES_IN_SUSPEND))
		lpm_setup_tracedata(lpm_drv->trace_data_value, lpm_drv);

	/*
	 * Request default value of EXIT_CPS, to be set while
	 * exiting from non-CPS warm reset cases, such as
	 * manual reset exit, WDT exit, shutdown exit.
	 */
	lpm_config_reboot(ST_LPM_REBOOT_WITH_DDR_OFF, lpm_drv);

	return 0;
}

SIMPLE_DEV_PM_OPS(st_lpm_pm_ops, st_lpm_pm_suspend, st_lpm_pm_resume);
#define ST_LPM_PM_OPS		(&st_lpm_pm_ops)
#else
#define ST_LPM_PM_OPS		NULL
#endif

static void st_lpm_shutdown(struct platform_device *pdev)
{
	struct st_lpm_driver_data *lpm_drv =
		platform_get_drvdata(pdev);

	static unsigned char reset_buf[] = LPM_DMEM_PEN_HOLD_VAR_RESET;
	struct st_lpm_version *version;
	u16 current_traces;
	int offset;

	st_lpm_setup_ir_init();

	st_lpm_set_wakeup_device(lpm_drv->wakeup_bitmap);

	if (lpm_drv->trace_data_value &&
	    !lpm_check_fw_version(&lpm_drv->fw_ver, LPM_SBC_TRACES_IN_SUSPEND)) {
		version = &lpm_fw_ver_vs_services[LPM_SBC_TRACES_IN_SUSPEND];

		dev_warn(lpm_drv->dev,
			 "This LPM Fw does not support SBC traces during suspend\n");
		dev_warn(lpm_drv->dev,
			 "SBC traces during suspend available from LPM Fw v%d.%d.%d\n",
			 version->major_soft, version->minor_soft,
			 version->patch_soft);

		current_traces = lpm_drv->trace_data_value;
		lpm_setup_tracedata(0x0, lpm_drv);
		lpm_drv->trace_data_value = current_traces;
	}

	lpm_config_reboot(ST_LPM_REBOOT_WITH_DDR_OFF, lpm_drv);
	offset = st_lpm_get_dmem_offset(ST_SBC_DMEM_PEN_HOLDING_VAR);
	if (offset >= 0)
		st_lpm_write_dmem(reset_buf, 4, offset);
}

static struct platform_driver st_lpm_driver = {
	.driver = {
		.name = "st-lpm",
		.owner = THIS_MODULE,
		.pm = ST_LPM_PM_OPS,
		.of_match_table = of_match_ptr(st_lpm_match),
	},
	.probe = st_lpm_probe,
	.remove = st_lpm_remove,
	.shutdown = st_lpm_shutdown,
};

static int __init st_lpm_init(void)
{
	int err = 0;

	err = platform_driver_register(&st_lpm_driver);
	if (err)
		pr_err("st-lpm driver fails on registrating (%x)\n" , err);
	else
		pr_info("st-lpm driver registered\n");

	return err;
}

void __exit st_lpm_exit(void)
{
	pr_info("st-lpm driver removed\n");
	platform_driver_unregister(&st_lpm_driver);
}

module_init(st_lpm_init);
module_exit(st_lpm_exit);

MODULE_AUTHOR("STMicroelectronics  <www.st.com>");
MODULE_DESCRIPTION("lpm device driver for STMicroelectronics devices");
MODULE_LICENSE("GPL");
