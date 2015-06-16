/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2004-2009 Emulex.  All rights reserved.           *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.emulex.com                                                  *
 * Portions Copyright (C) 2004-2005 Christoph Hellwig              *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of version 2 of the GNU General       *
 * Public License as published by the Free Software Foundation.    *
 * This program is distributed in the hope that it will be useful. *
 * ALL EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND          *
 * WARRANTIES, INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,  *
 * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT, ARE      *
 * DISCLAIMED, EXCEPT TO THE EXTENT THAT SUCH DISCLAIMERS ARE HELD *
 * TO BE LEGALLY INVALID.  See the GNU General Public License for  *
 * more details, a copy of which can be found in the file COPYING  *
 * included with this package.                                     *
 *******************************************************************/

#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/interrupt.h>

#include <scsi/scsi.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_transport_fc.h>

#include "lpfc_hw4.h"
#include "lpfc_hw.h"
#include "lpfc_sli.h"
#include "lpfc_sli4.h"
#include "lpfc_nl.h"
#include "lpfc_disc.h"
#include "lpfc_scsi.h"
#include "lpfc.h"
#include "lpfc_logmsg.h"
#include "lpfc_version.h"
#include "lpfc_compat.h"
#include "lpfc_crtn.h"
#include "lpfc_vport.h"

#define LPFC_DEF_DEVLOSS_TMO 30
#define LPFC_MIN_DEVLOSS_TMO 1
#define LPFC_MAX_DEVLOSS_TMO 255

#define LPFC_MAX_LINK_SPEED 8
#define LPFC_LINK_SPEED_BITMAP 0x00000117
#define LPFC_LINK_SPEED_STRING "0, 1, 2, 4, 8"

/**
 * lpfc_jedec_to_ascii - Hex to ascii convertor according to JEDEC rules
 * @incr: integer to convert.
 * @hdw: ascii string holding converted integer plus a string terminator.
 *
 * Description:
 * JEDEC Joint Electron Device Engineering Council.
 * Convert a 32 bit integer composed of 8 nibbles into an 8 byte ascii
 * character string. The string is then terminated with a NULL in byte 9.
 * Hex 0-9 becomes ascii '0' to '9'.
 * Hex a-f becomes ascii '=' to 'B' capital B.
 *
 * Notes:
 * Coded for 32 bit integers only.
 **/
static void
lpfc_jedec_to_ascii(int incr, char hdw[])
{
	int i, j;
	for (i = 0; i < 8; i++) {
		j = (incr & 0xf);
		if (j <= 9)
			hdw[7 - i] = 0x30 +  j;
		 else
			hdw[7 - i] = 0x61 + j - 10;
		incr = (incr >> 4);
	}
	hdw[8] = 0;
	return;
}

/**
 * lpfc_drvr_version_show - Return the Emulex driver string with version number
 * @dev: class unused variable.
 * @attr: device attribute, not used.
 * @buf: on return contains the module description text.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_drvr_version_show(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	return snprintf(buf, PAGE_SIZE, LPFC_MODULE_DESC "\n");
}

static ssize_t
lpfc_bg_info_show(struct device *dev, struct device_attribute *attr,
		  char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	if (phba->cfg_enable_bg)
		if (phba->sli3_options & LPFC_SLI3_BG_ENABLED)
			return snprintf(buf, PAGE_SIZE, "BlockGuard Enabled\n");
		else
			return snprintf(buf, PAGE_SIZE,
					"BlockGuard Not Supported\n");
	else
			return snprintf(buf, PAGE_SIZE,
					"BlockGuard Disabled\n");
}

static ssize_t
lpfc_bg_guard_err_show(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	return snprintf(buf, PAGE_SIZE, "%llu\n",
			(unsigned long long)phba->bg_guard_err_cnt);
}

static ssize_t
lpfc_bg_apptag_err_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	return snprintf(buf, PAGE_SIZE, "%llu\n",
			(unsigned long long)phba->bg_apptag_err_cnt);
}

static ssize_t
lpfc_bg_reftag_err_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	return snprintf(buf, PAGE_SIZE, "%llu\n",
			(unsigned long long)phba->bg_reftag_err_cnt);
}

/**
 * lpfc_info_show - Return some pci info about the host in ascii
 * @dev: class converted to a Scsi_host structure.
 * @attr: device attribute, not used.
 * @buf: on return contains the formatted text from lpfc_info().
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_info_show(struct device *dev, struct device_attribute *attr,
	       char *buf)
{
	struct Scsi_Host *host = class_to_shost(dev);

	return snprintf(buf, PAGE_SIZE, "%s\n",lpfc_info(host));
}

/**
 * lpfc_serialnum_show - Return the hba serial number in ascii
 * @dev: class converted to a Scsi_host structure.
 * @attr: device attribute, not used.
 * @buf: on return contains the formatted text serial number.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_serialnum_show(struct device *dev, struct device_attribute *attr,
		    char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	return snprintf(buf, PAGE_SIZE, "%s\n",phba->SerialNumber);
}

/**
 * lpfc_temp_sensor_show - Return the temperature sensor level
 * @dev: class converted to a Scsi_host structure.
 * @attr: device attribute, not used.
 * @buf: on return contains the formatted support level.
 *
 * Description:
 * Returns a number indicating the temperature sensor level currently
 * supported, zero or one in ascii.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_temp_sensor_show(struct device *dev, struct device_attribute *attr,
		      char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	return snprintf(buf, PAGE_SIZE, "%d\n",phba->temp_sensor_support);
}

/**
 * lpfc_modeldesc_show - Return the model description of the hba
 * @dev: class converted to a Scsi_host structure.
 * @attr: device attribute, not used.
 * @buf: on return contains the scsi vpd model description.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_modeldesc_show(struct device *dev, struct device_attribute *attr,
		    char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	return snprintf(buf, PAGE_SIZE, "%s\n",phba->ModelDesc);
}

/**
 * lpfc_modelname_show - Return the model name of the hba
 * @dev: class converted to a Scsi_host structure.
 * @attr: device attribute, not used.
 * @buf: on return contains the scsi vpd model name.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_modelname_show(struct device *dev, struct device_attribute *attr,
		    char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	return snprintf(buf, PAGE_SIZE, "%s\n",phba->ModelName);
}

/**
 * lpfc_programtype_show - Return the program type of the hba
 * @dev: class converted to a Scsi_host structure.
 * @attr: device attribute, not used.
 * @buf: on return contains the scsi vpd program type.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_programtype_show(struct device *dev, struct device_attribute *attr,
		      char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	return snprintf(buf, PAGE_SIZE, "%s\n",phba->ProgramType);
}

/**
 * lpfc_mlomgmt_show - Return the Menlo Maintenance sli flag
 * @dev: class converted to a Scsi_host structure.
 * @attr: device attribute, not used.
 * @buf: on return contains the Menlo Maintenance sli flag.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_mlomgmt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *)shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	return snprintf(buf, PAGE_SIZE, "%d\n",
		(phba->sli.sli_flag & LPFC_MENLO_MAINT));
}

/**
 * lpfc_vportnum_show - Return the port number in ascii of the hba
 * @dev: class converted to a Scsi_host structure.
 * @attr: device attribute, not used.
 * @buf: on return contains scsi vpd program type.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_vportnum_show(struct device *dev, struct device_attribute *attr,
		   char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	return snprintf(buf, PAGE_SIZE, "%s\n",phba->Port);
}

/**
 * lpfc_fwrev_show - Return the firmware rev running in the hba
 * @dev: class converted to a Scsi_host structure.
 * @attr: device attribute, not used.
 * @buf: on return contains the scsi vpd program type.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_fwrev_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	char fwrev[32];

	lpfc_decode_firmware_rev(phba, fwrev, 1);
	return snprintf(buf, PAGE_SIZE, "%s, sli-%d\n", fwrev, phba->sli_rev);
}

/**
 * lpfc_hdw_show - Return the jedec information about the hba
 * @dev: class converted to a Scsi_host structure.
 * @attr: device attribute, not used.
 * @buf: on return contains the scsi vpd program type.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_hdw_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	char hdw[9];
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	lpfc_vpd_t *vp = &phba->vpd;

	lpfc_jedec_to_ascii(vp->rev.biuRev, hdw);
	return snprintf(buf, PAGE_SIZE, "%s\n", hdw);
}

/**
 * lpfc_option_rom_version_show - Return the adapter ROM FCode version
 * @dev: class converted to a Scsi_host structure.
 * @attr: device attribute, not used.
 * @buf: on return contains the ROM and FCode ascii strings.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_option_rom_version_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	return snprintf(buf, PAGE_SIZE, "%s\n", phba->OptionROMVersion);
}

/**
 * lpfc_state_show - Return the link state of the port
 * @dev: class converted to a Scsi_host structure.
 * @attr: device attribute, not used.
 * @buf: on return contains text describing the state of the link.
 *
 * Notes:
 * The switch statement has no default so zero will be returned.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_link_state_show(struct device *dev, struct device_attribute *attr,
		     char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	int  len = 0;

	switch (phba->link_state) {
	case LPFC_LINK_UNKNOWN:
	case LPFC_WARM_START:
	case LPFC_INIT_START:
	case LPFC_INIT_MBX_CMDS:
	case LPFC_LINK_DOWN:
	case LPFC_HBA_ERROR:
		if (phba->hba_flag & LINK_DISABLED)
			len += snprintf(buf + len, PAGE_SIZE-len,
				"Link Down - User disabled\n");
		else
			len += snprintf(buf + len, PAGE_SIZE-len,
				"Link Down\n");
		break;
	case LPFC_LINK_UP:
	case LPFC_CLEAR_LA:
	case LPFC_HBA_READY:
		len += snprintf(buf + len, PAGE_SIZE-len, "Link Up - ");

		switch (vport->port_state) {
		case LPFC_LOCAL_CFG_LINK:
			len += snprintf(buf + len, PAGE_SIZE-len,
					"Configuring Link\n");
			break;
		case LPFC_FDISC:
		case LPFC_FLOGI:
		case LPFC_FABRIC_CFG_LINK:
		case LPFC_NS_REG:
		case LPFC_NS_QRY:
		case LPFC_BUILD_DISC_LIST:
		case LPFC_DISC_AUTH:
			len += snprintf(buf + len, PAGE_SIZE - len,
					"Discovery\n");
			break;
		case LPFC_VPORT_READY:
			len += snprintf(buf + len, PAGE_SIZE - len, "Ready\n");
			break;

		case LPFC_VPORT_FAILED:
			len += snprintf(buf + len, PAGE_SIZE - len, "Failed\n");
			break;

		case LPFC_VPORT_UNKNOWN:
			len += snprintf(buf + len, PAGE_SIZE - len,
					"Unknown\n");
			break;
		}
		if (phba->sli.sli_flag & LPFC_MENLO_MAINT)
			len += snprintf(buf + len, PAGE_SIZE-len,
					"   Menlo Maint Mode\n");
		else if (phba->fc_topology == TOPOLOGY_LOOP) {
			if (vport->fc_flag & FC_PUBLIC_LOOP)
				len += snprintf(buf + len, PAGE_SIZE-len,
						"   Public Loop\n");
			else
				len += snprintf(buf + len, PAGE_SIZE-len,
						"   Private Loop\n");
		} else {
			if (vport->fc_flag & FC_FABRIC)
				len += snprintf(buf + len, PAGE_SIZE-len,
						"   Fabric\n");
			else
				len += snprintf(buf + len, PAGE_SIZE-len,
						"   Point-2-Point\n");
		}
	}

	return len;
}

/**
 * lpfc_num_discovered_ports_show - Return sum of mapped and unmapped vports
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: on return contains the sum of fc mapped and unmapped.
 *
 * Description:
 * Returns the ascii text number of the sum of the fc mapped and unmapped
 * vport counts.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_num_discovered_ports_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;

	return snprintf(buf, PAGE_SIZE, "%d\n",
			vport->fc_map_cnt + vport->fc_unmap_cnt);
}

/**
 * lpfc_issue_lip - Misnomer, name carried over from long ago
 * @shost: Scsi_Host pointer.
 *
 * Description:
 * Bring the link down gracefully then re-init the link. The firmware will
 * re-init the fiber channel interface as required. Does not issue a LIP.
 *
 * Returns:
 * -EPERM port offline or management commands are being blocked
 * -ENOMEM cannot allocate memory for the mailbox command
 * -EIO error sending the mailbox command
 * zero for success
 **/
static int
lpfc_issue_lip(struct Scsi_Host *shost)
{
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	LPFC_MBOXQ_t *pmboxq;
	int mbxstatus = MBXERR_ERROR;

	if ((vport->fc_flag & FC_OFFLINE_MODE) ||
	    (phba->sli.sli_flag & LPFC_BLOCK_MGMT_IO))
		return -EPERM;

	pmboxq = mempool_alloc(phba->mbox_mem_pool,GFP_KERNEL);

	if (!pmboxq)
		return -ENOMEM;

	memset((void *)pmboxq, 0, sizeof (LPFC_MBOXQ_t));
	pmboxq->u.mb.mbxCommand = MBX_DOWN_LINK;
	pmboxq->u.mb.mbxOwner = OWN_HOST;

	mbxstatus = lpfc_sli_issue_mbox_wait(phba, pmboxq, LPFC_MBOX_TMO * 2);

	if ((mbxstatus == MBX_SUCCESS) &&
	    (pmboxq->u.mb.mbxStatus == 0 ||
	     pmboxq->u.mb.mbxStatus == MBXERR_LINK_DOWN)) {
		memset((void *)pmboxq, 0, sizeof (LPFC_MBOXQ_t));
		lpfc_init_link(phba, pmboxq, phba->cfg_topology,
			       phba->cfg_link_speed);
		mbxstatus = lpfc_sli_issue_mbox_wait(phba, pmboxq,
						     phba->fc_ratov * 2);
	}

	lpfc_set_loopback_flag(phba);
	if (mbxstatus != MBX_TIMEOUT)
		mempool_free(pmboxq, phba->mbox_mem_pool);

	if (mbxstatus == MBXERR_ERROR)
		return -EIO;

	return 0;
}

/**
 * lpfc_do_offline - Issues a mailbox command to bring the link down
 * @phba: lpfc_hba pointer.
 * @type: LPFC_EVT_OFFLINE, LPFC_EVT_WARM_START, LPFC_EVT_KILL.
 *
 * Notes:
 * Assumes any error from lpfc_do_offline() will be negative.
 * Can wait up to 5 seconds for the port ring buffers count
 * to reach zero, prints a warning if it is not zero and continues.
 * lpfc_workq_post_event() returns a non-zero return code if call fails.
 *
 * Returns:
 * -EIO error posting the event
 * zero for success
 **/
static int
lpfc_do_offline(struct lpfc_hba *phba, uint32_t type)
{
	struct completion online_compl;
	struct lpfc_sli_ring *pring;
	struct lpfc_sli *psli;
	int status = 0;
	int cnt = 0;
	int i;

	init_completion(&online_compl);
	lpfc_workq_post_event(phba, &status, &online_compl,
			      LPFC_EVT_OFFLINE_PREP);
	wait_for_completion(&online_compl);

	if (status != 0)
		return -EIO;

	psli = &phba->sli;

	/* Wait a little for things to settle down, but not
	 * long enough for dev loss timeout to expire.
	 */
	for (i = 0; i < psli->num_rings; i++) {
		pring = &psli->ring[i];
		while (pring->txcmplq_cnt) {
			msleep(10);
			if (cnt++ > 500) {  /* 5 secs */
				lpfc_printf_log(phba,
					KERN_WARNING, LOG_INIT,
					"0466 Outstanding IO when "
					"bringing Adapter offline\n");
				break;
			}
		}
	}

	init_completion(&online_compl);
	lpfc_workq_post_event(phba, &status, &online_compl, type);
	wait_for_completion(&online_compl);

	if (status != 0)
		return -EIO;

	return 0;
}

/**
 * lpfc_selective_reset - Offline then onlines the port
 * @phba: lpfc_hba pointer.
 *
 * Description:
 * If the port is configured to allow a reset then the hba is brought
 * offline then online.
 *
 * Notes:
 * Assumes any error from lpfc_do_offline() will be negative.
 *
 * Returns:
 * lpfc_do_offline() return code if not zero
 * -EIO reset not configured or error posting the event
 * zero for success
 **/
static int
lpfc_selective_reset(struct lpfc_hba *phba)
{
	struct completion online_compl;
	int status = 0;

	if (!phba->cfg_enable_hba_reset)
		return -EIO;

	status = lpfc_do_offline(phba, LPFC_EVT_OFFLINE);

	if (status != 0)
		return status;

	init_completion(&online_compl);
	lpfc_workq_post_event(phba, &status, &online_compl,
			      LPFC_EVT_ONLINE);
	wait_for_completion(&online_compl);

	if (status != 0)
		return -EIO;

	return 0;
}

/**
 * lpfc_issue_reset - Selectively resets an adapter
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: containing the string "selective".
 * @count: unused variable.
 *
 * Description:
 * If the buf contains the string "selective" then lpfc_selective_reset()
 * is called to perform the reset.
 *
 * Notes:
 * Assumes any error from lpfc_selective_reset() will be negative.
 * If lpfc_selective_reset() returns zero then the length of the buffer
 * is returned which indicates succcess
 *
 * Returns:
 * -EINVAL if the buffer does not contain the string "selective"
 * length of buf if lpfc-selective_reset() if the call succeeds
 * return value of lpfc_selective_reset() if the call fails
**/
static ssize_t
lpfc_issue_reset(struct device *dev, struct device_attribute *attr,
		 const char *buf, size_t count)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	int status = -EINVAL;

	if (strncmp(buf, "selective", sizeof("selective") - 1) == 0)
		status = lpfc_selective_reset(phba);

	if (status == 0)
		return strlen(buf);
	else
		return status;
}

