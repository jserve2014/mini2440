/*******************************************************************************

  Intel PRO/1000 Linux driver
  Copyright(c) 1999 - 2006 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  Linux NICS <linux.nics@intel.com>
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

#include "e1000.h"
#include <net/ip6_checksum.h>

char e1000_driver_name[] = "e1000";
static char e1000_driver_string[] = "Intel(R) PRO/1000 Network Driver";
#define DRV_VERSION "7.3.21-k5-NAPI"
const char e1000_driver_version[] = DRV_VERSION;
static const char e1000_copyright[] = "Copyright (c) 1999-2006 Intel Corporation.";

/* e1000_pci_tbl - PCI Device ID Table
 *
 * Last entry must be all 0s
 *
 * Macro expands to...
 *   {PCI_DEVICE(PCI_VENDOR_ID_INTEL, device_id)}
 */
static struct pci_device_id e1000_pci_tbl[] = {
	INTEL_E1000_ETHERNET_DEVICE(0x1000),
	INTEL_E1000_ETHERNET_DEVICE(0x1001),
	INTEL_E1000_ETHERNET_DEVICE(0x1004),
	INTEL_E1000_ETHERNET_DEVICE(0x1008),
	INTEL_E1000_ETHERNET_DEVICE(0x1009),
	INTEL_E1000_ETHERNET_DEVICE(0x100C),
	INTEL_E1000_ETHERNET_DEVICE(0x100D),
	INTEL_E1000_ETHERNET_DEVICE(0x100E),
	INTEL_E1000_ETHERNET_DEVICE(0x100F),
	INTEL_E1000_ETHERNET_DEVICE(0x1010),
	INTEL_E1000_ETHERNET_DEVICE(0x1011),
	INTEL_E1000_ETHERNET_DEVICE(0x1012),
	INTEL_E1000_ETHERNET_DEVICE(0x1013),
	INTEL_E1000_ETHERNET_DEVICE(0x1014),
	INTEL_E1000_ETHERNET_DEVICE(0x1015),
	INTEL_E1000_ETHERNET_DEVICE(0x1016),
	INTEL_E1000_ETHERNET_DEVICE(0x1017),
	INTEL_E1000_ETHERNET_DEVICE(0x1018),
	INTEL_E1000_ETHERNET_DEVICE(0x1019),
	INTEL_E1000_ETHERNET_DEVICE(0x101A),
	INTEL_E1000_ETHERNET_DEVICE(0x101D),
	INTEL_E1000_ETHERNET_DEVICE(0x101E),
	INTEL_E1000_ETHERNET_DEVICE(0x1026),
	INTEL_E1000_ETHERNET_DEVICE(0x1027),
	INTEL_E1000_ETHERNET_DEVICE(0x1028),
	INTEL_E1000_ETHERNET_DEVICE(0x1075),
	INTEL_E1000_ETHERNET_DEVICE(0x1076),
	INTEL_E1000_ETHERNET_DEVICE(0x1077),
	INTEL_E1000_ETHERNET_DEVICE(0x1078),
	INTEL_E1000_ETHERNET_DEVICE(0x1079),
	INTEL_E1000_ETHERNET_DEVICE(0x107A),
	INTEL_E1000_ETHERNET_DEVICE(0x107B),
	INTEL_E1000_ETHERNET_DEVICE(0x107C),
	INTEL_E1000_ETHERNET_DEVICE(0x108A),
	INTEL_E1000_ETHERNET_DEVICE(0x1099),
	INTEL_E1000_ETHERNET_DEVICE(0x10B5),
	/* required last entry */
	{0,}
};

MODULE_DEVICE_TABLE(pci, e1000_pci_tbl);

int e1000_up(struct e1000_adapter *adapter);
void e1000_down(struct e1000_adapter *adapter);
void e1000_reinit_locked(struct e1000_adapter *adapter);
void e1000_reset(struct e1000_adapter *adapter);
int e1000_set_spd_dplx(struct e1000_adapter *adapter, u16 spddplx);
int e1000_setup_all_tx_resources(struct e1000_adapter *adapter);
int e1000_setup_all_rx_resources(struct e1000_adapter *adapter);
void e1000_free_all_tx_resources(struct e1000_adapter *adapter);
void e1000_free_all_rx_resources(struct e1000_adapter *adapter);
static int e1000_setup_tx_resources(struct e1000_adapter *adapter,
                             struct e1000_tx_ring *txdr);
static int e1000_setup_rx_resources(struct e1000_adapter *adapter,
                             struct e1000_rx_ring *rxdr);
static void e1000_free_tx_resources(struct e1000_adapter *adapter,
                             struct e1000_tx_ring *tx_ring);
static void e1000_free_rx_resources(struct e1000_adapter *adapter,
                             struct e1000_rx_ring *rx_ring);
void e1000_update_stats(struct e1000_adapter *adapter);

static int e1000_init_module(void);
static void e1000_exit_module(void);
static int e1000_probe(struct pci_dev *pdev, const struct pci_device_id *ent);
static void __devexit e1000_remove(struct pci_dev *pdev);
static int e1000_alloc_queues(struct e1000_adapter *adapter);
static int e1000_sw_init(struct e1000_adapter *adapter);
static int e1000_open(struct net_device *netdev);
static int e1000_close(struct net_device *netdev);
static void e1000_configure_tx(struct e1000_adapter *adapter);
static void e1000_configure_rx(struct e1000_adapter *adapter);
static void e1000_setup_rctl(struct e1000_adapter *adapter);
static void e1000_clean_all_tx_rings(struct e1000_adapter *adapter);
static void e1000_clean_all_rx_rings(struct e1000_adapter *adapter);
static void e1000_clean_tx_ring(struct e1000_adapter *adapter,
                                struct e1000_tx_ring *tx_ring);
static void e1000_clean_rx_ring(struct e1000_adapter *adapter,
                                struct e1000_rx_ring *rx_ring);
static void e1000_set_rx_mode(struct net_device *netdev);
static void e1000_update_phy_info(unsigned long data);
static void e1000_watchdog(unsigned long data);
static void e1000_82547_tx_fifo_stall(unsigned long data);
static netdev_tx_t e1000_xmit_frame(struct sk_buff *skb,
				    struct net_device *netdev);
static struct net_device_stats * e1000_get_stats(struct net_device *netdev);
static int e1000_change_mtu(struct net_device *netdev, int new_mtu);
static int e1000_set_mac(struct net_device *netdev, void *p);
static irqreturn_t e1000_intr(int irq, void *data);
static bool e1000_clean_tx_irq(struct e1000_adapter *adapter,
			       struct e1000_tx_ring *tx_ring);
static int e1000_clean(struct napi_struct *napi, int budget);
static bool e1000_clean_rx_irq(struct e1000_adapter *adapter,
			       struct e1000_rx_ring *rx_ring,
			       int *work_done, int work_to_do);
static bool e1000_clean_jumbo_rx_irq(struct e1000_adapter *adapter,
				     struct e1000_rx_ring *rx_ring,
				     int *work_done, int work_to_do);
static void e1000_alloc_rx_buffers(struct e1000_adapter *adapter,
				   struct e1000_rx_ring *rx_ring,
				   int cleaned_count);
static void e1000_alloc_jumbo_rx_buffers(struct e1000_adapter *adapter,
					 struct e1000_rx_ring *rx_ring,
					 int cleaned_count);
static int e1000_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd);
static int e1000_mii_ioctl(struct net_device *netdev, struct ifreq *ifr,
			   int cmd);
static void e1000_enter_82542_rst(struct e1000_adapter *adapter);
static void e1000_leave_82542_rst(struct e1000_adapter *adapter);
static void e1000_tx_timeout(struct net_device *dev);
static void e1000_reset_task(struct work_struct *work);
static void e1000_smartspeed(struct e1000_adapter *adapter);
static int e1000_82547_fifo_workaround(struct e1000_adapter *adapter,
                                       struct sk_buff *skb);

static void e1000_vlan_rx_register(struct net_device *netdev, struct vlan_group *grp);
static void e1000_vlan_rx_add_vid(struct net_device *netdev, u16 vid);
static void e1000_vlan_rx_kill_vid(struct net_device *netdev, u16 vid);
static void e1000_restore_vlan(struct e1000_adapter *adapter);

#ifdef CONFIG_PM
static int e1000_suspend(struct pci_dev *pdev, pm_message_t state);
static int e1000_resume(struct pci_dev *pdev);
#endif
static void e1000_shutdown(struct pci_dev *pdev);

#ifdef CONFIG_NET_POLL_CONTROLLER
/* for netdump / net console */
static void e1000_netpoll (struct net_device *netdev);
#endif

#define COPYBREAK_DEFAULT 256
static unsigned int copybreak __read_mostly = COPYBREAK_DEFAULT;
module_param(copybreak, uint, 0644);
MODULE_PARM_DESC(copybreak,
	"Maximum size of packet that is copied to a new buffer on receive");

static pci_ers_result_t e1000_io_error_detected(struct pci_dev *pdev,
                     pci_channel_state_t state);
static pci_ers_result_t e1000_io_slot_reset(struct pci_dev *pdev);
static void e1000_io_resume(struct pci_dev *pdev);

static struct pci_error_handlers e1000_err_handler = {
	.error_detected = e1000_io_error_detected,
	.slot_reset = e1000_io_slot_reset,
	.resume = e1000_io_resume,
};

static struct pci_driver e1000_driver = {
	.name     = e1000_driver_name,
	.id_table = e1000_pci_tbl,
	.probe    = e1000_probe,
	.remove   = __devexit_p(e1000_remove),
#ifdef CONFIG_PM
	/* Power Managment Hooks */
	.suspend  = e1000_suspend,
	.resume   = e1000_resume,
#endif
	.shutdown = e1000_shutdown,
	.err_handler = &e1000_err_handler
};

MODULE_AUTHOR("Intel Corporation, <linux.nics@intel.com>");
MODULE_DESCRIPTION("Intel(R) PRO/1000 Network Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

static int debug = NETIF_MSG_DRV | NETIF_MSG_PROBE;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0=none,...,16=all)");

/**
 * e1000_init_module - Driver Registration Routine
 *
 * e1000_init_module is the first routine called when the driver is
 * loaded. All it does is register with the PCI subsystem.
 **/

static int __init e1000_init_module(void)
{
	int ret;
	printk(KERN_INFO "%s - version %s\n",
	       e1000_driver_string, e1000_driver_version);

	printk(KERN_INFO "%s\n", e1000_copyright);

	ret = pci_register_driver(&e1000_driver);
	if (copybreak != COPYBREAK_DEFAULT) {
		if (copybreak == 0)
			printk(KERN_INFO "e1000: copybreak disabled\n");
		else
			printk(KERN_INFO "e1000: copybreak enabled for "
			       "packets <= %u bytes\n", copybreak);
	}
	return ret;
}

module_init(e1000_init_module);

/**
 * e1000_exit_module - Driver Exit Cleanup Routine
 *
 * e1000_exit_module is called just before the driver is removed
 * from memory.
 **/

static void __exit e1000_exit_module(void)
{
	pci_unregister_driver(&e1000_driver);
}

module_exit(e1000_exit_module);

static int e1000_request_irq(struct e1000_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	irq_handler_t handler = e1000_intr;
	int irq_flags = IRQF_SHARED;
	int err;

	err = request_irq(adapter->pdev->irq, handler, irq_flags, netdev->name,
	                  netdev);
	if (err) {
		DPRINTK(PROBE, ERR,
		        "Unable to allocate interrupt Error: %d\n", err);
	}

	return err;
}

static void e1000_free_irq(struct e1000_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;

	free_irq(adapter->pdev->irq, netdev);
}

/**
 * e1000_irq_disable - Mask off interrupt generation on the NIC
 * @adapter: board private structure
 **/

static void e1000_irq_disable(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;

	ew32(IMC, ~0);
	E1000_WRITE_FLUSH();
	synchronize_irq(adapter->pdev->irq);
}

/**
 * e1000_irq_enable - Enable default interrupt generation settings
 * @adapter: board private structure
 **/

static void e1000_irq_enable(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;

	ew32(IMS, IMS_ENABLE_MASK);
	E1000_WRITE_FLUSH();
}

static void e1000_update_mng_vlan(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	struct net_device *netdev = adapter->netdev;
	u16 vid = hw->mng_cookie.vlan_id;
	u16 old_vid = adapter->mng_vlan_id;
	if (adapter->vlgrp) {
		if (!vlan_group_get_device(adapter->vlgrp, vid)) {
			if (hw->mng_cookie.status &
				E1000_MNG_DHCP_COOKIE_STATUS_VLAN_SUPPORT) {
				e1000_vlan_rx_add_vid(netdev, vid);
				adapter->mng_vlan_id = vid;
			} else
				adapter->mng_vlan_id = E1000_MNG_VLAN_NONE;

			if ((old_vid != (u16)E1000_MNG_VLAN_NONE) &&
					(vid != old_vid) &&
			    !vlan_group_get_device(adapter->vlgrp, old_vid))
				e1000_vlan_rx_kill_vid(netdev, old_vid);
		} else
			adapter->mng_vlan_id = vid;
	}
}

static void e1000_init_manageability(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;

	if (adapter->en_mng_pt) {
		u32 manc = er32(MANC);

		/* disable hardware interception of ARP */
		manc &= ~(E1000_MANC_ARP_EN);

		ew32(MANC, manc);
	}
}

static void e1000_release_manageability(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;

	if (adapter->en_mng_pt) {
		u32 manc = er32(MANC);

		/* re-enable hardware interception of ARP */
		manc |= E1000_MANC_ARP_EN;

		ew32(MANC, manc);
	}
}

/**
 * e1000_configure - configure the hardware for RX and TX
 * @adapter = private board structure
 **/
static void e1000_configure(struct e1000_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	int i;

	e1000_set_rx_mode(netdev);

	e1000_restore_vlan(adapter);
	e1000_init_manageability(adapter);

	e1000_configure_tx(adapter);
	e1000_setup_rctl(adapter);
	e1000_configure_rx(adapter);
	/* call E1000_DESC_UNUSED which always leaves
	 * at least 1 descriptor unused to make sure
	 * next_to_use != next_to_clean */
	for (i = 0; i < adapter->num_rx_queues; i++) {
		struct e1000_rx_ring *ring = &adapter->rx_ring[i];
		adapter->alloc_rx_buf(adapter, ring,
		                      E1000_DESC_UNUSED(ring));
	}

	adapter->tx_queue_len = netdev->tx_queue_len;
}

int e1000_up(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;

	/* hardware has been reset, we need to reload some things */
	e1000_configure(adapter);

	clear_bit(__E1000_DOWN, &adapter->flags);

	napi_enable(&adapter->napi);

	e1000_irq_enable(adapter);

	netif_wake_queue(adapter->netdev);

	/* fire a link change interrupt to start the watchdog */
	ew32(ICS, E1000_ICS_LSC);
	return 0;
}

/**
 * e1000_power_up_phy - restore link in case the phy was powered down
 * @adapter: address of board private structure
 *
 * The phy may be powered down to save power and turn off link when the
 * driver is unloaded and wake on lan is not enabled (among others)
 * *** this routine MUST be followed by a call to e1000_reset ***
 *
 **/

void e1000_power_up_phy(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u16 mii_reg = 0;

	/* Just clear the power down bit to wake the phy back up */
	if (hw->media_type == e1000_media_type_copper) {
		/* according to the manual, the phy will retain its
		 * settings across a power-down/up cycle */
		e1000_read_phy_reg(hw, PHY_CTRL, &mii_reg);
		mii_reg &= ~MII_CR_POWER_DOWN;
		e1000_write_phy_reg(hw, PHY_CTRL, mii_reg);
	}
}

static void e1000_power_down_phy(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;

	/* Power down the PHY so no link is implied when interface is down *
	 * The PHY cannot be powered down if any of the following is true *
	 * (a) WoL is enabled
	 * (b) AMT is active
	 * (c) SoL/IDER session is active */
	if (!adapter->wol && hw->mac_type >= e1000_82540 &&
	   hw->media_type == e1000_media_type_copper) {
		u16 mii_reg = 0;

		switch (hw->mac_type) {
		case e1000_82540:
		case e1000_82545:
		case e1000_82545_rev_3:
		case e1000_82546:
		case e1000_82546_rev_3:
		case e1000_82541:
		case e1000_82541_rev_2:
		case e1000_82547:
		case e1000_82547_rev_2:
			if (er32(MANC) & E1000_MANC_SMBUS_EN)
				goto out;
			break;
		default:
			goto out;
		}
		e1000_read_phy_reg(hw, PHY_CTRL, &mii_reg);
		mii_reg |= MII_CR_POWER_DOWN;
		e1000_write_phy_reg(hw, PHY_CTRL, mii_reg);
		mdelay(1);
	}
out:
	return;
}

void e1000_down(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	struct net_device *netdev = adapter->netdev;
	u32 rctl, tctl;

	/* signal that we're down so the interrupt handler does not
	 * reschedule our watchdog timer */
	set_bit(__E1000_DOWN, &adapter->flags);

	/* disable receives in the hardware */
	rctl = er32(RCTL);
	ew32(RCTL, rctl & ~E1000_RCTL_EN);
	/* flush and sleep below */

	netif_tx_disable(netdev);

	/* disable transmits in the hardware */
	tctl = er32(TCTL);
	tctl &= ~E1000_TCTL_EN;
	ew32(TCTL, tctl);
	/* flush both disables and wait for them to finish */
	E1000_WRITE_FLUSH();
	msleep(10);

	napi_disable(&adapter->napi);

	e1000_irq_disable(adapter);

	del_timer_sync(&adapter->tx_fifo_stall_timer);
	del_timer_sync(&adapter->watchdog_timer);
	del_timer_sync(&adapter->phy_info_timer);

	netdev->tx_queue_len = adapter->tx_queue_len;
	adapter->link_speed = 0;
	adapter->link_duplex = 0;
	netif_carrier_off(netdev);

	e1000_reset(adapter);
	e1000_clean_all_tx_rings(adapter);
	e1000_clean_all_rx_rings(adapter);
}

void e1000_reinit_locked(struct e1000_adapter *adapter)
{
	WARN_ON(in_interrupt());
	while (test_and_set_bit(__E1000_RESETTING, &adapter->flags))
		msleep(1);
	e1000_down(adapter);
	e1000_up(adapter);
	clear_bit(__E1000_RESETTING, &adapter->flags);
}

void e1000_reset(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 pba = 0, tx_space, min_tx_space, min_rx_space;
	bool legacy_pba_adjust = false;
	u16 hwm;

	/* Repartition Pba for greater than 9k mtu
	 * To take effect CTRL.RST is required.
	 */

	switch (hw->mac_type) {
	case e1000_82542_rev2_0:
	case e1000_82542_rev2_1:
	case e1000_82543:
	case e1000_82544:
	case e1000_82540:
	case e1000_82541:
	case e1000_82541_rev_2:
		legacy_pba_adjust = true;
		pba = E1000_PBA_48K;
		break;
	case e1000_82545:
	case e1000_82545_rev_3:
	case e1000_82546:
	case e1000_82546_rev_3:
		pba = E1000_PBA_48K;
		break;
	case e1000_82547:
	case e1000_82547_rev_2:
		legacy_pba_adjust = true;
		pba = E1000_PBA_30K;
		break;
	case e1000_undefined:
	case e1000_num_macs:
		break;
	}

	if (legacy_pba_adjust) {
		if (hw->max_frame_size > E1000_RXBUFFER_8192)
			pba -= 8; /* allocate more FIFO for Tx */

		if (hw->mac_type == e1000_82547) {
			adapter->tx_fifo_head = 0;
			adapter->tx_head_addr = pba << E1000_TX_HEAD_ADDR_SHIFT;
			adapter->tx_fifo_size =
				(E1000_PBA_40K - pba) << E1000_PBA_BYTES_SHIFT;
			atomic_set(&adapter->tx_fifo_stall, 0);
		}
	} else if (hw->max_frame_size >  ETH_FRAME_LEN + ETH_FCS_LEN) {
		/* adjust PBA for jumbo frames */
		ew32(PBA, pba);

		/* To maintain wire speed transmits, the Tx FIFO should be
		 * large enough to accommodate two full transmit packets,
		 * rounded up to the next 1KB and expressed in KB.  Likewise,
		 * the Rx FIFO should be large enough to accommodate at least
		 * one full receive packet and is similarly rounded up and
		 * expressed in KB. */
		pba = er32(PBA);
		/* upper 16 bits has Tx packet buffer allocation size in KB */
		tx_space = pba >> 16;
		/* lower 16 bits has Rx packet buffer allocation size in KB */
		pba &= 0xffff;
		/*
		 * the tx fifo also stores 16 bytes of information about the tx
		 * but don't include ethernet FCS because hardware appends it
		 */
		min_tx_space = (hw->max_frame_size +
		                sizeof(struct e1000_tx_desc) -
		                ETH_FCS_LEN) * 2;
		min_tx_space = ALIGN(min_tx_space, 1024);
		min_tx_space >>= 10;
		/* software strips receive CRC, so leave room for it */
		min_rx_space = hw->max_frame_size;
		min_rx_space = ALIGN(min_rx_space, 1024);
		min_rx_space >>= 10;

		/* If current Tx allocation is less than the min Tx FIFO size,
		 * and the min Tx FIFO size is less than the current Rx FIFO
		 * allocation, take space away from current Rx allocation */
		if (tx_space < min_tx_space &&
		    ((min_tx_space - tx_space) < pba)) {
			pba = pba - (min_tx_space - tx_space);

			/* PCI/PCIx hardware has PBA alignment constraints */
			switch (hw->mac_type) {
			case e1000_82545 ... e1000_82546_rev_3:
				pba &= ~(E1000_PBA_8K - 1);
				break;
			default:
				break;
			}

			/* if short on rx space, rx wins and must trump tx
			 * adjustment or use Early Receive if available */
			if (pba < min_rx_space)
				pba = min_rx_space;
		}
	}

	ew32(PBA, pba);

	/*
	 * flow control settings:
	 * The high water mark must be low enough to fit one full frame
	 * (or the size used for early receive) above it in the Rx FIFO.
	 * Set it to the lower of:
	 * - 90% of the Rx FIFO size, and
	 * - the full Rx FIFO size minus the early receive size (for parts
	 *   with ERT support assuming ERT set to E1000_ERT_2048), or
	 * - the full Rx FIFO size minus one full frame
	 */
	hwm = min(((pba << 10) * 9 / 10),
		  ((pba << 10) - hw->max_frame_size));

	hw->fc_high_water = hwm & 0xFFF8;	/* 8-byte granularity */
	hw->fc_low_water = hw->fc_high_water - 8;
	hw->fc_pause_time = E1000_FC_PAUSE_TIME;
	hw->fc_send_xon = 1;
	hw->fc = hw->original_fc;

	/* Allow time for pending master requests to run */
	e1000_reset_hw(hw);
	if (hw->mac_type >= e1000_82544)
		ew32(WUC, 0);

	if (e1000_init_hw(hw))
		DPRINTK(PROBE, ERR, "Hardware Error\n");
	e1000_update_mng_vlan(adapter);

	/* if (adapter->hwflags & HWFLAGS_PHY_PWR_BIT) { */
	if (hw->mac_type >= e1000_82544 &&
	    hw->autoneg == 1 &&
	    hw->autoneg_advertised == ADVERTISE_1000_FULL) {
		u32 ctrl = er32(CTRL);
		/* clear phy power management bit if we are in gig only mode,
		 * which if enabled will attempt negotiation to 100Mb, which
		 * can cause a loss of link at power off or driver unload */
		ctrl &= ~E1000_CTRL_SWDPIN3;
		ew32(CTRL, ctrl);
	}

	/* Enable h/w to recognize an 802.1Q VLAN Ethernet packet */
	ew32(VET, ETHERNET_IEEE_VLAN_TYPE);

	e1000_reset_adaptive(hw);
	e1000_phy_get_info(hw, &adapter->phy_info);

	e1000_release_manageability(adapter);
}