/**
 * lpfc_nport_evt_cnt_show - Return the number of nport events
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: on return contains the ascii number of nport events.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_nport_evt_cnt_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	return snprintf(buf, PAGE_SIZE, "%d\n", phba->nport_event_cnt);
}

/**
 * lpfc_board_mode_show - Return the state of the board
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: on return contains the state of the adapter.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_board_mode_show(struct device *dev, struct device_attribute *attr,
		     char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	char  * state;

	if (phba->link_state == LPFC_HBA_ERROR)
		state = "error";
	else if (phba->link_state == LPFC_WARM_START)
		state = "warm start";
	else if (phba->link_state == LPFC_INIT_START)
		state = "offline";
	else
		state = "online";

	return snprintf(buf, PAGE_SIZE, "%s\n", state);
}

/**
 * lpfc_board_mode_store - Puts the hba in online, offline, warm or error state
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: containing one of the strings "online", "offline", "warm" or "error".
 * @count: unused variable.
 *
 * Returns:
 * -EACCES if enable hba reset not enabled
 * -EINVAL if the buffer does not contain a valid string (see above)
 * -EIO if lpfc_workq_post_event() or lpfc_do_offline() fails
 * buf length greater than zero indicates success
 **/
static ssize_t
lpfc_board_mode_store(struct device *dev, struct device_attribute *attr,
		      const char *buf, size_t count)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	struct completion online_compl;
	int status=0;

	if (!phba->cfg_enable_hba_reset)
		return -EACCES;
	init_completion(&online_compl);

	if(strncmp(buf, "online", sizeof("online") - 1) == 0) {
		lpfc_workq_post_event(phba, &status, &online_compl,
				      LPFC_EVT_ONLINE);
		wait_for_completion(&online_compl);
	} else if (strncmp(buf, "offline", sizeof("offline") - 1) == 0)
		status = lpfc_do_offline(phba, LPFC_EVT_OFFLINE);
	else if (strncmp(buf, "warm", sizeof("warm") - 1) == 0)
		status = lpfc_do_offline(phba, LPFC_EVT_WARM_START);
	else if (strncmp(buf, "error", sizeof("error") - 1) == 0)
		status = lpfc_do_offline(phba, LPFC_EVT_KILL);
	else
		return -EINVAL;

	if (!status)
		return strlen(buf);
	else
		return -EIO;
}

/**
 * lpfc_get_hba_info - Return various bits of informaton about the adapter
 * @phba: pointer to the adapter structure.
 * @mxri: max xri count.
 * @axri: available xri count.
 * @mrpi: max rpi count.
 * @arpi: available rpi count.
 * @mvpi: max vpi count.
 * @avpi: available vpi count.
 *
 * Description:
 * If an integer pointer for an count is not null then the value for the
 * count is returned.
 *
 * Returns:
 * zero on error
 * one for success
 **/
static int
lpfc_get_hba_info(struct lpfc_hba *phba,
		  uint32_t *mxri, uint32_t *axri,
		  uint32_t *mrpi, uint32_t *arpi,
		  uint32_t *mvpi, uint32_t *avpi)
{
	struct lpfc_sli *psli = &phba->sli;
	struct lpfc_mbx_read_config *rd_config;
	LPFC_MBOXQ_t *pmboxq;
	MAILBOX_t *pmb;
	int rc = 0;

	/*
	 * prevent udev from issuing mailbox commands until the port is
	 * configured.
	 */
	if (phba->link_state < LPFC_LINK_DOWN ||
	    !phba->mbox_mem_pool ||
	    (phba->sli.sli_flag & LPFC_SLI_ACTIVE) == 0)
		return 0;

	if (phba->sli.sli_flag & LPFC_BLOCK_MGMT_IO)
		return 0;

	pmboxq = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!pmboxq)
		return 0;
	memset(pmboxq, 0, sizeof (LPFC_MBOXQ_t));

	pmb = &pmboxq->u.mb;
	pmb->mbxCommand = MBX_READ_CONFIG;
	pmb->mbxOwner = OWN_HOST;
	pmboxq->context1 = NULL;

	if ((phba->pport->fc_flag & FC_OFFLINE_MODE) ||
		(!(psli->sli_flag & LPFC_SLI_ACTIVE)))
		rc = MBX_NOT_FINISHED;
	else
		rc = lpfc_sli_issue_mbox_wait(phba, pmboxq, phba->fc_ratov * 2);

	if (rc != MBX_SUCCESS) {
		if (rc != MBX_TIMEOUT)
			mempool_free(pmboxq, phba->mbox_mem_pool);
		return 0;
	}

	if (phba->sli_rev == LPFC_SLI_REV4) {
		rd_config = &pmboxq->u.mqe.un.rd_config;
		if (mrpi)
			*mrpi = bf_get(lpfc_mbx_rd_conf_rpi_count, rd_config);
		if (arpi)
			*arpi = bf_get(lpfc_mbx_rd_conf_rpi_count, rd_config) -
					phba->sli4_hba.max_cfg_param.rpi_used;
		if (mxri)
			*mxri = bf_get(lpfc_mbx_rd_conf_xri_count, rd_config);
		if (axri)
			*axri = bf_get(lpfc_mbx_rd_conf_xri_count, rd_config) -
					phba->sli4_hba.max_cfg_param.xri_used;
		if (mvpi)
			*mvpi = bf_get(lpfc_mbx_rd_conf_vpi_count, rd_config);
		if (avpi)
			*avpi = bf_get(lpfc_mbx_rd_conf_vpi_count, rd_config) -
					phba->sli4_hba.max_cfg_param.vpi_used;
	} else {
		if (mrpi)
			*mrpi = pmb->un.varRdConfig.max_rpi;
		if (arpi)
			*arpi = pmb->un.varRdConfig.avail_rpi;
		if (mxri)
			*mxri = pmb->un.varRdConfig.max_xri;
		if (axri)
			*axri = pmb->un.varRdConfig.avail_xri;
		if (mvpi)
			*mvpi = pmb->un.varRdConfig.max_vpi;
		if (avpi)
			*avpi = pmb->un.varRdConfig.avail_vpi;
	}

	mempool_free(pmboxq, phba->mbox_mem_pool);
	return 1;
}

/**
 * lpfc_max_rpi_show - Return maximum rpi
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: on return contains the maximum rpi count in decimal or "Unknown".
 *
 * Description:
 * Calls lpfc_get_hba_info() asking for just the mrpi count.
 * If lpfc_get_hba_info() returns zero (failure) the buffer text is set
 * to "Unknown" and the buffer length is returned, therefore the caller
 * must check for "Unknown" in the buffer to detect a failure.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_max_rpi_show(struct device *dev, struct device_attribute *attr,
		  char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	uint32_t cnt;

	if (lpfc_get_hba_info(phba, NULL, NULL, &cnt, NULL, NULL, NULL))
		return snprintf(buf, PAGE_SIZE, "%d\n", cnt);
	return snprintf(buf, PAGE_SIZE, "Unknown\n");
}

/**
 * lpfc_used_rpi_show - Return maximum rpi minus available rpi
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: containing the used rpi count in decimal or "Unknown".
 *
 * Description:
 * Calls lpfc_get_hba_info() asking for just the mrpi and arpi counts.
 * If lpfc_get_hba_info() returns zero (failure) the buffer text is set
 * to "Unknown" and the buffer length is returned, therefore the caller
 * must check for "Unknown" in the buffer to detect a failure.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_used_rpi_show(struct device *dev, struct device_attribute *attr,
		   char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	uint32_t cnt, acnt;

	if (lpfc_get_hba_info(phba, NULL, NULL, &cnt, &acnt, NULL, NULL))
		return snprintf(buf, PAGE_SIZE, "%d\n", (cnt - acnt));
	return snprintf(buf, PAGE_SIZE, "Unknown\n");
}

/**
 * lpfc_max_xri_show - Return maximum xri
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: on return contains the maximum xri count in decimal or "Unknown".
 *
 * Description:
 * Calls lpfc_get_hba_info() asking for just the mrpi count.
 * If lpfc_get_hba_info() returns zero (failure) the buffer text is set
 * to "Unknown" and the buffer length is returned, therefore the caller
 * must check for "Unknown" in the buffer to detect a failure.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_max_xri_show(struct device *dev, struct device_attribute *attr,
		  char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	uint32_t cnt;

	if (lpfc_get_hba_info(phba, &cnt, NULL, NULL, NULL, NULL, NULL))
		return snprintf(buf, PAGE_SIZE, "%d\n", cnt);
	return snprintf(buf, PAGE_SIZE, "Unknown\n");
}

/**
 * lpfc_used_xri_show - Return maximum xpi minus the available xpi
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: on return contains the used xri count in decimal or "Unknown".
 *
 * Description:
 * Calls lpfc_get_hba_info() asking for just the mxri and axri counts.
 * If lpfc_get_hba_info() returns zero (failure) the buffer text is set
 * to "Unknown" and the buffer length is returned, therefore the caller
 * must check for "Unknown" in the buffer to detect a failure.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_used_xri_show(struct device *dev, struct device_attribute *attr,
		   char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	uint32_t cnt, acnt;

	if (lpfc_get_hba_info(phba, &cnt, &acnt, NULL, NULL, NULL, NULL))
		return snprintf(buf, PAGE_SIZE, "%d\n", (cnt - acnt));
	return snprintf(buf, PAGE_SIZE, "Unknown\n");
}

/**
 * lpfc_max_vpi_show - Return maximum vpi
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: on return contains the maximum vpi count in decimal or "Unknown".
 *
 * Description:
 * Calls lpfc_get_hba_info() asking for just the mvpi count.
 * If lpfc_get_hba_info() returns zero (failure) the buffer text is set
 * to "Unknown" and the buffer length is returned, therefore the caller
 * must check for "Unknown" in the buffer to detect a failure.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_max_vpi_show(struct device *dev, struct device_attribute *attr,
		  char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	uint32_t cnt;

	if (lpfc_get_hba_info(phba, NULL, NULL, NULL, NULL, &cnt, NULL))
		return snprintf(buf, PAGE_SIZE, "%d\n", cnt);
	return snprintf(buf, PAGE_SIZE, "Unknown\n");
}

/**
 * lpfc_used_vpi_show - Return maximum vpi minus the available vpi
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: on return contains the used vpi count in decimal or "Unknown".
 *
 * Description:
 * Calls lpfc_get_hba_info() asking for just the mvpi and avpi counts.
 * If lpfc_get_hba_info() returns zero (failure) the buffer text is set
 * to "Unknown" and the buffer length is returned, therefore the caller
 * must check for "Unknown" in the buffer to detect a failure.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_used_vpi_show(struct device *dev, struct device_attribute *attr,
		   char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	uint32_t cnt, acnt;

	if (lpfc_get_hba_info(phba, NULL, NULL, NULL, NULL, &cnt, &acnt))
		return snprintf(buf, PAGE_SIZE, "%d\n", (cnt - acnt));
	return snprintf(buf, PAGE_SIZE, "Unknown\n");
}

/**
 * lpfc_npiv_info_show - Return text about NPIV support for the adapter
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: text that must be interpreted to determine if npiv is supported.
 *
 * Description:
 * Buffer will contain text indicating npiv is not suppoerted on the port,
 * the port is an NPIV physical port, or it is an npiv virtual port with
 * the id of the vport.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_npiv_info_show(struct device *dev, struct device_attribute *attr,
		    char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	if (!(phba->max_vpi))
		return snprintf(buf, PAGE_SIZE, "NPIV Not Supported\n");
	if (vport->port_type == LPFC_PHYSICAL_PORT)
		return snprintf(buf, PAGE_SIZE, "NPIV Physical\n");
	return snprintf(buf, PAGE_SIZE, "NPIV Virtual (VPI %d)\n", vport->vpi);
}

/**
 * lpfc_poll_show - Return text about poll support for the adapter
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: on return contains the cfg_poll in hex.
 *
 * Notes:
 * cfg_poll should be a lpfc_polling_flags type.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_poll_show(struct device *dev, struct device_attribute *attr,
	       char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	return snprintf(buf, PAGE_SIZE, "%#x\n", phba->cfg_poll);
}

/**
 * lpfc_poll_store - Set the value of cfg_poll for the adapter
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: one or more lpfc_polling_flags values.
 * @count: not used.
 *
 * Notes:
 * buf contents converted to integer and checked for a valid value.
 *
 * Returns:
 * -EINVAL if the buffer connot be converted or is out of range
 * length of the buf on success
 **/
static ssize_t
lpfc_poll_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	uint32_t creg_val;
	uint32_t old_val;
	int val=0;

	if (!isdigit(buf[0]))
		return -EINVAL;

	if (sscanf(buf, "%i", &val) != 1)
		return -EINVAL;

	if ((val & 0x3) != val)
		return -EINVAL;

	spin_lock_irq(&phba->hbalock);

	old_val = phba->cfg_poll;

	if (val & ENABLE_FCP_RING_POLLING) {
		if ((val & DISABLE_FCP_RING_INT) &&
		    !(old_val & DISABLE_FCP_RING_INT)) {
			creg_val = readl(phba->HCregaddr);
			creg_val &= ~(HC_R0INT_ENA << LPFC_FCP_RING);
			writel(creg_val, phba->HCregaddr);
			readl(phba->HCregaddr); /* flush */

			lpfc_poll_start_timer(phba);
		}
	} else if (val != 0x0) {
		spin_unlock_irq(&phba->hbalock);
		return -EINVAL;
	}

	if (!(val & DISABLE_FCP_RING_INT) &&
	    (old_val & DISABLE_FCP_RING_INT))
	{
		spin_unlock_irq(&phba->hbalock);
		del_timer(&phba->fcp_poll_timer);
		spin_lock_irq(&phba->hbalock);
		creg_val = readl(phba->HCregaddr);
		creg_val |= (HC_R0INT_ENA << LPFC_FCP_RING);
		writel(creg_val, phba->HCregaddr);
		readl(phba->HCregaddr); /* flush */
	}

	phba->cfg_poll = val;

	spin_unlock_irq(&phba->hbalock);

	return strlen(buf);
}

/**
 * lpfc_param_show - Return a cfg attribute value in decimal
 *
 * Description:
 * Macro that given an attr e.g. hba_queue_depth expands
 * into a function with the name lpfc_hba_queue_depth_show.
 *
 * lpfc_##attr##_show: Return the decimal value of an adapters cfg_xxx field.
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: on return contains the attribute value in decimal.
 *
 * Returns: size of formatted string.
 **/
#define lpfc_param_show(attr)	\
static ssize_t \
lpfc_##attr##_show(struct device *dev, struct device_attribute *attr, \
		   char *buf) \
{ \
	struct Scsi_Host  *shost = class_to_shost(dev);\
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;\
	struct lpfc_hba   *phba = vport->phba;\
	int val = 0;\
	val = phba->cfg_##attr;\
	return snprintf(buf, PAGE_SIZE, "%d\n",\
			phba->cfg_##attr);\
}

/**
 * lpfc_param_hex_show - Return a cfg attribute value in hex
 *
 * Description:
 * Macro that given an attr e.g. hba_queue_depth expands
 * into a function with the name lpfc_hba_queue_depth_show
 *
 * lpfc_##attr##_show: Return the hex value of an adapters cfg_xxx field.
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: on return contains the attribute value in hexadecimal.
 *
 * Returns: size of formatted string.
 **/
#define lpfc_param_hex_show(attr)	\
static ssize_t \
lpfc_##attr##_show(struct device *dev, struct device_attribute *attr, \
		   char *buf) \
{ \
	struct Scsi_Host  *shost = class_to_shost(dev);\
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;\
	struct lpfc_hba   *phba = vport->phba;\
	int val = 0;\
	val = phba->cfg_##attr;\
	return snprintf(buf, PAGE_SIZE, "%#x\n",\
			phba->cfg_##attr);\
}

/**
 * lpfc_param_init - Intializes a cfg attribute
 *
 * Description:
 * Macro that given an attr e.g. hba_queue_depth expands
 * into a function with the name lpfc_hba_queue_depth_init. The macro also
 * takes a default argument, a minimum and maximum argument.
 *
 * lpfc_##attr##_init: Initializes an attribute.
 * @phba: pointer the the adapter structure.
 * @val: integer attribute value.
 *
 * Validates the min and max values then sets the adapter config field
 * accordingly, or uses the default if out of range and prints an error message.
 *
 * Returns:
 * zero on success
 * -EINVAL if default used
 **/
#define lpfc_param_init(attr, default, minval, maxval)	\
static int \
lpfc_##attr##_init(struct lpfc_hba *phba, int val) \
{ \
	if (val >= minval && val <= maxval) {\
		phba->cfg_##attr = val;\
		return 0;\
	}\
	lpfc_printf_log(phba, KERN_ERR, LOG_INIT, \
			"0449 lpfc_"#attr" attribute cannot be set to %d, "\
			"allowed range is ["#minval", "#maxval"]\n", val); \
	phba->cfg_##attr = default;\
	return -EINVAL;\
}

/**
 * lpfc_param_set - Set a cfg attribute value
 *
 * Description:
 * Macro that given an attr e.g. hba_queue_depth expands
 * into a function with the name lpfc_hba_queue_depth_set
 *
 * lpfc_##attr##_set: Sets an attribute value.
 * @phba: pointer the the adapter structure.
 * @val: integer attribute value.
 *
 * Description:
 * Validates the min and max values then sets the
 * adapter config field if in the valid range. prints error message
 * and does not set the parameter if invalid.
 *
 * Returns:
 * zero on success
 * -EINVAL if val is invalid
 **/
#define lpfc_param_set(attr, default, minval, maxval)	\
static int \
lpfc_##attr##_set(struct lpfc_hba *phba, int val) \
{ \
	if (val >= minval && val <= maxval) {\
		phba->cfg_##attr = val;\
		return 0;\
	}\
	lpfc_printf_log(phba, KERN_ERR, LOG_INIT, \
			"0450 lpfc_"#attr" attribute cannot be set to %d, "\
			"allowed range is ["#minval", "#maxval"]\n", val); \
	return -EINVAL;\
}

/**
 * lpfc_param_store - Set a vport attribute value
 *
 * Description:
 * Macro that given an attr e.g. hba_queue_depth expands
 * into a function with the name lpfc_hba_queue_depth_store.
 *
 * lpfc_##attr##_store: Set an sttribute value.
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: contains the attribute value in ascii.
 * @count: not used.
 *
 * Description:
 * Convert the ascii text number to an integer, then
 * use the lpfc_##attr##_set function to set the value.
 *
 * Returns:
 * -EINVAL if val is invalid or lpfc_##attr##_set() fails
 * length of buffer upon success.
 **/
#define lpfc_param_store(attr)	\
static ssize_t \
lpfc_##attr##_store(struct device *dev, struct device_attribute *attr, \
		    const char *buf, size_t count) \
{ \
	struct Scsi_Host  *shost = class_to_shost(dev);\
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;\
	struct lpfc_hba   *phba = vport->phba;\
	int val=0;\
	if (!isdigit(buf[0]))\
		return -EINVAL;\
	if (sscanf(buf, "%i", &val) != 1)\
		return -EINVAL;\
	if (lpfc_##attr##_set(phba, val) == 0) \
		return strlen(buf);\
	else \
		return -EINVAL;\
}

/**
 * lpfc_vport_param_show - Return decimal formatted cfg attribute value
 *
 * Description:
 * Macro that given an attr e.g. hba_queue_depth expands
 * into a function with the name lpfc_hba_queue_depth_show
 *
 * lpfc_##attr##_show: prints the attribute value in decimal.
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: on return contains the attribute value in decimal.
 *
 * Returns: length of formatted string.
 **/
#define lpfc_vport_param_show(attr)	\
static ssize_t \
lpfc_##attr##_show(struct device *dev, struct device_attribute *attr, \
		   char *buf) \
{ \
	struct Scsi_Host  *shost = class_to_shost(dev);\
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;\
	int val = 0;\
	val = vport->cfg_##attr;\
	return snprintf(buf, PAGE_SIZE, "%d\n", vport->cfg_##attr);\
}

/**
 * lpfc_vport_param_hex_show - Return hex formatted attribute value
 *
 * Description:
 * Macro that given an attr e.g.
 * hba_queue_depth expands into a function with the name
 * lpfc_hba_queue_depth_show
 *
 * lpfc_##attr##_show: prints the attribute value in hexadecimal.
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: on return contains the attribute value in hexadecimal.
 *
 * Returns: length of formatted string.
 **/
#define lpfc_vport_param_hex_show(attr)	\
static ssize_t \
lpfc_##attr##_show(struct device *dev, struct device_attribute *attr, \
		   char *buf) \
{ \
	struct Scsi_Host  *shost = class_to_shost(dev);\
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;\
	int val = 0;\
	val = vport->cfg_##attr;\
	return snprintf(buf, PAGE_SIZE, "%#x\n", vport->cfg_##attr);\
}

/**
 * lpfc_vport_param_init - Initialize a vport cfg attribute
 *
 * Description:
 * Macro that given an attr e.g. hba_queue_depth expands
 * into a function with the name lpfc_hba_queue_depth_init. The macro also
 * takes a default argument, a minimum and maximum argument.
 *
 * lpfc_##attr##_init: validates the min and max values then sets the
 * adapter config field accordingly, or uses the default if out of range
 * and prints an error message.
 * @phba: pointer the the adapter structure.
 * @val: integer attribute value.
 *
 * Returns:
 * zero on success
 * -EINVAL if default used
 **/
#define lpfc_vport_param_init(attr, default, minval, maxval)	\
static int \
lpfc_##attr##_init(struct lpfc_vport *vport, int val) \
{ \
	if (val >= minval && val <= maxval) {\
		vport->cfg_##attr = val;\
		return 0;\
	}\
	lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT, \
			 "0423 lpfc_"#attr" attribute cannot be set to %d, "\
			 "allowed range is ["#minval", "#maxval"]\n", val); \
	vport->cfg_##attr = default;\
	return -EINVAL;\
}

/**
 * lpfc_vport_param_set - Set a vport cfg attribute
 *
 * Description:
 * Macro that given an attr e.g. hba_queue_depth expands
 * into a function with the name lpfc_hba_queue_depth_set
 *
 * lpfc_##attr##_set: validates the min and max values then sets the
 * adapter config field if in the valid range. prints error message
 * and does not set the parameter if invalid.
 * @phba: pointer the the adapter structure.
 * @val:	integer attribute value.
 *
 * Returns:
 * zero on success
 * -EINVAL if val is invalid
 **/
#define lpfc_vport_param_set(attr, default, minval, maxval)	\
static int \
lpfc_##attr##_set(struct lpfc_vport *vport, int val) \
{ \
	if (val >= minval && val <= maxval) {\
		vport->cfg_##attr = val;\
		return 0;\
	}\
	lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT, \
			 "0424 lpfc_"#attr" attribute cannot be set to %d, "\
			 "allowed range is ["#minval", "#maxval"]\n", val); \
	return -EINVAL;\
}

/**
 * lpfc_vport_param_store - Set a vport attribute
 *
 * Description:
 * Macro that given an attr e.g. hba_queue_depth
 * expands into a function with the name lpfc_hba_queue_depth_store
 *
 * lpfc_##attr##_store: convert the ascii text number to an integer, then
 * use the lpfc_##attr##_set function to set the value.
 * @cdev: class device that is converted into a Scsi_host.
 * @buf:	contains the attribute value in decimal.
 * @count: not used.
 *
 * Returns:
 * -EINVAL if val is invalid or lpfc_##attr##_set() fails
 * length of buffer upon success.
 **/
#define lpfc_vport_param_store(attr)	\
static ssize_t \
lpfc_##attr##_store(struct device *dev, struct device_attribute *attr, \
		    const char *buf, size_t count) \
{ \
	struct Scsi_Host  *shost = class_to_shost(dev);\
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;\
	int val=0;\
	if (!isdigit(buf[0]))\
		return -EINVAL;\
	if (sscanf(buf, "%i", &val) != 1)\
		return -EINVAL;\
	if (lpfc_##attr##_set(vport, val) == 0) \
		return strlen(buf);\
	else \
		return -EINVAL;\
}


#define LPFC_ATTR(name, defval, minval, maxval, desc) \
static int lpfc_##name = defval;\
module_param(lpfc_##name, int, 0);\
MODULE_PARM_DESC(lpfc_##name, desc);\
lpfc_param_init(name, defval, minval, maxval)

#define LPFC_ATTR_R(name, defval, minval, maxval, desc) \
static int lpfc_##name = defval;\
module_param(lpfc_##name, int, 0);\
MODULE_PARM_DESC(lpfc_##name, desc);\
lpfc_param_show(name)\
lpfc_param_init(name, defval, minval, maxval)\
static DEVICE_ATTR(lpfc_##name, S_IRUGO , lpfc_##name##_show, NULL)

#define LPFC_ATTR_RW(name, defval, minval, maxval, desc) \
static int lpfc_##name = defval;\
module_param(lpfc_##name, int, 0);\
MODULE_PARM_DESC(lpfc_##name, desc);\
lpfc_param_show(name)\
lpfc_param_init(name, defval, minval, maxval)\
lpfc_param_set(name, defval, minval, maxval)\
lpfc_param_store(name)\
static DEVICE_ATTR(lpfc_##name, S_IRUGO | S_IWUSR,\
		   lpfc_##name##_show, lpfc_##name##_store)

#define LPFC_ATTR_HEX_R(name, defval, minval, maxval, desc) \
static int lpfc_##name = defval;\
module_param(lpfc_##name, int, 0);\
MODULE_PARM_DESC(lpfc_##name, desc);\
lpfc_param_hex_show(name)\
lpfc_param_init(name, defval, minval, maxval)\
static DEVICE_ATTR(lpfc_##name, S_IRUGO , lpfc_##name##_show, NULL)

#define LPFC_ATTR_HEX_RW(name, defval, minval, maxval, desc) \
static int lpfc_##name = defval;\
module_param(lpfc_##name, int, 0);\
MODULE_PARM_DESC(lpfc_##name, desc);\
lpfc_param_hex_show(name)\
lpfc_param_init(name, defval, minval, maxval)\
lpfc_param_set(name, defval, minval, maxval)\
lpfc_param_store(name)\
static DEVICE_ATTR(lpfc_##name, S_IRUGO | S_IWUSR,\
		   lpfc_##name##_show, lpfc_##name##_store)

#define LPFC_VPORT_ATTR(name, defval, minval, maxval, desc) \
static int lpfc_##name = defval;\
module_param(lpfc_##name, int, 0);\
MODULE_PARM_DESC(lpfc_##name, desc);\
lpfc_vport_param_init(name, defval, minval, maxval)

#define LPFC_VPORT_ATTR_R(name, defval, minval, maxval, desc) \
static int lpfc_##name = defval;\
module_param(lpfc_##name, int, 0);\
MODULE_PARM_DESC(lpfc_##name, desc);\
lpfc_vport_param_show(name)\
lpfc_vport_param_init(name, defval, minval, maxval)\
static DEVICE_ATTR(lpfc_##name, S_IRUGO , lpfc_##name##_show, NULL)

#define LPFC_VPORT_ATTR_RW(name, defval, minval, maxval, desc) \
static int lpfc_##name = defval;\
module_param(lpfc_##name, int, 0);\
MODULE_PARM_DESC(lpfc_##name, desc);\
lpfc_vport_param_show(name)\
lpfc_vport_param_init(name, defval, minval, maxval)\
lpfc_vport_param_set(name, defval, minval, maxval)\
lpfc_vport_param_store(name)\
static DEVICE_ATTR(lpfc_##name, S_IRUGO | S_IWUSR,\
		   lpfc_##name##_show, lpfc_##name##_store)

#define LPFC_VPORT_ATTR_HEX_R(name, defval, minval, maxval, desc) \
static int lpfc_##name = defval;\
module_param(lpfc_##name, int, 0);\
MODULE_PARM_DESC(lpfc_##name, desc);\
lpfc_vport_param_hex_show(name)\
lpfc_vport_param_init(name, defval, minval, maxval)\
static DEVICE_ATTR(lpfc_##name, S_IRUGO , lpfc_##name##_show, NULL)

#define LPFC_VPORT_ATTR_HEX_RW(name, defval, minval, maxval, desc) \
static int lpfc_##name = defval;\
module_param(lpfc_##name, int, 0);\
MODULE_PARM_DESC(lpfc_##name, desc);\
lpfc_vport_param_hex_show(name)\
lpfc_vport_param_init(name, defval, minval, maxval)\
lpfc_vport_param_set(name, defval, minval, maxval)\
lpfc_vport_param_store(name)\
static DEVICE_ATTR(lpfc_##name, S_IRUGO | S_IWUSR,\
		   lpfc_##name##_show, lpfc_##name##_store)

static DEVICE_ATTR(bg_info, S_IRUGO, lpfc_bg_info_show, NULL);
static DEVICE_ATTR(bg_guard_err, S_IRUGO, lpfc_bg_guard_err_show, NULL);
static DEVICE_ATTR(bg_apptag_err, S_IRUGO, lpfc_bg_apptag_err_show, NULL);
static DEVICE_ATTR(bg_reftag_err, S_IRUGO, lpfc_bg_reftag_err_show, NULL);
static DEVICE_ATTR(info, S_IRUGO, lpfc_info_show, NULL);
static DEVICE_ATTR(serialnum, S_IRUGO, lpfc_serialnum_show, NULL);
static DEVICE_ATTR(modeldesc, S_IRUGO, lpfc_modeldesc_show, NULL);
static DEVICE_ATTR(modelname, S_IRUGO, lpfc_modelname_show, NULL);
static DEVICE_ATTR(programtype, S_IRUGO, lpfc_programtype_show, NULL);
static DEVICE_ATTR(portnum, S_IRUGO, lpfc_vportnum_show, NULL);
static DEVICE_ATTR(fwrev, S_IRUGO, lpfc_fwrev_show, NULL);
static DEVICE_ATTR(hdw, S_IRUGO, lpfc_hdw_show, NULL);
static DEVICE_ATTR(link_state, S_IRUGO, lpfc_link_state_show, NULL);
static DEVICE_ATTR(option_rom_version, S_IRUGO,
		   lpfc_option_rom_version_show, NULL);
static DEVICE_ATTR(num_discovered_ports, S_IRUGO,
		   lpfc_num_discovered_ports_show, NULL);
static DEVICE_ATTR(menlo_mgmt_mode, S_IRUGO, lpfc_mlomgmt_show, NULL);
static DEVICE_ATTR(nport_evt_cnt, S_IRUGO, lpfc_nport_evt_cnt_show, NULL);
static DEVICE_ATTR(lpfc_drvr_version, S_IRUGO, lpfc_drvr_version_show, NULL);
static DEVICE_ATTR(board_mode, S_IRUGO | S_IWUSR,
		   lpfc_board_mode_show, lpfc_board_mode_store);
static DEVICE_ATTR(issue_reset, S_IWUSR, NULL, lpfc_issue_reset);
static DEVICE_ATTR(max_vpi, S_IRUGO, lpfc_max_vpi_show, NULL);
static DEVICE_ATTR(used_vpi, S_IRUGO, lpfc_used_vpi_show, NULL);
static DEVICE_ATTR(max_rpi, S_IRUGO, lpfc_max_rpi_show, NULL);
static DEVICE_ATTR(used_rpi, S_IRUGO, lpfc_used_rpi_show, NULL);
static DEVICE_ATTR(max_xri, S_IRUGO, lpfc_max_xri_show, NULL);
static DEVICE_ATTR(used_xri, S_IRUGO, lpfc_used_xri_show, NULL);
static DEVICE_ATTR(npiv_info, S_IRUGO, lpfc_npiv_info_show, NULL);
static DEVICE_ATTR(lpfc_temp_sensor, S_IRUGO, lpfc_temp_sensor_show, NULL);


static char *lpfc_soft_wwn_key = "C99G71SL8032A";

/**
 * lpfc_soft_wwn_enable_store - Allows setting of the wwn if the key is valid
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: containing the string lpfc_soft_wwn_key.
 * @count: must be size of lpfc_soft_wwn_key.
 *
 * Returns:
 * -EINVAL if the buffer does not contain lpfc_soft_wwn_key
 * length of buf indicates success
 **/
static ssize_t
lpfc_soft_wwn_enable_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	unsigned int cnt = count;

	/*
	 * We're doing a simple sanity check for soft_wwpn setting.
	 * We require that the user write a specific key to enable
	 * the soft_wwpn attribute to be settable. Once the attribute
	 * is written, the enable key resets. If further updates are
	 * desired, the key must be written again to re-enable the
	 * attribute.
	 *
	 * The "key" is not secret - it is a hardcoded string shown
	 * here. The intent is to protect against the random user or
	 * application that is just writing attributes.
	 */

	/* count may include a LF at end of string */
	if (buf[cnt-1] == '\n')
		cnt--;

	if ((cnt != strlen(lpfc_soft_wwn_key)) ||
	    (strncmp(buf, lpfc_soft_wwn_key, strlen(lpfc_soft_wwn_key)) != 0))
		return -EINVAL;

	phba->soft_wwn_enable = 1;
	return count;
}
static DEVICE_ATTR(lpfc_soft_wwn_enable, S_IWUSR, NULL,
		   lpfc_soft_wwn_enable_store);

/**
 * lpfc_soft_wwpn_show - Return the cfg soft ww port name of the adapter
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: on return contains the wwpn in hexadecimal.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_soft_wwpn_show(struct device *dev, struct device_attribute *attr,
		    char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	return snprintf(buf, PAGE_SIZE, "0x%llx\n",
			(unsigned long long)phba->cfg_soft_wwpn);
}

/**
 * lpfc_soft_wwpn_store - Set the ww port name of the adapter
 * @dev class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: contains the wwpn in hexadecimal.
 * @count: number of wwpn bytes in buf
 *
 * Returns:
 * -EACCES hba reset not enabled, adapter over temp
 * -EINVAL soft wwn not enabled, count is invalid, invalid wwpn byte invalid
 * -EIO error taking adapter offline or online
 * value of count on success
 **/
static ssize_t
lpfc_soft_wwpn_store(struct device *dev, struct device_attribute *attr,
		     const char *buf, size_t count)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	struct completion online_compl;
	int stat1=0, stat2=0;
	unsigned int i, j, cnt=count;
	u8 wwpn[8];

	if (!phba->cfg_enable_hba_reset)
		return -EACCES;
	spin_lock_irq(&phba->hbalock);
	if (phba->over_temp_state == HBA_OVER_TEMP) {
		spin_unlock_irq(&phba->hbalock);
		return -EACCES;
	}
	spin_unlock_irq(&phba->hbalock);
	/* count may include a LF at end of string */
	if (buf[cnt-1] == '\n')
		cnt--;

	if (!phba->soft_wwn_enable || (cnt < 16) || (cnt > 18) ||
	    ((cnt == 17) && (*buf++ != 'x')) ||
	    ((cnt == 18) && ((*buf++ != '0') || (*buf++ != 'x'))))
		return -EINVAL;

	phba->soft_wwn_enable = 0;

	memset(wwpn, 0, sizeof(wwpn));

	/* Validate and store the new name */
	for (i=0, j=0; i < 16; i++) {
		if ((*buf >= 'a') && (*buf <= 'f'))
			j = ((j << 4) | ((*buf++ -'a') + 10));
		else if ((*buf >= 'A') && (*buf <= 'F'))
			j = ((j << 4) | ((*buf++ -'A') + 10));
		else if ((*buf >= '0') && (*buf <= '9'))
			j = ((j << 4) | (*buf++ -'0'));
		else
			return -EINVAL;
		if (i % 2) {
			wwpn[i/2] = j & 0xff;
			j = 0;
		}
	}
	phba->cfg_soft_wwpn = wwn_to_u64(wwpn);
	fc_host_port_name(shost) = phba->cfg_soft_wwpn;
	if (phba->cfg_soft_wwnn)
		fc_host_node_name(shost) = phba->cfg_soft_wwnn;

	dev_printk(KERN_NOTICE, &phba->pcidev->dev,
		   "lpfc%d: Reinitializing to use soft_wwpn\n", phba->brd_no);

	stat1 = lpfc_do_offline(phba, LPFC_EVT_OFFLINE);
	if (stat1)
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0463 lpfc_soft_wwpn attribute set failed to "
				"reinit adapter - %d\n", stat1);
	init_completion(&online_compl);
	lpfc_workq_post_event(phba, &stat2, &online_compl, LPFC_EVT_ONLINE);
	wait_for_completion(&online_compl);
	if (stat2)
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0464 lpfc_soft_wwpn attribute set failed to "
				"reinit adapter - %d\n", stat2);
	return (stat1 || stat2) ? -EIO : count;
}
static DEVICE_ATTR(lpfc_soft_wwpn, S_IRUGO | S_IWUSR,\
		   lpfc_soft_wwpn_show, lpfc_soft_wwpn_store);

/**
 * lpfc_soft_wwnn_show - Return the cfg soft ww node name for the adapter
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: on return contains the wwnn in hexadecimal.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_soft_wwnn_show(struct device *dev, struct device_attribute *attr,
		    char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct lpfc_hba *phba = ((struct lpfc_vport *)shost->hostdata)->phba;
	return snprintf(buf, PAGE_SIZE, "0x%llx\n",
			(unsigned long long)phba->cfg_soft_wwnn);
}

/**
 * lpfc_soft_wwnn_store - sets the ww node name of the adapter
 * @cdev: class device that is converted into a Scsi_host.
 * @buf: contains the ww node name in hexadecimal.
 * @count: number of wwnn bytes in buf.
 *
 * Returns:
 * -EINVAL soft wwn not enabled, count is invalid, invalid wwnn byte invalid
 * value of count on success
 **/
static ssize_t
lpfc_soft_wwnn_store(struct device *dev, struct device_attribute *attr,
		     const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct lpfc_hba *phba = ((struct lpfc_vport *)shost->hostdata)->phba;
	unsigned int i, j, cnt=count;
	u8 wwnn[8];

	/* count may include a LF at end of string */
	if (buf[cnt-1] == '\n')
		cnt--;

	if (!phba->soft_wwn_enable || (cnt < 16) || (cnt > 18) ||
	    ((cnt == 17) && (*buf++ != 'x')) ||
	    ((cnt == 18) && ((*buf++ != '0') || (*buf++ != 'x'))))
		return -EINVAL;

	/*
	 * Allow wwnn to be set many times, as long as the enable is set.
	 * However, once the wwpn is set, everything locks.
	 */

	memset(wwnn, 0, sizeof(wwnn));

	/* Validate and store the new name */
	for (i=0, j=0; i < 16; i++) {
		if ((*buf >= 'a') && (*buf <= 'f'))
			j = ((j << 4) | ((*buf++ -'a') + 10));
		else if ((*buf >= 'A') && (*buf <= 'F'))
			j = ((j << 4) | ((*buf++ -'A') + 10));
		else if ((*buf >= '0') && (*buf <= '9'))
			j = ((j << 4) | (*buf++ -'0'));
		else
			return -EINVAL;
		if (i % 2) {
			wwnn[i/2] = j & 0xff;
			j = 0;
		}
	}
	phba->cfg_soft_wwnn = wwn_to_u64(wwnn);

	dev_printk(KERN_NOTICE, &phba->pcidev->dev,
		   "lpfc%d: soft_wwnn set. Value will take effect upon "
		   "setting of the soft_wwpn\n", phba->brd_no);

	return count;
}
static DEVICE_ATTR(lpfc_soft_wwnn, S_IRUGO | S_IWUSR,\
		   lpfc_soft_wwnn_show, lpfc_soft_wwnn_store);


static int lpfc_poll = 0;
module_param(lpfc_poll, int, 0);
MODULE_PARM_DESC(lpfc_poll, "FCP ring polling mode control:"
		 " 0 - none,"
		 " 1 - poll with interrupts enabled"
		 " 3 - poll and disable FCP ring interrupts");

static DEVICE_ATTR(lpfc_poll, S_IRUGO | S_IWUSR,
		   lpfc_poll_show, lpfc_poll_store);

int  lpfc_sli_mode = 0;
module_param(lpfc_sli_mode, int, 0);
MODULE_PARM_DESC(lpfc_sli_mode, "SLI mode selector:"
		 " 0 - auto (SLI-3 if supported),"
		 " 2 - select SLI-2 even on SLI-3 capable HBAs,"
		 " 3 - select SLI-3");

int lpfc_enable_npiv = 0;
module_param(lpfc_enable_npiv, int, 0);
MODULE_PARM_DESC(lpfc_enable_npiv, "Enable NPIV functionality");
lpfc_param_show(enable_npiv);
lpfc_param_init(enable_npiv, 0, 0, 1);
static DEVICE_ATTR(lpfc_enable_npiv, S_IRUGO,
			 lpfc_enable_npiv_show, NULL);

/*
# lpfc_nodev_tmo: If set, it will hold all I/O errors on devices that disappear
# until the timer expires. Value range is [0,255]. Default value is 30.
*/
static int lpfc_nodev_tmo = LPFC_DEF_DEVLOSS_TMO;
static int lpfc_devloss_tmo = LPFC_DEF_DEVLOSS_TMO;
module_param(lpfc_nodev_tmo, int, 0);
MODULE_PARM_DESC(lpfc_nodev_tmo,
		 "Seconds driver will hold I/O waiting "
		 "for a device to come back");

/**
 * lpfc_nodev_tmo_show - Return the hba dev loss timeout value
 * @dev: class converted to a Scsi_host structure.
 * @attr: device attribute, not used.
 * @buf: on return contains the dev loss timeout in decimal.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_nodev_tmo_show(struct device *dev, struct device_attribute *attr,
		    char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	int val = 0;
	val = vport->cfg_devloss_tmo;
	return snprintf(buf, PAGE_SIZE, "%d\n",	vport->cfg_devloss_tmo);
}

/**
 * lpfc_nodev_tmo_init - Set the hba nodev timeout value
 * @vport: lpfc vport structure pointer.
 * @val: contains the nodev timeout value.
 *
 * Description:
 * If the devloss tmo is already set then nodev tmo is set to devloss tmo,
 * a kernel error message is printed and zero is returned.
 * Else if val is in range then nodev tmo and devloss tmo are set to val.
 * Otherwise nodev tmo is set to the default value.
 *
 * Returns:
 * zero if already set or if val is in range
 * -EINVAL val out of range
 **/
static int
lpfc_nodev_tmo_init(struct lpfc_vport *vport, int val)
{
	if (vport->cfg_devloss_tmo != LPFC_DEF_DEVLOSS_TMO) {
		vport->cfg_nodev_tmo = vport->cfg_devloss_tmo;
		if (val != LPFC_DEF_DEVLOSS_TMO)
			lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
					 "0407 Ignoring nodev_tmo module "
					 "parameter because devloss_tmo is "
					 "set.\n");
		return 0;
	}

	if (val >= LPFC_MIN_DEVLOSS_TMO && val <= LPFC_MAX_DEVLOSS_TMO) {
		vport->cfg_nodev_tmo = val;
		vport->cfg_devloss_tmo = val;
		return 0;
	}
	lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
			 "0400 lpfc_nodev_tmo attribute cannot be set to"
			 " %d, allowed range is [%d, %d]\n",
			 val, LPFC_MIN_DEVLOSS_TMO, LPFC_MAX_DEVLOSS_TMO);
	vport->cfg_nodev_tmo = LPFC_DEF_DEVLOSS_TMO;
	return -EINVAL;
}

/**
 * lpfc_update_rport_devloss_tmo - Update dev loss tmo value
 * @vport: lpfc vport structure pointer.
 *
 * Description:
 * Update all the ndlp's dev loss tmo with the vport devloss tmo value.
 **/
static void
lpfc_update_rport_devloss_tmo(struct lpfc_vport *vport)
{
	struct Scsi_Host  *shost;
	struct lpfc_nodelist  *ndlp;

	shost = lpfc_shost_from_vport(vport);
	spin_lock_irq(shost->host_lock);
	list_for_each_entry(ndlp, &vport->fc_nodes, nlp_listp)
		if (NLP_CHK_NODE_ACT(ndlp) && ndlp->rport)
			ndlp->rport->dev_loss_tmo = vport->cfg_devloss_tmo;
	spin_unlock_irq(shost->host_lock);
}

/**
 * lpfc_nodev_tmo_set - Set the vport nodev tmo and devloss tmo values
 * @vport: lpfc vport structure pointer.
 * @val: contains the tmo value.
 *
 * Description:
 * If the devloss tmo is already set or the vport dev loss tmo has changed
 * then a kernel error message is printed and zero is returned.
 * Else if val is in range then nodev tmo and devloss tmo are set to val.
 * Otherwise nodev tmo is set to the default value.
 *
 * Returns:
 * zero if already set or if val is in range
 * -EINVAL val out of range
 **/
static int
lpfc_nodev_tmo_set(struct lpfc_vport *vport, int val)
{
	if (vport->dev_loss_tmo_changed ||
	    (lpfc_devloss_tmo != LPFC_DEF_DEVLOSS_TMO)) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
				 "0401 Ignoring change to nodev_tmo "
				 "because devloss_tmo is set.\n");
		return 0;
	}
	if (val >= LPFC_MIN_DEVLOSS_TMO && val <= LPFC_MAX_DEVLOSS_TMO) {
		vport->cfg_nodev_tmo = val;
		vport->cfg_devloss_tmo = val;
		lpfc_update_rport_devloss_tmo(vport);
		return 0;
	}
	lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
			 "0403 lpfc_nodev_tmo attribute cannot be set to"
			 "%d, allowed range is [%d, %d]\n",
			 val, LPFC_MIN_DEVLOSS_TMO, LPFC_MAX_DEVLOSS_TMO);
	return -EINVAL;
}

lpfc_vport_param_store(nodev_tmo)

static DEVICE_ATTR(lpfc_nodev_tmo, S_IRUGO | S_IWUSR,
		   lpfc_nodev_tmo_show, lpfc_nodev_tmo_store);

/*
# lpfc_devloss_tmo: If set, it will hold all I/O errors on devices that
# disappear until the timer expires. Value range is [0,255]. Default
# value is 30.
*/
module_param(lpfc_devloss_tmo, int, 0);
MODULE_PARM_DESC(lpfc_devloss_tmo,
		 "Seconds driver will hold I/O waiting "
		 "for a device to come back");
lpfc_vport_param_init(devloss_tmo, LPFC_DEF_DEVLOSS_TMO,
		      LPFC_MIN_DEVLOSS_TMO, LPFC_MAX_DEVLOSS_TMO)
lpfc_vport_param_show(devloss_tmo)

/**
 * lpfc_devloss_tmo_set - Sets vport nodev tmo, devloss tmo values, changed bit
 * @vport: lpfc vport structure pointer.
 * @val: contains the tmo value.
 *
 * Description:
 * If val is in a valid range then set the vport nodev tmo,
 * devloss tmo, also set the vport dev loss tmo changed flag.
 * Else a kernel error message is printed.
 *
 * Returns:
 * zero if val is in range
 * -EINVAL val out of range
 **/
static int
lpfc_devloss_tmo_set(struct lpfc_vport *vport, int val)
{
	if (val >= LPFC_MIN_DEVLOSS_TMO && val <= LPFC_MAX_DEVLOSS_TMO) {
		vport->cfg_nodev_tmo = val;
		vport->cfg_devloss_tmo = val;
		vport->dev_loss_tmo_changed = 1;
		lpfc_update_rport_devloss_tmo(vport);
		return 0;
	}

	lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
			 "0404 lpfc_devloss_tmo attribute cannot be set to"
			 " %d, allowed range is [%d, %d]\n",
			 val, LPFC_MIN_DEVLOSS_TMO, LPFC_MAX_DEVLOSS_TMO);
	return -EINVAL;
}

lpfc_vport_param_store(devloss_tmo)
static DEVICE_ATTR(lpfc_devloss_tmo, S_IRUGO | S_IWUSR,
		   lpfc_devloss_tmo_show, lpfc_devloss_tmo_store);

/*
# lpfc_log_verbose: Only turn this flag on if you are willing to risk being
# deluged with LOTS of information.
# You can set a bit mask to record specific types of verbose messages:
# See lpfc_logmsh.h for definitions.
*/
LPFC_VPORT_ATTR_HEX_RW(log_verbose, 0x0, 0x0, 0xffffffff,
		       "Verbose logging bit-mask");

/*
# lpfc_enable_da_id: This turns on the DA_ID CT command that deregisters
# objects that have been registered with the nameserver after login.
*/
LPFC_VPORT_ATTR_R(enable_da_id, 0, 0, 1,
		  "Deregister nameserver objects before LOGO");

/*
# lun_queue_depth:  This parameter is used to limit the number of outstanding
# commands per FCP LUN. Value range is [1,128]. Default value is 30.
*/
LPFC_VPORT_ATTR_R(lun_queue_depth, 30, 1, 128,
		  "Max number of FCP commands we can queue to a specific LUN");

/*
# hba_queue_depth:  This parameter is used to limit the number of outstanding
# commands per lpfc HBA. Value range is [32,8192]. If this parameter
# value is greater than the maximum number of exchanges supported by the HBA,
# then maximum number of exchanges supported by the HBA is used to determine
# the hba_queue_depth.
*/
LPFC_ATTR_R(hba_queue_depth, 8192, 32, 8192,
	    "Max number of FCP commands we can queue to a lpfc HBA");

/*
# peer_port_login:  This parameter allows/prevents logins
# between peer ports hosted on the same physical port.
# When this parameter is set 0 peer ports of same physical port
# are not allowed to login to each other.
# When this parameter is set 1 peer ports of same physical port
# are allowed to login to each other.
# Default value of this parameter is 0.
*/
LPFC_VPORT_ATTR_R(peer_port_login, 0, 0, 1,
		  "Allow peer ports on the same physical port to login to each "
		  "other.");

/*
# restrict_login:  This parameter allows/prevents logins
# between Virtual Ports and remote initiators.
# When this parameter is not set (0) Virtual Ports will accept PLOGIs from
# other initiators and will attempt to PLOGI all remote ports.
# When this parameter is set (1) Virtual Ports will reject PLOGIs from
# remote ports and will not attempt to PLOGI to other initiators.
# This parameter does not restrict to the physical port.
# This parameter does not restrict logins to Fabric resident remote ports.
# Default value of this parameter is 1.
*/
static int lpfc_restrict_login = 1;
module_param(lpfc_restrict_login, int, 0);
MODULE_PARM_DESC(lpfc_restrict_login,
		 "Restrict virtual ports login to remote initiators.");
lpfc_vport_param_show(restrict_login);

/**
 * lpfc_restrict_login_init - Set the vport restrict login flag
 * @vport: lpfc vport structure pointer.
 * @val: contains the restrict login value.
 *
 * Description:
 * If val is not in a valid range then log a kernel error message and set
 * the vport restrict login to one.
 * If the port type is physical clear the restrict login flag and return.
 * Else set the restrict login flag to val.
 *
 * Returns:
 * zero if val is in range
 * -EINVAL val out of range
 **/
static int
lpfc_restrict_login_init(struct lpfc_vport *vport, int val)
{
	if (val < 0 || val > 1) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
				 "0422 lpfc_restrict_login attribute cannot "
				 "be set to %d, allowed range is [0, 1]\n",
				 val);
		vport->cfg_restrict_login = 1;
		return -EINVAL;
	}
	if (vport->port_type == LPFC_PHYSICAL_PORT) {
		vport->cfg_restrict_login = 0;
		return 0;
	}
	vport->cfg_restrict_login = val;
	return 0;
}

/**
 * lpfc_restrict_login_set - Set the vport restrict login flag
 * @vport: lpfc vport structure pointer.
 * @val: contains the restrict login value.
 *
 * Description:
 * If val is not in a valid range then log a kernel error message and set
 * the vport restrict login to one.
 * If the port type is physical and the val is not zero log a kernel
 * error message, clear the restrict login flag and return zero.
 * Else set the restrict login flag to val.
 *
 * Returns:
 * zero if val is in range
 * -EINVAL val out of range
 **/
static int
lpfc_restrict_login_set(struct lpfc_vport *vport, int val)
{
	if (val < 0 || val > 1) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
				 "0425 lpfc_restrict_login attribute cannot "
				 "be set to %d, allowed range is [0, 1]\n",
				 val);
		vport->cfg_restrict_login = 1;
		return -EINVAL;
	}
	if (vport->port_type == LPFC_PHYSICAL_PORT && val != 0) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
				 "0468 lpfc_restrict_login must be 0 for "
				 "Physical ports.\n");
		vport->cfg_restrict_login = 0;
		return 0;
	}
	vport->cfg_restrict_login = val;
	return 0;
}
lpfc_vport_param_store(restrict_login);
static DEVICE_ATTR(lpfc_restrict_login, S_IRUGO | S_IWUSR,
		   lpfc_restrict_login_show, lpfc_restrict_login_store);

/*
# Some disk devices have a "select ID" or "select Target" capability.
# From a protocol standpoint "select ID" usually means select the
# Fibre channel "ALPA".  In the FC-AL Profile there is an "informative
# annex" which contains a table that maps a "select ID" (a number
# between 0 and 7F) to an ALPA.  By default, for compatibility with
# older drivers, the lpfc driver scans this table from low ALPA to high
# ALPA.
#
# Turning on the scan-down variable (on  = 1, off = 0) will
# cause the lpfc driver to use an inverted table, effectively
# scanning ALPAs from high to low. Value range is [0,1]. Default value is 1.
#
# (Note: This "select ID" functionality is a LOOP ONLY characteristic
# and will not work across a fabric. Also this parameter will take
# effect only in the case when ALPA map is not available.)
*/
LPFC_VPORT_ATTR_R(scan_down, 1, 0, 1,
		  "Start scanning for devices from highest ALPA to lowest");

/*
# lpfc_topology:  link topology for init link
#            0x0  = attempt loop mode then point-to-point
#            0x01 = internal loopback mode
#            0x02 = attempt point-to-point mode only
#            0x04 = attempt loop mode only
#            0x06 = attempt point-to-point mode then loop
# Set point-to-point mode if you want to run as an N_Port.
# Set loop mode if you want to run as an NL_Port. Value range is [0,0x6].
# Default value is 0.
*/

/**
 * lpfc_topology_set - Set the adapters topology field
 * @phba: lpfc_hba pointer.
 * @val: topology value.
 *
 * Description:
 * If val is in a valid range then set the adapter's topology field and
 * issue a lip; if the lip fails reset the topology to the old value.
 *
 * If the value is not in range log a kernel error message and return an error.
 *
 * Returns:
 * zero if val is in range and lip okay
 * non-zero return value from lpfc_issue_lip()
 * -EINVAL val out of range
 **/
static ssize_t
lpfc_topology_store(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	int val = 0;
	int nolip = 0;
	const char *val_buf = buf;
	int err;
	uint32_t prev_val;

	if (!strncmp(buf, "nolip ", strlen("nolip "))) {
		nolip = 1;
		val_buf = &buf[strlen("nolip ")];
	}

	if (!isdigit(val_buf[0]))
		return -EINVAL;
	if (sscanf(val_buf, "%i", &val) != 1)
		return -EINVAL;

	if (val >= 0 && val <= 6) {
		prev_val = phba->cfg_topology;
		phba->cfg_topology = val;
		if (nolip)
			return strlen(buf);

		err = lpfc_issue_lip(lpfc_shost_from_vport(phba->pport));
		if (err) {
			phba->cfg_topology = prev_val;
			return -EINVAL;
		} else
			return strlen(buf);
	}
	lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
		"%d:0467 lpfc_topology attribute cannot be set to %d, "
		"allowed range is [0, 6]\n",
		phba->brd_no, val);
	return -EINVAL;
}
static int lpfc_topology = 0;
module_param(lpfc_topology, int, 0);
MODULE_PARM_DESC(lpfc_topology, "Select Fibre Channel topology");
lpfc_param_show(topology)
lpfc_param_init(topology, 0, 0, 6)
static DEVICE_ATTR(lpfc_topology, S_IRUGO | S_IWUSR,
		lpfc_topology_show, lpfc_topology_store);

/**
 * lpfc_static_vport_show: Read callback function for
 *   lpfc_static_vport sysfs file.
 * @dev: Pointer to class device object.
 * @attr: device attribute structure.
 * @buf: Data buffer.
 *
 * This function is the read call back function for
 * lpfc_static_vport sysfs file. The lpfc_static_vport
 * sysfs file report the mageability of the vport.
 **/
static ssize_t
lpfc_static_vport_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	if (vport->vport_flag & STATIC_VPORT)
		sprintf(buf, "1\n");
	else
		sprintf(buf, "0\n");

	return strlen(buf);
}

/*
 * Sysfs attribute to control the statistical data collection.
 */
static DEVICE_ATTR(lpfc_static_vport, S_IRUGO,
		   lpfc_static_vport_show, NULL);

/**
 * lpfc_stat_data_ctrl_store - write call back for lpfc_stat_data_ctrl sysfs file
 * @dev: Pointer to class device.
 * @buf: Data buffer.
 * @count: Size of the data buffer.
 *
 * This function get called when an user write to the lpfc_stat_data_ctrl
 * sysfs file. This function parse the command written to the sysfs file
 * and take appropriate action. These commands are used for controlling
 * driver statistical data collection.
 * Following are the command this function handles.
 *
 *    setbucket <bucket_type> <base> <step>
 *			       = Set the latency buckets.
 *    destroybucket            = destroy all the buckets.
 *    start                    = start data collection
 *    stop                     = stop data collection
 *    reset                    = reset the collected data
 **/
static ssize_t
lpfc_stat_data_ctrl_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
#define LPFC_MAX_DATA_CTRL_LEN 1024
	static char bucket_data[LPFC_MAX_DATA_CTRL_LEN];
	unsigned long i;
	char *str_ptr, *token;
	struct lpfc_vport **vports;
	struct Scsi_Host *v_shost;
	char *bucket_type_str, *base_str, *step_str;
	unsigned long base, step, bucket_type;

	if (!strncmp(buf, "setbucket", strlen("setbucket"))) {
		if (strlen(buf) > (LPFC_MAX_DATA_CTRL_LEN - 1))
			return -EINVAL;

		strcpy(bucket_data, buf);
		str_ptr = &bucket_data[0];
		/* Ignore this token - this is command token */
		token = strsep(&str_ptr, "\t ");
		if (!token)
			return -EINVAL;

		bucket_type_str = strsep(&str_ptr, "\t ");
		if (!bucket_type_str)
			return -EINVAL;

		if (!strncmp(bucket_type_str, "linear", strlen("linear")))
			bucket_type = LPFC_LINEAR_BUCKET;
		else if (!strncmp(bucket_type_str, "power2", strlen("power2")))
			bucket_type = LPFC_POWER2_BUCKET;
		else
			return -EINVAL;

		base_str = strsep(&str_ptr, "\t ");
		if (!base_str)
			return -EINVAL;
		base = simple_strtoul(base_str, NULL, 0);

		step_str = strsep(&str_ptr, "\t ");
		if (!step_str)
			return -EINVAL;
		step = simple_strtoul(step_str, NULL, 0);
		if (!step)
			return -EINVAL;

		/* Block the data collection for every vport */
		vports = lpfc_create_vport_work_array(phba);
		if (vports == NULL)
			return -ENOMEM;

		for (i = 0; i <= phba->max_vports && vports[i] != NULL; i++) {
			v_shost = lpfc_shost_from_vport(vports[i]);
			spin_lock_irq(v_shost->host_lock);
			/* Block and reset data collection */
			vports[i]->stat_data_blocked = 1;
			if (vports[i]->stat_data_enabled)
				lpfc_vport_reset_stat_data(vports[i]);
			spin_unlock_irq(v_shost->host_lock);
		}

		/* Set the bucket attributes */
		phba->bucket_type = bucket_type;
		phba->bucket_base = base;
		phba->bucket_step = step;

		for (i = 0; i <= phba->max_vports && vports[i] != NULL; i++) {
			v_shost = lpfc_shost_from_vport(vports[i]);

			/* Unblock data collection */
			spin_lock_irq(v_shost->host_lock);
			vports[i]->stat_data_blocked = 0;
			spin_unlock_irq(v_shost->host_lock);
		}
		lpfc_destroy_vport_work_array(phba, vports);
		return strlen(buf);
	}

	if (!strncmp(buf, "destroybucket", strlen("destroybucket"))) {
		vports = lpfc_create_vport_work_array(phba);
		if (vports == NULL)
			return -ENOMEM;

		for (i = 0; i <= phba->max_vports && vports[i] != NULL; i++) {
			v_shost = lpfc_shost_from_vport(vports[i]);
			spin_lock_irq(shost->host_lock);
			vports[i]->stat_data_blocked = 1;
			lpfc_free_bucket(vport);
			vport->stat_data_enabled = 0;
			vports[i]->stat_data_blocked = 0;
			spin_unlock_irq(shost->host_lock);
		}
		lpfc_destroy_vport_work_array(phba, vports);
		phba->bucket_type = LPFC_NO_BUCKET;
		phba->bucket_base = 0;
		phba->bucket_step = 0;
		return strlen(buf);
	}

	if (!strncmp(buf, "start", strlen("start"))) {
		/* If no buckets configured return error */
		if (phba->bucket_type == LPFC_NO_BUCKET)
			return -EINVAL;
		spin_lock_irq(shost->host_lock);
		if (vport->stat_data_enabled) {
			spin_unlock_irq(shost->host_lock);
			return strlen(buf);
		}
		lpfc_alloc_bucket(vport);
		vport->stat_data_enabled = 1;
		spin_unlock_irq(shost->host_lock);
		return strlen(buf);
	}

	if (!strncmp(buf, "stop", strlen("stop"))) {
		spin_lock_irq(shost->host_lock);
		if (vport->stat_data_enabled == 0) {
			spin_unlock_irq(shost->host_lock);
			return strlen(buf);
		}
		lpfc_free_bucket(vport);
		vport->stat_data_enabled = 0;
		spin_unlock_irq(shost->host_lock);
		return strlen(buf);
	}

	if (!strncmp(buf, "reset", strlen("reset"))) {
		if ((phba->bucket_type == LPFC_NO_BUCKET)
			|| !vport->stat_data_enabled)
			return strlen(buf);
		spin_lock_irq(shost->host_lock);
		vport->stat_data_blocked = 1;
		lpfc_vport_reset_stat_data(vport);
		vport->stat_data_blocked = 0;
		spin_unlock_irq(shost->host_lock);
		return strlen(buf);
	}
	return -EINVAL;
}


/**
 * lpfc_stat_data_ctrl_show - Read function for lpfc_stat_data_ctrl sysfs file
 * @dev: Pointer to class device object.
 * @buf: Data buffer.
 *
 * This function is the read call back function for
 * lpfc_stat_data_ctrl sysfs file. This function report the
 * current statistical data collection state.
 **/
static ssize_t
lpfc_stat_data_ctrl_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	int index = 0;
	int i;
	char *bucket_type;
	unsigned long bucket_value;

	switch (phba->bucket_type) {
	case LPFC_LINEAR_BUCKET:
		bucket_type = "linear";
		break;
	case LPFC_POWER2_BUCKET:
		bucket_type = "power2";
		break;
	default:
		bucket_type = "No Bucket";
		break;
	}

	sprintf(&buf[index], "Statistical Data enabled :%d, "
		"blocked :%d, Bucket type :%s, Bucket base :%d,"
		" Bucket step :%d\nLatency Ranges :",
		vport->stat_data_enabled, vport->stat_data_blocked,
		bucket_type, phba->bucket_base, phba->bucket_step);
	index = strlen(buf);
	if (phba->bucket_type != LPFC_NO_BUCKET) {
		for (i = 0; i < LPFC_MAX_BUCKET_COUNT; i++) {
			if (phba->bucket_type == LPFC_LINEAR_BUCKET)
				bucket_value = phba->bucket_base +
					phba->bucket_step * i;
			else
				bucket_value = phba->bucket_base +
				(1 << i) * phba->bucket_step;

			if (index + 10 > PAGE_SIZE)
				break;
			sprintf(&buf[index], "%08ld ", bucket_value);
			index = strlen(buf);
		}
	}
	sprintf(&buf[index], "\n");
	return strlen(buf);
}

/*
 * Sysfs attribute to control the statistical data collection.
 */
static DEVICE_ATTR(lpfc_stat_data_ctrl, S_IRUGO | S_IWUSR,
		   lpfc_stat_data_ctrl_show, lpfc_stat_data_ctrl_store);

/*
 * lpfc_drvr_stat_data: sysfs attr to get driver statistical data.
 */

/*
 * Each Bucket takes 11 characters and 1 new line + 17 bytes WWN
 * for each target.
 */
#define STAT_DATA_SIZE_PER_TARGET(NUM_BUCKETS) ((NUM_BUCKETS) * 11 + 18)
#define MAX_STAT_DATA_SIZE_PER_TARGET \
	STAT_DATA_SIZE_PER_TARGET(LPFC_MAX_BUCKET_COUNT)


/**
 * sysfs_drvr_stat_data_read - Read function for lpfc_drvr_stat_data attribute
 * @kobj: Pointer to the kernel object
 * @bin_attr: Attribute object
 * @buff: Buffer pointer
 * @off: File offset
 * @count: Buffer size
 *
 * This function is the read call back function for lpfc_drvr_stat_data
 * sysfs file. This function export the statistical data to user
 * applications.
 **/
static ssize_t
sysfs_drvr_stat_data_read(struct kobject *kobj, struct bin_attribute *bin_attr,
		char *buf, loff_t off, size_t count)
{
	struct device *dev = container_of(kobj, struct device,
		kobj);
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	int i = 0, index = 0;
	unsigned long nport_index;
	struct lpfc_nodelist *ndlp = NULL;
	nport_index = (unsigned long)off /
		MAX_STAT_DATA_SIZE_PER_TARGET;

	if (!vport->stat_data_enabled || vport->stat_data_blocked
		|| (phba->bucket_type == LPFC_NO_BUCKET))
		return 0;

	spin_lock_irq(shost->host_lock);
	list_for_each_entry(ndlp, &vport->fc_nodes, nlp_listp) {
		if (!NLP_CHK_NODE_ACT(ndlp) || !ndlp->lat_data)
			continue;

		if (nport_index > 0) {
			nport_index--;
			continue;
		}

		if ((index + MAX_STAT_DATA_SIZE_PER_TARGET)
			> count)
			break;

		if (!ndlp->lat_data)
			continue;

		/* Print the WWN */
		sprintf(&buf[index], "%02x%02x%02x%02x%02x%02x%02x%02x:",
			ndlp->nlp_portname.u.wwn[0],
			ndlp->nlp_portname.u.wwn[1],
			ndlp->nlp_portname.u.wwn[2],
			ndlp->nlp_portname.u.wwn[3],
			ndlp->nlp_portname.u.wwn[4],
			ndlp->nlp_portname.u.wwn[5],
			ndlp->nlp_portname.u.wwn[6],
			ndlp->nlp_portname.u.wwn[7]);

		index = strlen(buf);

		for (i = 0; i < LPFC_MAX_BUCKET_COUNT; i++) {
			sprintf(&buf[index], "%010u,",
				ndlp->lat_data[i].cmd_count);
			index = strlen(buf);
		}
		sprintf(&buf[index], "\n");
		index = strlen(buf);
	}
	spin_unlock_irq(shost->host_lock);
	return index;
}

static struct bin_attribute sysfs_drvr_stat_data_attr = {
	.attr = {
		.name = "lpfc_drvr_stat_data",
		.mode = S_IRUSR,
		.owner = THIS_MODULE,
	},
	.size = LPFC_MAX_TARGET * MAX_STAT_DATA_SIZE_PER_TARGET,
	.read = sysfs_drvr_stat_data_read,
	.write = NULL,
};

/*
# lpfc_link_speed: Link speed selection for initializing the Fibre Channel
# connection.
#       0  = auto select (default)
#       1  = 1 Gigabaud
#       2  = 2 Gigabaud
#       4  = 4 Gigabaud
#       8  = 8 Gigabaud
# Value range is [0,8]. Default value is 0.
*/

/**
 * lpfc_link_speed_set - Set the adapters link speed
 * @phba: lpfc_hba pointer.
 * @val: link speed value.
 *
 * Description:
 * If val is in a valid range then set the adapter's link speed field and
 * issue a lip; if the lip fails reset the link speed to the old value.
 *
 * Notes:
 * If the value is not in range log a kernel error message and return an error.
 *
 * Returns:
 * zero if val is in range and lip okay.
 * non-zero return value from lpfc_issue_lip()
 * -EINVAL val out of range
 **/
static ssize_t
lpfc_link_speed_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	int val = 0;
	int nolip = 0;
	const char *val_buf = buf;
	int err;
	uint32_t prev_val;

	if (!strncmp(buf, "nolip ", strlen("nolip "))) {
		nolip = 1;
		val_buf = &buf[strlen("nolip ")];
	}

	if (!isdigit(val_buf[0]))
		return -EINVAL;
	if (sscanf(val_buf, "%i", &val) != 1)
		return -EINVAL;

	if (((val == LINK_SPEED_1G) && !(phba->lmt & LMT_1Gb)) ||
		((val == LINK_SPEED_2G) && !(phba->lmt & LMT_2Gb)) ||
		((val == LINK_SPEED_4G) && !(phba->lmt & LMT_4Gb)) ||
		((val == LINK_SPEED_8G) && !(phba->lmt & LMT_8Gb)) ||
		((val == LINK_SPEED_10G) && !(phba->lmt & LMT_10Gb)))
		return -EINVAL;

	if ((val >= 0 && val <= 8)
		&& (LPFC_LINK_SPEED_BITMAP & (1 << val))) {
		prev_val = phba->cfg_link_speed;
		phba->cfg_link_speed = val;
		if (nolip)
			return strlen(buf);

		err = lpfc_issue_lip(lpfc_shost_from_vport(phba->pport));
		if (err) {
			phba->cfg_link_speed = prev_val;
			return -EINVAL;
		} else
			return strlen(buf);
	}

	lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
		"%d:0469 lpfc_link_speed attribute cannot be set to %d, "
		"allowed range is [0, 8]\n",
		phba->brd_no, val);
	return -EINVAL;
}

static int lpfc_link_speed = 0;
module_param(lpfc_link_speed, int, 0);
MODULE_PARM_DESC(lpfc_link_speed, "Select link speed");
lpfc_param_show(link_speed)

/**
 * lpfc_link_speed_init - Set the adapters link speed
 * @phba: lpfc_hba pointer.
 * @val: link speed value.
 *
 * Description:
 * If val is in a valid range then set the adapter's link speed field.
 *
 * Notes:
 * If the value is not in range log a kernel error message, clear the link
 * speed and return an error.
 *
 * Returns:
 * zero if val saved.
 * -EINVAL val out of range
 **/
static int
lpfc_link_speed_init(struct lpfc_hba *phba, int val)
{
	if ((val >= 0 && val <= LPFC_MAX_LINK_SPEED)
		&& (LPFC_LINK_SPEED_BITMAP & (1 << val))) {
		phba->cfg_link_speed = val;
		return 0;
	}
	lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
			"0405 lpfc_link_speed attribute cannot "
			"be set to %d, allowed values are "
			"["LPFC_LINK_SPEED_STRING"]\n", val);
	phba->cfg_link_speed = 0;
	return -EINVAL;
}

static DEVICE_ATTR(lpfc_link_speed, S_IRUGO | S_IWUSR,
		lpfc_link_speed_show, lpfc_link_speed_store);

/*
# lpfc_fcp_class:  Determines FC class to use for the FCP protocol.
# Value range is [2,3]. Default value is 3.
*/
LPFC_VPORT_ATTR_R(fcp_class, 3, 2, 3,
		  "Select Fibre Channel class of service for FCP sequences");

/*
# lpfc_use_adisc: Use ADISC for FCP rediscovery instead of PLOGI. Value range
# is [0,1]. Default value is 0.
*/
LPFC_VPORT_ATTR_RW(use_adisc, 0, 0, 1,
		   "Use ADISC on rediscovery to authenticate FCP devices");

/*
# lpfc_max_scsicmpl_time: Use scsi command completion time to control I/O queue
# depth. Default value is 0. When the value of this parameter is zero the
# SCSI command completion time is not used for controlling I/O queue depth. When
# the parameter is set to a non-zero value, the I/O queue depth is controlled
# to limit the I/O completion time to the parameter value.
# The value is set in milliseconds.
*/
static int lpfc_max_scsicmpl_time;
module_param(lpfc_max_scsicmpl_time, int, 0);
MODULE_PARM_DESC(lpfc_max_scsicmpl_time,
	"Use command completion time to control queue depth");
lpfc_vport_param_show(max_scsicmpl_time);
lpfc_vport_param_init(max_scsicmpl_time, 0, 0, 60000);
static int
lpfc_max_scsicmpl_time_set(struct lpfc_vport *vport, int val)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	struct lpfc_nodelist *ndlp, *next_ndlp;

	if (val == vport->cfg_max_scsicmpl_time)
		return 0;
	if ((val < 0) || (val > 60000))
		return -EINVAL;
	vport->cfg_max_scsicmpl_time = val;

	spin_lock_irq(shost->host_lock);
	list_for_each_entry_safe(ndlp, next_ndlp, &vport->fc_nodes, nlp_listp) {
		if (!NLP_CHK_NODE_ACT(ndlp))
			continue;
		if (ndlp->nlp_state == NLP_STE_UNUSED_NODE)
			continue;
		ndlp->cmd_qdepth = LPFC_MAX_TGT_QDEPTH;
	}
	spin_unlock_irq(shost->host_lock);
	return 0;
}
lpfc_vport_param_store(max_scsicmpl_time);
static DEVICE_ATTR(lpfc_max_scsicmpl_time, S_IRUGO | S_IWUSR,
		   lpfc_max_scsicmpl_time_show,
		   lpfc_max_scsicmpl_time_store);

/*
# lpfc_ack0: Use ACK0, instead of ACK1 for class 2 acknowledgement. Value
# range is [0,1]. Default value is 0.
*/
LPFC_ATTR_R(ack0, 0, 0, 1, "Enable ACK0 support");

/*
# lpfc_cr_delay & lpfc_cr_count: Default values for I/O colaesing
# cr_delay (msec) or cr_count outstanding commands. cr_delay can take
# value [0,63]. cr_count can take value [1,255]. Default value of cr_delay
# is 0. Default value of cr_count is 1. The cr_count feature is disabled if
# cr_delay is set to 0.
*/
LPFC_ATTR_RW(cr_delay, 0, 0, 63, "A count of milliseconds after which an "
		"interrupt response is generated");

LPFC_ATTR_RW(cr_count, 1, 1, 255, "A count of I/O completions after which an "
		"interrupt response is generated");

/*
# lpfc_multi_ring_support:  Determines how many rings to spread available
# cmd/rsp IOCB entries across.
# Value range is [1,2]. Default value is 1.
*/
LPFC_ATTR_R(multi_ring_support, 1, 1, 2, "Determines number of primary "
		"SLI rings to spread IOCB entries across");

/*
# lpfc_multi_ring_rctl:  If lpfc_multi_ring_support is enabled, this
# identifies what rctl value to configure the additional ring for.
# Value range is [1,0xff]. Default value is 4 (Unsolicated Data).
*/
LPFC_ATTR_R(multi_ring_rctl, FC_UNSOL_DATA, 1,
	     255, "Identifies RCTL for additional ring configuration");

/*
# lpfc_multi_ring_type:  If lpfc_multi_ring_support is enabled, this
# identifies what type value to configure the additional ring for.
# Value range is [1,0xff]. Default value is 5 (LLC/SNAP).
*/
LPFC_ATTR_R(multi_ring_type, FC_LLC_SNAP, 1,
	     255, "Identifies TYPE for additional ring configuration");

/*
# lpfc_fdmi_on: controls FDMI support.
#       0 = no FDMI support
#       1 = support FDMI without attribute of hostname
#       2 = support FDMI with attribute of hostname
# Value range [0,2]. Default value is 0.
*/
LPFC_VPORT_ATTR_RW(fdmi_on, 0, 0, 2, "Enable FDMI support");

/*
# Specifies the maximum number of ELS cmds we can have outstanding (for
# discovery). Value range is [1,64]. Default value = 32.
*/
LPFC_VPORT_ATTR(discovery_threads, 32, 1, 64, "Maximum number of ELS commands "
		 "during discovery");

/*
# lpfc_max_luns: maximum allowed LUN.
# Value range is [0,65535]. Default value is 255.
# NOTE: The SCSI layer might probe all allowed LUN on some old targets.
*/
LPFC_VPORT_ATTR_R(max_luns, 255, 0, 65535, "Maximum allowed LUN");

/*
# lpfc_poll_tmo: .Milliseconds driver will wait between polling FCP ring.
# Value range is [1,255], default value is 10.
*/
LPFC_ATTR_RW(poll_tmo, 10, 1, 255,
	     "Milliseconds driver will wait between polling FCP ring");

/*
# lpfc_use_msi: Use MSI (Message Signaled Interrupts) in systems that
#		support this feature
#       0  = MSI disabled (default)
#       1  = MSI enabled
#       2  = MSI-X enabled
# Value range is [0,2]. Default value is 0.
*/
LPFC_ATTR_R(use_msi, 0, 0, 2, "Use Message Signaled Interrupts (1) or "
	    "MSI-X (2), if possible");

/*
# lpfc_fcp_imax: Set the maximum number of fast-path FCP interrupts per second
#
# Value range is [636,651042]. Default value is 10000.
*/
LPFC_ATTR_R(fcp_imax, LPFC_FP_DEF_IMAX, LPFC_MIM_IMAX, LPFC_DMULT_CONST,
	    "Set the maximum number of fast-path FCP interrupts per second");

/*
# lpfc_fcp_wq_count: Set the number of fast-path FCP work queues
#
# Value range is [1,31]. Default value is 4.
*/
LPFC_ATTR_R(fcp_wq_count, LPFC_FP_WQN_DEF, LPFC_FP_WQN_MIN, LPFC_FP_WQN_MAX,
	    "Set the number of fast-path FCP work queues, if possible");

/*
# lpfc_fcp_eq_count: Set the number of fast-path FCP event queues
#
# Value range is [1,7]. Default value is 1.
*/
LPFC_ATTR_R(fcp_eq_count, LPFC_FP_EQN_DEF, LPFC_FP_EQN_MIN, LPFC_FP_EQN_MAX,
	    "Set the number of fast-path FCP event queues, if possible");

/*
# lpfc_enable_hba_reset: Allow or prevent HBA resets to the hardware.
#       0  = HBA resets disabled
#       1  = HBA resets enabled (default)
# Value range is [0,1]. Default value is 1.
*/
LPFC_ATTR_R(enable_hba_reset, 1, 0, 1, "Enable HBA resets from the driver.");

/*
# lpfc_enable_hba_heartbeat: Enable HBA heartbeat timer..
#       0  = HBA Heartbeat disabled
#       1  = HBA Heartbeat enabled (default)
# Value range is [0,1]. Default value is 1.
*/
LPFC_ATTR_R(enable_hba_heartbeat, 1, 0, 1, "Enable HBA Heartbeat.");

/*
# lpfc_enable_bg: Enable BlockGuard (Emulex's Implementation of T10-DIF)
#       0  = BlockGuard disabled (default)
#       1  = BlockGuard enabled
# Value range is [0,1]. Default value is 0.
*/
LPFC_ATTR_R(enable_bg, 0, 0, 1, "Enable BlockGuard Support");

/*
# lpfc_enable_fip: When set, FIP is required to start discovery. If not
# set, the driver will add an FCF record manually if the port has no
# FCF records available and start discovery.
# Value range is [0,1]. Default value is 1 (enabled)
*/
LPFC_ATTR_RW(enable_fip, 0, 0, 1, "Enable FIP Discovery");


/*
# lpfc_prot_mask: i
#	- Bit mask of host protection capabilities used to register with the
#	  SCSI mid-layer
# 	- Only meaningful if BG is turned on (lpfc_enable_bg=1).
#	- Allows you to ultimately specify which profiles to use
#	- Default will result in registering capabilities for all profiles.
#
*/
unsigned int lpfc_prot_mask =   SHOST_DIX_TYPE0_PROTECTION;

module_param(lpfc_prot_mask, uint, 0);
MODULE_PARM_DESC(lpfc_prot_mask, "host protection mask");

/*
# lpfc_prot_guard: i
#	- Bit mask of protection guard types to register with the SCSI mid-layer
# 	- Guard types are currently either 1) IP checksum 2) T10-DIF CRC
#	- Allows you to ultimately specify which profiles to use
#	- Default will result in registering capabilities for all guard types
#
*/
unsigned char lpfc_prot_guard = SHOST_DIX_GUARD_IP;
module_param(lpfc_prot_guard, byte, 0);
MODULE_PARM_DESC(lpfc_prot_guard, "host protection guard type");


/*
 * lpfc_sg_seg_cnt - Initial Maximum DMA Segment Count
 * This value can be set to values between 64 and 256. The default value is
 * 64, but may be increased to allow for larger Max I/O sizes. The scsi layer
 * will be allowed to request I/Os of sizes up to (MAX_SEG_COUNT * SEG_SIZE).
 */
LPFC_ATTR_R(sg_seg_cnt, LPFC_DEFAULT_SG_SEG_CNT, LPFC_DEFAULT_SG_SEG_CNT,
	    LPFC_MAX_SG_SEG_CNT, "Max Scatter Gather Segment Count");

LPFC_ATTR_R(prot_sg_seg_cnt, LPFC_DEFAULT_PROT_SG_SEG_CNT,
		LPFC_DEFAULT_PROT_SG_SEG_CNT, LPFC_MAX_PROT_SG_SEG_CNT,
		"Max Protection Scatter Gather Segment Count");

struct device_attribute *lpfc_hba_attrs[] = {
	&dev_attr_bg_info,
	&dev_attr_bg_guard_err,
	&dev_attr_bg_apptag_err,
	&dev_attr_bg_reftag_err,
	&dev_attr_info,
	&dev_attr_serialnum,
	&dev_attr_modeldesc,
	&dev_attr_modelname,
	&dev_attr_programtype,
	&dev_attr_portnum,
	&dev_attr_fwrev,
	&dev_attr_hdw,
	&dev_attr_option_rom_version,
	&dev_attr_link_state,
	&dev_attr_num_discovered_ports,
	&dev_attr_menlo_mgmt_mode,
	&dev_attr_lpfc_drvr_version,
	&dev_attr_lpfc_temp_sensor,
	&dev_attr_lpfc_log_verbose,
	&dev_attr_lpfc_lun_queue_depth,
	&dev_attr_lpfc_hba_queue_depth,
	&dev_attr_lpfc_peer_port_login,
	&dev_attr_lpfc_nodev_tmo,
	&dev_attr_lpfc_devloss_tmo,
	&dev_attr_lpfc_enable_fip,
	&dev_attr_lpfc_fcp_class,
	&dev_attr_lpfc_use_adisc,
	&dev_attr_lpfc_ack0,
	&dev_attr_lpfc_topology,
	&dev_attr_lpfc_scan_down,
	&dev_attr_lpfc_link_speed,
	&dev_attr_lpfc_cr_delay,
	&dev_attr_lpfc_cr_count,
	&dev_attr_lpfc_multi_ring_support,
	&dev_attr_lpfc_multi_ring_rctl,
	&dev_attr_lpfc_multi_ring_type,
	&dev_attr_lpfc_fdmi_on,
	&dev_attr_lpfc_max_luns,
	&dev_attr_lpfc_enable_npiv,
	&dev_attr_nport_evt_cnt,
	&dev_attr_board_mode,
	&dev_attr_max_vpi,
	&dev_attr_used_vpi,
	&dev_attr_max_rpi,
	&dev_attr_used_rpi,
	&dev_attr_max_xri,
	&dev_attr_used_xri,
	&dev_attr_npiv_info,
	&dev_attr_issue_reset,
	&dev_attr_lpfc_poll,
	&dev_attr_lpfc_poll_tmo,
	&dev_attr_lpfc_use_msi,
	&dev_attr_lpfc_fcp_imax,
	&dev_attr_lpfc_fcp_wq_count,
	&dev_attr_lpfc_fcp_eq_count,
	&dev_attr_lpfc_enable_bg,
	&dev_attr_lpfc_soft_wwnn,
	&dev_attr_lpfc_soft_wwpn,
	&dev_attr_lpfc_soft_wwn_enable,
	&dev_attr_lpfc_enable_hba_reset,
	&dev_attr_lpfc_enable_hba_heartbeat,
	&dev_attr_lpfc_sg_seg_cnt,
	&dev_attr_lpfc_max_scsicmpl_time,
	&dev_attr_lpfc_stat_data_ctrl,
	&dev_attr_lpfc_prot_sg_seg_cnt,
	NULL,
};

struct device_attribute *lpfc_vport_attrs[] = {
	&dev_attr_info,
	&dev_attr_link_state,
	&dev_attr_num_discovered_ports,
	&dev_attr_lpfc_drvr_version,
	&dev_attr_lpfc_log_verbose,
	&dev_attr_lpfc_lun_queue_depth,
	&dev_attr_lpfc_nodev_tmo,
	&dev_attr_lpfc_devloss_tmo,
	&dev_attr_lpfc_enable_fip,
	&dev_attr_lpfc_hba_queue_depth,
	&dev_attr_lpfc_peer_port_login,
	&dev_attr_lpfc_restrict_login,
	&dev_attr_lpfc_fcp_class,
	&dev_attr_lpfc_use_adisc,
	&dev_attr_lpfc_fdmi_on,
	&dev_attr_lpfc_max_luns,
	&dev_attr_nport_evt_cnt,
	&dev_attr_npiv_info,
	&dev_attr_lpfc_enable_da_id,
	&dev_attr_lpfc_max_scsicmpl_time,
	&dev_attr_lpfc_stat_data_ctrl,
	&dev_attr_lpfc_static_vport,
	NULL,
};

/**
 * sysfs_ctlreg_write - Write method for writing to ctlreg
 * @kobj: kernel kobject that contains the kernel class device.
 * @bin_attr: kernel attributes passed to us.
 * @buf: contains the data to be written to the adapter IOREG space.
 * @off: offset into buffer to beginning of data.
 * @count: bytes to transfer.
 *
 * Description:
 * Accessed via /sys/class/scsi_host/hostxxx/ctlreg.
 * Uses the adapter io control registers to send buf contents to the adapter.
 *
 * Returns:
 * -ERANGE off and count combo out of range
 * -EINVAL off, count or buff address invalid
 * -EPERM adapter is offline
 * value of count, buf contents written
 **/
static ssize_t
sysfs_ctlreg_write(struct kobject *kobj, struct bin_attribute *bin_attr,
		   char *buf, loff_t off, size_t count)
{
	size_t buf_off;
	struct device *dev = container_of(kobj, struct device, kobj);
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	if (phba->sli_rev >= LPFC_SLI_REV4)
		return -EPERM;

	if ((off + count) > FF_REG_AREA_SIZE)
		return -ERANGE;

	if (count == 0) return 0;

	if (off % 4 || count % 4 || (unsigned long)buf % 4)
		return -EINVAL;

	if (!(vport->fc_flag & FC_OFFLINE_MODE)) {
		return -EPERM;
	}

	spin_lock_irq(&phba->hbalock);
	for (buf_off = 0; buf_off < count; buf_off += sizeof(uint32_t))
		writel(*((uint32_t *)(buf + buf_off)),
		       phba->ctrl_regs_memmap_p + off + buf_off);

	spin_unlock_irq(&phba->hbalock);

	return count;
}

/**
 * sysfs_ctlreg_read - Read method for reading from ctlreg
 * @kobj: kernel kobject that contains the kernel class device.
 * @bin_attr: kernel attributes passed to us.
 * @buf: if succesful contains the data from the adapter IOREG space.
 * @off: offset into buffer to beginning of data.
 * @count: bytes to transfer.
 *
 * Description:
 * Accessed via /sys/class/scsi_host/hostxxx/ctlreg.
 * Uses the adapter io control registers to read data into buf.
 *
 * Returns:
 * -ERANGE off and count combo out of range
 * -EINVAL off, count or buff address invalid
 * value of count, buf contents read
 **/
static ssize_t
sysfs_ctlreg_read(struct kobject *kobj, struct bin_attribute *bin_attr,
		  char *buf, loff_t off, size_t count)
{
	size_t buf_off;
	uint32_t * tmp_ptr;
	struct device *dev = container_of(kobj, struct device, kobj);
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	if (phba->sli_rev >= LPFC_SLI_REV4)
		return -EPERM;

	if (off > FF_REG_AREA_SIZE)
		return -ERANGE;

	if ((off + count) > FF_REG_AREA_SIZE)
		count = FF_REG_AREA_SIZE - off;

	if (count == 0) return 0;

	if (off % 4 || count % 4 || (unsigned long)buf % 4)
		return -EINVAL;

	spin_lock_irq(&phba->hbalock);

	for (buf_off = 0; buf_off < count; buf_off += sizeof(uint32_t)) {
		tmp_ptr = (uint32_t *)(buf + buf_off);
		*tmp_ptr = readl(phba->ctrl_regs_memmap_p + off + buf_off);
	}

	spin_unlock_irq(&phba->hbalock);

	return count;
}

static struct bin_attribute sysfs_ctlreg_attr = {
	.attr = {
		.name = "ctlreg",
		.mode = S_IRUSR | S_IWUSR,
	},
	.size = 256,
	.read = sysfs_ctlreg_read,
	.write = sysfs_ctlreg_write,
};

/**
 * sysfs_mbox_idle - frees the sysfs mailbox
 * @phba: lpfc_hba pointer
 **/
static void
sysfs_mbox_idle(struct lpfc_hba *phba)
{
	phba->sysfs_mbox.state = SMBOX_IDLE;
	phba->sysfs_mbox.offset = 0;

	if (phba->sysfs_mbox.mbox) {
		mempool_free(phba->sysfs_mbox.mbox,
			     phba->mbox_mem_pool);
		phba->sysfs_mbox.mbox = NULL;
	}
}

/**
 * sysfs_mbox_write - Write method for writing information via mbox
 * @kobj: kernel kobject that contains the kernel class device.
 * @bin_attr: kernel attributes passed to us.
 * @buf: contains the data to be written to sysfs mbox.
 * @off: offset into buffer to beginning of data.
 * @count: bytes to transfer.
 *
 * Description:
 * Accessed via /sys/class/scsi_host/hostxxx/mbox.
 * Uses the sysfs mbox to send buf contents to the adapter.
 *
 * Returns:
 * -ERANGE off and count combo out of range
 * -EINVAL off, count or buff address invalid
 * zero if count is zero
 * -EPERM adapter is offline
 * -ENOMEM failed to allocate memory for the mail box
 * -EAGAIN offset, state or mbox is NULL
 * count number of bytes transferred
 **/
static ssize_t
sysfs_mbox_write(struct kobject *kobj, struct bin_attribute *bin_attr,
		 char *buf, loff_t off, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	struct lpfcMboxq  *mbox = NULL;

	if ((count + off) > MAILBOX_CMD_SIZE)
		return -ERANGE;

	if (off % 4 ||  count % 4 || (unsigned long)buf % 4)
		return -EINVAL;

	if (count == 0)
		return 0;

	if (off == 0) {
		mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
		if (!mbox)
			return -ENOMEM;
		memset(mbox, 0, sizeof (LPFC_MBOXQ_t));
	}

	spin_lock_irq(&phba->hbalock);

	if (off == 0) {
		if (phba->sysfs_mbox.mbox)
			mempool_free(mbox, phba->mbox_mem_pool);
		else
			phba->sysfs_mbox.mbox = mbox;
		phba->sysfs_mbox.state = SMBOX_WRITING;
	} else {
		if (phba->sysfs_mbox.state  != SMBOX_WRITING ||
		    phba->sysfs_mbox.offset != off           ||
		    phba->sysfs_mbox.mbox   == NULL) {
			sysfs_mbox_idle(phba);
			spin_unlock_irq(&phba->hbalock);
			return -EAGAIN;
		}
	}

	memcpy((uint8_t *) &phba->sysfs_mbox.mbox->u.mb + off,
	       buf, count);

	phba->sysfs_mbox.offset = off + count;

	spin_unlock_irq(&phba->hbalock);

	return count;
}

/**
 * sysfs_mbox_read - Read method for reading information via mbox
 * @kobj: kernel kobject that contains the kernel class device.
 * @bin_attr: kernel attributes passed to us.
 * @buf: contains the data to be read from sysfs mbox.
 * @off: offset into buffer to beginning of data.
 * @count: bytes to transfer.
 *
 * Description:
 * Accessed via /sys/class/scsi_host/hostxxx/mbox.
 * Uses the sysfs mbox to receive data from to the adapter.
 *
 * Returns:
 * -ERANGE off greater than mailbox command size
 * -EINVAL off, count or buff address invalid
 * zero if off and count are zero
 * -EACCES adapter over temp
 * -EPERM garbage can value to catch a multitude of errors
 * -EAGAIN management IO not permitted, state or off error
 * -ETIME mailbox timeout
 * -ENODEV mailbox error****count number of bytes transferred
 **/
static ssize_t
sysfs_mbox_read(struct kobject *vice, inux Debin_attribute *  *
 * F,
		char *buf, loff_t off, is par******)
{
	inux Dedevice *dev = container_of(er for       * Copy,evice);      *
 Scsi_Host  *sh* EM= class_to_EX an(deved.        lpfc_vport *      = Linux De            ) EX an->X andataf Emulex.      hba   *p    =      ->    ;
	int rc;
	MAILBOX_t *pmb;

	if (off > yright (CMD_SIZE)
		return -ERANGE4-2005 C(******+    )stoph Hellwig          ******=oph Hellwig       -    4-2005 Chris% 4 || *******oftware(unsigned long)bu soft        *
 * INVAL4-2005 Chris&&*******== 0        *
 04-20spin_lock_irq(&    ->hba    )4-2005 Cublic over_temp_is feon 2HBA_OVER_TEMP) {
		 of the Emuidleisheded. ral   un    *
 * Public License as      *
  -EACCES;
	}-2005 Chrisn 2  &&
	    ublic  of the Em. SoftwLIEDSMht (WRITINGCONDITIONS, REPRESENTATIONoffset >= 2 *     of(uint32_t)   *
 pmb = Public RESENTATIONe Em->u.004-		switch (pmb->mbxCommand   *
 	/* Offline only */    ase MBX_INIT_LINK:XCEPT TO THEDOWNENT THAT SUCH DISCCONFIGERS ARE HELD *
 * TO BE LRINGHAT SUCH DISCRESETe GNU General PublUNREG_LOGINRE HELD *
 * TLEAR_LAHAT SUCH DISCLUMP TO TEXT General PubliUN_DIAGS General Public TARYING  *
 * inc LicMASTHAT SUCH DISC LicDEBUU Gen005 C!(      *fc_flag & FC_OFFLINE_MODEITY,  *		printk(KERN_WARNWARR"e Emulex :ENT, AR 0x%x "*****TION   "is illegal in on-ISCLA Soft\n"st Bux/delay.NFRINGEMENT, ARESE, 
 * This program is distribibuted in the hope that it will be us      *
 * PERMscsi_}XCEPT TO THE
 * E_NVHAT SUCH DISCh>
#inVPARMth this packagLOAD_SM General PublicADnclude <scsi/scslude TO BE pfc_hw.h"
#includeR "lpfc_sli.h"
#include "STATUth this package.AD_XRI_sli.h"
#include "lElpfc_hw.h"
#includeLNKpfc_nbe found in the fiMEMORYHAT SUCH DISCLAIMEROAD for  *
 * morPDATE_CFc_sli.h"
#inclKILL_BOARn.h"
#include de "lARE be found in tde "lEXP_ROfc_hw4.h"
#incBEACO, a copy of whDEL_LD_ENT"
#include "lpf LicVARIABLEude <scsi/scsi_tranWW, a copy of whPORT_CAPABILITIEth this packagPEED IOVile CROL*****breakSE, "
#include "lpfport64csi.h"
#include "lp be found in t"

/**
 "0, 1, 2, 4, 8"
etails, a copy of whii conver "0, 1, 2, 4, 8TO BE LPEEDING  *
 * includeBIUed wi************/

#include <linux/ctype Include .h>
#includenclude <inux/interrupt.h>

#include scsi/scsi.h>
#include <scs/scsi_device.h>
#include <scsi/scsi_ost.h>
#include <default string holding converted integer pluUnknowning terminator.
 *
 * Description:
 * JEDEC Joint Electron Device Engineering Council.
 * Convert a 32 bit integer composed of 8RESS *
 Ife Fo en*****ered an****** attention, allowAIMED,he f
		 * or e.     *********cNT, ARs until the*
 * is restarted.statiEXCEublished bp     *stoppedCONDIITIONSFRINGEMENT, AR !="lpfc_logmsg.h"
		j = (incr & 0xf);
		if (j <= 9)id
lpfc_	j = (incr & 0xf);
		if (j <= 9)i_transport_ 0x61 + j - 10;
		incr = (incr >> 4);
	WWN    	     *****f_logished, /

#include , LOG_    ude <l	"1259 e Em: Issued_jedec_to_md <linux	"lude while <lii++) {
	 Soft.nclude <l*
 *rupt.h>

#inclu  *
ESS FOR A PARTICULAR P             rn co/* Don'tgers onjedec_to_ascii(into be sent when b    edstatic voatic inr, chmiddle****discy thyi, j;
	for (i = 0; sli.sli********LPFC_BLOCK_MGMTMAP******* Electron Device Engineering Council.
 * Convert a 32 bit integer com
 * GAIN' capital     ********************************* || = (inc(!evice_attribute *attr,
		  SLI_ACTIVE)ITY, eering Council.
 * Convert a 32 bit integc = www.ebuteiv: che Emlished used.
	ONS, REPRESENTATIONe Emport *) shPEED_BLLineering Co    *
 * Public License as p	} else)
{
	re= class_to_shost(dev);
	struct lpfc_vport *vport = (struct _waitlpfc_vport *) shux/intst->hostdata;
	struct lpfc     e Emutmo_vale EmuleNFRINGEMENT, ARE * HZ = vport->phba;

	if (phba->cfg_enablg_info_showrc(j <= 9)SUALL S*******(buf, P= <= 9)TIMEOUT*********ESS FOR A PARTICULAR = NULLde <scsi/return snprintf(buf, PAGE_SIZE, LPFC_MODULE_DESC "\n");
}

static ssizerd Disabled\n");
}

? -E\n") :******** snpriic ssize_t
lpfc_bg_ Softwa      *lpfcINGEXPRE	
		ift device_atNG ANY IMPLIED WA!=    _attribS, REPRESENTATIONS AND  !t = (struct lpfTY,  *
******/

#include <d integer pluBad S>
#inclstribu This program is distributed in the hope that it will be useful. *_t
lpfc_bgRESSmemcpy(ptersCHANT8(C) ) &
 *      ,       rn cINCLUDING ANY IMPLIED WA_hba  +******as published b_Host *shost = clas         *
 * This    d_err_cnt);
}

static seral   in the hope that it will be 
eger comost(dev}
his file         *
 * Fibre Cd_err_cnt); * F =  *
.ng long)ph	.namort linuxlude .modort =_IRUSR |ze_tWUSR,
	},
	.is p          *
 * Thisr_shlex  =(unsigned lolex r_shwriport d_err_cnt);
			c,
};

/******     ers c_d_err_ng lo- Crea*****he ctlreg if (guardentries****@     : address****    text.
E_SIZE,ure.
 st *sRturn snpdes:****zero on succestruct for 3eturn snpde fromr *buf)c);
	s_  *
file()
 * Thint
host = class_to_shost   *
 * www.emulex.c              *
    *
 * EMLEX and St *vpoX an_IZE,      (strucn sntion*****

	r for 3ar *buf)n",
			(unsigned&       e hostdev.er fode <linux/i&d_err_drvre Sof_    shost;

	r/* Virtual     s do not need ctrl_c_vport *vpor;
	f05 C for 3||       *
ort_typtware
		  NPIVger t    goto oudev);d to a Scsi_host structure.
 * @attr: device attribute, not used.
 * @blpfc_vcontains size of fpfc_info_sho_removeon ret * Fw(struct device *dev, struct device_attribute *attr,
	       char *buf)
{
	ed long l_Host *host = class_to_shost(dev)struct Scsi

	return s0;
number in ascii
 * @de:
 shost-st(dev)cture.
 * @attr: device attributbuf)
{
	struct Scsi_Ho_shost(dev);

	returructure.
 * @attr: device attribute, not used.
 ruct.
 * @buf: on return containsoues i    *
  conver}i_Host *shost freess_to_shost(deRt(devtruct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpf*/
void)phba-ev, struct devint);
}

/**
 * lpfc_info_show - Return some pci info about the host in ascii
 * @dev: c serial number.
 *
 * Returns: size of formatted.
 * @buf: on return containsthe formatted text from lpfc_info().
 *
 * Returns: sizetted string.
 **/
static ssize_t
lpfc_    *
return snprintf(buf, PAGE_SIZE, "%s\n",phba->Serserialnum_show - Retuture.
 * @attr: device attribute, not used.
 * @buf: on return cont}
i_Ho****Dynamic FC  * EMA* Fibre s Sui < 8
rns:_Host *shost get_ hostring.id(devopyruct       DID intoruct scsi X and     i*
 * @e hos: kernelformatted stri:
 *(dev);
is file	struct lp zero or one in Linux Den some pci info          *
                        *
 * www.emulex.com                 _sensnote: n snyns: allex yrsiocpu endiann*) s;
	f    uct device_at              *vport = (struct device *d zero or one i
 **/- Set *
 * aluruct of formatted string
 ** **/
static ssize_t
lpfc_temp_sensor_show(struct device *dev, struct devic
 **attribute *attr,
		      char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct l                                 *
 * Poeral       *
 *            phba; as publitted string.
 **/
static ssize_t
lp  *
 stdata;
	strudev: cpfc_hba FCger tTYPE ssizstru)
		if05 C     is_link_upis diseldescublished bfc_topology= (sTOPOLOGY_LOO    *
 ed string.
 *************PUBLICt(dev)csi_h_show(struct device *dev, struct device_LPEEDscsi_
		i_vport *) shost->hostdata;
	struct lpfc_hba  *phba = g)
		if (phbuct lpfc_vport *vport = FABRICc_vport *) shost->hostdata;
	struct lpfc_hba  *phba = vport->phba;

	return snprintf(buf, PAGE_SIZE, "%PTP(dev);
ute *aesc_show(struct device *dev, struct deviceUNKNOWNuct lpfc_hba   *phbarns: size of formatphba;
	return snprintf(buf, P SoftwIZE, "%d\n",phba->temp_sensor_support) Soft **/
static ssize_t
lpfc_temp_sensor_show(struct device *dev, struct devic Soft class converted to a Scsi_host structure.
 * @attr: device attribute, not used.
 * @buf: on return contains the scsi vpd model description.
 *
 * Returns: size of formatted string.
 **************************esc_show(struct lass_to *dev, struct dfc_nE********;ort *)  *
 *R NON-INblic ar *b SofteldescPT TO
		  NT Tsi vpd mHAT SUCH tr: deviceLAIM***** - Return the program type of the hba
 * NT TLAIMscsi_efine LPFC_LINtr: device Pute, not used.ich can be found 
		   Foulpfc
#incthe Links up, beyond thisstrinPAGE_Sred text Softw;
	fow - Return the program type of the hba
 * @Nv: clasrns: size of formatte FouERROR on return contains the scsi vpd program type.port o_shost(dev);
 nibbles int - Return the program type of the hba
 * i vpd modshost(dev);
butedel name.
 *
 * Returns: size of formatted string.
 **/
staticspfc_iIZE, "%d\n",phba->temp_sensor_supce sl **/
static ssize_t
lpfc_temp_sensor_show(struct device *dev, struct ce sl class converted to a Scsi_host structure.
 * @attr: device attribute, not used.
 * @buf: on return contains the scsi vpd model description.
 *
 * Returns: size of formatted st		    char *buf)
{
	struct ed to i_Host  *sar *ce sl);
	strc_progA_1GHZENT THAT vport->phbreturn m type of the hbPEED_1GBIba = vefine LPF
	struct 2pfc_vport *vport = (struct lpfc_vport *)shost->hos2data;
	struct lpfc_hba   *p4pfc_vport *vport = (struct lpfc_vport *)shost->hos4data;
	struct lpfc_hba   *p8pfc_vport *vport = (struct lpfc_vport *)shost->hos8data;
	struct lpfc_hba   *p10pfc_vport *vport = (struct lpfc_vport *)shost->host0data;
	struct lpfc_ nibbles intort = (struct lpfc_vport *)shost->hosZE, "%s\n",phba->ProgramT, not used.
 * @s: size of formatted string.
 **/
stael name.
 *
 * Returns: size of formatted string.
 **/
staticfabric__err_IZE, "%d\n",phba->temp_sensor_supass_to _err **/
static ssize_t
lpfc_temp_sensor_show(struct device *dev, struct ass_to_shost class converted to a Scsi_host structure.
 * @attr: device attribute, not used.
 * @buf: on return contains the scsi vpd model descrip	u64 node_shosiption.
 *
 * Returns: size of formatted st

/**
 * lpfc_modelname_show_attrbute i_Host  *shost = class_to_shost(dev);ONDITIONt lpfc_vport *vport = (struct lpfc)showo a Scsi_ = wwne tru64i_Host  *sfabparam.o a Name.u.wwnmp_s not us/* = (struis loctted tettr,therehar no F/FL_P     EXCEze_t
lpfc_fwGeneral   .
 *
 * Returns: size of formahostdata;
ass_to_shoslpfc_vporto a Scsi_hophba;
	return snpriis fsce_at  *
 is fistiuf)
informa int abou "%d\nadapte*****
static ssize_t
lpfc_temp_sensor_sho **/Not= vportrr_sba;
 for 3for ar * down,ost guardpool, sli2 active, **/managem/
stom lers oed, memoryec inc_decod*****,c vo*vport**** lpfc_hba   *p"%s, sli-%dv, p*********ort *) shosware_rev(phted to fwrev[32truc This file nux Dem_show(std.
 * @bu *e *dev, strt->pprintf(buf, PAGE_SIZE, "%s\n",phba->Port);
}

/**
 * lpfc_fwrev_show - Return the firmware rev running in the hba
 * @dev: class convert *buf)
{
	stsli     struFITNESS FOli	char hdw[s the scsi vpd progrhs_Host  *shost strucs	char hdw[9];
	lt struc *shso_Hostsli->ort *) s_LIED W (st
		      Q(C) 200oxqCopyright (C) 2004-	tribute it an second (sttions (dev);
	/*
aticprev/
stught IZE, = (sing_jedec_to_ascii(int incr, ch{
	strs.biuRconfigurnt i,rns: sizeScsi_host struct < used.
 * @buf:n contain!Scsi_he Emumem_**
 n containsttr,
		  char *buf)
{
	struct Scsi_Hoon 2 of the GNU rr_sho);
	struct lptribute *attr,
		       char *buf attribute, not us>phba; =atio**
  = clad to a * @dev: class, GFP_/

#Ea = v*****>phba; FCode ascii stri devsetINFRoxq, 0      of (*phba = vpor)struct * FITNruct  PURPOSE,r & 0xf);
		if (	hdw[7 -"lpfc_nl.  char *bufOwne a SAIMEHOSba =attr,
		) 20ext1d_err_showattr,
		iption text.
 *
 ute, not used.
 * @buf: ostruct device_attri *atata;
har *buf)
{
	struct Scsi_Hosttrucvport *vport = (struct  PAGE_SIZEct de   *phba = v not usED)
			return snprintf(buf, POMVersion);
}

_Host  *sratov * 2 as publi, PAGE_SIZE,
					"Bloc a Scsi_host s\n");
}
pfc_Returns:ev, struct deof formatted stringbe useful. *rr_showuct devow(shsdevice *dev, contains the scsi vpd proe_attrhs->tx_frame lpf * @bun.varRdphbaus.xmitFult C(devs no defword lpfINFRINill be returned.
 *BytRetuvert56mp_s no refault so zero will be returnedrcv
 * Returns: sire of formatted string.
 **/
starcvssize_t
lpfc_linon_show(struct device *dev, struct device_atchar *buf)
{
	struct Scsi_Hopfc.h"
#host = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vpbute *atphba;

	return snprintf(buf, PAGE_SIZE, "%s\n", phba->OptionROMVersion);
}

/**
 * lpfc_state_show - Return the link state of the port
 * @dev: class converted to a Scsi_host structure.
 * @attr: device attribute, not used.
 * @buf: on return contains text describing the state  no ar *bfailure_        ted string.
 Lnk.ar *F Down\eturns: siloss_of_syncn");
		break;
	case LPFC_LIossSyncse LPFC_CLEAR_LA:
	ignale LPFC_HBA_READY:
		len += snprIZE-leturns: siprim_seq_protocol_errn");
		break;
	case LPFC_Le) {SeqErreturns: siinvalid_ze of fn");
		break;
	case LPFC_L_SIZE-lXmitWordn, PAGE_SIZE-lecrse LPFC_HBA_READY:
		len +=crtf(buf + le convfault so zero will be rFC_FABRIC_CFn,
				"Link Down\n");
		-=st->			"Link Down\n");
	LPFC_CLEAR_LA:
	case LPFC__LIST:
		AR_LA:
	case LPFCuf + len, PAGE_SIZE-len, "Li + len, PAGE_SIZEIZE-len, "Lrt_state) {
		case LPFC_LOCAL_CFG_L_LIST:
	e) {
		case LPFC_LOCAL_CFG_n, PAGE_SIZE-len,
					"Confi_LIST:
	_SIZE-len,
					"ConfLPFC_FDISC:
		case LPFC_Fen += snprintf(base LPFC_CFG_LINK:
		case LPF_LIST:
	K:
		case LPt used.
 * @bu *shost = class_to_shost(dev);
	st
				"pn");
		brGE_SIZE - , hdwTag >> 1n snpk;
		}
		if (p_LIST:
		case, hdw (stO_MAInosn");
		br-1ribute *atbreak;
		}
		if (phb					, PAGE_SIZE-len,
a->sli.sli_flag & LPFC_MENLO_MAIE_SIZE-len += snprintf(buf + lenlen,
			dumpedfault so z				
	pfc_jed = e.
 *fc_jed(_Host *hn, PAGE_< PFC_WARt =  *
 o_she if (en,
			_since_lashost classn, PAGE_+ *vpo(stribute it and-1 -\n");
			else
				lfc_state_s+= snprintf(buf + len, PAGE_SIZE-len,
	;
		} else {
			if shost =used.
 * @buf: on return contains text dserialnumhspfc_hba   *phba =PAGE_ort->phbaii.
 *
 *ibute, nhba->rt->ph
	lpfc_deco **/
static ssize_t
lpfc_temp_sensor_show(struct device *devE-len,
				Returns: size of formatted string.
 **/
static ssize_t
lpfc_hdw_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	char hdw[9];
	struct Scsi_Host  *shost = class_tfc_vport *) shot->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	lpfc_vpd_t *vp = &to_ascii(vp->red.
 * @buf: on return contains the ROM and FCode ascrings.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_option_romion_show(struct device *dev struct device_attribute *attr,
			     char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	stru string.ase s[0](devx1; /*[])
et requepci /	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	return snprintf(buf, PAGE_SIZE, "%s\n", phba->OptionROMVersion);
}

/**
 * lpfc_state_show - Return the link state of the port
 * @dev: class converted to a Scsi_host structure.
 * @attr: device attribute, not used.
 * @buf: on return contains text describin state of the te *attr, char *buf)
{
	struct Scst *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	int  len = 0;

	switch (phba->link_state) {
	case LPFC_LINK_UNKNOWN:
	case LPFC_WARM_START:
	case LPFC_INIT_START:
	case LPFC_INIT_MBX_CMDS:
	case LPFC_LINK_DOWN:
	case LPFC_HBA_ERROR:
		if (phba->hba_flag & LINK_DISABLED)
			len += snprintf(buf + len, PAGE_SIZE-len,
				"Link Down - User disabled\n");
	port
 * @dev: c -EPERM port offline or managemST:
		case LPFC_DISC_AU	break;
	case LPFC_LINK_UP:
	case LPFlen, PAGE_SIZE - len,
	_HBA_READY:
		len += snprintf(buf 	case LPFC_VPORT_READY:
	ink Up - ");

		switch (vport->port_ - len, "Ready\n");
			break;

	LINK:
			len += snprintf(buf + len, += snprintf(buf + len, PAGiguring Link\n");
			break;
		case LPF		break;

		case LPFC_FLOGI:
		case LPFC_FABRIC_CFG= snprintf(buf + mboxq->u.mb.mbxStatus == 0 ||
	   intf(buf + topology == TOPOLOGY_LOOP) {

	n");
			else
				_SIZE-len,
						"en,
						"   Fabric\n");
			else
				len += snprintfpfc_hba **/Throgram driver t);
	s\n");sli_ handlintfas target PAGE memset(se of rlpfc_areost d_err_wait(ersev, phba-_sli_ntf( lpfy
 * supported, zero a Sby_mboxq,
ba;
	charuct o a lirt =r 32pmboxq, **/
smboxq,c ssize_t
lpfcmboxq,
tf(buf, PAGE_SI a Scsi_hosttr: device attro a  q, phif founcsi_hli-%dmboxq,
om lfline - on return contain     mboxq, phram type.
 BX_TIMEOUT)
		tes:
 * ormaOUT)
		m*);

	ifshow - Return some pcii info abodeve trademar);

	if-> attpare
	strEmulex.                        *
 * www.emulex.com                              
 * @phba:ndlpiption.
 *
 * Returns: size of format*attSearchhba->t de, ma) {
,s a mailIDGE_SIq, p_for_each_t = y(rs c, &me);
}

/*o a s, nlp_q, ppe.
 * @attNLP_CHK_**** Scs retueturns
	casrs c->n co Softwares.
 STE_MAPPED* Ret 0x61 + j * Assumesid= (sposting theit(dev);
 name.
 *
 * Returns: size of formanteger comrs couogramTyhba *phba, uint32_t type)
{
	struct ccribing the sc_hba   *phba = vport-oxq, one in ascE, "%d\n MBXERR_Eing.
ing./**
 dlprns: or -1_pool);

	if (mbxstatus == MBXERR_ERROR)
		rw(struct device *dev, st 0;
	int cnt = ype: LPFC_EVT_OFFLINE, LPFC_EVT_WARM_STAport ring buffers cabout thhba pointer.
 * @typeLPFC_Et lpfc__OFFLINE_PREP);
	wLPFC_Ea;
	
		r?offline(strns: :uf + psli;
	int status = 0;
	intze_t
lpfc_;
	int i;

	init_ * lpct lpfc_vp;

	if (mbxstatus == MBXERR_ERROR)
		returDescrip int:.
	 */
	for (i = 0; i < pion(&online_c) {
			mslwwnl);
->phine_compl,
			      LPFC_EVT_OFFLINEo a Scsi_
	wait_for_completion(&online_compl);

	if (status != 0)
		return -EIO;

	psli = &phba->sli;

	/* Wait a litt,
					KERN_ngs to sass_tle dorev_show(stposting tho a _errevice_a :ev);psli;
	int status = 0;
	int cnt expire.
	 */
	for (i =compl < psli->num_rings; i++) {
		pring = &psli->ring[i];
		while (pri ntf(tion(&online_compl);ion(&online_c - Offline> 500) {  /* 5 secs */
				lpfc_printf_log(phba	wait_for
	wait_for_completion(&online_compl);

	if (status != 0)
		return -EIO;

	psli = &phba->sli;

	/* Wait a little fo
	init_completion(&online_compl);
	lpfc_workq_comp_event(phba, &status, &online_colen,rring.PAGE_tmore.
	 */
	furn c *
 
					tmo **/
urn c *vp-EIO reort *)  lpf @t/*****: newn",phbaba->set not confring[i];
		while (prventIf  * zeroi_Hostnt->phbselectiv*
 *de if noton(&/*****,)
		ifsem_poocompletion onlinont(dev);
truct device *dev return code if notes:
 * TheEIO re*urn c, HANTABILne_compl     05 CT_OFFLINE		urn ces anpletion on=ne_complfc_state_sh0)
		return status;

ong enough for devurn coshow_funcdecodba;
	charEIO remboxq,


	return len;c_selective_reset(stMacrn(&oat uses fieltion(generturna s, &onlinwith(&onlilineut the hwphba, &);
	wai 0;
}

/**
 * lp##

	if:f(buf, truct ******lpfc_dtedrsiobufvent
cdev:SLI ar * lve	intnlinanfc_do_offevent
buf:ba;
eturn snp2004-truct 0;
	int

	if 0) {  /* 5 eturn -EIO;
     ****@dev: classtringine_com#defSCLAent(phba, &status, &onli(

	if,.
 * @c_: unus, sz, cast)	\his file is parrt *) 		\t)
		ret - Selectively rotes:
 * * Copyright,ctive_*) serform the re
 * Fibre Chl Hos
 * Notesus Adapte)ective_{selectiv* Nos = lpfc_do_offline(ph;

	****ring.LI are tr&onlirks ofive.
 * If lent(phba, &     *rretur=
			             ive.
    *
 sn Retur_attriing ontains the st*
 * Not(ed wh->_complet? "sel contain the s->

	if : 0);	\atusable.
 *
 * Descriprdvport e buf contains the strin() w\
turn sescription:
 * If the buf contains the string )lue is fileFC_RPEED ATTR call sue_t
lGO,ue_reset - Selectively r,Issue)c_do_offline() ret     _symbolto_shost(dev);
	strcomp's f, size_mpl);

	if        :	mbxs         whosi_Host  *shost  has beentivengnt i;
	wait_for_completionT devs, &onlinis called b
 *
 *t() retur afe, nuct ass_to_shsi_Host  *shost = cl = (struct lpfc_va;
	struct lpfcre-registset_of fo_vport *vport-EIO;

	vented to ato propogturnuct l lpfc size of fr,
		chae attr      is_hdw_shfg_enable_hba_reset)
		returar *buf, size_t coutes:
 * The       number o char *buf)
{
	struct Scsi_Host *   *
 * www.emulex.c*)number o->dd retuatted string.
 **/
s Software
		  VPEED show(s !=port rs_cmdtring., uct CTNS_RSPN_IDdevic0rmatted string.
 *hban th_verbose_ini	memE, "hba'r *bg rmatted levelvent
Scsi: Psensorphba         contaivport *);
	struct lpfc_hba   *phba = vpurn -EIO;cfg*dev,() routSCLAt{
	structw - Rodular *buff formatted size of f_evt_ cfgev);
	struct ba->uspfc_seissueog messsage accordintfon(&onl_to_shtatit(dev);
	struct *dev,eow(ssettingventbefore*/
ses theor
	else
n",
		_vpornable_hba_reset)
		rze of formatted stri   *
 * www.e, "%     ba, LPFC_EVrmatted     Scsi_hrt = (struct lpf=dev: clatf(buf,ass_to_ss, &onlie Frelturnvportt() returns, &onlit((v{ing ifix Codensor leveuct c= lpfcs currest->ho.()
 *ata;
ze_t
lpfc_fw1r_shoeturns: s	wait_for_matted string.
 *dapter.edns zeru.mb.ize_t
lpfc_board_mode_shofc4ct device *dev, struct devicece slct device *dev, strmaxault _ow(struize_t
lpfc_board, size_t couns_to_ing idhe tempins the state of the adapter.
 *
 *  zero or one in aeturn -EIO;ata;
	structted string.
 **/
sta   *rt *v	struct lpfc_hb
 **/
turn snprintf(buf, PAGE_ted string.
 **/
stC_HBA_Ea->link_state == LPF *vport si_Host  *shost = class_ted string.
 **/
st *vport rt *vporhdw_she_attrism   w devt doesrns:s == 0)(thuHost xq,
s, &onli) *
 * Returns: sSTART)
		statate == LPFC_WARMce slitate = "warm stace slze_t
lpfc_board**
 * la->link_state =ass_to_shosttate = "warm staass_to_shosted string.
 *warm or error rt *vpo.biuRmbxstatus = lpfc_sli_issue_mbox_wait(phba, pmboxq,
						     .biuRhba->fc_ov * 2);
	}

	lpfc_set_loopback_flag(phba)n_romlink_sts the scsi v"%s\m type.
 *
 * structlen,used variable.
 *
 * E-len,
				->lindce_ahba, &sw(struOF MERC the buffer
 * is retu)ted strinurn co *shost = class_to_shost(dhba, &s_mode_show(struct devi * Rreturn coeturn status;

)
		return -EIO;

	stat_workq_post_eve_compl);
	lpfc_w->link_st 0;
	int cnt = 0.
 *
 * Returns
	int cnt = ted strin 0;
	int cnt = 0lpfc_board_mode_storze_t
lpfc_fwc_printf_log(phba,
					KE_attribute *attr,ize of formatteboard_mode_store(strcount)
{
	struct Scsi_Hoort = (st_attribute *attr,
		  );
	struct lp= (strstdata;
li	return -= (strlipr_sheturn status_a   bkt)
{
	stcompl;
	int status=r_shtermin
			urn coireater threset)
		return -E
 * -EINVAar *bufe buffer does not containmulex.coct lpar *budisablror statee") - 1) == 0ne() failar *buf, size_t counater than zar *buf, size_t couct lpbsg_buf, PAG)
{
	st
		wait_forr_sh
		wt lpfc_hcompletion(e_compl;Scsi_ost.
 * @attr: device attribute, ar *bunot used.
 * @buf: on return contains the state of the adapter.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_board_mode_show(struct device *dev, struct device_attribute *attr,
		     char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	char  * state;

	if (phba->link_state == LPFC_HBA_ERROR)
		state = "error";
	else if (phba->link_state == LPFC_WARM_START)
		state = "warm start";
	else if (phba->link_state == LPFC_INIT_START)
		state = "offline";
	else
		state = "online";

	return snprintf(buf, PAGE_SIZE, "%s\n", state);
}

/**
 * lpfc_board_mode_store - Puts the hba in online, offline, warm or error state
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: containing one of the strings "online", "offline", "warm" or "error".
 * @count: unused variable.
 *
 * Returns:
 * -EACCES if enable hba reset not enabled
 * -EINVAL if the buffer does not contain a valid string (see above)
 * -EIO if lpfc_workq_post_event() or lpfc_do_offline() fails
 * buf length greater than zero indicates success
 **/
static ssize_t
lpfc_board_mode_store(struct device *dev, struct device_attribute *attr,
		      const char *buf, size_t count)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vcompl;
	int status=0;

	if (!phba->cfg_enable_hba_reset)
		return -EACCES;
	init_completion(&onlinee") - 1) == 0) {
		lpfc_workq_post_event(phba, &status, &online_compl,
				      LPFC_EVT_ONLINcsi_Host *shost )
{
	struct  - Used duunusa);
be_ot  *shstrinint-2-Point\;
	structport_evt_cn state of_sensor_show(sevice *dev, st	struct Surn the state of the      vportcr_delay - Retthe boBX_SUCCESS) {ct cBX_SUCCE*****
		if (rc != MBX_TIM)
{
	strturn snulti_unusent() or
		if (rc != MBX_turn 0;
	}

	if (pol);
		return 0;
	}
rctlhba->sli_rev == LPFC_SLI_REV>u.mol);
		return 0;
	}

 **hba->sli_rev == LPFC_SLI_REV
 **ol);
		reack0
		if (rc != MBX_)
		ol);
		rehost = c
		if (rc != MBX_host = col);
		reost ste sl
		if (rc != MBX__hba.max_col);
		repollntf(b		if (rc != MBX_xri = bfol);
		reen== 0_npiv_get(lpfc_mbx_rd_d_config);
ol);
		reuse_msi_get(lpfc_mbx_rd_x_rd_cool);
		refcp_imax_get(lpfc_mbx_rd_hba->sli -
					phba-wq_free(pmboxq, phba->mbo (mvpi)
			* -
					phba-ei)
			*mvpi = bf_get(lpfc_mbrd_confi_count, rd_confize oEACCES	if (axri)
			*axri = b_conf_vpi_get(lpfc_mbx_rd_conheartbeai_count, rd_config) -
					phm.vpi_use_count, rd_confibg_count, rd_config) -
			bgMEM cice that xri e if (stxri rpi = pmb->unsoft_wwnn(devt = (pi;
		if (mxri)
p		*mxri =)
		reg_seg_ce(pmboxq, phba->mboif (axri)
ri)
			*mxrot	if (axri)
			*axri = pmb->u_xri;
		if (mvpol);
		reze oqueue_depth_count, rd_config;
		if (avpi)
	_count, rd_confifip_count, rd_config) -
			fipg.max_vpi;
		d_mode_show - Retam.rpi_used; formatteda->cfg_link_speed)DE) ||
		(!(psar *buli->sli_flag & LPFC_SLI_->nport_ev,	rc = MBX_stdata;
	structruct lpfc_vp           ssue_mbox_wait(phba, pmboxq,s device that nt);
}

/**
 * lpfc_info_show vport->phba;

	r_countascii n_max_rpi_show - Ret
 * Desun	if (avpi)
			*avpls lpfc_get_h just the mrpiasking fodevl;
	int scount.
 * If lpfczero (failuasking foo a vailure) the buffer texown" and ri)
			*mxeershost-loginre) the buffer texfore the callerasking fo])
{ric caller
 * must check for to detect a f -
					phba-LI arecount.
 * If lpfcstring.
 t(lpfc_mbx_rdadevi**/
static ssize_t(struct dol);
		retax_ormacmplmp(bu**/
static ssize_t*attr,
		  char * -
					phdmi_or
 * must check forshost(d returns zevice *d_thlex  **/
static ssize_tstruct lpfc_vportttribute *attlun **/
static ssize_ta = vporasking foscan_flag(count.
 * If lpfcget_hba_i_count, rd_confida_icfg_parls lpfc_get_LL, NULL))
	struct lpftatu