/**
 *  Dump the eeprom for users having checksum issues
 **/
static void e1000_dump_eeprom(struct e1000_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct ethtool_eeprom eeprom;
	const struct ethtool_ops *ops = netdev->ethtool_ops;
	u8 *data;
	int i;
	u16 csum_old, csum_new = 0;

	eeprom.len = ops->get_eeprom_len(netdev);
	eeprom.offset = 0;

	data = kmalloc(eeprom.len, GFP_KERNEL);
	if (!data) {
		printk(KERN_ERR "Unable to allocate memory to dump EEPROM"
		       " data\n");
		return;
	}

	ops->get_eeprom(netdev, &eeprom, data);

	csum_old = (data[EEPROM_CHECKSUM_REG * 2]) +
		   (data[EEPROM_CHECKSUM_REG * 2 + 1] << 8);
	for (i = 0; i < EEPROM_CHECKSUM_REG * 2; i += 2)
		csum_new += data[i] + (data[i + 1] << 8);
	csum_new = EEPROM_SUM - csum_new;

	printk(KERN_ERR "/*********************/\n");
	printk(KERN_ERR "Current EEPROM Checksum : 0x%04x\n", csum_old);
	printk(KERN_ERR "Calculated              : 0x%04x\n", csum_new);

	printk(KERN_ERR "Offset    Values\n");
	printk(KERN_ERR "========  ======\n");
	print_hex_dump(KERN_ERR, "", DUMP_PREFIX_OFFSET, 16, 1, data, 128, 0);

	printk(KERN_ERR "Include this output when contacting your support "
	       "provider.\n");
	printk(KERN_ERR "This is not a software error! Something bad "
	       "happened to your hardware or\n");
	printk(KERN_ERR "EEPROM image. Ignoring this "
	       "problem could result in further problems,\n");
	printk(KERN_ERR "possibly loss of data, corruption or system hangs!\n");
	printk(KERN_ERR "The MAC Address will be reset to 00:00:00:00:00:00, "
	       "which is invalid\n");
	printk(KERN_ERR "and requires you to set the proper MAC "
	       "address manually before continuing\n");
	printk(KERN_ERR "to enable this network device.\n");
	printk(KERN_ERR "Please inspect the EEPROM dump and report the issue "
	       "to your hardware vendor\n");
	printk(KERN_ERR "or Intel Customer Support.\n");
	printk(KERN_ERR "/*********************/\n");

	kfree(data);
}

/**
 * e1000_is_need_ioport - determine if an adapter needs ioport resources or not
 * @pdev: PCI device information struct
 *
 * Return true if an adapter needs ioport resources
 **/
static int e1000_is_need_ioport(struct pci_dev *pdev)
{
	switch (pdev->device) {
	case E1000_DEV_ID_82540EM:
	case E1000_DEV_ID_82540EM_LOM:
	case E1000_DEV_ID_82540EP:
	case E1000_DEV_ID_82540EP_LOM:
	case E1000_DEV_ID_82540EP_LP:
	case E1000_DEV_ID_82541EI:
	case E1000_DEV_ID_82541EI_MOBILE:
	case E1000_DEV_ID_82541ER:
	case E1000_DEV_ID_82541ER_LOM:
	case E1000_DEV_ID_82541GI:
	case E1000_DEV_ID_82541GI_LF:
	case E1000_DEV_ID_82541GI_MOBILE:
	case E1000_DEV_ID_82544EI_COPPER:
	case E1000_DEV_ID_82544EI_FIBER:
	case E1000_DEV_ID_82544GC_COPPER:
	case E1000_DEV_ID_82544GC_LOM:
	case E1000_DEV_ID_82545EM_COPPER:
	case E1000_DEV_ID_82545EM_FIBER:
	case E1000_DEV_ID_82546EB_COPPER:
	case E1000_DEV_ID_82546EB_FIBER:
	case E1000_DEV_ID_82546EB_QUAD_COPPER:
		return true;
	default:
		return false;
	}
}

static const struct net_device_ops e1000_netdev_ops = {
	.ndo_open		= e1000_open,
	.ndo_stop		= e1000_close,
	.ndo_start_xmit		= e1000_xmit_frame,
	.ndo_get_stats		= e1000_get_stats,
	.ndo_set_rx_mode	= e1000_set_rx_mode,
	.ndo_set_mac_address	= e1000_set_mac,
	.ndo_tx_timeout 	= e1000_tx_timeout,
	.ndo_change_mtu		= e1000_change_mtu,
	.ndo_do_ioctl		= e1000_ioctl,
	.ndo_validate_addr	= eth_validate_addr,

	.ndo_vlan_rx_register	= e1000_vlan_rx_register,
	.ndo_vlan_rx_add_vid	= e1000_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= e1000_vlan_rx_kill_vid,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= e1000_netpoll,
#endif
};

/**
 * e1000_probe - Device Initialization Routine
 * @pdev: PCI device information struct
 * @ent: entry in e1000_pci_tbl
 *
 * Returns 0 on success, negative on failure
 *
 * e1000_probe initializes an adapter identified by a pci_dev structure.
 * The OS initialization, configuring of the adapter private structure,
 * and a hardware reset occur.
 **/
static int __devinit e1000_probe(struct pci_dev *pdev,
				 const struct pci_device_id *ent)
{
	struct net_device *netdev;
	struct e1000_adapter *adapter;
	struct e1000_hw *hw;

	static int cards_found = 0;
	static int global_quad_port_a = 0; /* global ksp3 port a indication */
	int i, err, pci_using_dac;
	u16 eeprom_data = 0;
	u16 eeprom_apme_mask = E1000_EEPROM_APME;
	int bars, need_ioport;

	/* do not allocate ioport bars when not needed */
	need_ioport = e1000_is_need_ioport(pdev);
	if (need_ioport) {
		bars = pci_select_bars(pdev, IORESOURCE_MEM | IORESOURCE_IO);
		err = pci_enable_device(pdev);
	} else {
		bars = pci_select_bars(pdev, IORESOURCE_MEM);
		err = pci_enable_device_mem(pdev);
	}
	if (err)
		return err;

	if (!pci_set_dma_mask(pdev, DMA_BIT_MASK(64)) &&
	    !pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64))) {
		pci_using_dac = 1;
	} else {
		err = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (err) {
			err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
			if (err) {
				E1000_ERR("No usable DMA configuration, "
					  "aborting\n");
				goto err_dma;
			}
		}
		pci_using_dac = 0;
	}

	err = pci_request_selected_regions(pdev, bars, e1000_driver_name);
	if (err)
		goto err_pci_reg;

	pci_set_master(pdev);

	err = -ENOMEM;
	netdev = alloc_etherdev(sizeof(struct e1000_adapter));
	if (!netdev)
		goto err_alloc_etherdev;

	SET_NETDEV_DEV(netdev, &pdev->dev);

	pci_set_drvdata(pdev, netdev);
	adapter = netdev_priv(netdev);
	adapter->netdev = netdev;
	adapter->pdev = pdev;
	adapter->msg_enable = (1 << debug) - 1;
	adapter->bars = bars;
	adapter->need_ioport = need_ioport;

	hw = &adapter->hw;
	hw->back = adapter;

	err = -EIO;
	hw->hw_addr = pci_ioremap_bar(pdev, BAR_0);
	if (!hw->hw_addr)
		goto err_ioremap;

	if (adapter->need_ioport) {
		for (i = BAR_1; i <= BAR_5; i++) {
			if (pci_resource_len(pdev, i) == 0)
				continue;
			if (pci_resource_flags(pdev, i) & IORESOURCE_IO) {
				hw->io_base = pci_resource_start(pdev, i);
				break;
			}
		}
	}

	netdev->netdev_ops = &e1000_netdev_ops;
	e1000_set_ethtool_ops(netdev);
	netdev->watchdog_timeo = 5 * HZ;
	netif_napi_add(netdev, &adapter->napi, e1000_clean, 64);

	strncpy(netdev->name, pci_name(pdev), sizeof(netdev->name) - 1);

	adapter->bd_number = cards_found;

	/* setup the private structure */

	err = e1000_sw_init(adapter);
	if (err)
		goto err_sw_init;

	err = -EIO;

	if (hw->mac_type >= e1000_82543) {
		netdev->features = NETIF_F_SG |
				   NETIF_F_HW_CSUM |
				   NETIF_F_HW_VLAN_TX |
				   NETIF_F_HW_VLAN_RX |
				   NETIF_F_HW_VLAN_FILTER;
	}

	if ((hw->mac_type >= e1000_82544) &&
	   (hw->mac_type != e1000_82547))
		netdev->features |= NETIF_F_TSO;

	if (pci_using_dac)
		netdev->features |= NETIF_F_HIGHDMA;

	netdev->vlan_features |= NETIF_F_TSO;
	netdev->vlan_features |= NETIF_F_HW_CSUM;
	netdev->vlan_features |= NETIF_F_SG;

	adapter->en_mng_pt = e1000_enable_mng_pass_thru(hw);

	/* initialize eeprom parameters */
	if (e1000_init_eeprom_params(hw)) {
		E1000_ERR("EEPROM initialization failed\n");
		goto err_eeprom;
	}

	/* before reading the EEPROM, reset the controller to
	 * put the device in a known good starting state */

	e1000_reset_hw(hw);

	/* make sure the EEPROM is good */
	if (e1000_validate_eeprom_checksum(hw) < 0) {
		DPRINTK(PROBE, ERR, "The EEPROM Checksum Is Not Valid\n");
		e1000_dump_eeprom(adapter);
		/*
		 * set MAC address to all zeroes to invalidate and temporary
		 * disable this device for the user. This blocks regular
		 * traffic while still permitting ethtool ioctls from reaching
		 * the hardware as well as allowing the user to run the
		 * interface after manually setting a hw addr using
		 * `ip set address`
		 */
		memset(hw->mac_addr, 0, netdev->addr_len);
	} else {
		/* copy the MAC address out of the EEPROM */
		if (e1000_read_mac_addr(hw))
			DPRINTK(PROBE, ERR, "EEPROM Read Error\n");
	}
	/* don't block initalization here due to bad MAC address */
	memcpy(netdev->dev_addr, hw->mac_addr, netdev->addr_len);
	memcpy(netdev->perm_addr, hw->mac_addr, netdev->addr_len);

	if (!is_valid_ether_addr(netdev->perm_addr))
		DPRINTK(PROBE, ERR, "Invalid MAC Address\n");

	e1000_get_bus_info(hw);

	init_timer(&adapter->tx_fifo_stall_timer);
	adapter->tx_fifo_stall_timer.function = &e1000_82547_tx_fifo_stall;
	adapter->tx_fifo_stall_timer.data = (unsigned long)adapter;

	init_timer(&adapter->watchdog_timer);
	adapter->watchdog_timer.function = &e1000_watchdog;
	adapter->watchdog_timer.data = (unsigned long) adapter;

	init_timer(&adapter->phy_info_timer);
	adapter->phy_info_timer.function = &e1000_update_phy_info;
	adapter->phy_info_timer.data = (unsigned long)adapter;

	INIT_WORK(&adapter->reset_task, e1000_reset_task);

	e1000_check_options(adapter);

	/* Initial Wake on LAN setting
	 * If APM wake is enabled in the EEPROM,
	 * enable the ACPI Magic Packet filter
	 */

	switch (hw->mac_type) {
	case e1000_82542_rev2_0:
	case e1000_82542_rev2_1:
	case e1000_82543:
		break;
	case e1000_82544:
		e1000_read_eeprom(hw,
			EEPROM_INIT_CONTROL2_REG, 1, &eeprom_data);
		eeprom_apme_mask = E1000_EEPROM_82544_APM;
		break;
	case e1000_82546:
	case e1000_82546_rev_3:
		if (er32(STATUS) & E1000_STATUS_FUNC_1){
			e1000_read_eeprom(hw,
				EEPROM_INIT_CONTROL3_PORT_B, 1, &eeprom_data);
			break;
		}
		/* Fall Through */
	default:
		e1000_read_eeprom(hw,
			EEPROM_INIT_CONTROL3_PORT_A, 1, &eeprom_data);
		break;
	}
	if (eeprom_data & eeprom_apme_mask)
		adapter->eeprom_wol |= E1000_WUFC_MAG;

	/* now that we have the eeprom settings, apply the special cases
	 * where the eeprom may be wrong or the board simply won't support
	 * wake on lan on a particular port */
	switch (pdev->device) {
	case E1000_DEV_ID_82546GB_PCIE:
		adapter->eeprom_wol = 0;
		break;
	case E1000_DEV_ID_82546EB_FIBER:
	case E1000_DEV_ID_82546GB_FIBER:
		/* Wake events only supported on port A for dual fiber
		 * regardless of eeprom setting */
		if (er32(STATUS) & E1000_STATUS_FUNC_1)
			adapter->eeprom_wol = 0;
		break;
	case E1000_DEV_ID_82546GB_QUAD_COPPER_KSP3:
		/* if quad port adapter, disable WoL on all but port A */
		if (global_quad_port_a != 0)
			adapter->eeprom_wol = 0;
		else
			adapter->quad_port_a = 1;
		/* Reset for multiple quad port adapters */
		if (++global_quad_port_a == 4)
			global_quad_port_a = 0;
		break;
	}

	/* initialize the wol settings based on the eeprom settings */
	adapter->wol = adapter->eeprom_wol;
	device_set_wakeup_enable(&adapter->pdev->dev, adapter->wol);

	/* print bus type/speed/width info */
	DPRINTK(PROBE, INFO, "(PCI%s:%s:%s) ",
		((hw->bus_type == e1000_bus_type_pcix) ? "-X" : ""),
		((hw->bus_speed == e1000_bus_speed_133) ? "133MHz" :
		 (hw->bus_speed == e1000_bus_speed_120) ? "120MHz" :
		 (hw->bus_speed == e1000_bus_speed_100) ? "100MHz" :
		 (hw->bus_speed == e1000_bus_speed_66) ? "66MHz" : "33MHz"),
		((hw->bus_width == e1000_bus_width_64) ? "64-bit" : "32-bit"));

	printk("%pM\n", netdev->dev_addr);

	/* reset the hardware with the new settings */
	e1000_reset(adapter);

	strcpy(netdev->name, "eth%d");
	err = register_netdev(netdev);
	if (err)
		goto err_register;

	/* carrier off reporting is important to ethtool even BEFORE open */
	netif_carrier_off(netdev);

	DPRINTK(PROBE, INFO, "Intel(R) PRO/1000 Network Connection\n");

	cards_found++;
	return 0;

err_register:
err_eeprom:
	e1000_phy_hw_reset(hw);

	if (hw->flash_address)
		iounmap(hw->flash_address);
	kfree(adapter->tx_ring);
	kfree(adapter->rx_ring);
err_sw_init:
	iounmap(hw->hw_addr);
err_ioremap:
	free_netdev(netdev);
err_alloc_etherdev:
	pci_release_selected_regions(pdev, bars);
err_pci_reg:
err_dma:
	pci_disable_device(pdev);
	return err;
}

/**
 * e1000_remove - Device Removal Routine
 * @pdev: PCI device information struct
 *
 * e1000_remove is called by the PCI subsystem to alert the driver
 * that it should release a PCI device.  The could be caused by a
 * Hot-Plug event, or because the driver is going to be removed from
 * memory.
 **/

static void __devexit e1000_remove(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;

	set_bit(__E1000_DOWN, &adapter->flags);
	del_timer_sync(&adapter->tx_fifo_stall_timer);
	del_timer_sync(&adapter->watchdog_timer);
	del_timer_sync(&adapter->phy_info_timer);

	cancel_work_sync(&adapter->reset_task);

	e1000_release_manageability(adapter);

	unregister_netdev(netdev);

	e1000_phy_hw_reset(hw);

	kfree(adapter->tx_ring);
	kfree(adapter->rx_ring);

	iounmap(hw->hw_addr);
	if (hw->flash_address)
		iounmap(hw->flash_address);
	pci_release_selected_regions(pdev, adapter->bars);

	free_netdev(netdev);

	pci_disable_device(pdev);
}

/**
 * e1000_sw_init - Initialize general software structures (struct e1000_adapter)
 * @adapter: board private structure to initialize
 *
 * e1000_sw_init initializes the Adapter private data structure.
 * Fields are initialized based on PCI device information and
 * OS network device settings (MTU size).
 **/

static int __devinit e1000_sw_init(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	struct net_device *netdev = adapter->netdev;
	struct pci_dev *pdev = adapter->pdev;

	/* PCI config space info */

	hw->vendor_id = pdev->vendor;
	hw->device_id = pdev->device;
	hw->subsystem_vendor_id = pdev->subsystem_vendor;
	hw->subsystem_id = pdev->subsystem_device;
	hw->revision_id = pdev->revision;

	pci_read_config_word(pdev, PCI_COMMAND, &hw->pci_cmd_word);

	adapter->rx_buffer_len = MAXIMUM_ETHERNET_VLAN_SIZE;
	hw->max_frame_size = netdev->mtu +
			     ENET_HEADER_SIZE + ETHERNET_FCS_SIZE;
	hw->min_frame_size = MINIMUM_ETHERNET_FRAME_SIZE;

	/* identify the MAC */

	if (e1000_set_mac_type(hw)) {
		DPRINTK(PROBE, ERR, "Unknown MAC Type\n");
		return -EIO;
	}

	switch (hw->mac_type) {
	default:
		break;
	case e1000_82541:
	case e1000_82547:
	case e1000_82541_rev_2:
	case e1000_82547_rev_2:
		hw->phy_init_script = 1;
		break;
	}

	e1000_set_media_type(hw);

	hw->wait_autoneg_complete = false;
	hw->tbi_compatibility_en = true;
	hw->adaptive_ifs = true;

	/* Copper options */

	if (hw->media_type == e1000_media_type_copper) {
		hw->mdix = AUTO_ALL_MODES;
		hw->disable_polarity_correction = false;
		hw->master_slave = E1000_MASTER_SLAVE;
	}

	adapter->num_tx_queues = 1;
	adapter->num_rx_queues = 1;

	if (e1000_alloc_queues(adapter)) {
		DPRINTK(PROBE, ERR, "Unable to allocate memory for queues\n");
		return -ENOMEM;
	}

	/* Explicitly disable IRQ since the NIC can be in any state. */
	e1000_irq_disable(adapter);

	spin_lock_init(&adapter->stats_lock);

	set_bit(__E1000_DOWN, &adapter->flags);

	return 0;
}

/**
 * e1000_alloc_queues - Allocate memory for all rings
 * @adapter: board private structure to initialize
 *
 * We allocate one ring per queue at run-time since we don't know the
 * number of queues at compile-time.
 **/

static int __devinit e1000_alloc_queues(struct e1000_adapter *adapter)
{
	adapter->tx_ring = kcalloc(adapter->num_tx_queues,
	                           sizeof(struct e1000_tx_ring), GFP_KERNEL);
	if (!adapter->tx_ring)
		return -ENOMEM;

	adapter->rx_ring = kcalloc(adapter->num_rx_queues,
	                           sizeof(struct e1000_rx_ring), GFP_KERNEL);
	if (!adapter->rx_ring) {
		kfree(adapter->tx_ring);
		return -ENOMEM;
	}

	return E1000_SUCCESS;
}

/**
 * e1000_open - Called when a network interface is made active
 * @netdev: network interface device structure
 *
 * Returns 0 on success, negative value on failure
 *
 * The open entry point is called when a network interface is made
 * active by the system (IFF_UP).  At this point all resources needed
 * for transmit and receive operations are allocated, the interrupt
 * handler is registered with the OS, the watchdog timer is started,
 * and the stack is notified that the interface is ready.
 **/

static int e1000_open(struct net_device *netdev)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	int err;

	/* disallow open during test */
	if (test_bit(__E1000_TESTING, &adapter->flags))
		return -EBUSY;

	netif_carrier_off(netdev);

	/* allocate transmit descriptors */
	err = e1000_setup_all_tx_resources(adapter);
	if (err)
		goto err_setup_tx;

	/* allocate receive descriptors */
	err = e1000_setup_all_rx_resources(adapter);
	if (err)
		goto err_setup_rx;

	e1000_power_up_phy(adapter);

	adapter->mng_vlan_id = E1000_MNG_VLAN_NONE;
	if ((hw->mng_cookie.status &
			  E1000_MNG_DHCP_COOKIE_STATUS_VLAN_SUPPORT)) {
		e1000_update_mng_vlan(adapter);
	}

	/* before we allocate an interrupt, we must be ready to handle it.
	 * Setting DEBUG_SHIRQ in the kernel makes it fire an interrupt
	 * as soon as we call pci_request_irq, so we have to setup our
	 * clean_rx handler before we do so.  */
	e1000_configure(adapter);

	err = e1000_request_irq(adapter);
	if (err)
		goto err_req_irq;

	/* From here on the code is the same as e1000_up() */
	clear_bit(__E1000_DOWN, &adapter->flags);

	napi_enable(&adapter->napi);

	e1000_irq_enable(adapter);

	netif_start_queue(netdev);

	/* fire a link status change interrupt to start the watchdog */
	ew32(ICS, E1000_ICS_LSC);

	return E1000_SUCCESS;

err_req_irq:
	e1000_power_down_phy(adapter);
	e1000_free_all_rx_resources(adapter);
err_setup_rx:
	e1000_free_all_tx_resources(adapter);
err_setup_tx:
	e1000_reset(adapter);

	return err;
}

/**
 * e1000_close - Disables a network interface
 * @netdev: network interface device structure
 *
 * Returns 0, this is not allowed to fail
 *
 * The close entry point is called when an interface is de-activated
 * by the OS.  The hardware is still under the drivers control, but
 * needs to be disabled.  A global MAC reset is issued to stop the
 * hardware, and all transmit and receive resources are freed.
 **/

static int e1000_close(struct net_device *netdev)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;

	WARN_ON(test_bit(__E1000_RESETTING, &adapter->flags));
	e1000_down(adapter);
	e1000_power_down_phy(adapter);
	e1000_free_irq(adapter);

	e1000_free_all_tx_resources(adapter);
	e1000_free_all_rx_resources(adapter);

	/* kill manageability vlan ID if supported, but not if a vlan with
	 * the same ID is registered on the host OS (let 8021q kill it) */
	if ((hw->mng_cookie.status &
			  E1000_MNG_DHCP_COOKIE_STATUS_VLAN_SUPPORT) &&
	     !(adapter->vlgrp &&
	       vlan_group_get_device(adapter->vlgrp, adapter->mng_vlan_id))) {
		e1000_vlan_rx_kill_vid(netdev, adapter->mng_vlan_id);
	}

	return 0;
}

/**
 * e1000_check_64k_bound - check that memory doesn't cross 64kB boundary
 * @adapter: address of board private structure
 * @start: address of beginning of memory
 * @len: length of memory
 **/
static bool e1000_check_64k_bound(struct e1000_adapter *adapter, void *start,
				  unsigned long len)
{
	struct e1000_hw *hw = &adapter->hw;
	unsigned long begin = (unsigned long)start;
	unsigned long end = begin + len;

	/* First rev 82545 and 82546 need to not allow any memory
	 * write location to cross 64k boundary due to errata 23 */
	if (hw->mac_type == e1000_82545 ||
	    hw->mac_type == e1000_82546) {
		return ((begin ^ (end - 1)) >> 16) != 0 ? false : true;
	}

	return true;
}

/**
 * e1000_setup_tx_resources - allocate Tx resources (Descriptors)
 * @adapter: board private structure
 * @txdr:    tx descriptor ring (for a specific queue) to setup
 *
 * Return 0 on success, negative on failure
 **/

static int e1000_setup_tx_resources(struct e1000_adapter *adapter,
				    struct e1000_tx_ring *txdr)
{
	struct pci_dev *pdev = adapter->pdev;
	int size;

	size = sizeof(struct e1000_buffer) * txdr->count;
	txdr->buffer_info = vmalloc(size);
	if (!txdr->buffer_info) {
		DPRINTK(PROBE, ERR,
		"Unable to allocate memory for the transmit descriptor ring\n");
		return -ENOMEM;
	}
	memset(txdr->buffer_info, 0, size);

	/* round up to nearest 4K */

	txdr->size = txdr->count * sizeof(struct e1000_tx_desc);
	txdr->size = ALIGN(txdr->size, 4096);

	txdr->desc = pci_alloc_consistent(pdev, txdr->size, &txdr->dma);
	if (!txdr->desc) {
setup_tx_desc_die:
		vfree(txdr->buffer_info);
		DPRINTK(PROBE, ERR,
		"Unable to allocate memory for the transmit descriptor ring\n");
		return -ENOMEM;
	}

	/* Fix for errata 23, can't cross 64kB boundary */
	if (!e1000_check_64k_bound(adapter, txdr->desc, txdr->size)) {
		void *olddesc = txdr->desc;
		dma_addr_t olddma = txdr->dma;
		DPRINTK(TX_ERR, ERR, "txdr align check failed: %u bytes "
				     "at %p\n", txdr->size, txdr->desc);
		/* Try again, without freeing the previous */
		txdr->desc = pci_alloc_consistent(pdev, txdr->size, &txdr->dma);
		/* Failed allocation, critical failure */
		if (!txdr->desc) {
			pci_free_consistent(pdev, txdr->size, olddesc, olddma);
			goto setup_tx_desc_die;
		}

		if (!e1000_check_64k_bound(adapter, txdr->desc, txdr->size)) {
			/* give up */
			pci_free_consistent(pdev, txdr->size, txdr->desc,
					    txdr->dma);
			pci_free_consistent(pdev, txdr->size, olddesc, olddma);
			DPRINTK(PROBE, ERR,
				"Unable to allocate aligned memory "
				"for the transmit descriptor ring\n");
			vfree(txdr->buffer_info);
			return -ENOMEM;
		} else {
			/* Free old allocation, new allocation was successful */
			pci_free_consistent(pdev, txdr->size, olddesc, olddma);
		}
	}
	memset(txdr->desc, 0, txdr->size);

	txdr->next_to_use = 0;
	txdr->next_to_clean = 0;

	return 0;
}

/**
 * e1000_setup_all_tx_resources - wrapper to allocate Tx resources
 * 				  (Descriptors) for all queues
 * @adapter: board private structure
 *
 * Return 0 on success, negative on failure
 **/

int e1000_setup_all_tx_resources(struct e1000_adapter *adapter)
{
	int i, err = 0;

	for (i = 0; i < adapter->num_tx_queues; i++) {
		err = e1000_setup_tx_resources(adapter, &adapter->tx_ring[i]);
		if (err) {
			DPRINTK(PROBE, ERR,
				"Allocation for Tx Queue %u failed\n", i);
			for (i-- ; i >= 0; i--)
				e1000_free_tx_resources(adapter,
							&adapter->tx_ring[i]);
			break;
		}
	}

	return err;
}

/**
 * e1000_configure_tx - Configure 8254x Transmit Unit after Reset
 * @adapter: board private structure
 *
 * Configure the Tx unit of the MAC after a reset.
 **/

static void e1000_configure_tx(struct e1000_adapter *adapter)
{
	u64 tdba;
	struct e1000_hw *hw = &adapter->hw;
	u32 tdlen, tctl, tipg;
	u32 ipgr1, ipgr2;

	/* Setup the HW Tx Head and Tail descriptor pointers */

	switch (adapter->num_tx_queues) {
	case 1:
	default:
		tdba = adapter->tx_ring[0].dma;
		tdlen = adapter->tx_ring[0].count *
			sizeof(struct e1000_tx_desc);
		ew32(TDLEN, tdlen);
		ew32(TDBAH, (tdba >> 32));
		ew32(TDBAL, (tdba & 0x00000000ffffffffULL));
		ew32(TDT, 0);
		ew32(TDH, 0);
		adapter->tx_ring[0].tdh = ((hw->mac_type >= e1000_82543) ? E1000_TDH : E1000_82542_TDH);
		adapter->tx_ring[0].tdt = ((hw->mac_type >= e1000_82543) ? E1000_TDT : E1000_82542_TDT);
		break;
	}

	/* Set the default values for the Tx Inter Packet Gap timer */
	if ((hw->media_type == e1000_media_type_fiber ||
	     hw->media_type == e1000_media_type_internal_serdes))
		tipg = DEFAULT_82543_TIPG_IPGT_FIBER;
	else
		tipg = DEFAULT_82543_TIPG_IPGT_COPPER;

	switch (hw->mac_type) {
	case e1000_82542_rev2_0:
	case e1000_82542_rev2_1:
		tipg = DEFAULT_82542_TIPG_IPGT;
		ipgr1 = DEFAULT_82542_TIPG_IPGR1;
		ipgr2 = DEFAULT_82542_TIPG_IPGR2;
		break;
	default:
		ipgr1 = DEFAULT_82543_TIPG_IPGR1;
		ipgr2 = DEFAULT_82543_TIPG_IPGR2;
		break;
	}
	tipg |= ipgr1 << E1000_TIPG_IPGR1_SHIFT;
	tipg |= ipgr2 << E1000_TIPG_IPGR2_SHIFT;
	ew32(TIPG, tipg);

	/* Set the Tx Interrupt Delay register */

	ew32(TIDV, adapter->tx_int_delay);
	if (hw->mac_type >= e1000_82540)
		ew32(TADV, adapter->tx_abs_int_delay);

	/* Program the Transmit Control Register */

	tctl = er32(TCTL);
	tctl &= ~E1000_TCTL_CT;
	tctl |= E1000_TCTL_PSP | E1000_TCTL_RTLC |
		(E1000_COLLISION_THRESHOLD << E1000_CT_SHIFT);

	e1000_config_collision_dist(hw);

	/* Setup Transmit Descriptor Settings for eop descriptor */
	adapter->txd_cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS;

	/* only set IDE if we are delaying interrupts using the timers */
	if (adapter->tx_int_delay)
		adapter->txd_cmd |= E1000_TXD_CMD_IDE;

	if (hw->mac_type < e1000_82543)
		adapter->txd_cmd |= E1000_TXD_CMD_RPS;
	else
		adapter->txd_cmd |= E1000_TXD_CMD_RS;

	/* Cache if we're 82544 running in PCI-X because we'll
	 * need this to apply a workaround later in the send path. */
	if (hw->mac_type == e1000_82544 &&
	    hw->bus_type == e1000_bus_type_pcix)
		adapter->pcix_82544 = 1;

	ew32(TCTL, tctl);

}

/**
 * e1000_setup_rx_resources - allocate Rx resources (Descriptors)
 * @adapter: board private structure
 * @rxdr:    rx descriptor ring (for a specific queue) to setup
 *
 * Returns 0 on success, negative on failure
 **/

static int e1000_setup_rx_resources(struct e1000_adapter *adapter,
				    struct e1000_rx_ring *rxdr)
{
	struct pci_dev *pdev = adapter->pdev;
	int size, desc_len;

	size = sizeof(struct e1000_buffer) * rxdr->count;
	rxdr->buffer_info = vmalloc(size);
	if (!rxdr->buffer_info) {
		DPRINTK(PROBE, ERR,
		"Unable to allocate memory for the receive descriptor ring\n");
		return -ENOMEM;
	}
	memset(rxdr->buffer_info, 0, size);

	desc_len = sizeof(struct e1000_rx_desc);

	/* Round up to nearest 4K */

	rxdr->size = rxdr->count * desc_len;
	rxdr->size = ALIGN(rxdr->size, 4096);

	rxdr->desc = pci_alloc_consistent(pdev, rxdr->size, &rxdr->dma);

	if (!rxdr->desc) {
		DPRINTK(PROBE, ERR,
		"Unable to allocate memory for the receive descriptor ring\n");
setup_rx_desc_die:
		vfree(rxdr->buffer_info);
		return -ENOMEM;
	}

	/* Fix for errata 23, can't cross 64kB boundary */
	if (!e1000_check_64k_bound(adapter, rxdr->desc, rxdr->size)) {
		void *olddesc = rxdr->desc;
		dma_addr_t olddma = rxdr->dma;
		DPRINTK(RX_ERR, ERR, "rxdr align check failed: %u bytes "
				     "at %p\n", rxdr->size, rxdr->desc);
		/* Try again, without freeing the previous */
		rxdr->desc = pci_alloc_consistent(pdev, rxdr->size, &rxdr->dma);
		/* Failed allocation, critical failure */
		if (!rxdr->desc) {
			pci_free_consistent(pdev, rxdr->size, olddesc, olddma);
			DPRINTK(PROBE, ERR,
				"Unable to allocate memory "
				"for the receive descriptor ring\n");
			goto setup_rx_desc_die;
		}

		if (!e1000_check_64k_bound(adapter, rxdr->desc, rxdr->size)) {
			/* give up */
			pci_free_consistent(pdev, rxdr->size, rxdr->desc,
					    rxdr->dma);
			pci_free_consistent(pdev, rxdr->size, olddesc, olddma);
			DPRINTK(PROBE, ERR,
				"Unable to allocate aligned memory "
				"for the receive descriptor ring\n");
			goto setup_rx_desc_die;
		} else {
			/* Free old allocation, new allocation was successful */
			pci_free_consistent(pdev, rxdr->size, olddesc, olddma);
		}
	}
	memset(rxdr->desc, 0, rxdr->size);

	rxdr->next_to_clean = 0;
	rxdr->next_to_use = 0;
	rxdr->rx_skb_top = NULL;

	return 0;
}

/**
 * e1000_setup_all_rx_resources - wrapper to allocate Rx resources
 * 				  (Descriptors) for all queues
 * @adapter: board private structure
 *
 * Return 0 on success, negative on failure
 **/

int e1000_setup_all_rx_resources(struct e1000_adapter *adapter)
{
	int i, err = 0;

	for (i = 0; i < adapter->num_rx_queues; i++) {
		err = e1000_setup_rx_resources(adapter, &adapter->rx_ring[i]);
		if (err) {
			DPRINTK(PROBE, ERR,
				"Allocation for Rx Queue %u failed\n", i);
			for (i-- ; i >= 0; i--)
				e1000_free_rx_resources(adapter,
							&adapter->rx_ring[i]);
			break;
		}
	}

	return err;
}

/**
 * e1000_setup_rctl - configure the receive control registers
 * @adapter: Board private structure
 **/
static void e1000_setup_rctl(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u32 rctl;

	rctl = er32(RCTL);

	rctl &= ~(3 << E1000_RCTL_MO_SHIFT);

	rctl |= E1000_RCTL_EN | E1000_RCTL_BAM |
		E1000_RCTL_LBM_NO | E1000_RCTL_RDMTS_HALF |
		(hw->mc_filter_type << E1000_RCTL_MO_SHIFT);

	if (hw->tbi_compatibility_on == 1)
		rctl |= E1000_RCTL_SBP;
	else
		rctl &= ~E1000_RCTL_SBP;

	if (adapter->netdev->mtu <= ETH_DATA_LEN)
		rctl &= ~E1000_RCTL_LPE;
	else
		rctl |= E1000_RCTL_LPE;

	/* Setup buffer sizes */
	rctl &= ~E1000_RCTL_SZ_4096;
	rctl |= E1000_RCTL_BSEX;
	switch (adapter->rx_buffer_len) {
		case E1000_RXBUFFER_256:
			rctl |= E1000_RCTL_SZ_256;
			rctl &= ~E1000_RCTL_BSEX;
			break;
		case E1000_RXBUFFER_512:
			rctl |= E1000_RCTL_SZ_512;
			rctl &= ~E1000_RCTL_BSEX;
			break;
		case E1000_RXBUFFER_1024:
			rctl |= E1000_RCTL_SZ_1024;
			rctl &= ~E1000_RCTL_BSEX;
			break;
		case E1000_RXBUFFER_2048:
		default:
			rctl |= E1000_RCTL_SZ_2048;
			rctl &= ~E1000_RCTL_BSEX;
			break;
		case E1000_RXBUFFER_4096:
			rctl |= E1000_RCTL_SZ_4096;
			break;
		case E1000_RXBUFFER_8192:
			rctl |= E1000_RCTL_SZ_8192;
			break;
		case E1000_RXBUFFER_16384:
			rctl |= E1000_RCTL_SZ_16384;
			break;
	}

	ew32(RCTL, rctl);
}

/**
 * e1000_configure_rx - Configure 8254x Receive Unit after Reset
 * @adapter: board private structure
 *
 * Configure the Rx unit of the MAC after a reset.
 **/

static void e1000_configure_rx(struct e1000_adapter *adapter)
{
	u64 rdba;
	struct e1000_hw *hw = &adapter->hw;
	u32 rdlen, rctl, rxcsum;

	if (adapter->netdev->mtu > ETH_DATA_LEN) {
		rdlen = adapter->rx_ring[0].count *
		        sizeof(struct e1000_rx_desc);
		adapter->clean_rx = e1000_clean_jumbo_rx_irq;
		adapter->alloc_rx_buf = e1000_alloc_jumbo_rx_buffers;
	} else {
		rdlen = adapter->rx_ring[0].count *
		        sizeof(struct e1000_rx_desc);
		adapter->clean_rx = e1000_clean_rx_irq;
		adapter->alloc_rx_buf = e1000_alloc_rx_buffers;
	}

	/* disable receives while setting up the descriptors */
	rctl = er32(RCTL);
	ew32(RCTL, rctl & ~E1000_RCTL_EN);

	/* set the Receive Delay Timer Register */
	ew32(RDTR, adapter->rx_int_delay);

	if (hw->mac_type >= e1000_82540) {
		ew32(RADV, adapter->rx_abs_int_delay);
		if (adapter->itr_setting != 0)
			ew32(ITR, 1000000000 / (adapter->itr * 256));
	}

	/* Setup the HW Rx Head and Tail Descriptor Pointers and
	 * the Base and Length of the Rx Descriptor Ring */
	switch (adapter->num_rx_queues) {
	case 1:
	default:
		rdba = adapter->rx_ring[0].dma;
		ew32(RDLEN, rdlen);
		ew32(RDBAH, (rdba >> 32));
		ew32(RDBAL, (rdba & 0x00000000ffffffffULL));
		ew32(RDT, 0);
		ew32(RDH, 0);
		adapter->rx_ring[0].rdh = ((hw->mac_type >= e1000_82543) ? E1000_RDH : E1000_82542_RDH);
		adapter->rx_ring[0].rdt = ((hw->mac_type >= e1000_82543) ? E1000_RDT : E1000_82542_RDT);
		break;
	}

	/* Enable 82543 Receive Checksum Offload for TCP and UDP */
	if (hw->mac_type >= e1000_82543) {
		rxcsum = er32(RXCSUM);
		if (adapter->rx_csum)
			rxcsum |= E1000_RXCSUM_TUOFL;
		else
			/* don't need to clear IPPCSE as it defaults to 0 */
			rxcsum &= ~E1000_RXCSUM_TUOFL;
		ew32(RXCSUM, rxcsum);
	}

	/* Enable Receives */
	ew32(RCTL, rctl);
}

/**
 * e1000_free_tx_resources - Free Tx Resources per Queue
 * @adapter: board private structure
 * @tx_ring: Tx descriptor ring for a specific queue
 *
 * Free all transmit software resources
 **/

static void e1000_free_tx_resources(struct e1000_adapter *adapter,
				    struct e1000_tx_ring *tx_ring)
{
	struct pci_dev *pdev = adapter->pdev;

	e1000_clean_tx_ring(adapter, tx_ring);

	vfree(tx_ring->buffer_info);
	tx_ring->buffer_info = NULL;

	pci_free_consistent(pdev, tx_ring->size, tx_ring->desc, tx_ring->dma);

	tx_ring->desc = NULL;
}

/**
 * e1000_free_all_tx_resources - Free Tx Resources for All Queues
 * @adapter: board private structure
 *
 * Free all transmit software resources
 **/

void e1000_free_all_tx_resources(struct e1000_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_tx_queues; i++)
		e1000_free_tx_resources(adapter, &adapter->tx_ring[i]);
}

static void e1000_unmap_and_free_tx_resource(struct e1000_adapter *adapter,
					     struct e1000_buffer *buffer_info)
{
	buffer_info->dma = 0;
	if (buffer_info->skb) {
		skb_dma_unmap(&adapter->pdev->dev, buffer_info->skb,
		              DMA_TO_DEVICE);
		dev_kfree_skb_any(buffer_info->skb);
		buffer_info->skb = NULL;
	}
	buffer_info->time_stamp = 0;
	/* buffer_info must be completely set up in the transmit path */
}

/**
 * e1000_clean_tx_ring - Free Tx Buffers
 * @adapter: board private structure
 * @tx_ring: ring to be cleaned
 **/

static void e1000_clean_tx_ring(struct e1000_adapter *adapter,
				struct e1000_tx_ring *tx_ring)
{
	struct e1000_hw *hw = &adapter->hw;
	struct e1000_buffer *buffer_info;
	unsigned long size;
	unsigned int i;

	/* Free all the Tx ring sk_buffs */

	for (i = 0; i < tx_ring->count; i++) {
		buffer_info = &tx_ring->buffer_info[i];
		e1000_unmap_and_free_tx_resource(adapter, buffer_info);
	}

	size = sizeof(struct e1000_buffer) * tx_ring->count;
	memset(tx_ring->buffer_info, 0, size);

	/* Zero out the descriptor ring */

	memset(tx_ring->desc, 0, tx_ring->size);

	tx_ring->next_to_use = 0;
	tx_ring->next_to_clean = 0;
	tx_ring->last_tx_tso = 0;

	writel(0, hw->hw_addr + tx_ring->tdh);
	writel(0, hw->hw_addr + tx_ring->tdt);
}

/**
 * e1000_clean_all_tx_rings - Free Tx Buffers for all queues
 * @adapter: board private structure
 **/

static void e1000_clean_all_tx_rings(struct e1000_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_tx_queues; i++)
		e1000_clean_tx_ring(adapter, &adapter->tx_ring[i]);
}

/**
 * e1000_free_rx_resources - Free Rx Resources
 * @adapter: board private structure
 * @rx_ring: ring to clean the resources from
 *
 * Free all receive software resources
 **/

static void e1000_free_rx_resources(struct e1000_adapter *adapter,
				    struct e1000_rx_ring *rx_ring)
{
	struct pci_dev *pdev = adapter->pdev;

	e1000_clean_rx_ring(adapter, rx_ring);

	vfree(rx_ring->buffer_info);
	rx_ring->buffer_info = NULL;

	pci_free_consistent(pdev, rx_ring->size, rx_ring->desc, rx_ring->dma);

	rx_ring->desc = NULL;
}

/**
 * e1000_free_all_rx_resources - Free Rx Resources for All Queues
 * @adapter: board private structure
 *
 * Free all receive software resources
 **/

void e1000_free_all_rx_resources(struct e1000_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_rx_queues; i++)
		e1000_free_rx_resources(adapter, &adapter->rx_ring[i]);
}

/**
 * e1000_clean_rx_ring - Free Rx Buffers per Queue
 * @adapter: board private structure
 * @rx_ring: ring to free buffers from
 **/

static void e1000_clean_rx_ring(struct e1000_adapter *adapter,
				struct e1000_rx_ring *rx_ring)
{
	struct e1000_hw *hw = &adapter->hw;
	struct e1000_buffer *buffer_info;
	struct pci_dev *pdev = adapter->pdev;
	unsigned long size;
	unsigned int i;

	/* Free all the Rx ring sk_buffs */
	for (i = 0; i < rx_ring->count; i++) {
		buffer_info = &rx_ring->buffer_info[i];
		if (buffer_info->dma &&
		    adapter->clean_rx == e1000_clean_rx_irq) {
			pci_unmap_single(pdev, buffer_info->dma,
			                 buffer_info->length,
			                 PCI_DMA_FROMDEVICE);
		} else if (buffer_info->dma &&
		           adapter->clean_rx == e1000_clean_jumbo_rx_irq) {
			pci_unmap_page(pdev, buffer_info->dma,
			               buffer_info->length,
			               PCI_DMA_FROMDEVICE);
		}

		buffer_info->dma = 0;
		if (buffer_info->page) {
			put_page(buffer_info->page);
			buffer_info->page = NULL;
		}
		if (buffer_info->skb) {
			dev_kfree_skb(buffer_info->skb);
			buffer_info->skb = NULL;
		}
	}

	/* there also may be some cached data from a chained receive */
	if (rx_ring->rx_skb_top) {
		dev_kfree_skb(rx_ring->rx_skb_top);
		rx_ring->rx_skb_top = NULL;
	}

	size = sizeof(struct e1000_buffer) * rx_ring->count;
	memset(rx_ring->buffer_info, 0, size);

	/* Zero out the descriptor ring */
	memset(rx_ring->desc, 0, rx_ring->size);

	rx_ring->next_to_clean = 0;
	rx_ring->next_to_use = 0;

	writel(0, hw->hw_addr + rx_ring->rdh);
	writel(0, hw->hw_addr + rx_ring->rdt);
}

/**
 * e1000_clean_all_rx_rings - Free Rx Buffers for all queues
 * @adapter: board private structure
 **/

static void e1000_clean_all_rx_rings(struct e1000_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_rx_queues; i++)
		e1000_clean_rx_ring(adapter, &adapter->rx_ring[i]);
}

/* The 82542 2.0 (revision 2) needs to have the receive unit in reset
 * and memory write and invalidate disabled for certain operations
 */
static void e1000_enter_82542_rst(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	struct net_device *netdev = adapter->netdev;
	u32 rctl;

	e1000_pci_clear_mwi(hw);

	rctl = er32(RCTL);
	rctl |= E1000_RCTL_RST;
	ew32(RCTL, rctl);
	E1000_WRITE_FLUSH();
	mdelay(5);

	if (netif_running(netdev))
		e1000_clean_all_rx_rings(adapter);
}

static void e1000_leave_82542_rst(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	struct net_device *netdev = adapter->netdev;
	u32 rctl;

	rctl = er32(RCTL);
	rctl &= ~E1000_RCTL_RST;
	ew32(RCTL, rctl);
	E1000_WRITE_FLUSH();
	mdelay(5);

	if (hw->pci_cmd_word & PCI_COMMAND_INVALIDATE)
		e1000_pci_set_mwi(hw);

	if (netif_running(netdev)) {
		/* No need to loop, because 82542 supports only 1 queue */
		struct e1000_rx_ring *ring = &adapter->rx_ring[0];
		e1000_configure_rx(adapter);
		adapter->alloc_rx_buf(adapter, ring, E1000_DESC_UNUSED(ring));
	}
}

/**
 * e1000_set_mac - Change the Ethernet Address of the NIC
 * @netdev: network interface device structure
 * @p: pointer to an address structure
 *
 * Returns 0 on success, negative on failure
 **/

static int e1000_set_mac(struct net_device *netdev, void *p)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	/* 82542 2.0 needs to be in reset to write receive address registers */

	if (hw->mac_type == e1000_82542_rev2_0)
		e1000_enter_82542_rst(adapter);

	memcpy(netdev->dev_addr, addr->sa_data, netdev->addr_len);
	memcpy(hw->mac_addr, addr->sa_data, netdev->addr_len);

	e1000_rar_set(hw, hw->mac_addr, 0);

	if (hw->mac_type == e1000_82542_rev2_0)
		e1000_leave_82542_rst(adapter);

	return 0;
}

/**
 * e1000_set_rx_mode - Secondary Unicast, Multicast and Promiscuous mode set
 * @netdev: network interface device structure
 *
 * The set_rx_mode entry point is called whenever the unicast or multicast
 * address lists or the network interface flags are updated. This routine is
 * responsible for configuring the hardware for proper unicast, multicast,
 * promiscuous mode, and all-multi behavior.
 **/

static void e1000_set_rx_mode(struct net_device *netdev)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	struct netdev_hw_addr *ha;
	bool use_uc = false;
	struct dev_addr_list *mc_ptr;
	u32 rctl;
	u32 hash_value;
	int i, rar_entries = E1000_RAR_ENTRIES;
	int mta_reg_count = E1000_NUM_MTA_REGISTERS;
	u32 *mcarray = kcalloc(mta_reg_count, sizeof(u32), GFP_ATOMIC);

	if (!mcarray) {
		DPRINTK(PROBE, ERR, "memory allocation failed\n");
		return;
	}

	/* Check for Promiscuous and All Multicast modes */

	rctl = er32(RCTL);

	if (netdev->flags & IFF_PROMISC) {
		rctl |= (E1000_RCTL_UPE | E1000_RCTL_MPE);
		rctl &= ~E1000_RCTL_VFE;
	} else {
		if (netdev->flags & IFF_ALLMULTI)
			rctl |= E1000_RCTL_MPE;
		else
			rctl &= ~E1000_RCTL_MPE;
		/* Enable VLAN filter if there is a VLAN */
		if (adapter->vlgrp)
			rctl |= E1000_RCTL_VFE;
	}

	if (netdev->uc.count > rar_entries - 1) {
		rctl |= E1000_RCTL_UPE;
	} else if (!(netdev->flags & IFF_PROMISC)) {
		rctl &= ~E1000_RCTL_UPE;
		use_uc = true;
	}

	ew32(RCTL, rctl);

	/* 82542 2.0 needs to be in reset to write receive address registers */

	if (hw->mac_type == e1000_82542_rev2_0)
		e1000_enter_82542_rst(adapter);

	/* load the first 14 addresses into the exact filters 1-14. Unicast
	 * addresses take precedence to avoid disabling unicast filtering
	 * when possible.
	 *
	 * RAR 0 is used for the station MAC adddress
	 * if there are not 14 addresses, go ahead and clear the filters
	 */
	i = 1;
	if (use_uc)
		list_for_each_entry(ha, &netdev->uc.list, list) {
			if (i == rar_entries)
				break;
			e1000_rar_set(hw, ha->addr, i++);
		}

	WARN_ON(i == rar_entries);

	mc_ptr = netdev->mc_list;

	for (; i < rar_entries; i++) {
		if (mc_ptr) {
			e1000_rar_set(hw, mc_ptr->da_addr, i);
			mc_ptr = mc_ptr->next;
		} else {
			E1000_WRITE_REG_ARRAY(hw, RA, i << 1, 0);
			E1000_WRITE_FLUSH();
			E1000_WRITE_REG_ARRAY(hw, RA, (i << 1) + 1, 0);
			E1000_WRITE_FLUSH();
		}
	}

	/* load any remaining addresses into the hash table */

	for (; mc_ptr; mc_ptr = mc_ptr->next) {
		u32 hash_reg, hash_bit, mta;
		hash_value = e1000_hash_mc_addr(hw, mc_ptr->da_addr);
		hash_reg = (hash_value >> 5) & 0x7F;
		hash_bit = hash_value & 0x1F;
		mta = (1 << hash_bit);
		mcarray[hash_reg] |= mta;
	}

	/* write the hash table completely, write from bottom to avoid
	 * both stupid write combining chipsets, and flushing each write */
	for (i = mta_reg_count - 1; i >= 0 ; i--) {
		/*
		 * If we are on an 82544 has an errata where writing odd
		 * offsets overwrites the previous even offset, but writing
		 * backwards over the range solves the issue by always
		 * writing the odd offset first
		 */
		E1000_WRITE_REG_ARRAY(hw, MTA, i, mcarray[i]);
	}
	E1000_WRITE_FLUSH();

	if (hw->mac_type == e1000_82542_rev2_0)
		e1000_leave_82542_rst(adapter);

	kfree(mcarray);
}

/* Need to wait a few seconds after link up to get diagnostic information from
 * the phy */

static void e1000_update_phy_info(unsigned long data)
{
	struct e1000_adapter *adapter = (struct e1000_adapter *)data;
	struct e1000_hw *hw = &adapter->hw;
	e1000_phy_get_info(hw, &adapter->phy_info);
}

/**
 * e1000_82547_tx_fifo_stall - Timer Call-back
 * @data: pointer to adapter cast into an unsigned long
 **/

static void e1000_82547_tx_fifo_stall(unsigned long data)
{
	struct e1000_adapter *adapter = (struct e1000_adapter *)data;
	struct e1000_hw *hw = &adapter->hw;
	struct net_device *netdev = adapter->netdev;
	u32 tctl;

	if (atomic_read(&adapter->tx_fifo_stall)) {
		if ((er32(TDT) == er32(TDH)) &&
		   (er32(TDFT) == er32(TDFH)) &&
		   (er32(TDFTS) == er32(TDFHS))) {
			tctl = er32(TCTL);
			ew32(TCTL, tctl & ~E1000_TCTL_EN);
			ew32(TDFT, adapter->tx_head_addr);
			ew32(TDFH, adapter->tx_head_addr);
			ew32(TDFTS, adapter->tx_head_addr);
			ew32(TDFHS, adapter->tx_head_addr);
			ew32(TCTL, tctl);
			E1000_WRITE_FLUSH();

			adapter->tx_fifo_head = 0;
			atomic_set(&adapter->tx_fifo_stall, 0);
			netif_wake_queue(netdev);
		} else if (!test_bit(__E1000_DOWN, &adapter->flags)) {
			mod_timer(&adapter->tx_fifo_stall_timer, jiffies + 1);
		}
	}
}

static bool e1000_has_link(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	bool link_active = false;

	/* get_link_status is set on LSC (link status) interrupt or
	 * rx sequence error interrupt.  get_link_status will stay
	 * false until the e1000_check_for_link establishes link
	 * for copper adapters ONLY
	 */
	switch (hw->media_type) {
	case e1000_media_type_copper:
		if (hw->get_link_status) {
			e1000_check_for_link(hw);
			link_active = !hw->get_link_status;
		} else {
			link_active = true;
		}
		break;
	case e1000_media_type_fiber:
		e1000_check_for_link(hw);
		link_active = !!(er32(STATUS) & E1000_STATUS_LU);
		break;
	case e1000_media_type_internal_serdes:
		e1000_check_for_link(hw);
		link_active = hw->serdes_has_link;
		break;
	default:
		break;
	}

	return link_active;
}

/**
 * e1000_watchdog - Timer Call-back
 * @data: pointer to adapter cast into an unsigned long
 **/
static void e1000_watchdog(unsigned long data)
{
	struct e1000_adapter *adapter = (struct e1000_adapter *)data;
	struct e1000_hw *hw = &adapter->hw;
	struct net_device *netdev = adapter->netdev;
	struct e1000_tx_ring *txdr = adapter->tx_ring;
	u32 link, tctl;

	link = e1000_has_link(adapter);
	if ((netif_carrier_ok(netdev)) && link)
		goto link_up;

	if (link) {
		if (!netif_carrier_ok(netdev)) {
			u32 ctrl;
			bool txb2b = true;
			/* update snapshot of PHY registers on LSC */
			e1000_get_speed_and_duplex(hw,
			                           &adapter->link_speed,
			                           &adapter->link_duplex);

			ctrl = er32(CTRL);
			printk(KERN_INFO "e1000: %s NIC Link is Up %d Mbps %s, "
			       "Flow Control: %s\n",
			       netdev->name,
			       adapter->link_speed,
			       adapter->link_duplex == FULL_DUPLEX ?
			        "Full Duplex" : "Half Duplex",
			        ((ctrl & E1000_CTRL_TFCE) && (ctrl &
			        E1000_CTRL_RFCE)) ? "RX/TX" : ((ctrl &
			        E1000_CTRL_RFCE) ? "RX" : ((ctrl &
			        E1000_CTRL_TFCE) ? "TX" : "None" )));

			/* tweak tx_queue_len according to speed/duplex
			 * and adjust the timeout factor */
			netdev->tx_queue_len = adapter->tx_queue_len;
			adapter->tx_timeout_factor = 1;
			switch (adapter->link_speed) {
			case SPEED_10:
				txb2b = false;
				netdev->tx_queue_len = 10;
				adapter->tx_timeout_factor = 16;
				break;
			case SPEED_100:
				txb2b = false;
				netdev->tx_queue_len = 100;
				/* maybe add some timeout factor ? */
				break;
			}

			/* enable transmits in the hardware */
			tctl = er32(TCTL);
			tctl |= E1000_TCTL_EN;
			ew32(TCTL, tctl);

			netif_carrier_on(netdev);
			if (!test_bit(__E1000_DOWN, &adapter->flags))
				mod_timer(&adapter->phy_info_timer,
				          round_jiffies(jiffies + 2 * HZ));
			adapter->smartspeed = 0;
		}
	} else {
		if (netif_carrier_ok(netdev)) {
			adapter->link_speed = 0;
			adapter->link_duplex = 0;
			printk(KERN_INFO "e1000: %s NIC Link is Down\n",
			       netdev->name);
			netif_carrier_off(netdev);

			if (!test_bit(__E1000_DOWN, &adapter->flags))
				mod_timer(&adapter->phy_info_timer,
				          round_jiffies(jiffies + 2 * HZ));
		}

		e1000_smartspeed(adapter);
	}

link_up:
	e1000_update_stats(adapter);

	hw->tx_packet_delta = adapter->stats.tpt - adapter->tpt_old;
	adapter->tpt_old = adapter->stats.tpt;
	hw->collision_delta = adapter->stats.colc - adapter->colc_old;
	adapter->colc_old = adapter->stats.colc;

	adapter->gorcl = adapter->stats.gorcl - adapter->gorcl_old;
	adapter->gorcl_old = adapter->stats.gorcl;
	adapter->gotcl = adapter->stats.gotcl - adapter->gotcl_old;
	adapter->gotcl_old = adapter->stats.gotcl;

	e1000_update_adaptive(hw);

	if (!netif_carrier_ok(netdev)) {
		if (E1000_DESC_UNUSED(txdr) + 1 < txdr->count) {
			/* We've lost link, so the controller stops DMA,
			 * but we've got queued Tx work that's never going
			 * to get done, so reset controller to flush Tx.
			 * (Do the reset outside of interrupt context). */
			adapter->tx_timeout_count++;
			schedule_work(&adapter->reset_task);
			/* return immediately since reset is imminent */
			return;
		}
	}

	/* Cause software interrupt to ensure rx ring is cleaned */
	ew32(ICS, E1000_ICS_RXDMT0);

	/* Force detection of hung controller every watchdog period */
	adapter->detect_tx_hung = true;

	/* Reset the timer */
	if (!test_bit(__E1000_DOWN, &adapter->flags))
		mod_timer(&adapter->watchdog_timer,
		          round_jiffies(jiffies + 2 * HZ));
}

enum latency_range {
	lowest_latency = 0,
	low_latency = 1,
	bulk_latency = 2,
	latency_invalid = 255
};

/**
 * e1000_update_itr - update the dynamic ITR value based on statistics
 * @adapter: pointer to adapter
 * @itr_setting: current adapter->itr
 * @packets: the number of packets during this measurement interval
 * @bytes: the number of bytes during this measurement interval
 *
 *      Stores a new ITR value based on packets and byte
 *      counts during the last interrupt.  The advantage of per interrupt
 *      computation is faster updates and more accurate ITR for the current
 *      traffic pattern.  Constants in this function were computed
 *      based on theoretical maximum wire speed and thresholds were set based
 *      on testing data as well as attempting to minimize response time
 *      while increasing bulk throughput.
 *      this functionality is controlled by the InterruptThrottleRate module
 *      parameter (see e1000_param.c)
 **/
static unsigned int e1000_update_itr(struct e1000_adapter *adapter,
				     u16 itr_setting, int packets, int bytes)
{
	unsigned int retval = itr_setting;
	struct e1000_hw *hw = &adapter->hw;

	if (unlikely(hw->mac_type < e1000_82540))
		goto update_itr_done;

	if (packets == 0)
		goto update_itr_done;

	switch (itr_setting) {
	case lowest_latency:
		/* jumbo frames get bulk treatment*/
		if (bytes/packets > 8000)
			retval = bulk_latency;
		else if ((packets < 5) && (bytes > 512))
			retval = low_latency;
		break;
	case low_latency:  /* 50 usec aka 20000 ints/s */
		if (bytes > 10000) {
			/* jumbo frames need bulk latency setting */
			if (bytes/packets > 8000)
				retval = bulk_latency;
			else if ((packets < 10) || ((bytes/packets) > 1200))
				retval = bulk_latency;
			else if ((packets > 35))
				retval = lowest_latency;
		} else if (bytes/packets > 2000)
			retval = bulk_latency;
		else if (packets <= 2 && bytes < 512)
			retval = lowest_latency;
		break;
	case bulk_latency: /* 250 usec aka 4000 ints/s */
		if (bytes > 25000) {
			if (packets > 35)
				retval = low_latency;
		} else if (bytes < 6000) {
			retval = low_latency;
		}
		break;
	}

update_itr_done:
	return retval;
}

static void e1000_set_itr(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	u16 current_itr;
	u32 new_itr = adapter->itr;

	if (unlikely(hw->mac_type < e1000_82540))
		return;

	/* for non-gigabit speeds, just fix the interrupt rate at 4000 */
	if (unlikely(adapter->link_speed != SPEED_1000)) {
		current_itr = 0;
		new_itr = 4000;
		goto set_itr_now;
	}

	adapter->tx_itr = e1000_update_itr(adapter,
	                            adapter->tx_itr,
	                            adapter->total_tx_packets,
	                            adapter->total_tx_bytes);
	/* conservative mode (itr 3) eliminates the lowest_latency setting */
	if (adapter->itr_setting == 3 && adapter->tx_itr == lowest_latency)
		adapter->tx_itr = low_latency;

	adapter->rx_itr = e1000_update_itr(adapter,
	                            adapter->rx_itr,
	                            adapter->total_rx_packets,
	                            adapter->total_rx_bytes);
	/* conservative mode (itr 3) eliminates the lowest_latency setting */
	if (adapter->itr_setting == 3 && adapter->rx_itr == lowest_latency)
		adapter->rx_itr = low_latency;

	current_itr = max(adapter->rx_itr, adapter->tx_itr);

	switch (current_itr) {
	/* counts and packets in update_itr are dependent on these numbers */
	case lowest_latency:
		new_itr = 70000;
		break;
	case low_latency:
		new_itr = 20000; /* aka hwitr = ~200 */
		break;
	case bulk_latency:
		new_itr = 4000;
		break;
	default:
		break;
	}

set_itr_now:
	if (new_itr != adapter->itr) {
		/* this attempts to bias the interrupt rate towards Bulk
		 * by adding intermediate steps when interrupt rate is
		 * increasing */
		new_itr = new_itr > adapter->itr ?
		             min(adapter->itr + (new_itr >> 2), new_itr) :
		             new_itr;
		adapter->itr = new_itr;
		ew32(ITR, 1000000000 / (new_itr * 256));
	}

	return;
}

#define E1000_TX_FLAGS_CSUM		0x00000001
#define E1000_TX_FLAGS_VLAN		0x00000002
#define E1000_TX_FLAGS_TSO		0x00000004
#define E1000_TX_FLAGS_IPV4		0x00000008
#define E1000_TX_FLAGS_VLAN_MASK	0xffff0000
#define E1000_TX_FLAGS_VLAN_SHIFT	16

static int e1000_tso(struct e1000_adapter *adapter,
		     struct e1000_tx_ring *tx_ring, struct sk_buff *skb)
{
	struct e1000_context_desc *context_desc;
	struct e1000_buffer *buffer_info;
	unsigned int i;
	u32 cmd_length = 0;
	u16 ipcse = 0, tucse, mss;
	u8 ipcss, ipcso, tucss, tucso, hdr_len;
	int err;

	if (skb_is_gso(skb)) {
		if (skb_header_cloned(skb)) {
			err = pskb_expand_head(skb, 0, 0, GFP_ATOMIC);
			if (err)
				return err;
		}

		hdr_len = skb_transport_offset(skb) + tcp_hdrlen(skb);
		mss = skb_shinfo(skb)->gso_size;
		if (skb->protocol == htons(ETH_P_IP)) {
			struct iphdr *iph = ip_hdr(skb);
			iph->tot_len = 0;
			iph->check = 0;
			tcp_hdr(skb)->check = ~csum_tcpudp_magic(iph->saddr,
								 iph->daddr, 0,
								 IPPROTO_TCP,
								 0);
			cmd_length = E1000_TXD_CMD_IP;
			ipcse = skb_transport_offset(skb) - 1;
		} else if (skb->protocol == htons(ETH_P_IPV6)) {
			ipv6_hdr(skb)->payload_len = 0;
			tcp_hdr(skb)->check =
				~csum_ipv6_magic(&ipv6_hdr(skb)->saddr,
						 &ipv6_hdr(skb)->daddr,
						 0, IPPROTO_TCP, 0);
			ipcse = 0;
		}
		ipcss = skb_network_offset(skb);
		ipcso = (void *)&(ip_hdr(skb)->check) - (void *)skb->data;
		tucss = skb_transport_offset(skb);
		tucso = (void *)&(tcp_hdr(skb)->check) - (void *)skb->data;
		tucse = 0;

		cmd_length |= (E1000_TXD_CMD_DEXT | E1000_TXD_CMD_TSE |
			       E1000_TXD_CMD_TCP | (skb->len - (hdr_len)));

		i = tx_ring->next_to_use;
		context_desc = E1000_CONTEXT_DESC(*tx_ring, i);
		buffer_info = &tx_ring->buffer_info[i];

		context_desc->lower_setup.ip_fields.ipcss  = ipcss;
		context_desc->lower_setup.ip_fields.ipcso  = ipcso;
		context_desc->lower_setup.ip_fields.ipcse  = cpu_to_le16(ipcse);
		context_desc->upper_setup.tcp_fields.tucss = tucss;
		context_desc->upper_setup.tcp_fields.tucso = tucso;
		context_desc->upper_setup.tcp_fields.tucse = cpu_to_le16(tucse);
		context_desc->tcp_seg_setup.fields.mss     = cpu_to_le16(mss);
		context_desc->tcp_seg_setup.fields.hdr_len = hdr_len;
		context_desc->cmd_and_length = cpu_to_le32(cmd_length);

		buffer_info->time_stamp = jiffies;
		buffer_info->next_to_watch = i;

		if (++i == tx_ring->count) i = 0;
		tx_ring->next_to_use = i;

		return true;
	}
	return false;
}

static bool e1000_tx_csum(struct e1000_adapter *adapter,
			  struct e1000_tx_ring *tx_ring, struct sk_buff *skb)
{
	struct e1000_context_desc *context_desc;
	struct e1000_buffer *buffer_info;
	unsigned int i;
	u8 css;
	u32 cmd_len = E1000_TXD_CMD_DEXT;

	if (skb->ip_summed != CHECKSUM_PARTIAL)
		return false;

	switch (skb->protocol) {
	case cpu_to_be16(ETH_P_IP):
		if (ip_hdr(skb)->protocol == IPPROTO_TCP)
			cmd_len |= E1000_TXD_CMD_TCP;
		break;
	case cpu_to_be16(ETH_P_IPV6):
		/* XXX not handling all IPV6 headers */
		if (ipv6_hdr(skb)->nexthdr == IPPROTO_TCP)
			cmd_len |= E1000_TXD_CMD_TCP;
		break;
	default:
		if (unlikely(net_ratelimit()))
			DPRINTK(DRV, WARNING,
			        "checksum_partial proto=%x!\n", skb->protocol);
		break;
	}

	css = skb_transport_offset(skb);

	i = tx_ring->next_to_use;
	buffer_info = &tx_ring->buffer_info[i];
	context_desc = E1000_CONTEXT_DESC(*tx_ring, i);

	context_desc->lower_setup.ip_config = 0;
	context_desc->upper_setup.tcp_fields.tucss = css;
	context_desc->upper_setup.tcp_fields.tucso =
		css + skb->csum_offset;
	context_desc->upper_setup.tcp_fields.tucse = 0;
	context_desc->tcp_seg_setup.data = 0;
	context_desc->cmd_and_length = cpu_to_le32(cmd_len);

	buffer_info->time_stamp = jiffies;
	buffer_info->next_to_watch = i;

	if (unlikely(++i == tx_ring->count)) i = 0;
	tx_ring->next_to_use = i;

	return true;
}

#define E1000_MAX_TXD_PWR	12
#define E1000_MAX_DATA_PER_TXD	(1<<E1000_MAX_TXD_PWR)

static int e1000_tx_map(struct e1000_adapter *adapter,
			struct e1000_tx_ring *tx_ring,
			struct sk_buff *skb, unsigned int first,
			unsigned int max_per_txd, unsigned int nr_frags,
			unsigned int mss)
{
	struct e1000_hw *hw = &adapter->hw;
	struct e1000_buffer *buffer_info;
	unsigned int len = skb_headlen(skb);
	unsigned int offset, size, count = 0, i;
	unsigned int f;
	dma_addr_t *map;

	i = tx_ring->next_to_use;

	if (skb_dma_map(&adapter->pdev->dev, skb, DMA_TO_DEVICE)) {
		dev_err(&adapter->pdev->dev, "TX DMA map failed\n");
		return 0;
	}

	map = skb_shinfo(skb)->dma_maps;
	offset = 0;

	while (len) {
		buffer_info = &tx_ring->buffer_info[i];
		size = min(len, max_per_txd);
		/* Workaround for Controller erratum --
		 * descriptor for non-tso packet in a linear SKB that follows a
		 * tso gets written back prematurely before the data is fully
		 * DMA'd to the controller */
		if (!skb->data_len && tx_ring->last_tx_tso &&
		    !skb_is_gso(skb)) {
			tx_ring->last_tx_tso = 0;
			size -= 4;
		}

		/* Workaround for premature desc write-backs
		 * in TSO mode.  Append 4-byte sentinel desc */
		if (unlikely(mss && !nr_frags && size == len && size > 8))
			size -= 4;
		/* work-around for errata 10 and it applies
		 * to all controllers in PCI-X mode
		 * The fix is to make sure that the first descriptor of a
		 * packet is smaller than 2048 - 16 - 16 (or 2016) bytes
		 */
		if (unlikely((hw->bus_type == e1000_bus_type_pcix) &&
		                (size > 2015) && count == 0))
		        size = 2015;

		/* Workaround for potential 82544 hang in PCI-X.  Avoid
		 * terminating buffers within evenly-aligned dwords. */
		if (unlikely(adapter->pcix_82544 &&
		   !((unsigned long)(skb->data + offset + size - 1) & 4) &&
		   size > 4))
			size -= 4;

		buffer_info->length = size;
		/* set time_stamp *before* dma to help avoid a possible race */
		buffer_info->time_stamp = jiffies;
		buffer_info->dma = skb_shinfo(skb)->dma_head + offset;
		buffer_info->next_to_watch = i;

		len -= size;
		offset += size;
		count++;
		if (len) {
			i++;
			if (unlikely(i == tx_ring->count))
				i = 0;
		}
	}

	for (f = 0; f < nr_frags; f++) {
		struct skb_frag_struct *frag;

		frag = &skb_shinfo(skb)->frags[f];
		len = frag->size;
		offset = 0;

		while (len) {
			i++;
			if (unlikely(i == tx_ring->count))
				i = 0;

			buffer_info = &tx_ring->buffer_info[i];
			size = min(len, max_per_txd);
			/* Workaround for premature desc write-backs
			 * in TSO mode.  Append 4-byte sentinel desc */
			if (unlikely(mss && f == (nr_frags-1) && size == len && size > 8))
				size -= 4;
			/* Workaround for potential 82544 hang in PCI-X.
			 * Avoid terminating buffers within evenly-aligned
			 * dwords. */
			if (unlikely(adapter->pcix_82544 &&
			    !((unsigned long)(page_to_phys(frag->page) + offset
			                      + size - 1) & 4) &&
			    size > 4))
				size -= 4;

			buffer_info->length = size;
			buffer_info->time_stamp = jiffies;
			buffer_info->dma = map[f] + offset;
			buffer_info->next_to_watch = i;

			len -= size;
			offset += size;
			count++;
		}
	}

	tx_ring->buffer_info[i].skb = skb;
	tx_ring->buffer_info[first].next_to_watch = i;

	return count;
}

static void e1000_tx_queue(struct e1000_adapter *adapter,
			   struct e1000_tx_ring *tx_ring, int tx_flags,
			   int count)
{
	struct e1000_hw *hw = &adapter->hw;
	struct e1000_tx_desc *tx_desc = NULL;
	struct e1000_buffer *buffer_info;
	u32 txd_upper = 0, txd_lower = E1000_TXD_CMD_IFCS;
	unsigned int i;

	if (likely(tx_flags & E1000_TX_FLAGS_TSO)) {
		txd_lower |= E1000_TXD_CMD_DEXT | E1000_TXD_DTYP_D |
		             E1000_TXD_CMD_TSE;
		txd_upper |= E1000_TXD_POPTS_TXSM << 8;

		if (likely(tx_flags & E1000_TX_FLAGS_IPV4))
			txd_upper |= E1000_TXD_POPTS_IXSM << 8;
	}

	if (likely(tx_flags & E1000_TX_FLAGS_CSUM)) {
		txd_lower |= E1000_TXD_CMD_DEXT | E1000_TXD_DTYP_D;
		txd_upper |= E1000_TXD_POPTS_TXSM << 8;
	}

	if (unlikely(tx_flags & E1000_TX_FLAGS_VLAN)) {
		txd_lower |= E1000_TXD_CMD_VLE;
		txd_upper |= (tx_flags & E1000_TX_FLAGS_VLAN_MASK);
	}

	i = tx_ring->next_to_use;

	while (count--) {
		buffer_info = &tx_ring->buffer_info[i];
		tx_desc = E1000_TX_DESC(*tx_ring, i);
		tx_desc->buffer_addr = cpu_to_le64(buffer_info->dma);
		tx_desc->lower.data =
			cpu_to_le32(txd_lower | buffer_info->length);
		tx_desc->upper.data = cpu_to_le32(txd_upper);
		if (unlikely(++i == tx_ring->count)) i = 0;
	}

	tx_desc->lower.data |= cpu_to_le32(adapter->txd_cmd);

	/* Force memory writes to complete before letting h/w
	 * know there are new descriptors to fetch.  (Only
	 * applicable for weak-ordered memory model archs,
	 * such as IA-64). */
	wmb();

	tx_ring->next_to_use = i;
	writel(i, hw->hw_addr + tx_ring->tdt);
	/* we need this if more than one processor can write to our tail
	 * at a time, it syncronizes IO on IA64/Altix systems */
	mmiowb();
}

/**
 * 82547 workaround to avoid controller hang in half-duplex environment.
 * The workaround is to avoid queuing a large packet that would span
 * the internal Tx FIFO ring boundary by notifying the stack to resend
 * the packet at a later time.  This gives the Tx FIFO an opportunity to
 * flush all packets.  When that occurs, we reset the Tx FIFO pointers
 * to the beginning of the Tx FIFO.
 **/

#define E1000_FIFO_HDR			0x10
#define E1000_82547_PAD_LEN		0x3E0

static int e1000_82547_fifo_workaround(struct e1000_adapter *adapter,
				       struct sk_buff *skb)
{
	u32 fifo_space = adapter->tx_fifo_size - adapter->tx_fifo_head;
	u32 skb_fifo_len = skb->len + E1000_FIFO_HDR;

	skb_fifo_len = ALIGN(skb_fifo_len, E1000_FIFO_HDR);

	if (adapter->link_duplex != HALF_DUPLEX)
		goto no_fifo_stall_required;

	if (atomic_read(&adapter->tx_fifo_stall))
		return 1;

	if (skb_fifo_len >= (E1000_82547_PAD_LEN + fifo_space)) {
		atomic_set(&adapter->tx_fifo_stall, 1);
		return 1;
	}

no_fifo_stall_required:
	adapter->tx_fifo_head += skb_fifo_len;
	if (adapter->tx_fifo_head >= adapter->tx_fifo_size)
		adapter->tx_fifo_head -= adapter->tx_fifo_size;
	return 0;
}

static int __e1000_maybe_stop_tx(struct net_device *netdev, int size)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_tx_ring *tx_ring = adapter->tx_ring;

	netif_stop_queue(netdev);
	/* Herbert's original patch had:
	 *  smp_mb__after_netif_stop_queue();
	 * but since that doesn't exist yet, just open code it. */
	smp_mb();

	/* We need to check again in a case another CPU has just
	 * made room available. */
	if (likely(E1000_DESC_UNUSED(tx_ring) < size))
		return -EBUSY;

	/* A reprieve! */
	netif_start_queue(netdev);
	++adapter->restart_queue;
	return 0;
}

static int e1000_maybe_stop_tx(struct net_device *netdev,
                               struct e1000_tx_ring *tx_ring, int size)
{
	if (likely(E1000_DESC_UNUSED(tx_ring) >= size))
		return 0;
	return __e1000_maybe_stop_tx(netdev, size);
}

#define TXD_USE_COUNT(S, X) (((S) >> (X)) + 1 )
static netdev_tx_t e1000_xmit_frame(struct sk_buff *skb,
				    struct net_device *netdev)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	struct e1000_tx_ring *tx_ring;
	unsigned int first, max_per_txd = E1000_MAX_DATA_PER_TXD;
	unsigned int max_txd_pwr = E1000_MAX_TXD_PWR;
	unsigned int tx_flags = 0;
	unsigned int len = skb->len - skb->data_len;
	unsigned int nr_frags;
	unsigned int mss;
	int count = 0;
	int tso;
	unsigned int f;

	/* This goes back to the question of how to logically map a tx queue
	 * to a flow.  Right now, performance is impacted slightly negatively
	 * if using multiple tx queues.  If the stack breaks away from a
	 * single qdisc implementation, we can look at this again. */
	tx_ring = adapter->tx_ring;

	if (unlikely(skb->len <= 0)) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	mss = skb_shinfo(skb)->gso_size;
	/* The controller does a simple calculation to
	 * make sure there is enough room in the FIFO before
	 * initiating the DMA for each buffer.  The calc is:
	 * 4 = ceil(buffer len/mss).  To make sure we don't
	 * overrun the FIFO, adjust the max buffer len if mss
	 * drops. */
	if (mss) {
		u8 hdr_len;
		max_per_txd = min(mss << 2, max_per_txd);
		max_txd_pwr = fls(max_per_txd) - 1;

		hdr_len = skb_transport_offset(skb) + tcp_hdrlen(skb);
		if (skb->data_len && hdr_len == len) {
			switch (hw->mac_type) {
				unsigned int pull_size;
			case e1000_82544:
				/* Make sure we have room to chop off 4 bytes,
				 * and that the end alignment will work out to
				 * this hardware's requirements
				 * NOTE: this is a TSO only workaround
				 * if end byte alignment not correct move us
				 * into the next dword */
				if ((unsigned long)(skb_tail_pointer(skb) - 1) & 4)
					break;
				/* fall through */
				pull_size = min((unsigned int)4, skb->data_len);
				if (!__pskb_pull_tail(skb, pull_size)) {
					DPRINTK(DRV, ERR,
						"__pskb_pull_tail failed.\n");
					dev_kfree_skb_any(skb);
					return NETDEV_TX_OK;
				}
				len = skb->len - skb->data_len;
				break;
			default:
				/* do nothing */
				break;
			}
		}
	}

	/* reserve a descriptor for the offload context */
	if ((mss) || (skb->ip_summed == CHECKSUM_PARTIAL))
		count++;
	count++;

	/* Controller Erratum workaround */
	if (!skb->data_len && tx_ring->last_tx_tso && !skb_is_gso(skb))
		count++;

	count += TXD_USE_COUNT(len, max_txd_pwr);

	if (adapter->pcix_82544)
		count++;

	/* work-around for errata 10 and it applies to all controllers
	 * in PCI-X mode, so add one more descriptor to the count
	 */
	if (unlikely((hw->bus_type == e1000_bus_type_pcix) &&
			(len > 2015)))
		count++;

	nr_frags = skb_shinfo(skb)->nr_frags;
	for (f = 0; f < nr_frags; f++)
		count += TXD_USE_COUNT(skb_shinfo(skb)->frags[f].size,
				       max_txd_pwr);
	if (adapter->pcix_82544)
		count += nr_frags;

	/* need: count + 2 desc gap to keep tail from touching
	 * head, otherwise try next time */
	if (unlikely(e1000_maybe_stop_tx(netdev, tx_ring, count + 2)))
		return NETDEV_TX_BUSY;

	if (unlikely(hw->mac_type == e1000_82547)) {
		if (unlikely(e1000_82547_fifo_workaround(adapter, skb))) {
			netif_stop_queue(netdev);
			if (!test_bit(__E1000_DOWN, &adapter->flags))
				mod_timer(&adapter->tx_fifo_stall_timer,
				          jiffies + 1);
			return NETDEV_TX_BUSY;
		}
	}

	if (unlikely(adapter->vlgrp && vlan_tx_tag_present(skb))) {
		tx_flags |= E1000_TX_FLAGS_VLAN;
		tx_flags |= (vlan_tx_tag_get(skb) << E1000_TX_FLAGS_VLAN_SHIFT);
	}

	first = tx_ring->next_to_use;

	tso = e1000_tso(adapter, tx_ring, skb);
	if (tso < 0) {
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	if (likely(tso)) {
		if (likely(hw->mac_type != e1000_82544))
			tx_ring->last_tx_tso = 1;
		tx_flags |= E1000_TX_FLAGS_TSO;
	} else if (likely(e1000_tx_csum(adapter, tx_ring, skb)))
		tx_flags |= E1000_TX_FLAGS_CSUM;

	if (likely(skb->protocol == htons(ETH_P_IP)))
		tx_flags |= E1000_TX_FLAGS_IPV4;

	count = e1000_tx_map(adapter, tx_ring, skb, first, max_per_txd,
	                     nr_frags, mss);

	if (count) {
		e1000_tx_queue(adapter, tx_ring, tx_flags, count);
		/* Make sure there is space in the ring for the next send. */
		e1000_maybe_stop_tx(netdev, tx_ring, MAX_SKB_FRAGS + 2);

	} else {
		dev_kfree_skb_any(skb);
		tx_ring->buffer_info[first].time_stamp = 0;
		tx_ring->next_to_use = first;
	}

	return NETDEV_TX_OK;
}

/**
 * e1000_tx_timeout - Respond to a Tx Hang
 * @netdev: network interface device structure
 **/

static void e1000_tx_timeout(struct net_device *netdev)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);

	/* Do the reset outside of interrupt context */
	adapter->tx_timeout_count++;
	schedule_work(&adapter->reset_task);
}

static void e1000_reset_task(struct work_struct *work)
{
	struct e1000_adapter *adapter =
		container_of(work, struct e1000_adapter, reset_task);

	e1000_reinit_locked(adapter);
}

/**
 * e1000_get_stats - Get System Network Statistics
 * @netdev: network interface device structure
 *
 * Returns the address of the device statistics structure.
 * The statistics are actually updated from the timer callback.
 **/

static struct net_device_stats *e1000_get_stats(struct net_device *netdev)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);

	/* only return the current stats */
	return &adapter->net_stats;
}

/**
 * e1000_change_mtu - Change the Maximum Transfer Unit
 * @netdev: network interface device structure
 * @new_mtu: new value for maximum frame size
 *
 * Returns 0 on success, negative on failure
 **/

static int e1000_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	int max_frame = new_mtu + ENET_HEADER_SIZE + ETHERNET_FCS_SIZE;

	if ((max_frame < MINIMUM_ETHERNET_FRAME_SIZE) ||
	    (max_frame > MAX_JUMBO_FRAME_SIZE)) {
		DPRINTK(PROBE, ERR, "Invalid MTU setting\n");
		return -EINVAL;
	}

	/* Adapter-specific max frame size limits. */
	switch (hw->mac_type) {
	case e1000_undefined ... e1000_82542_rev2_1:
		if (max_frame > (ETH_FRAME_LEN + ETH_FCS_LEN)) {
			DPRINTK(PROBE, ERR, "Jumbo Frames not supported.\n");
			return -EINVAL;
		}
		break;
	default:
		/* Capable of supporting up to MAX_JUMBO_FRAME_SIZE limit. */
		break;
	}

	while (test_and_set_bit(__E1000_RESETTING, &adapter->flags))
		msleep(1);
	/* e1000_down has a dependency on max_frame_size */
	hw->max_frame_size = max_frame;
	if (netif_running(netdev))
		e1000_down(adapter);

	/* NOTE: netdev_alloc_skb reserves 16 bytes, and typically NET_IP_ALIGN
	 * means we reserve 2 more, this pushes us to allocate from the next
	 * larger slab size.
	 * i.e. RXBUFFER_2048 --> size-4096 slab
	 *  however with the new *_jumbo_rx* routines, jumbo receives will use
	 *  fragmented skbs */

	if (max_frame <= E1000_RXBUFFER_256)
		adapter->rx_buffer_len = E1000_RXBUFFER_256;
	else if (max_frame <= E1000_RXBUFFER_512)
		adapter->rx_buffer_len = E1000_RXBUFFER_512;
	else if (max_frame <= E1000_RXBUFFER_1024)
		adapter->rx_buffer_len = E1000_RXBUFFER_1024;
	else if (max_frame <= E1000_RXBUFFER_2048)
		adapter->rx_buffer_len = E1000_RXBUFFER_2048;
	else
#if (PAGE_SIZE >= E1000_RXBUFFER_16384)
		adapter->rx_buffer_len = E1000_RXBUFFER_16384;
#elif (PAGE_SIZE >= E1000_RXBUFFER_4096)
		adapter->rx_buffer_len = PAGE_SIZE;
#endif

	/* adjust allocation if LPE protects us, and we aren't using SBP */
	if (!hw->tbi_compatibility_on &&
	    ((max_frame == (ETH_FRAME_LEN + ETH_FCS_LEN)) ||
	     (max_frame == MAXIMUM_ETHERNET_VLAN_SIZE)))
		adapter->rx_buffer_len = MAXIMUM_ETHERNET_VLAN_SIZE;

	printk(KERN_INFO "e1000: %s changing MTU from %d to %d\n",
	       netdev->name, netdev->mtu, new_mtu);
	netdev->mtu = new_mtu;

	if (netif_running(netdev))
		e1000_up(adapter);
	else
		e1000_reset(adapter);

	clear_bit(__E1000_RESETTING, &adapter->flags);

	return 0;
}

/**
 * e1000_update_stats - Update the board statistics counters
 * @adapter: board private structure
 **/

void e1000_update_stats(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	struct pci_dev *pdev = adapter->pdev;
	unsigned long flags;
	u16 phy_tmp;

#define PHY_IDLE_ERROR_COUNT_MASK 0x00FF

	/*
	 * Prevent stats update while adapter is being reset, or if the pci
	 * connection is down.
	 */
	if (adapter->link_speed == 0)
		return;
	if (pci_channel_offline(pdev))
		return;

	spin_lock_irqsave(&adapter->stats_lock, flags);

	/* these counters are modified from e1000_tbi_adjust_stats,
	 * called from the interrupt context, so they must only
	 * be written while holding adapter->stats_lock
	 */

	adapter->stats.crcerrs += er32(CRCERRS);
	adapter->stats.gprc += er32(GPRC);
	adapter->stats.gorcl += er32(GORCL);
	adapter->stats.gorch += er32(GORCH);
	adapter->stats.bprc += er32(BPRC);
	adapter->stats.mprc += er32(MPRC);
	adapter->stats.roc += er32(ROC);

	adapter->stats.prc64 += er32(PRC64);
	adapter->stats.prc127 += er32(PRC127);
	adapter->stats.prc255 += er32(PRC255);
	adapter->stats.prc511 += er32(PRC511);
	adapter->stats.prc1023 += er32(PRC1023);
	adapter->stats.prc1522 += er32(PRC1522);

	adapter->stats.symerrs += er32(SYMERRS);
	adapter->stats.mpc += er32(MPC);
	adapter->stats.scc += er32(SCC);
	adapter->stats.ecol += er32(ECOL);
	adapter->stats.mcc += er32(MCC);
	adapter->stats.latecol += er32(LATECOL);
	adapter->stats.dc += er32(DC);
	adapter->stats.sec += er32(SEC);
	adapter->stats.rlec += er32(RLEC);
	adapter->stats.xonrxc += er32(XONRXC);
	adapter->stats.xontxc += er32(XONTXC);
	adapter->stats.xoffrxc += er32(XOFFRXC);
	adapter->stats.xofftxc += er32(XOFFTXC);
	adapter->stats.fcruc += er32(FCRUC);
	adapter->stats.gptc += er32(GPTC);
	adapter->stats.gotcl += er32(GOTCL);
	adapter->stats.gotch += er32(GOTCH);
	adapter->stats.rnbc += er32(RNBC);
	adapter->stats.ruc += er32(RUC);
	adapter->stats.rfc += er32(RFC);
	adapter->stats.rjc += er32(RJC);
	adapter->stats.torl += er32(TORL);
	adapter->stats.torh += er32(TORH);
	adapter->stats.totl += er32(TOTL);
	adapter->stats.toth += er32(TOTH);
	adapter->stats.tpr += er32(TPR);

	adapter->stats.ptc64 += er32(PTC64);
	adapter->stats.ptc127 += er32(PTC127);
	adapter->stats.ptc255 += er32(PTC255);
	adapter->stats.ptc511 += er32(PTC511);
	adapter->stats.ptc1023 += er32(PTC1023);
	adapter->stats.ptc1522 += er32(PTC1522);

	adapter->stats.mptc += er32(MPTC);
	adapter->stats.bptc += er32(BPTC);

	/* used for adaptive IFS */

	hw->tx_packet_delta = er32(TPT);
	adapter->stats.tpt += hw->tx_packet_delta;
	hw->collision_delta = er32(COLC);
	adapter->stats.colc += hw->collision_delta;

	if (hw->mac_type >= e1000_82543) {
		adapter->stats.algnerrc += er32(ALGNERRC);
		adapter->stats.rxerrc += er32(RXERRC);
		adapter->stats.tncrs += er32(TNCRS);
		adapter->stats.cexterr += er32(CEXTERR);
		adapter->stats.tsctc += er32(TSCTC);
		adapter->stats.tsctfc += er32(TSCTFC);
	}

	/* Fill out the OS statistics structure */
	adapter->net_stats.multicast = adapter->stats.mprc;
	adapter->net_stats.collisions = adapter->stats.colc;

	/* Rx Errors */

	/* RLEC on some newer hardware can be incorrect so build
	* our own version based on RUC and ROC */
	adapter->net_stats.rx_errors = adapter->stats.rxerrc +
		adapter->stats.crcerrs + adapter->stats.algnerrc +
		adapter->stats.ruc + adapter->stats.roc +
		adapter->stats.cexterr;
	adapter->stats.rlerrc = adapter->stats.ruc + adapter->stats.roc;
	adapter->net_stats.rx_length_errors = adapter->stats.rlerrc;
	adapter->net_stats.rx_crc_errors = adapter->stats.crcerrs;
	adapter->net_stats.rx_frame_errors = adapter->stats.algnerrc;
	adapter->net_stats.rx_missed_errors = adapter->stats.mpc;

	/* Tx Errors */
	adapter->stats.txerrc = adapter->stats.ecol + adapter->stats.latecol;
	adapter->net_stats.tx_errors = adapter->stats.txerrc;
	adapter->net_stats.tx_aborted_errors = adapter->stats.ecol;
	adapter->net_stats.tx_window_errors = adapter->stats.latecol;
	adapter->net_stats.tx_carrier_errors = adapter->stats.tncrs;
	if (hw->bad_tx_carr_stats_fd &&
	    adapter->link_duplex == FULL_DUPLEX) {
		adapter->net_stats.tx_carrier_errors = 0;
		adapter->stats.tncrs = 0;
	}

	/* Tx Dropped needs to be maintained elsewhere */

	/* Phy Stats */
	if (hw->media_type == e1000_media_type_copper) {
		if ((adapter->link_speed == SPEED_1000) &&
		   (!e1000_read_phy_reg(hw, PHY_1000T_STATUS, &phy_tmp))) {
			phy_tmp &= PHY_IDLE_ERROR_COUNT_MASK;
			adapter->phy_stats.idle_errors += phy_tmp;
		}

		if ((hw->mac_type <= e1000_82546) &&
		   (hw->phy_type == e1000_phy_m88) &&
		   !e1000_read_phy_reg(hw, M88E1000_RX_ERR_CNTR, &phy_tmp))
			adapter->phy_stats.receive_errors += phy_tmp;
	}

	/* Management Stats */
	if (hw->has_smbus) {
		adapter->stats.mgptc += er32(MGTPTC);
		adapter->stats.mgprc += er32(MGTPRC);
		adapter->stats.mgpdc += er32(MGTPDC);
	}

	spin_unlock_irqrestore(&adapter->stats_lock, flags);
}

/**
 * e1000_intr - Interrupt Handler
 * @irq: interrupt number
 * @data: pointer to a network interface device structure
 **/

static irqreturn_t e1000_intr(int irq, void *data)
{
	struct net_device *netdev = data;
	struct e1000_adapter *adapter = netdev_priv(netdev);
	struct e1000_hw *hw = &adapter->hw;
	u32 icr = er32(ICR);

	if (unlikely((!icr) || test_bit(__E1000_DOWN, &adapter->flags)))
		return IRQ_NONE;  /* Not our interrupt */

	if (unlikely(icr & (E1000_ICR_RXSEQ | E1000_ICR_LSC))) {
		hw->get_link_status = 1;
		/* guard against interrupt when we're going down */
		if (!test_bit(__E1000_DOWN, &adapter->flags))
			mod_timer(&adapter->watchdog_timer, jiffies + 1);
	}

	/* disable interrupts, without the synchronize_irq bit */
	ew32(IMC, ~0);
	E1000_WRITE_FLUSH();

	if (likely(napi_schedule_prep(&adapter->napi))) {
		adapter->total_tx_bytes = 0;
		adapter->total_tx_packets = 0;
		adapter->total_rx_bytes = 0;
		adapter->total_rx_packets = 0;
		__napi_schedule(&adapter->napi);
	} else {
		/* this really should not happen! if it does it is basically a
		 * bug, but not a hard error, so enable ints and continue */
		if (!test_bit(__E1000_DOWN, &adapter->flags))
			e1000_irq_enable(adapter);
	}

	return IRQ_HANDLED;
}

/**
 * e1000_clean - NAPI Rx polling callback
 * @adapter: board private structure
 **/
static int e1000_clean(struct napi_struct *napi, int budget)
{
	struct e1000_adapter *adapter = container_of(napi, struct e1000_adapter, napi);
	int tx_clean_complete = 0, work_done = 0;

	tx_clean_complete = e1000_clean_tx_irq(adapter, &adapter->tx_ring[0]);

	adapter->clean_rx(adapter, &adapter->rx_ring[0], &work_done, budget);

	if (!tx_clean_complete)
		work_done = budget;

	/* If budget not fully consumed, exit the polling mode */
	if (work_done < budget) {
		if (likely(adapter->itr_setting & 3))
			e1000_set_itr(adapter);
		napi_complete(napi);
		if (!test_bit(__E1000_DOWN, &adapter->flags))
			e1000_irq_enable(adapter);
	}

	return work_done;
}

/**
 * e1000_clean_tx_irq - Reclaim resources after transmit completes
 * @adapter: board private structure
 **/
static bool e1000_clean_tx_irq(struct e1000_adapter *adapter,
			       struct e1000_tx_ring *tx_ring)
{
	struct e1000_hw *hw = &adapter->hw;
	struct net_device *netdev = adapter->netdev;
	struct e1000_tx_desc *tx_desc, *eop_desc;
	struct e1000_buffer *buffer_info;
	unsigned int i, eop;
	unsigned int count = 0;
	unsigned int total_tx_bytes=0, total_tx_packets=0;

	i = tx_ring->next_to_clean;
	eop = tx_ring->buffer_info[i].next_to_watch;
	eop_desc = E1000_TX_DESC(*tx_ring, eop);

	while ((eop_desc->upper.data & cpu_to_le32(E1000_TXD_STAT_DD)) &&
	       (count < tx_ring->count)) {
		bool cleaned = false;
		for ( ; !cleaned; count++) {
			tx_desc = E1000_TX_DESC(*tx_ring, i);
			buffer_info = &tx_ring->buffer_info[i];
			cleaned = (i == eop);

			if (cleaned) {
				struct sk_buff *skb = buffer_info->skb;
				unsigned int segs, bytecount;
				segs = skb_shinfo(skb)->gso_segs ?: 1;
				/* multiply data chunks by size of headers */
				bytecount = ((segs - 1) * skb_headlen(skb)) +
				            skb->len;
				total_tx_packets += segs;
				total_tx_bytes += bytecount;
			}
			e1000_unmap_and_free_tx_resource(adapter, buffer_info);
			tx_desc->upper.data = 0;

			if (unlikely(++i == tx_ring->count)) i = 0;
		}

		eop = tx_ring->buffer_info[i].next_to_watch;
		eop_desc = E1000_TX_DESC(*tx_ring, eop);
	}

	tx_ring->next_to_clean = i;

#define TX_WAKE_THRESHOLD 32
	if (unlikely(count && netif_carrier_ok(netdev) &&
		     E1000_DESC_UNUSED(tx_ring) >= TX_WAKE_THRESHOLD)) {
		/* Make sure that anybody stopping the queue after this
		 * sees the new next_to_clean.
		 */
		smp_mb();

		if (netif_queue_stopped(netdev) &&
		    !(test_bit(__E1000_DOWN, &adapter->flags))) {
			netif_wake_queue(netdev);
			++adapter->restart_queue;
		}
	}

	if (adapter->detect_tx_hung) {
		/* Detect a transmit hang in hardware, this serializes the
		 * check with the clearing of time_stamp and movement of i */
		adapter->detect_tx_hung = false;
		if (tx_ring->buffer_info[eop].time_stamp &&
		    time_after(jiffies, tx_ring->buffer_info[eop].time_stamp +
		               (adapter->tx_timeout_factor * HZ))
		    && !(er32(STATUS) & E1000_STATUS_TXOFF)) {

			/* detected Tx unit hang */
			DPRINTK(DRV, ERR, "Detected Tx Unit Hang\n"
					"  Tx Queue             <%lu>\n"
					"  TDH                  <%x>\n"
					"  TDT                  <%x>\n"
					"  next_to_use          <%x>\n"
					"  next_to_clean        <%x>\n"
					"buffer_info[next_to_clean]\n"
					"  time_stamp           <%lx>\n"
					"  next_to_watch        <%x>\n"
					"  jiffies              <%lx>\n"
					"  next_to_watch.status <%x>\n",
				(unsigned long)((tx_ring - adapter->tx_ring) /
					sizeof(struct e1000_tx_ring)),
				readl(hw->hw_addr + tx_ring->tdh),
				readl(hw->hw_addr + tx_ring->tdt),
				tx_ring->next_to_use,
				tx_ring->next_to_clean,
				tx_ring->buffer_info[eop].time_stamp,
				eop,
				jiffies,
				eop_desc->upper.fields.status);
			netif_stop_queue(netdev);
		}
	}
	adapter->total_tx_bytes += total_tx_bytes;
	adapter->total_tx_packets += total_tx_packets;
	adapter->net_stats.tx_bytes += total_tx_bytes;
	adapter->net_stats.tx_packets += total_tx_packets;
	return (count < tx_ring->count);
}

/**
 * e1000_rx_checksum - Receive Checksum Offload for 82543
 * @adapter:     board private structure
 * @status_err:  receive descriptor status and error fields
 * @csum:        receive descriptor csum field
 * @sk_buff:     socket buffer with received data
 **/

static void e1000_rx_checksum(struct e1000_adapter *adapter, u32 status_err,
			      u32 csum, struct sk_buff *skb)
{
	struct e1000_hw *hw = &adapter->hw;
	u16 status = (u16)status_err;
	u8 errors = (u8)(status_err >> 24);
	skb->ip_summed = CHECKSUM_NONE;

	/* 82543 or newer only */
	if (unlikely(hw->mac_type < e1000_82543)) return;
	/* Ignore Checksum bit is set */
	if (unlikely(status & E1000_RXD_STAT_IXSM)) return;
	/* TCP/UDP checksum error bit is set */
	if (unlikely(errors & E1000_RXD_ERR_TCPE)) {
		/* let the stack verify checksum errors */
		adapter->hw_csum_err++;
		return;
	}
	/* TCP/UDP Checksum has not been calculated */
	if (!(status & E1000_RXD_STAT_TCPCS))
		return;

	/* It must be a TCP or UDP packet with a valid checksum */
	if (likely(status & E1000_RXD_STAT_TCPCS)) {
		/* TCP checksum is good */
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	}
	adapter->hw_csum_good++;
}

/**
 * e1000_consume_page - helper function
 **/
static void e1000_consume_page(struct e1000_buffer *bi, struct sk_buff *skb,
 ******************************u16 length)
{
	bi->page = NULL;
	skb->len +=*******l PRO/10data_00 Linux driver
  Coptruesize) 1999 - 200}

/**
 * e1000_receive_skb - helper function to handle rx indicastrisgram@adapter: board private********re  undstatus: descriptor U Gene field as written by hardwa the GNvlanral Public LiFree
  verstrib2,sionpwareshedpubl(no le/be con.

  Th) the Ukb: pointerbuditions o*to ber modifyedut WItack
 */
censic voidm is free software(*******arrantyrby th * FITNES, u8icense,,
			******__le16undat**************************if (unlikely( FITNES->vlgrp && (ULAR P
& ErrantRXD_STAT_VP))) {
		ndat_hwaccelty of MERCHANTkb, s.

  You shouURPO*************************y th_to_cpu(ndat) & if not,n 2, ebutey thndat Sed a copy oPC_VLAN_MASK);
	} elseense,netifCULAR  along wit2110-his pro  unarrantclean_jumbo_rx_irq - Send y of MEd yrig uplin Snetworkhout e; legacyhe GN FITNESterms andthe distrisensey the rx_ring: el.c  ANncludhe GN"COP_done: amountensenapi ilinYcompleY; wibliccallel Mailingto_do: max < is f-devsts.sallowed fore.net>
  Ie100do
 icens
  Ireturn valu WARRANTY;s whether actualE. Ela
  ewas  Lis,497
x.nicsis no guarantee that everyth********0-devedvenx.nicimplboo01 Uis iinclude inet/is diNTABILITY o06 IFITNESS FOR A PARPOSnc.,
 ame[] = "e100ul, 
  e*"Intel( is fr - 20int *Corpo***** DrivCorporatio*****ame[] = "e100hw *hw = &  this prhwer
 0.h"
#iet_device ] = dev =
 _driveprON;
st_.

  Th[]pciDRV_ *p
U Geicthe st cCopy000_copyrig[] = "InoftwR) PRoftw,SIONxt_rxd006 IInte _VERSIUT
 tatiTable
_info00_pci_t
 * La;
	unsigned NU Geirq_flagsl 0s32999 - 2006s
 ogramM DRVi;
	_VENDinclud_cN.E. = 0;
	ecksuNTEL, d = falseI_DEVICE(PCI_VENtotalratibytes=0,y mustbl[]packets=0;

	i = "Intel(Rchaxtare F-dev;
	ON "n.";=ifth Floo_DESC(

/* Cop0 ial PgramLast enar exis f),
	NET_DEVICE([i]vicewhile (
/am is->ve ee softth Floor, ux.nDDcense,*******************;
		TICULAR Pvice
  moer";
#defi >=V_VER0_pc "7.			breakETHEC	INTINTEL_)++CE(0xve rece(004),
8,
	INTEL_100Dare;= NET_DEVICE(->00_100DE(0x1RNETevice_iboro DeviEVICE(C++i =EVICE(0	INT_VER_) 0_ET}
 *	EL_E100 ce_idVICE(1,
	INTEL_EEis fr0F),
	prefetch(T_DEVICE)
	INT must bl GNVICE(4INTEL_E1000_ETHERNET_	INT	ight******viceINTETEL_, RV_VER_s fr		ght[unmap_***
(99-2,INTEL_E1000DEVdma1015INTEL_E1000E1000_E Inc.,
  51 Frankl e10DMA_FROMvice_i	INTEETHERNET_DEVI1THERNET_
OSE.  Se 1999ERCHA Foun0x1011),
*******	INTEx100rrorsUT
 onlyfor
 kway, DD + EOPSoftware Fos<netT_DEVIre detaila	INTEL_E1000_ETHERNET_DEEOP) &, Inc.,
000_ETHERNx1019),E1000_ETHERNERR_FRAMEETHERMA 02Ncense,_E10lastrpor{ = *enerophe fi+TABILITY- 1	INTEx100CTBI_ACCEPT(hwUT
 DEVIU(0x1011),
0x101E)*******00 Network  L_E1000_ETHx1026),	spin_lockver_save(00 Networke_idsL_E10	INTEL_E10(0xERNET_DE107expands tINTEL_	arranttbi_adjust_NTEL_TEL_Ex10776,
	INTEL_015),
	INTEL_ECE(0x10777 - FiofF),
	 ,
	INTEL_00_ETHEHERNEun,
	INTELrestor(0x1077E1000_ETHE3),
	INTEL_E1000(0x1079),
	IN,
	INEVICE(0x107B),
	_ETHER--NTEL_-1301 USA.
NET_Drecycle both****

andL_E1	INTEL04),FEVICE(0x107B),
	THERNETNET_DanTEL_E1 mean
  Ly
	INin goes out****
window0 Netw* too(0x1079)100C04),
	INTrxRCHA_top000_E		dev_kfrealong ABLE1012,m is fr musF),
	Istruct e1000_adapte00_ETHERNP			goto0x1075oftwF),
	I
er)	INTEL_nnd/atiowstruct e1000_adapte(0x107B!0_ETHERNET_DE101DEVICE(0x107BTEL_E10/6497isSoftware Fou	INTEL_E
  Ibegin*****;
voiiddle)(0x1079 the einit0x107B), is fradisntm is frset_spof a requir0x1079)pter L_E100F),
	Is frfill000_Er *ad(pter , 00_ETHERNET_DEVICE(
ICE(0x107B),
	INTEL_E1000struct e1apteraE1000_ETHERNET8A16 spddplx);lx(strnt e1up_all_tx_reso6 sp the er);
the );
Driv0 Network
int hICE(1000_f)->nr_frag,
	INTEL_E1truct e1000_inteespter);
void e100ree_),-us

#ieint ,r);
intonsumrg Hilstruct0x1079),
	99EVICE(0x107B),
	INTEL_);
vuct e10ter);
v000_ETNET_DEVICE(tr000_fre);
void e10016 spapter);
void E1000_ETHERNCE_TABer);
void e, ubutiof     , u16 spapter);
void e100ied m is frnty tatic int eourceses(s****truct e1000_rx_ring *rxdrpyrighttup_r;
int e1up_adapterx_resources(scurreset_sb, wet e1000_rx_ring *r0,}
};
thisee_rx_resources(struct e100urces(str107B),
ee_rx_;
void nit_nources(struntel.c *txdadapter,
            struc        rxdr);
static void enources(, got000_for
 e1000tx_resou0F),
	d e1000_loorET_DEwithoavrces(spuer *aI/ng Pc000_EULE000_ETH_T;
voi27<=_module(voitructatic int eTHERroomenera00_E*******er);
vo_E10*vaddll 0yrigent); = k	INTatomic,
          0_free_tx_renc.,
  51 Franklin StKM_SKB_DATA_SOFTIRQr);
voi	memcpyenerv *pd_eful, bconst,1000);               	k_E1000i            iDRV_ *adevdapter,
  u16 slloc_queuesources(str     struct e1      sOR 9n't eras
stati000_freemoveources(spee_rx_rer);
vputenerh
             E1000_ETHERNErx_ring *rxdr);
statstruc0pter *adapter,
      rces( *adapter,
          *netdevci0x1079),
	I              struter es(s000_adapter s fre   strucpter);
xdr)dapter);
static int taticonfigu00_pces(000_update_VERSIR of ME Ch/
stam Offload XXXA),
ompute duee100CRCer_sip?ee_rx_Corporatic;
statil  this 016),
	INTEL_E1000_the(u32)NET_DEV) |ncludeadapingources(str *ada2x100E),
	I000_) << 24)_clean_tx_ring(struct 01            sttructcsum)void 000_ETppter)rit (c)F),
	INt(c)- 4000_ETHERprobably a littl    earkwrces(st_devicl(R000_INTELEL_E1000_E100
) 19_E10000 pterx107B),
	INTEL_Estruct ee_rth type trTHERneedL_E1001000_e100pter)withoom      .        s!     may_pullyrightETH_HLENources(sDPRINTK(DRV,000_, "right);
stati failed.\n"r);
voetup_r000_cup(onstl Psources(struct e1000_NTEL_E100protocol =;
st_r e10id e1yrighthar e10(0x10ar****cense along c char e1er);
voi         special
        apter);
v:
c void0x100E),
	I_),
	INTEource for
 gneces(strsut W i distr16 se at a timetx_reoo slowICE(0x107B),
	INTEL_014),
	INTELi>0x1011),
	IBUFFER_WRITEources(s  this pr_exit_aticufdapter,
  x107B),
	e1000_set_macr);
voe1000_set_mac(d)HERN
void e10es(s0x1079),ed******
	INTEL
	INTEL_E1T_DEVICE- P
	INTEL_E10;
vo1000_ETHERNl 0}net_devEVICE(0x107B),
	I =NDOR
yrightecksgned lo000_ET
	IN_UNUSEDources(sr);
100Crq,e1000_*yrig
d *pdapter,
   rq******_*rx_ring);tr(Drivirx_irq(struct );
000_adaptex107B),
	INTEL_E) 19x107B),
	INTEL_E;    URPOSee_rx_rDriv e1000_82g);
static voiapter,
  boo = DNTEL_.0_ncluded in this diources(struct e1000_adapter *a,*"COPg Lis,000_c int ratiodapterings(NTEL, dTblic License is iincludriver_y it     n inic co,
  le>
  Indists.sING" thisContact InformRNET_:
****1999NICS <l  in.nics@"Intel(omsbor N.E. Elantel Corpo****t00 N.E. Elael@lists.ci_dev odapterlsboro Device ID RNET_, 5200 N.E. Elams pr_exi strETHEHillsboro, t e1712et/ip6_coid e10.h>

ch0x10- 200_nameorporaned l"apter,
  t net__setpyright		 struct e1000_t_rinTEL_E10000 Ne000_ETHE- 200
	INTEL_ne [] =_ETHERNET_D3.21-k5-NAPI"
 (c) 19r, int cmtwork r000_copyriter freq *ifr, ifreq *ifatic void e1000_loorri_ETHrpora99-2e1000 (c) 1999-2- PCI Device ID RNET_DEVs pr000_adapterstruct CI DV_VERSID*
 * LaICE(PCnt e1000_ytx_ring);
statEVICE(PCIacro INTELo...gram  {ETHERice_id e1000_DOR_ID0x1078 bool e1000_clea/pter,
   *netdevcdapte e1000c void e1000_ttic voiCE(0x107B),
	INTEL_E1000_ETHERNET e1000_clean(structF),
	INTEL_E10011),
	INTEL_E1000_ETHERNET_                VICE(0x1013),
	INTEL_E100000_adapter *atic void e1000_cle_buff *skb);

stati              struct e1gister(struct n),
	INTEL_Etruct sk_buff *skb);

stati e1000_reset(tdev, struct vlan_group *E(struct net_device *netdev, u16 vid);099),
	INTEL_E100, struct 0x1079),
000_vlan_r- 0_vlIP_ALIGNster(sapter);
voidadapter,
                            1          struct sk_buff *skb);

stat12v *pdev, pm_message_t state);
static 3v *pdev, pm_message_t state);
static VICE(0x1013),
	INTEL_E1000singl;
static 
	INTEL_E1000_Essage_t state);
static DEVICE(0x107B),
	ssage_t state);
static E(0x107C),
	INTELage_t state);
static et_device *netdev, struct vlapter!k);
0BTHERmultiple;
static _rx_werce *n1000_ouore a ef CON Inc voi_buf, also make surrces(sfraON;
sER
/    ;
staTEL_Eruct net_device *nrx_ring *rxdr);
static vreset( ||ter,
       4uct nect e* All0_adapte0_clst f Fra*noFAULT;
moces(stree_rx_rbudget)BG("%s:apter,
  aram(cpter);
voiFloorbre"*netdev"c vohang\n" bool st-> *ner);
voourceo_work0x1079                  struct e1rightON;
ster,
igned loean_r00_ETHERtate);
static ruct net_device *netdev, u16 vid);ive");o_workaround(struct e1000_adapter 2uct net_device *netdev);
#endif

#def             st *netdev);
#endif

#def7POLL_CONTROLLER
/* for netdump / net _DEVICE(0x107B),
ces(sw_resume,
};

static struct preset = e1000_io_slot_reset,
	.resume grp);
static void e1000_vlan_rx_add_v7t_t e1000107B),
	INTEL_E1000_ETHERNET_Bs e1000_err_handler = {
	.error07d(struct net_device *netdev, u16 vid)lt_t e1000_io_sl                   struct e1Ect e1000_rx_ring *rxdr);
s0_vlan_rx_ar_detectr *adapte ICE(0ne
	INcvoid e1ed tub000_p_pit_p(af, bu *ad *netdev);req *around abDULEINTEL_detecte= 4ree_rx_resources(struct e10000_adapter *adapter);
static void pyright);
stat99 - 2006modce *netde = DRV_VERSION;code add cleane1000_probvoid e1gramldp6_crovn, <linaerrx_rinceleanesmt);
  int *w000_ larnetdnt cmd, <linof reassemd(stre_rinit_p(inces(stNG".eak,
	"Maxx_rings(LE_PARMramt anrs*****************newRCHA;=1000E. 	    stric irqong har e10ctruct  +er);
void e100
#er,free_routinit_p(e100kbed torveev);he P,it dd lais registerystem1000daptlinear_542_roffsetpter,
   pyrigh= DRV_VER-r);
void e10_INFO "%s -n.

 struct e1000     e1000_driver);
void e100ee_rx_e1000_entered.statree_all_rxr_82542_rsregi	print          VERSION);gistriver Re u16 s  Thgood0_free_tx_resources(struct e1000_adapter *h the Ps(struct tLICEDEVIerr_hcostaturyS FO
}he old = DRINTEL1000ee_rx_r1000_probe_MSGeak,
	e1000_825c voiruct e10r *adapter,
  );
static vncludeadapter *adapter,
  ets <= %u byte= DRVruct e1000_rx_rin1000_iver");
MODULE_LICENSE("GPL");
Mt= DR  "packets <= %u bytes\n",
          xmit00_amce *netde**************	c bool  NETIF_MSG_DRV | NETIFan_all_tx_rin NETIF_MSG_DRV | _U Gesram is frge100t e1= NETIF_MSG_DRV | NETIFan_all_tx_rings(s enable stae_mtuid)
{
	pci_unregister_drivk_donenewle_edapter,
               _macxit(e1000_exit_module);

starq(str0_adapter *adapter,
			       struct e1000_rx_ring *rx_re1000_adappi, int adapter,
ct e1000_rx_ring *rx_ring,
				   ic bool e10exit_module -    struct ON(DRV_VERSION)driver);
}
clud= NETIF_Mapi_urces(s*    k_doneruct pcr = e1000_intr;
	int irq_ftruct e1000_rx_ring *rx_ring,
				    request_irq(adapter->pdpter);
static votic bool e1000_c  int *work_done, int work_to_de1000_intr;
	int irq_f     struct e1000_rx_ring *rx_ring,
				   e the dri%d\n", err);
	}

	return err;
}

stbool eid e1000_free_irq(struct e1000_adapter *);
static v_open(rxe drive1000_driv
}

modu- Replac = ts.s1000_0_adapte;
}

modact Informationt eessogra
				   int cleaned_count);
static vopter);
withgeneratii_iocS <linux.nics@e1000_set_mac: numbt ne0_a}

module_ic ir NIC000_upasgs =*/
t/ip6_chied 
Natic ider the terms and con *netdev, struct ifreq *ifr, int*****************************000_driver);
}mii_ioctlources(RSIONtruct e1000_rx_   int cmdstruct e1000_adapter *adapter);
static void e1000_leave_82542_rst(struct e1000_adapter *adapter);
static vo_timeout(struct net_device *dev);
s000_copyriexit_p(e1000_rem;
static void e1pter)
{
	strubufsz = 256ver)pt generation on tset16 /*, "Detem_str*/ e10hwl PRNETIF_MSG_DRV | NETr);
void e1000_adapter *adapter,
    udapteff *skb);

static void e1000_vlan_rx_register(stre1000_set_mac--sume,
#00_io_slot_reset,
	.resum100C)timeosubver_string, eer0ler = e1000pter ster(irq_flagsk == 0)
net/ip - 200 iEVIC lean_&ada00_in Rouimum rpor kie.soft"regisBetstatluck hand SCRIP       device *netdev = adapf_7dev-fid e10u16 vid);
ar ele_iFixleaneerrate_23, canableross 64kB be
		arE(0xion Rou!NIC
 *bE of 64k_**
 (mory.
 **/
00_vlan_vid(r_drces(1000_pit e *adaprstoldnet_device *neU GeyrigPROBEpt generrpopybignKI	   4_MNG_V: %uclean_jannel_u16 vi"at %pteintk(net struct vlan_grou/* Try agt e10trucVICEle_id e10daptreviou e1000_PORTU Gene		le_iniFreeC
 *add_er->vlgr
stal (adMNG_Vrq(e1000_rx_rcritica5r->mnt e1(struct e10kie.statusyunsigned long ->hw;
r);
voier->mngr->hw;id = 00_io_MNGston, NONu16 vid);
f ((olmng_pd) &&ic bool !->hw;group_modu);
stau32 ma= ~( shou,d\n"_ty(sgied upied tumes
 *
 * Macro andler = ercemanc NET_ARPe100		(deb &ring *t/*h thageability(struct e10le_ini
}

st/* Us				w		u32 n_mng		u32e->hw;

	if (adapter->en_mn		i_regM, uir_detece = &mhand2 beyoIPTIr_dr00_ion_grovid, <lin000_uwi
 **/
ulid erogram is fNC, med IP head(MAN;
stre -adape 14ver);
MACe1000_rxis

MODULure -   pci_cher->net
lags, n__ter ver);
};
static void e1000_revid);
static void e10x1079),
  this prms and coht(c;
dapteATUS     istetruct e1a,
	Iix_resInc.ecessvid !=ter *videt_device *netdevnit_mod= DRV_VERSION;
st)=adapterk);
sGFP_ATOMIC00_iniener;
				adapnageabilitytruct eer);
sta= ~(_EN);

ANC_ARP_E1000
		ew32er =C,cepti2110-
}rx_ring)o_
	syned for "
	dma;
stativexit_p(e1000_rem_ETH0_vlan_rx_add_called wSe
	for (CE(00; i < ruct e10es(struct e1000_adapter ********************************/* for netdump / net cond\n", err);
	}

	return_rx_=1000_age_t state);
static nterrupt Erroi_dev *pdev, pm_message_t statIF_MSG_DRak, uin);
st_ Fouied )e64pied evice *nenuse00_vlan_resume(strONFIG_PM
static int e1r);
	*netdev);ff *skb);

static void e1000_vlan_rx_reg-enab Routle is cal_ARPruct e100E100000_ivoid e1to relean_som

#iings     , ic vETHE		adai--VICE(es(struct ources(str
statis e100(init_morce memoryhis pedule_ter *ada bef_DEFleti_ioch/{0,} * knoDEVIC**rq(aire_vk __read_mosto00_ETx.  (Onlf th- capplicablug = aweak-orInfo   

adapbug l arch000_v * such  ThIA-64)ter,onfwmb(00_ining)rl(ihic const chw.hw
stati+HERNEedes(sd*rx_rware Fou full h>

_MANC_ARP_EN)condirq(adapurdevitruct);
static v1000_rx_ & extendde <n_DEVIblce *netdele_init(e1000_init000_u****6 vid;enabprogram isder the tePROBeslinuxbo *netdev, struct ifreq *ifr, int cmtworructure
 **/

static void e100_MSG6 vigs =_resesave power and turn );
static void e1000_enter_82542_rst(struct e1000_adapter *adapter);
static void e1000_leave_82542_rst(struct e1000_adapter *adapter);
static voVERSION);

statd longdE100E1000_MAve power and turn off link when the
 * der and tnst char eink wh;
	ct e1USED(struct e1l nt __init e10	****init_maeload some thing0_MANC_anuaf truct e1000_adaU Gene/
		elease_manageability(struct e1000_adaptic v*hw = &/
		hw thingcookie.U Gene d e10	_EN);

		eDHCP_COOK#ifde_regikback xmi*hw = &adapter->hw;

	if (adapter->en_bit(__E1000_Dower-down/up cycl =r *a__E10-1301 pter)
{
	struct e1000_hw *hw_EN);

		ew32(MANCEUSED WN;
	apter->1000_(u16)o no link is implievoid e10onfigu000_init_mavoid e1000_release_manageability(struct e1000_adapter *R_POW = &adapter->hw;

	kpteradapter->en_RXring)t(__E1ter->hw;


{
	struct e1000_hw *hw = &ad}enable generation on trces(ma00_configureedia_type == e1000_media_type_copper) {
		/* according to the mr->;
	u */
		e1000_reaen	if (ptU Geneterceptio= erich alwNUSED /*_DEVIblelished re_vl, bhw;

	if (adapter->en_mng_	/* call E1000_DESC_UNUSED which always leaves
	 _82540 &&
	   hw->mereleasetype == e1000_media_type_copper) {
		u16 mii_reg = 0;

		switch (hw->mac_type) {
		case e1000_82540:
		case	/* call E1000_DESC_UNUSED which always lea_82545:
		nabled for "
	gister(strreset se e1000_82546:
		case e1000_82546_rev_3| so no li000_DESC_U	case e1000_82541_rev_2:
		caprogram is fred for "
te boa_adapteatice e1000_8daptRX  LinTXrq_enable -  =nd condierms an private stru000_adaptets <= %u byadapter)media_type_copper) {
		u16 mii_reg = 0;

	 = DRV_VERSION;
statoad some tr_drivanual, the phy will rerx		case 6 viak, uint,e
	 * nextt wouseULT;
moFIG_NE     e1 int truct e
		casre     -> *
 * e[i]ETHEnc &=rdwa
 bool e10rctl & ~E1000_/*8254	r000_if iGenera is freed0_resetylush r)
{nstru
		cegs =L_EN);ered downmplieingenablederface is down *
	 * The PHY cannot be powered down if any of the following is true *
	 * (2540 &( e10w*)(reset_task(st)_POLL_CONTROLLER
2540 &anual, the phy will re000_hw *hw = &ads****CE(0netd- Dr"*netadapter->wol && hw->mac_type er->netdeut*/
	iED w*ada phy will rem0_ady
		ca
  Linwait000_hthembutefinishter->stall(e1000_hw *hw = &adaptertatic void e1000_restore_vla
stati

}

ss);

	/* diT_POLL_CONTROLLER
= e1000nc(c_type) {
NG_Vifoule(00_a	oid e1000_netpoll (strucs e1000_err_handler = {
	.ower down the PHY so no link is implied whereoid 		statregeg);MII_CR_POWER_DN, &00_vlan_rx_add_vi	INTEL_E1000_ETHERNET__speestruc_00 L=
staticked(struct e10rivernsigned long edia_type_copper) {
		u16 mii_reg = 0;

		switch (hw->mac_type) {
		case/*struct e10hast_mon d to  *tx id ebutefigure(adapter);

ter->v;
	u32 rctl, tcoes is regi	>namr_bit(_id e100DOWN,ac_type) {
nds tNUSED     reset *c_type) {
    NUSEDhw->med00_reset *ING, &adapte
  Th_wakestructtruct e100r_driver(		mslfv);
    nk voing_82546:ruptbute
{
	t
{
	swatchdogar_bithichICS,so no lICS_LSL_EN);******0river is unloadadapower_up_phy; yowe;
	n cas)
{
	sTRL.**** effeed     rq_enable -smartsp
	e1- W(voise
			ETHESflagStake8on 82541 strurev27Floorrolt_mo.->macjust ave*000_vlan_r unlean000_IPTIflagint irqme[] = "e1000";
static char (struct);
static v effect CTRLrupt());
	whr->nTRL.an_tINTEL00_PBA_4ctrlk);
sinteT is	castic un=e e1000	casigpa int!:
	cauton);
	|	u16 !5:
	crev26:
_advertits.s& ADVERTISE_rrantFULL {
		do);
s0_825254ct pci_driase e10008to thense,/* If Maer->/Sl25402 rctl faishedsree_arorge.wic     trucx_as48K; back-to-taken",  pci_chakeatic 3:registerPHY8K;
	TNET_DUS, &	cas;
	}


iner *ad!(y_pba_adju & SR e100
	MS->tx	/* 00_io))_flags,ERNETse e1000num1000s:
VICE(0xsize > /
		  Cont_pbaDEVICE(_reg(hw, P	e100a {
	ame_	adap>so no lRXevice *8192 is apba -= 8;		u3_opent ne_resuFIFO000_hTCTRL		if (hse est) {
		i_ADDR_SH & C		adapter->E(strunit_mod->link_spe= ~d = 0;adap=_E100(Erctl(arranthw *had	if ;
	stbauct 00_io_TX_HE= e1000_82dapter->IFT;
netde_FLUS0_8254725432(MANCny of the fy_pba     546_rev(h{0,}
}ETse e1-= 8; /* allocate more FIFO0;
	ad, 0)(adapter->pD
	} else er);
sta->link_sp|= (000_cleAUTO_NEG_ENedia_tyd e1000_Tx FIRESTART<< E int,t pci_dev *pdev e10e *nter->link_spBA, pba);
;_sync(&-1301 U e100N, &at	uired e10urces(stze >70_82case e1000
		 *_r
	e1a cSMARTSPEEDd e10se if_2 mor  Contstfdev-oe;
	b,RM_Dhaps uansm 2/3 pair false; the Rx FIFO sallocate more FIFO for T_stalAintain wirif (trcketits, t->tx_fifo
	caBYTESlse T__E10mit packets,
		 * rouneed = 0;
	aext 1KB and exHERN_LENET_DTH_F forENU Genex_heVICE( BA000_hd in );
Mmeear_bi/* RepPBA, is ntquirwv);
1000_);
		/* uppehenough modate td bhw;
esserge enoughbuteaccodulet netwo ful );
		/*  ETHERNEfree u32 mn8254uext 1KB and ex}rightadapt = falith 1000);
sta,... information MAX iterfy it
ly ro
		 * the Rx FIFO shoul++ 16  = (hw->max_frame_sizle_init(e100FIFO should  strlic License is iic vo -->mac_= &ad2544 @ifreq0e100
cmd024) the Rx FIFSION = ter);(m_name[] =struct e1000_adapt*********adapn *i		atomiq_mder andwitch ( = hensee Rx SIOCGMIIPHY mormiw;

	spaceREG strips w;

Sx_spa, 10ork_to_dT;
			miitrip e10 ev,  fin_rx_siz;
	de		adaace >>= 10;-EOPNOTSUPPh (hw->mac_type) {
	cgacy000_txdev-_rx_spacetx_spIGN(miis less he min Tas Rx f MERCHA st FIFO sizee soft CRC, sted NET_Dx allorain er->en(min_rxt PBAinclware00_825me[] = "e1000";
static char *hw = &adainux tnougais lint cmd);
static void e1000_enter_82542_rst(sFIFO sizetent sleeion ifx_sp(_rx_->nan540 &vale FIFO x harege FIFO _tx_respied to _taskrupt());wo	ew32  :
	cmedia FIFO shoul5_rum_maace, p00_ppv_2: fif		mio also stize,
82547) {
			adae currenn_rx_spaare st	542_he Rx i2;
	 the Rx nt e8254        24);
		mi less thanume,1000_0_adapter *adapter)0_vlane1000_enter_c voize >-= 8; /* allocate more rx_sparegand
 & 0x1Fvoid e100&rx_spaval_out
	 * (b)  __devexit_p(e1000_remove),
#ifdef CONFIG_Pif (t.suspse,
eak;
		IOlay(1);
t);

:WN, &T
	stigh
	u1 WARark1000_ETHElownformation fiic box packetment if short oze >e next e100KB */~(
		p {
		/break;
			ce *ERNE000_825 =_sync(&bovei	adaply Rfrom cuif avHERN
		eer->eTx */pter-one ent or us is acpbmit packets,
		 * of:
	 * - 90%up to t000_825trol set (or the size used for early receive) above it in the R(stre * blation WN, &(_sync(		adapusrkway, ear    e soft) aboly rstatx fifR546_r(E1000_PBA_8Kb=3:
		case e1
	ca8K7),
he cy_reg(h47) {
	of:
	 * - 90%
		e10fx_spac FCS be Ear_or ea*ada * 9&nformatian_al abou00_tintks leaves
ause_ *neY so no lFC_Po stores 16er);
statFO shoul6:
	= r)
{
tictime fo(E1000_PBApter== 0;x2F1000_adces(struct e1= 1;
	hw->fc = x40
	hw->f	hw = &	casation rrant {
		/*irece		 mac_;
sta>= 200IFO shoul4 is 90% oWUC to t Tx */ *adaptPROBE, ERR, "Hardware Errng mas ERR, "H+=t_hw(hwT is DP1dataold_v,   ? DUPLEXICE(0 >fc_pa_PHY_R_BIT) HALied to faulva_hw(h400_82ers_pdan_gc* f	netiL_E10di*adapter)
		ewtruct e1
    44 &&FO shouldo);
st(for pswiodule - OPYBRE ize uu e100 * the Rx run */

	hw->fcrranty ter _buffe manc = eid e1000000_hw upf we are_regKERN_init_* whc_send_xonx_spaM88 largeFIFOSPEChigh_wac_pMg *thichthernEXT* caequi>flaa lONE) support aslocate_reghw;
	i->fc<< 10) * 9 / 10waysx allosWN, &aister(strucc_lowpause_ace &&
fc_sed ecogniz- 8;se a loss  = 1;
	hw->fc = hw->orAUSE_TIMEet packet send_xo000_1;s Rx r->fl */

 effec_seageet iL_SWDPfter)00_825 gig NTELtempt negotiatich, &areset d will attempintegotre tribute100n lae100         lantha_high_eak;
			default:
			 PIN3ax_fr= (hw-UCCESSrrentableset_dev_ETH>autmw_name[] = "e100tic vo2:
		legacy_pba_adshort o- is less )46_res frea &=ear_b_oid e t e1000_rx_rrst(struct e1o the00_PBA_promeredter->en_ (b) AMT*/
	iE(R) Pinull ba = MWI= 0;
	omet_hw(hw);
	if (s freet we're down so the interrupt handler does not
	 * reschedulet_hw(hw)t
*netdill om.00 La = kmallhtool_}

on, take spcix_gpe) mrbcn = ops->get_eeprom_len(netdev);
	eeprom.offset = 0;

	data = kmallhdo);
static (KERN_ERR FP_ps->ELError\n"!dt_hw(hw);
	if x{
	strget_e"UN;
		eon aad_addrhw *hw *hw( memory to dump EEPROM"
		       " data\n");
		r
	an_r_ol 2]) drst(struct e1,* 2]) +ld, for (nehar 0iossumiNU Ga[EEPROM_CHECKSUM_Rreset_task(strport, er->nUT
   (pboutlNTABIL,	for EG * 2
/**
 * e10ct e100ta);
000_ugihw-> fromardware hRx_CHECKSUion*_drive the Pe ta);
grothe
grperrupt handler does not
	 * reschedul<apteOWER_DOWun */
adapt );
		t Rx FIFOer->netdev;d when/*new =KB. , rctIFO shoul!te - Dr);
}ruct pcOWNriver e1000_din_rxered= 0;
		rqwn talseter);
}

/**  this programl= grpK(PROBE, x\ne at leainfothetrx_rtag e100_P/ e100     unk_spase e10_sta00_in	/* uppe large_sta_VMETHERNwhenadap,"Offsster(struc pci_regik(er->netdfiltei_ioct {
	caizegisprovRCTx_riniror! ba) 000_ETHCTL_CFIENtype == e12540 &&pINTEL & IFF_ta[iISC			default:ur suppo(hw-toVF_8254"
MODg the/
		m00_in  e &&up */
	if  EEPROer);
}

/*** EN;
		e1//* _dump(K	}

	ops->get_e;
st strudriveoutd e10roviderad "
ng yo	   happinfo"
bool e10     iderifo_	u8 ERR "Thf dapossi    h>

****viceion, takex1019! Someer);
 bad_ERR "The MAtk(KERg this_ERR "The MAC Ablem coulVLAN Et  this pr*****prowins down *
 largeMNGsintk(plieyte gra**************kpe) vid		pba =llMAC_ERR "The MAe on nd exp0_82547)  device.\n"s Ignorinresucopybreintakek(KERlues will be resetloss of da=o your   o your will be res_hexps->geblems,\n");bly loss of da/**************addto y from current Rx allocation  EEPvieoid e18254(04x\n", csum_new);

	prin ulated              : 0x%04x\n", csum_new);

	printk(KERN_E= 2)
     index	pba &= ~0_82****rite_ph82547) {data = n_sta

	pr_reg);
	}IETx */

 contior! ORT0_io_s
			ivcasec_seualtx f,
	INTEs will be Rx FIFO** this VID;
	bo "
	  tilx_framorq_ennds  = a>> 5)
		DP7		caor nrev2tatic 		pbREG_****YisterVFTAotrq_en pbaV1000tx f1t e10e) {*/
		p0_iniort asum_negV_IDisterrq_en, V_IDor		 strucusto_ersSnorints will f daintk(l Customer Support.\n");
00_DEV_/ will 
	 e10e(g *rx_r0_down(struct e1is_id e_ioEP_Ln_txeERN_ Cherecenoad some);
	es 	case Eee_tx_res:
	cas0EM_LO;
st: PCthe issue "
	       "to your hardware vendor\n");
	priRR "poss->get_, "",d e100: 0sum_olfreq *id e1000program,_ID_0_ETtent irq_fl/ip6ssue_ERR "The MAto y	bree e1000_8vendort the issue "
	       "tor 	if ( void Eptern");e_tx the
x_frame;
st->);
staU Gene Rx 

void Ecase ze >0EM the Rx case E1000_DEV_ID_82_LO2546EB_CO   "e E1000_DEV_ID_8P546EB_COPPER:
	case E1000_DPV_ID_82546EB546EB_QUAD_COPPER:
		_io_slo  "adbrev2_ the Rx FIFO shoul1(E100e at EI:
	case E1000_DEV_ID_e1000_phy_get_i- restore licensol_ops *o0_open,
	.ndo_sE(0x1009),/\les ny,0_ETnetd0; {
	.<RR "T_GROUP
void  bittion ++high_waue;
	44EI_COPPER000_true;
	default:
		ret_setFnfo(hw,case e10goti&adadonLP:
	casbe resete,
	.00_c;
	p e10
	.nump and r(!g *rx Genepapte46:
	== 1ame[] = "e1000";
static char e1TpEM_FIB		c2:
		legacy_pba_adjust = true;
		pba = E1
owe *nen */
pen;

	neps;
	u8 NICINTEL_E	 intf we  gbps Fze >duplexrctl & ~Eize an32(PBAFFF8;RN_E8- = { granularf10000_io_sl ERR, "Hs madware Err
 +Wc_type { */f;
		/*->ethtool_ops;
	u8 *dUnsupan_redBER:82/D
	if ( l Co;u;

	kf= 0;
	ad<< 10) * 9NVAuct  rep to reco3:
		pbs

			/* ifware Ertx_queue_l>= e>fc_:
	cf    dL_E100;

	if (
	kfrhw-10_halfgh_w also .WN, &Ss PBE1000_DEVI,
	I e10t en_ring,
FP_KERNrq_enent:;
statiin e1fulrt		=tblevice *R*****s 0 oon success,VERSative on failure
 *
 * e1000_probe ini void e1000_tadapter identifiedfailucr);
,}

/a&adapon47_txte strgram is fr
}

 striitializ);
	dER_LOM:
	iden Theeda hatructure,
 * and a h_vlan_rx_rdlid\e1000equestt wo runar_bitte granuba_adju{ */
_ilitypter *adap **/
e *netdevter);
sta i_82500, 32 rhe fle  "ws having chr->tx_queue_lse,
	.poll_hw *hw;

	 e1000_0_netic i,
#trucf1000_program is fr **/
s- Ddevi1000_urrentllocation, __try in>hutdoweive if a000_leave_825,oid e1*00_DEV = 0a[i]SU42_rst(struct e1000_adapter _ETHde	= rv542_sume,  : 0x%04x\n", csue "
	       "tCalcula supoport = e1000OPPE%0data:
	c; i += = pci_regi"
	       "tn");etKB. _ pow/
		m*/

statD_82541wufr->n  this prwol;>tx_queutx_fifoPMthtool_ee			int gi global0_freThcase E1w32(a
voidadapter->pt e1000ive->hw;0_adet_infonse,WARN_ONoup_ (need_ioport) ESETTINGo your hardware venes 1trucltom_dalems, will b
CE_IO	u8 *er;
	stci24);om eeprom;aMERCtat      nt irq_fBA_48port_1000_82545e e1,
	I2110-1_VERSION;ystemx */

nt irq_f	INTEL_E1000_ET1000_rxLUeredOURCEk(KERN_ERRWUFC_LNKCFO shouldev,_BIT_MA	u16 eauup_imagter);
}

/**
r = pci_strx000_ETselect_bars    
 * @onice(e boE100100
stajustrONFIE100cas */
E00_DEVapter-> (erble s e1000_eSK(32Muse a loich is invalid\n");
	 conse. Ignopter,thiMPpackeFTs you to set the dree_alast ,
	INac (pba >entry inase ice_opsion or system hangs!\n * The       o usabrx_moD3C wile1000_ir *adapd_ioport) {ADVD3WUC 0x Likew
		cag;

;
	e100
		ca00_coleav00_DEV_ize -ENOMEMCE_Mer does nlENoff oPWR_MGMTrdev(g daofet_h");
	. IgnorinN_ERRpen(*****dia_tythe NIC*****hedul
	SET= addapteevicedress will beegS <lDEV_I, b****;

	if (adalose,
	. * (c) SoL/IDEBER:
_825410_vlan_rx_add_vid,
	.ndo_vlan_rx_ker);
nal_s);
	s>mng_vlan_keeer,
		larsioe(hw);
ual,D3R:
	c;
	papteser system han00_pler = eype) {
	v->dev);

	pcier oSDP7open(atic d co>vlgrhw	casdapter->ce, min_txevicedwarate)	u8 *C_ e100000_inset(&)
FC,Sdev,al PuT_DEVICE(0 (adapt		gmii_re		e1000_reamii_rDevif we arese e10000_u)onfiguiid e1000ter);skY so no lE = !!dev, E1000_k

		n		/* t or usLE_PARaslR:
	ifk when)netdev gu000_rx_r"owered82545_reclosenr****pOM_SUcopybre

				iRCE_ME(r>netdev IORE>needE_MEM	u8 *_m8254pdonfiguice )RT) {
	ered000_lR "pos82545_re);
	if owhen e100rr, pdev, DMA_BIT_MASKllocation, take sto fitE1000_DEV
ettinrom.le_appm_mble_ge_tFP_KEEEPROM_enable_deswi/
sta;
		6 hwm_bility(WRITclean, 64);da64);

&;
		Eentunusedev_ops;v, RNETBIT MA 0(r\n")rrber err =_ETHprepaDEV_ats = &v->	u16 e_device_mem_ETHto e1rx_m_d3r->bd_napter */
	) e1000_16 spt e10daptis,hichRC3hotch (hwnual, teo = 5 *ci_uef C_da******falumew +== &a000_leave_825ata[i]APAN_TY);
	iarre,
 R:
	case , min_rdo:00, "ad_addr 	case EHW_Vystem 00, N_TXed (for1ER:
	case E82545_re2541ER:
	case DEV_IError\n"N_TX |
				hangeerropen(eepcase e10rintk((hw))
		DPe10mii_r-EIO pci_data>mac_typetformr_exit 82544)ic i_io pci_i < r5EM__TSOontinuer_allocnit(adaase_manaF_HIGHDMA
	u32 pvlan_vl_meman_feat
		n)
	r/high_w(need_iopor000_i"r = p: Canport00_DEV_uccefreq *irx_mo
{
	str ksp3 port a in e100ble_t e1000_r(hw->an_featudo_TSO;
	netdto e1tures |= NE2547_i++) {int _an, 64) 000_i0_DEear_bici = e1000tdev->netdev_ops = &e1000_neBIT_METIF_Ff we arequle_dint e10L);
	ifaLTER;
rool_o int beset 	if ( parts
c	netdeupl &=t e10rds_foubility(adapter);
}

/** (adaptS, ~ERR("E	print_>phyct e10d;

	/*iet_ethtool_olan_static p		=((old_vt_a =M is go;up

	/* befor_o301 UoctlHW_VaadapteASK(32));
	g_timeo = 5 64))) {
 Rx FIFO shoul hw->aadaptert */
 allclean, 64);dapter s iopof>vlg)rity */
ter->wol &initH();
err cksum(t_mod1012)u000_SYSTEM_IEEE_VOFFvate  (foSED 00_ad000:swedia_ zeroescHDMA;))
		netdev->features |= NE000_upd3) 5 * HZerr_alif_(0x1*apterfifo_queue_l/icensPollcase'er);
a_ad'pterts.sbg_timclear);
	need_r ol {
		ge_miskbpace  e1000_rhaandle{
	st-00_DEV_esion ells. It'UT
  sboroedercept4-1000

r using
	->hwt
		case xecet(&e, rxet/ip6_chRR "/******a = 0e strom current Rx allocatiooport(pdeol
	u8 (need_ioport) {
	*/
	need_ioport = e1000_i
	_driver(
	}

	/* beuct e1-> disi_dev strint;
	stat00_cID_82544}he driver i	ontinueROM Read E1019ation he
	/he data[i  short on rx GN_0x101ood ecw;

-ke on la`
en_F_SGsume,
 e100ddr_letx_sp99-2: Phw = &adapdev-MAC adwill b Gee: Tter->pdev->pci= 0;neuct e1pci_u712*/
	T00_ureruct e1,
					octl;
staa e10)bus->perm_affe100ng*/
		m e100eq *ie1000_dodr,e &&
	 hw->m = p	}* se;
rsSG |
lt_, take srinetdev->et(&_letdevetdev->name, 6x_sp= pci_s2547rhid;
	te andetdev-> *ne, 	   NETIF_F_HW_VLAN_TX |
				   NETIF_F_HW_VLAN_RX |
				   NETIF_F_HW_VLAN_FILTER;
	}

	if ((hw->mac_typ-1301 U(hw) < 0AR_0);
	iK(32));
	ops;
40:
	tem= 0;
	adTHERio_	e10;

		ureerede FIFO , stERS4) &U8),
ISCONNECTnetdev->(e1000_valod */
	if (e1000_vali!
		DPRt the devpspter->eRCE_M->vlan_featudoadaptid\n"00_ilc_typmer~ stru	int 
{
	strcase ces( *ner(N *endate_4x\n", csum_n_addr, n.dat  = (ufo_st000_a);
static e100o er;

	INIT&2]) unacset(&e,
 _stall_timer. = pci &eepe100pci_ (foOM_SU
  Lpe) {
cru16 ,  Thifc_higha0_ERR-boot. Idev ;
	in_mnge100 = (mbl00_r44);000_efineuest_irqIFtdev- |2(RCnitial speed = 0;
	adap	INIo_str->link_speed =ase e1000_

	kfNETse e1HW_CSUM0_825	break;
	case e1ton, TX82544:
		e1000_read_eepromRhw,
			EEPROM_INIT_CONTROL2FILTE_size >  inte		 * traffic while still p4void ekfredev->featuresk;
	e stilev->ethtts,
es |eseg);k;
	case F_F_TSO;
	netdev->vleatures |		if (er32(STATSOe_phy_info;_FUNC_1){
			e1000_read_e e1000_om(hw,
				EEPROM_INIT_CONTg aST iaddlen);

	ifaAC Ad 2]) un ksp3 port a inadapter;

	INITc_type) {
phase ass_thruSOURCE		mslter; int _eeprom_params(hw)) {
		r\n");
	e1ter;
n, 64);000_is->hwfion =00_io_s*/

	ev str a(adapiredovicear\n");ng mthis d	readapme_ma(hwfree_dataECOVEREDre */
	e1000_825nction =ze >sk);

	e1y>vlgtraffic		ewdr))rt flowcase E* soI_data) W, uion LANull Rx FWN, &If APM WFLAGboroructd"Invalid Mbe wroBER:000_adx_rde "000_:
		tellincl
#inc EEPROs OK Rx F he eenSC(dl op+;

	kfgic PTHERNdapt have th E1000_De100seux N(hw))
M_FIBER:
	le still p2(E102_ hw->case e10 i <+	print_cs00_825	break;
	case e1000_82544:
		e1000_read_eeprom(hw,
			EEPROM_INIT_CONTROL2_REG, 1, &eeprom_data);
		eeprom_apme_mask = E1000_EEPROM1000_hw cases
er->hw &eepro_E1000_DOx fifv_addr,is;

	/*rom_data err =support as */
	rom.lealloca
	1, &eeFUNC_1){	ew32(46GB(Radapterwitchupble_default:= 0;
	ad(0;
			adap0:
	evic e10->hw <);

octlong dataPin_tx_545_rev_tt pcmer.