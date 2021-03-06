/*
 * Copyright (c) 2008-2009 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/io.h>
#include <asm/unaligned.h>
#include <linux/pci.h>

#include "ath9k.h"
#include "initvals.h"

#define ATH9K_CLOCK_RATE_CCK		22
#define ATH9K_CLOCK_RATE_5GHZ_OFDM	40
#define ATH9K_CLOCK_RATE_2GHZ_OFDM	44

static bool ath9k_hw_set_reset_reg(struct ath_hw *ah, u32 type);
static void ath9k_hw_set_regs(struct ath_hw *ah, struct ath9k_channel *chan,
			      enum ath9k_ht_macmode macmode);
static u32 ath9k_hw_ini_fixup(struct ath_hw *ah,
			      struct ar5416_eeprom_def *pEepData,
			      u32 reg, u32 value);
static void ath9k_hw_9280_spur_mitigate(struct ath_hw *ah, struct ath9k_channel *chan);
static void ath9k_hw_spur_mitigate(struct ath_hw *ah, struct ath9k_channel *chan);

/********************/
/* Helper Functions */
/********************/

static u32 ath9k_hw_mac_usec(struct ath_hw *ah, u32 clks)
{
	struct ieee80211_conf *conf = &ah->ah_sc->hw->conf;

	if (!ah->curchan) /* should really check for CCK instead */
		return clks / ATH9K_CLOCK_RATE_CCK;
	if (conf->channel->band == IEEE80211_BAND_2GHZ)
		return clks / ATH9K_CLOCK_RATE_2GHZ_OFDM;

	return clks / ATH9K_CLOCK_RATE_5GHZ_OFDM;
}

static u32 ath9k_hw_mac_to_usec(struct ath_hw *ah, u32 clks)
{
	struct ieee80211_conf *conf = &ah->ah_sc->hw->conf;

	if (conf_is_ht40(conf))
		return ath9k_hw_mac_usec(ah, clks) / 2;
	else
		return ath9k_hw_mac_usec(ah, clks);
}

static u32 ath9k_hw_mac_clks(struct ath_hw *ah, u32 usecs)
{
	struct ieee80211_conf *conf = &ah->ah_sc->hw->conf;

	if (!ah->curchan) /* should really check for CCK instead */
		return usecs *ATH9K_CLOCK_RATE_CCK;
	if (conf->channel->band == IEEE80211_BAND_2GHZ)
		return usecs *ATH9K_CLOCK_RATE_2GHZ_OFDM;
	return usecs *ATH9K_CLOCK_RATE_5GHZ_OFDM;
}

static u32 ath9k_hw_mac_to_clks(struct ath_hw *ah, u32 usecs)
{
	struct ieee80211_conf *conf = &ah->ah_sc->hw->conf;

	if (conf_is_ht40(conf))
		return ath9k_hw_mac_clks(ah, usecs) * 2;
	else
		return ath9k_hw_mac_clks(ah, usecs);
}

/*
 * Read and write, they both share the same lock. We do this to serialize
 * reads and writes on Atheros 802.11n PCI devices only. This is required
 * as the FIFO on these devices can only accept sanely 2 requests. After
 * that the device goes bananas. Serializing the reads/writes prevents this
 * from happening.
 */

void ath9k_iowrite32(struct ath_hw *ah, u32 reg_offset, u32 val)
{
	if (ah->config.serialize_regmode == SER_REG_MODE_ON) {
		unsigned long flags;
		spin_lock_irqsave(&ah->ah_sc->sc_serial_rw, flags);
		iowrite32(val, ah->ah_sc->mem + reg_offset);
		spin_unlock_irqrestore(&ah->ah_sc->sc_serial_rw, flags);
	} else
		iowrite32(val, ah->ah_sc->mem + reg_offset);
}

unsigned int ath9k_ioread32(struct ath_hw *ah, u32 reg_offset)
{
	u32 val;
	if (ah->config.serialize_regmode == SER_REG_MODE_ON) {
		unsigned long flags;
		spin_lock_irqsave(&ah->ah_sc->sc_serial_rw, flags);
		val = ioread32(ah->ah_sc->mem + reg_offset);
		spin_unlock_irqrestore(&ah->ah_sc->sc_serial_rw, flags);
	} else
		val = ioread32(ah->ah_sc->mem + reg_offset);
	return val;
}

bool ath9k_hw_wait(struct ath_hw *ah, u32 reg, u32 mask, u32 val, u32 timeout)
{
	int i;

	BUG_ON(timeout < AH_TIME_QUANTUM);

	for (i = 0; i < (timeout / AH_TIME_QUANTUM); i++) {
		if ((REG_READ(ah, reg) & mask) == val)
			return true;

		udelay(AH_TIME_QUANTUM);
	}

	DPRINTF(ah->ah_sc, ATH_DBG_ANY,
		"timeout (%d us) on reg 0x%x: 0x%08x & 0x%08x != 0x%08x\n",
		timeout, reg, REG_READ(ah, reg), mask, val);

	return false;
}

u32 ath9k_hw_reverse_bits(u32 val, u32 n)
{
	u32 retval;
	int i;

	for (i = 0, retval = 0; i < n; i++) {
		retval = (retval << 1) | (val & 1);
		val >>= 1;
	}
	return retval;
}

bool ath9k_get_channel_edges(struct ath_hw *ah,
			     u16 flags, u16 *low,
			     u16 *high)
{
	struct ath9k_hw_capabilities *pCap = &ah->caps;

	if (flags & CHANNEL_5GHZ) {
		*low = pCap->low_5ghz_chan;
		*high = pCap->high_5ghz_chan;
		return true;
	}
	if ((flags & CHANNEL_2GHZ)) {
		*low = pCap->low_2ghz_chan;
		*high = pCap->high_2ghz_chan;
		return true;
	}
	return false;
}

u16 ath9k_hw_computetxtime(struct ath_hw *ah,
			   const struct ath_rate_table *rates,
			   u32 frameLen, u16 rateix,
			   bool shortPreamble)
{
	u32 bitsPerSymbol, numBits, numSymbols, phyTime, txTime;
	u32 kbps;

	kbps = rates->info[rateix].ratekbps;

	if (kbps == 0)
		return 0;

	switch (rates->info[rateix].phy) {
	case WLAN_RC_PHY_CCK:
		phyTime = CCK_PREAMBLE_BITS + CCK_PLCP_BITS;
		if (shortPreamble && rates->info[rateix].short_preamble)
			phyTime >>= 1;
		numBits = frameLen << 3;
		txTime = CCK_SIFS_TIME + phyTime + ((numBits * 1000) / kbps);
		break;
	case WLAN_RC_PHY_OFDM:
		if (ah->curchan && IS_CHAN_QUARTER_RATE(ah->curchan)) {
			bitsPerSymbol =	(kbps * OFDM_SYMBOL_TIME_QUARTER) / 1000;
			numBits = OFDM_PLCP_BITS + (frameLen << 3);
			numSymbols = DIV_ROUND_UP(numBits, bitsPerSymbol);
			txTime = OFDM_SIFS_TIME_QUARTER
				+ OFDM_PREAMBLE_TIME_QUARTER
				+ (numSymbols * OFDM_SYMBOL_TIME_QUARTER);
		} else if (ah->curchan &&
			   IS_CHAN_HALF_RATE(ah->curchan)) {
			bitsPerSymbol =	(kbps * OFDM_SYMBOL_TIME_HALF) / 1000;
			numBits = OFDM_PLCP_BITS + (frameLen << 3);
			numSymbols = DIV_ROUND_UP(numBits, bitsPerSymbol);
			txTime = OFDM_SIFS_TIME_HALF +
				OFDM_PREAMBLE_TIME_HALF
				+ (numSymbols * OFDM_SYMBOL_TIME_HALF);
		} else {
			bitsPerSymbol = (kbps * OFDM_SYMBOL_TIME) / 1000;
			numBits = OFDM_PLCP_BITS + (frameLen << 3);
			numSymbols = DIV_ROUND_UP(numBits, bitsPerSymbol);
			txTime = OFDM_SIFS_TIME + OFDM_PREAMBLE_TIME
				+ (numSymbols * OFDM_SYMBOL_TIME);
		}
		break;
	default:
		DPRINTF(ah->ah_sc, ATH_DBG_FATAL,
			"Unknown phy %u (rate ix %u)\n",
			rates->info[rateix].phy, rateix);
		txTime = 0;
		break;
	}

	return txTime;
}

void ath9k_hw_get_channel_centers(struct ath_hw *ah,
				  struct ath9k_channel *chan,
				  struct chan_centers *centers)
{
	int8_t extoff;

	if (!IS_CHAN_HT40(chan)) {
		centers->ctl_center = centers->ext_center =
			centers->synth_center = chan->channel;
		return;
	}

	if ((chan->chanmode == CHANNEL_A_HT40PLUS) ||
	    (chan->chanmode == CHANNEL_G_HT40PLUS)) {
		centers->synth_center =
			chan->channel + HT40_CHANNEL_CENTER_SHIFT;
		extoff = 1;
	} else {
		centers->synth_center =
			chan->channel - HT40_CHANNEL_CENTER_SHIFT;
		extoff = -1;
	}

	centers->ctl_center =
		centers->synth_center - (extoff * HT40_CHANNEL_CENTER_SHIFT);
	centers->ext_center =
		centers->synth_center + (extoff *
			 ((ah->extprotspacing == ATH9K_HT_EXTPROTSPACING_20) ?
			  HT40_CHANNEL_CENTER_SHIFT : 15));
}

/******************/
/* Chip Revisions */
/******************/

static void ath9k_hw_read_revisions(struct ath_hw *ah)
{
	u32 val;

	val = REG_READ(ah, AR_SREV) & AR_SREV_ID;

	if (val == 0xFF) {
		val = REG_READ(ah, AR_SREV);
		ah->hw_version.macVersion =
			(val & AR_SREV_VERSION2) >> AR_SREV_TYPE2_S;
		ah->hw_version.macRev = MS(val, AR_SREV_REVISION2);
		ah->is_pciexpress = (val & AR_SREV_TYPE2_HOST_MODE) ? 0 : 1;
	} else {
		if (!AR_SREV_9100(ah))
			ah->hw_version.macVersion = MS(val, AR_SREV_VERSION);

		ah->hw_version.macRev = val & AR_SREV_REVISION;

		if (ah->hw_version.macVersion == AR_SREV_VERSION_5416_PCIE)
			ah->is_pciexpress = true;
	}
}

static int ath9k_hw_get_radiorev(struct ath_hw *ah)
{
	u32 val;
	int i;

	REG_WRITE(ah, AR_PHY(0x36), 0x00007058);

	for (i = 0; i < 8; i++)
		REG_WRITE(ah, AR_PHY(0x20), 0x00010000);
	val = (REG_READ(ah, AR_PHY(256)) >> 24) & 0xff;
	val = ((val & 0xf0) >> 4) | ((val & 0x0f) << 4);

	return ath9k_hw_reverse_bits(val, 8);
}

/************************************/
/* HW Attach, Detach, Init Routines */
/************************************/

static void ath9k_hw_disablepcie(struct ath_hw *ah)
{
	if (AR_SREV_9100(ah))
		return;

	REG_WRITE(ah, AR_PCIE_SERDES, 0x9248fc00);
	REG_WRITE(ah, AR_PCIE_SERDES, 0x24924924);
	REG_WRITE(ah, AR_PCIE_SERDES, 0x28000029);
	REG_WRITE(ah, AR_PCIE_SERDES, 0x57160824);
	REG_WRITE(ah, AR_PCIE_SERDES, 0x25980579);
	REG_WRITE(ah, AR_PCIE_SERDES, 0x00000000);
	REG_WRITE(ah, AR_PCIE_SERDES, 0x1aaabe40);
	REG_WRITE(ah, AR_PCIE_SERDES, 0xbe105554);
	REG_WRITE(ah, AR_PCIE_SERDES, 0x000e1007);

	REG_WRITE(ah, AR_PCIE_SERDES2, 0x00000000);
}

static bool ath9k_hw_chip_test(struct ath_hw *ah)
{
	u32 regAddr[2] = { AR_STA_ID0, AR_PHY_BASE + (8 << 2) };
	u32 regHold[2];
	u32 patternData[4] = { 0x55555555,
			       0xaaaaaaaa,
			       0x66666666,
			       0x99999999 };
	int i, j;

	for (i = 0; i < 2; i++) {
		u32 addr = regAddr[i];
		u32 wrData, rdData;

		regHold[i] = REG_READ(ah, addr);
		for (j = 0; j < 0x100; j++) {
			wrData = (j << 16) | j;
			REG_WRITE(ah, addr, wrData);
			rdData = REG_READ(ah, addr);
			if (rdData != wrData) {
				DPRINTF(ah->ah_sc, ATH_DBG_FATAL,
					"address test failed "
					"addr: 0x%08x - wr:0x%08x != rd:0x%08x\n",
					addr, wrData, rdData);
				return false;
			}
		}
		for (j = 0; j < 4; j++) {
			wrData = patternData[j];
			REG_WRITE(ah, addr, wrData);
			rdData = REG_READ(ah, addr);
			if (wrData != rdData) {
				DPRINTF(ah->ah_sc, ATH_DBG_FATAL,
					"address test failed "
					"addr: 0x%08x - wr:0x%08x != rd:0x%08x\n",
					addr, wrData, rdData);
				return false;
			}
		}
		REG_WRITE(ah, regAddr[i], regHold[i]);
	}
	udelay(100);

	return true;
}

static const char *ath9k_hw_devname(u16 devid)
{
	switch (devid) {
	case AR5416_DEVID_PCI:
		return "Atheros 5416";
	case AR5416_DEVID_PCIE:
		return "Atheros 5418";
	case AR9160_DEVID_PCI:
		return "Atheros 9160";
	case AR5416_AR9100_DEVID:
		return "Atheros 9100";
	case AR9280_DEVID_PCI:
	case AR9280_DEVID_PCIE:
		return "Atheros 9280";
	case AR9285_DEVID_PCIE:
		return "Atheros 9285";
	case AR5416_DEVID_AR9287_PCI:
	case AR5416_DEVID_AR9287_PCIE:
		return "Atheros 9287";
	}

	return NULL;
}

static void ath9k_hw_init_config(struct ath_hw *ah)
{
	int i;

	ah->config.dma_beacon_response_time = 2;
	ah->config.sw_beacon_response_time = 10;
	ah->config.additional_swba_backoff = 0;
	ah->config.ack_6mb = 0x0;
	ah->config.cwm_ignore_extcca = 0;
	ah->config.pcie_powersave_enable = 0;
	ah->config.pcie_clock_req = 0;
	ah->config.pcie_waen = 0;
	ah->config.analog_shiftreg = 1;
	ah->config.ht_enable = 1;
	ah->config.ofdm_trig_low = 200;
	ah->config.ofdm_trig_high = 500;
	ah->config.cck_trig_high = 200;
	ah->config.cck_trig_low = 100;
	ah->config.enable_ani = 1;
	ah->config.diversity_control = ATH9K_ANT_VARIABLE;
	ah->config.antenna_switch_swap = 0;

	for (i = 0; i < AR_EEPROM_MODAL_SPURS; i++) {
		ah->config.spurchans[i][0] = AR_NO_SPUR;
		ah->config.spurchans[i][1] = AR_NO_SPUR;
	}

	ah->config.intr_mitigation = true;

	/*
	 * We need this for PCI devices only (Cardbus, PCI, miniPCI)
	 * _and_ if on non-uniprocessor systems (Multiprocessor/HT).
	 * This means we use it for all AR5416 devices, and the few
	 * minor PCI AR9280 devices out there.
	 *
	 * Serialization is required because these devices do not handle
	 * well the case of two concurrent reads/writes due to the latency
	 * involved. During one read/write another read/write can be issued
	 * on another CPU while the previous read/write may still be working
	 * on our hardware, if we hit this case the hardware poops in a loop.
	 * We prevent this by serializing reads and writes.
	 *
	 * This issue is not present on PCI-Express devices or pre-AR5416
	 * devices (legacy, 802.11abg).
	 */
	if (num_possible_cpus() > 1)
		ah->config.serialize_regmode = SER_REG_MODE_AUTO;
}

static void ath9k_hw_init_defaults(struct ath_hw *ah)
{
	struct ath_regulatory *regulatory = ath9k_hw_regulatory(ah);

	regulatory->country_code = CTRY_DEFAULT;
	regulatory->power_limit = MAX_RATE_POWER;
	regulatory->tp_scale = ATH9K_TP_SCALE_MAX;

	ah->hw_version.magic = AR5416_MAGIC;
	ah->hw_version.subvendorid = 0;

	ah->ah_flags = 0;
	if (ah->hw_version.devid == AR5416_AR9100_DEVID)
		ah->hw_version.macVersion = AR_SREV_VERSION_9100;
	if (!AR_SREV_9100(ah))
		ah->ah_flags = AH_USE_EEPROM;

	ah->atim_window = 0;
	ah->sta_id1_defaults = AR_STA_ID1_CRPT_MIC_ENABLE;
	ah->beacon_interval = 100;
	ah->enable_32kHz_clock = DONT_USE_32KHZ;
	ah->slottime = (u32) -1;
	ah->acktimeout = (u32) -1;
	ah->ctstimeout = (u32) -1;
	ah->globaltxtimeout = (u32) -1;

	ah->gbeacon_rate = 0;

	ah->power_mode = ATH9K_PM_UNDEFINED;
}

static int ath9k_hw_rfattach(struct ath_hw *ah)
{
	bool rfStatus = false;
	int ecode = 0;

	rfStatus = ath9k_hw_init_rf(ah, &ecode);
	if (!rfStatus) {
		DPRINTF(ah->ah_sc, ATH_DBG_FATAL,
			"RF setup failed, status: %u\n", ecode);
		return ecode;
	}

	return 0;
}

static int ath9k_hw_rf_claim(struct ath_hw *ah)
{
	u32 val;

	REG_WRITE(ah, AR_PHY(0), 0x00000007);

	val = ath9k_hw_get_radiorev(ah);
	switch (val & AR_RADIO_SREV_MAJOR) {
	case 0:
		val = AR_RAD5133_SREV_MAJOR;
		break;
	case AR_RAD5133_SREV_MAJOR:
	case AR_RAD5122_SREV_MAJOR:
	case AR_RAD2133_SREV_MAJOR:
	case AR_RAD2122_SREV_MAJOR:
		break;
	default:
		DPRINTF(ah->ah_sc, ATH_DBG_FATAL,
			"Radio Chip Rev 0x%02X not supported\n",
			val & AR_RADIO_SREV_MAJOR);
		return -EOPNOTSUPP;
	}

	ah->hw_version.analog5GhzRev = val;

	return 0;
}

static int ath9k_hw_init_macaddr(struct ath_hw *ah)
{
	u32 sum;
	int i;
	u16 eeval;

	sum = 0;
	for (i = 0; i < 3; i++) {
		eeval = ah->eep_ops->get_eeprom(ah, AR_EEPROM_MAC(i));
		sum += eeval;
		ah->macaddr[2 * i] = eeval >> 8;
		ah->macaddr[2 * i + 1] = eeval & 0xff;
	}
	if (sum == 0 || sum == 0xffff * 3)
		return -EADDRNOTAVAIL;

	return 0;
}

static void ath9k_hw_init_rxgain_ini(struct ath_hw *ah)
{
	u32 rxgain_type;

	if (ah->eep_ops->get_eeprom(ah, EEP_MINOR_REV) >= AR5416_EEP_MINOR_VER_17) {
		rxgain_type = ah->eep_ops->get_eeprom(ah, EEP_RXGAIN_TYPE);

		if (rxgain_type == AR5416_EEP_RXGAIN_13DB_BACKOFF)
			INIT_INI_ARRAY(&ah->iniModesRxGain,
			ar9280Modes_backoff_13db_rxgain_9280_2,
			ARRAY_SIZE(ar9280Modes_backoff_13db_rxgain_9280_2), 6);
		else if (rxgain_type == AR5416_EEP_RXGAIN_23DB_BACKOFF)
			INIT_INI_ARRAY(&ah->iniModesRxGain,
			ar9280Modes_backoff_23db_rxgain_9280_2,
			ARRAY_SIZE(ar9280Modes_backoff_23db_rxgain_9280_2), 6);
		else
			INIT_INI_ARRAY(&ah->iniModesRxGain,
			ar9280Modes_original_rxgain_9280_2,
			ARRAY_SIZE(ar9280Modes_original_rxgain_9280_2), 6);
	} else {
		INIT_INI_ARRAY(&ah->iniModesRxGain,
			ar9280Modes_original_rxgain_9280_2,
			ARRAY_SIZE(ar9280Modes_original_rxgain_9280_2), 6);
	}
}

static void ath9k_hw_init_txgain_ini(struct ath_hw *ah)
{
	u32 txgain_type;

	if (ah->eep_ops->get_eeprom(ah, EEP_MINOR_REV) >= AR5416_EEP_MINOR_VER_19) {
		txgain_type = ah->eep_ops->get_eeprom(ah, EEP_TXGAIN_TYPE);

		if (txgain_type == AR5416_EEP_TXGAIN_HIGH_POWER)
			INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9280Modes_high_power_tx_gain_9280_2,
			ARRAY_SIZE(ar9280Modes_high_power_tx_gain_9280_2), 6);
		else
			INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9280Modes_original_tx_gain_9280_2,
			ARRAY_SIZE(ar9280Modes_original_tx_gain_9280_2), 6);
	} else {
		INIT_INI_ARRAY(&ah->iniModesTxGain,
		ar9280Modes_original_tx_gain_9280_2,
		ARRAY_SIZE(ar9280Modes_original_tx_gain_9280_2), 6);
	}
}

static int ath9k_hw_post_init(struct ath_hw *ah)
{
	int ecode;

	if (!ath9k_hw_chip_test(ah))
		return -ENODEV;

	ecode = ath9k_hw_rf_claim(ah);
	if (ecode != 0)
		return ecode;

	ecode = ath9k_hw_eeprom_init(ah);
	if (ecode != 0)
		return ecode;

	DPRINTF(ah->ah_sc, ATH_DBG_CONFIG, "Eeprom VER: %d, REV: %d\n",
		ah->eep_ops->get_eeprom_ver(ah), ah->eep_ops->get_eeprom_rev(ah));

	ecode = ath9k_hw_rfattach(ah);
	if (ecode != 0)
		return ecode;

	if (!AR_SREV_9100(ah)) {
		ath9k_hw_ani_setup(ah);
		ath9k_hw_ani_init(ah);
	}

	return 0;
}

static bool ath9k_hw_devid_supported(u16 devid)
{
	switch (devid) {
	case AR5416_DEVID_PCI:
	case AR5416_DEVID_PCIE:
	case AR5416_AR9100_DEVID:
	case AR9160_DEVID_PCI:
	case AR9280_DEVID_PCI:
	case AR9280_DEVID_PCIE:
	case AR9285_DEVID_PCIE:
	case AR5416_DEVID_AR9287_PCI:
	case AR5416_DEVID_AR9287_PCIE:
		return true;
	default:
		break;
	}
	return false;
}

static bool ath9k_hw_macversion_supported(u32 macversion)
{
	switch (macversion) {
	case AR_SREV_VERSION_5416_PCI:
	case AR_SREV_VERSION_5416_PCIE:
	case AR_SREV_VERSION_9160:
	case AR_SREV_VERSION_9100:
	case AR_SREV_VERSION_9280:
	case AR_SREV_VERSION_9285:
	case AR_SREV_VERSION_9287:
		return true;
	/* Not yet */
	case AR_SREV_VERSION_9271:
	default:
		break;
	}
	return false;
}

static void ath9k_hw_init_cal_settings(struct ath_hw *ah)
{
	if (AR_SREV_9160_10_OR_LATER(ah)) {
		if (AR_SREV_9280_10_OR_LATER(ah)) {
			ah->iq_caldata.calData = &iq_cal_single_sample;
			ah->adcgain_caldata.calData =
				&adc_gain_cal_single_sample;
			ah->adcdc_caldata.calData =
				&adc_dc_cal_single_sample;
			ah->adcdc_calinitdata.calData =
				&adc_init_dc_cal;
		} else {
			ah->iq_caldata.calData = &iq_cal_multi_sample;
			ah->adcgain_caldata.calData =
				&adc_gain_cal_multi_sample;
			ah->adcdc_caldata.calData =
				&adc_dc_cal_multi_sample;
			ah->adcdc_calinitdata.calData =
				&adc_init_dc_cal;
		}
		ah->supp_cals = ADC_GAIN_CAL | ADC_DC_CAL | IQ_MISMATCH_CAL;
	}
}

static void ath9k_hw_init_mode_regs(struct ath_hw *ah)
{
	if (AR_SREV_9271(ah)) {
		INIT_INI_ARRAY(&ah->iniModes, ar9271Modes_9271_1_0,
			       ARRAY_SIZE(ar9271Modes_9271_1_0), 6);
		INIT_INI_ARRAY(&ah->iniCommon, ar9271Common_9271_1_0,
			       ARRAY_SIZE(ar9271Common_9271_1_0), 2);
		return;
	}

	if (AR_SREV_9287_11_OR_LATER(ah)) {
		INIT_INI_ARRAY(&ah->iniModes, ar9287Modes_9287_1_1,
				ARRAY_SIZE(ar9287Modes_9287_1_1), 6);
		INIT_INI_ARRAY(&ah->iniCommon, ar9287Common_9287_1_1,
				ARRAY_SIZE(ar9287Common_9287_1_1), 2);
		if (ah->config.pcie_clock_req)
			INIT_INI_ARRAY(&ah->iniPcieSerdes,
			ar9287PciePhy_clkreq_off_L1_9287_1_1,
			ARRAY_SIZE(ar9287PciePhy_clkreq_off_L1_9287_1_1), 2);
		else
			INIT_INI_ARRAY(&ah->iniPcieSerdes,
			ar9287PciePhy_clkreq_always_on_L1_9287_1_1,
			ARRAY_SIZE(ar9287PciePhy_clkreq_always_on_L1_9287_1_1),
					2);
	} else if (AR_SREV_9287_10_OR_LATER(ah)) {
		INIT_INI_ARRAY(&ah->iniModes, ar9287Modes_9287_1_0,
				ARRAY_SIZE(ar9287Modes_9287_1_0), 6);
		INIT_INI_ARRAY(&ah->iniCommon, ar9287Common_9287_1_0,
				ARRAY_SIZE(ar9287Common_9287_1_0), 2);

		if (ah->config.pcie_clock_req)
			INIT_INI_ARRAY(&ah->iniPcieSerdes,
			ar9287PciePhy_clkreq_off_L1_9287_1_0,
			ARRAY_SIZE(ar9287PciePhy_clkreq_off_L1_9287_1_0), 2);
		else
			INIT_INI_ARRAY(&ah->iniPcieSerdes,
			ar9287PciePhy_clkreq_always_on_L1_9287_1_0,
			ARRAY_SIZE(ar9287PciePhy_clkreq_always_on_L1_9287_1_0),
				  2);
	} else if (AR_SREV_9285_12_OR_LATER(ah)) {


		INIT_INI_ARRAY(&ah->iniModes, ar9285Modes_9285_1_2,
			       ARRAY_SIZE(ar9285Modes_9285_1_2), 6);
		INIT_INI_ARRAY(&ah->iniCommon, ar9285Common_9285_1_2,
			       ARRAY_SIZE(ar9285Common_9285_1_2), 2);

		if (ah->config.pcie_clock_req) {
			INIT_INI_ARRAY(&ah->iniPcieSerdes,
			ar9285PciePhy_clkreq_off_L1_9285_1_2,
			ARRAY_SIZE(ar9285PciePhy_clkreq_off_L1_9285_1_2), 2);
		} else {
			INIT_INI_ARRAY(&ah->iniPcieSerdes,
			ar9285PciePhy_clkreq_always_on_L1_9285_1_2,
			ARRAY_SIZE(ar9285PciePhy_clkreq_always_on_L1_9285_1_2),
				  2);
		}
	} else if (AR_SREV_9285_10_OR_LATER(ah)) {
		INIT_INI_ARRAY(&ah->iniModes, ar9285Modes_9285,
			       ARRAY_SIZE(ar9285Modes_9285), 6);
		INIT_INI_ARRAY(&ah->iniCommon, ar9285Common_9285,
			       ARRAY_SIZE(ar9285Common_9285), 2);

		if (ah->config.pcie_clock_req) {
			INIT_INI_ARRAY(&ah->iniPcieSerdes,
			ar9285PciePhy_clkreq_off_L1_9285,
			ARRAY_SIZE(ar9285PciePhy_clkreq_off_L1_9285), 2);
		} else {
			INIT_INI_ARRAY(&ah->iniPcieSerdes,
			ar9285PciePhy_clkreq_always_on_L1_9285,
			ARRAY_SIZE(ar9285PciePhy_clkreq_always_on_L1_9285), 2);
		}
	} else if (AR_SREV_9280_20_OR_LATER(ah)) {
		INIT_INI_ARRAY(&ah->iniModes, ar9280Modes_9280_2,
			       ARRAY_SIZE(ar9280Modes_9280_2), 6);
		INIT_INI_ARRAY(&ah->iniCommon, ar9280Common_9280_2,
			       ARRAY_SIZE(ar9280Common_9280_2), 2);

		if (ah->config.pcie_clock_req) {
			INIT_INI_ARRAY(&ah->iniPcieSerdes,
			       ar9280PciePhy_clkreq_off_L1_9280,
			       ARRAY_SIZE(ar9280PciePhy_clkreq_off_L1_9280),2);
		} else {
			INIT_INI_ARRAY(&ah->iniPcieSerdes,
			       ar9280PciePhy_clkreq_always_on_L1_9280,
			       ARRAY_SIZE(ar9280PciePhy_clkreq_always_on_L1_9280), 2);
		}
		INIT_INI_ARRAY(&ah->iniModesAdditional,
			       ar9280Modes_fast_clock_9280_2,
			       ARRAY_SIZE(ar9280Modes_fast_clock_9280_2), 3);
	} else if (AR_SREV_9280_10_OR_LATER(ah)) {
		INIT_INI_ARRAY(&ah->iniModes, ar9280Modes_9280,
			       ARRAY_SIZE(ar9280Modes_9280), 6);
		INIT_INI_ARRAY(&ah->iniCommon, ar9280Common_9280,
			       ARRAY_SIZE(ar9280Common_9280), 2);
	} else if (AR_SREV_9160_10_OR_LATER(ah)) {
		INIT_INI_ARRAY(&ah->iniModes, ar5416Modes_9160,
			       ARRAY_SIZE(ar5416Modes_9160), 6);
		INIT_INI_ARRAY(&ah->iniCommon, ar5416Common_9160,
			       ARRAY_SIZE(ar5416Common_9160), 2);
		INIT_INI_ARRAY(&ah->iniBank0, ar5416Bank0_9160,
			       ARRAY_SIZE(ar5416Bank0_9160), 2);
		INIT_INI_ARRAY(&ah->iniBB_RfGain, ar5416BB_RfGain_9160,
			       ARRAY_SIZE(ar5416BB_RfGain_9160), 3);
		INIT_INI_ARRAY(&ah->iniBank1, ar5416Bank1_9160,
			       ARRAY_SIZE(ar5416Bank1_9160), 2);
		INIT_INI_ARRAY(&ah->iniBank2, ar5416Bank2_9160,
			       ARRAY_SIZE(ar5416Bank2_9160), 2);
		INIT_INI_ARRAY(&ah->iniBank3, ar5416Bank3_9160,
			       ARRAY_SIZE(ar5416Bank3_9160), 3);
		INIT_INI_ARRAY(&ah->iniBank6, ar5416Bank6_9160,
			       ARRAY_SIZE(ar5416Bank6_9160), 3);
		INIT_INI_ARRAY(&ah->iniBank6TPC, ar5416Bank6TPC_9160,
			       ARRAY_SIZE(ar5416Bank6TPC_9160), 3);
		INIT_INI_ARRAY(&ah->iniBank7, ar5416Bank7_9160,
			       ARRAY_SIZE(ar5416Bank7_9160), 2);
		if (AR_SREV_9160_11(ah)) {
			INIT_INI_ARRAY(&ah->iniAddac,
				       ar5416Addac_91601_1,
				       ARRAY_SIZE(ar5416Addac_91601_1), 2);
		} else {
			INIT_INI_ARRAY(&ah->iniAddac, ar5416Addac_9160,
				       ARRAY_SIZE(ar5416Addac_9160), 2);
		}
	} else if (AR_SREV_9100_OR_LATER(ah)) {
		INIT_INI_ARRAY(&ah->iniModes, ar5416Modes_9100,
			       ARRAY_SIZE(ar5416Modes_9100), 6);
		INIT_INI_ARRAY(&ah->iniCommon, ar5416Common_9100,
			       ARRAY_SIZE(ar5416Common_9100), 2);
		INIT_INI_ARRAY(&ah->iniBank0, ar5416Bank0_9100,
			       ARRAY_SIZE(ar5416Bank0_9100), 2);
		INIT_INI_ARRAY(&ah->iniBB_RfGain, ar5416BB_RfGain_9100,
			       ARRAY_SIZE(ar5416BB_RfGain_9100), 3);
		INIT_INI_ARRAY(&ah->iniBank1, ar5416Bank1_9100,
			       ARRAY_SIZE(ar5416Bank1_9100), 2);
		INIT_INI_ARRAY(&ah->iniBank2, ar5416Bank2_9100,
			       ARRAY_SIZE(ar5416Bank2_9100), 2);
		INIT_INI_ARRAY(&ah->iniBank3, ar5416Bank3_9100,
			       ARRAY_SIZE(ar5416Bank3_9100), 3);
		INIT_INI_ARRAY(&ah->iniBank6, ar5416Bank6_9100,
			       ARRAY_SIZE(ar5416Bank6_9100), 3);
		INIT_INI_ARRAY(&ah->iniBank6TPC, ar5416Bank6TPC_9100,
			       ARRAY_SIZE(ar5416Bank6TPC_9100), 3);
		INIT_INI_ARRAY(&ah->iniBank7, ar5416Bank7_9100,
			       ARRAY_SIZE(ar5416Bank7_9100), 2);
		INIT_INI_ARRAY(&ah->iniAddac, ar5416Addac_9100,
			       ARRAY_SIZE(ar5416Addac_9100), 2);
	} else {
		INIT_INI_ARRAY(&ah->iniModes, ar5416Modes,
			       ARRAY_SIZE(ar5416Modes), 6);
		INIT_INI_ARRAY(&ah->iniCommon, ar5416Common,
			       ARRAY_SIZE(ar5416Common), 2);
		INIT_INI_ARRAY(&ah->iniBank0, ar5416Bank0,
			       ARRAY_SIZE(ar5416Bank0), 2);
		INIT_INI_ARRAY(&ah->iniBB_RfGain, ar5416BB_RfGain,
			       ARRAY_SIZE(ar5416BB_RfGain), 3);
		INIT_INI_ARRAY(&ah->iniBank1, ar5416Bank1,
			       ARRAY_SIZE(ar5416Bank1), 2);
		INIT_INI_ARRAY(&ah->iniBank2, ar5416Bank2,
			       ARRAY_SIZE(ar5416Bank2), 2);
		INIT_INI_ARRAY(&ah->iniBank3, ar5416Bank3,
			       ARRAY_SIZE(ar5416Bank3), 3);
		INIT_INI_ARRAY(&ah->iniBank6, ar5416Bank6,
			       ARRAY_SIZE(ar5416Bank6), 3);
		INIT_INI_ARRAY(&ah->iniBank6TPC, ar5416Bank6TPC,
			       ARRAY_SIZE(ar5416Bank6TPC), 3);
		INIT_INI_ARRAY(&ah->iniBank7, ar5416Bank7,
			       ARRAY_SIZE(ar5416Bank7), 2);
		INIT_INI_ARRAY(&ah->iniAddac, ar5416Addac,
			       ARRAY_SIZE(ar5416Addac), 2);
	}
}

static void ath9k_hw_init_mode_gain_regs(struct ath_hw *ah)
{
	if (AR_SREV_9287_11_OR_LATER(ah))
		INIT_INI_ARRAY(&ah->iniModesRxGain,
		ar9287Modes_rx_gain_9287_1_1,
		ARRAY_SIZE(ar9287Modes_rx_gain_9287_1_1), 6);
	else if (AR_SREV_9287_10(ah))
		INIT_INI_ARRAY(&ah->iniModesRxGain,
		ar9287Modes_rx_gain_9287_1_0,
		ARRAY_SIZE(ar9287Modes_rx_gain_9287_1_0), 6);
	else if (AR_SREV_9280_20(ah))
		ath9k_hw_init_rxgain_ini(ah);

	if (AR_SREV_9287_11_OR_LATER(ah)) {
		INIT_INI_ARRAY(&ah->iniModesTxGain,
		ar9287Modes_tx_gain_9287_1_1,
		ARRAY_SIZE(ar9287Modes_tx_gain_9287_1_1), 6);
	} else if (AR_SREV_9287_10(ah)) {
		INIT_INI_ARRAY(&ah->iniModesTxGain,
		ar9287Modes_tx_gain_9287_1_0,
		ARRAY_SIZE(ar9287Modes_tx_gain_9287_1_0), 6);
	} else if (AR_SREV_9280_20(ah)) {
		ath9k_hw_init_txgain_ini(ah);
	} else if (AR_SREV_9285_12_OR_LATER(ah)) {
		u32 txgain_type = ah->eep_ops->get_eeprom(ah, EEP_TXGAIN_TYPE);

		/* txgain table */
		if (txgain_type == AR5416_EEP_TXGAIN_HIGH_POWER) {
			INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9285Modes_high_power_tx_gain_9285_1_2,
			ARRAY_SIZE(ar9285Modes_high_power_tx_gain_9285_1_2), 6);
		} else {
			INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9285Modes_original_tx_gain_9285_1_2,
			ARRAY_SIZE(ar9285Modes_original_tx_gain_9285_1_2), 6);
		}

	}
}

static void ath9k_hw_init_11a_eeprom_fix(struct ath_hw *ah)
{
	u32 i, j;

	if ((ah->hw_version.devid == AR9280_DEVID_PCI) &&
	    test_bit(ATH9K_MODE_11A, ah->caps.wireless_modes)) {

		/* EEPROM Fixup */
		for (i = 0; i < ah->iniModes.ia_rows; i++) {
			u32 reg = INI_RA(&ah->iniModes, i, 0);

			for (j = 1; j < ah->iniModes.ia_columns; j++) {
				u32 val = INI_RA(&ah->iniModes, i, j);

				INI_RA(&ah->iniModes, i, j) =
					ath9k_hw_ini_fixup(ah,
							   &ah->eeprom.def,
							   reg, val);
			}
		}
	}
}

int ath9k_hw_init(struct ath_hw *ah)
{
	int r = 0;

	if (!ath9k_hw_devid_supported(ah->hw_version.devid))
		return -EOPNOTSUPP;

	ath9k_hw_init_defaults(ah);
	ath9k_hw_init_config(ah);

	if (!ath9k_hw_set_reset_reg(ah, ATH9K_RESET_POWER_ON)) {
		DPRINTF(ah->ah_sc, ATH_DBG_FATAL, "Couldn't reset chip\n");
		return -EIO;
	}

	if (!ath9k_hw_setpower(ah, ATH9K_PM_AWAKE)) {
		DPRINTF(ah->ah_sc, ATH_DBG_FATAL, "Couldn't wakeup chip\n");
		return -EIO;
	}

	if (ah->config.serialize_regmode == SER_REG_MODE_AUTO) {
		if (ah->hw_version.macVersion == AR_SREV_VERSION_5416_PCI ||
		    (AR_SREV_9280(ah) && !ah->is_pciexpress)) {
			ah->config.serialize_regmode =
				SER_REG_MODE_ON;
		} else {
			ah->config.serialize_regmode =
				SER_REG_MODE_OFF;
		}
	}

	DPRINTF(ah->ah_sc, ATH_DBG_RESET, "serialize_regmode is %d\n",
		ah->config.serialize_regmode);

	if (AR_SREV_9285(ah) || AR_SREV_9271(ah))
		ah->config.max_txtrig_level = MAX_TX_FIFO_THRESHOLD >> 1;
	else
		ah->config.max_txtrig_level = MAX_TX_FIFO_THRESHOLD;

	if (!ath9k_hw_macversion_supported(ah->hw_version.macVersion)) {
		DPRINTF(ah->ah_sc, ATH_DBG_FATAL,
			"Mac Chip Rev 0x%02x.%x is not supported by "
			"this driver\n", ah->hw_version.macVersion,
			ah->hw_version.macRev);
		return -EOPNOTSUPP;
	}

	if (AR_SREV_9100(ah)) {
		ah->iq_caldata.calData = &iq_cal_multi_sample;
		ah->supp_cals = IQ_MISMATCH_CAL;
		ah->is_pciexpress = false;
	}

	if (AR_SREV_9271(ah))
		ah->is_pciexpress = false;

	ah->hw_version.phyRev = REG_READ(ah, AR_PHY_CHIP_ID);

	ath9k_hw_init_cal_settings(ah);

	ah->ani_function = ATH9K_ANI_ALL;
	if (AR_SREV_9280_10_OR_LATER(ah))
		ah->ani_function &= ~ATH9K_ANI_NOISE_IMMUNITY_LEVEL;

	ath9k_hw_init_mode_regs(ah);

	if (ah->is_pciexpress)
		ath9k_hw_configpcipowersave(ah, 0, 0);
	else
		ath9k_hw_disablepcie(ah);

	r = ath9k_hw_post_init(ah);
	if (r)
		return r;

	ath9k_hw_init_mode_gain_regs(ah);
	ath9k_hw_fill_cap_info(ah);
	ath9k_hw_init_11a_eeprom_fix(ah);

	r = ath9k_hw_init_macaddr(ah);
	if (r) {
		DPRINTF(ah->ah_sc, ATH_DBG_FATAL,
			"Failed to initialize MAC address\n");
		return r;
	}

	if (AR_SREV_9285(ah) || AR_SREV_9271(ah))
		ah->tx_trig_level = (AR_FTRIG_256B >> AR_FTRIG_S);
	else
		ah->tx_trig_level = (AR_FTRIG_512B >> AR_FTRIG_S);

	ath9k_init_nfcal_hist_buffer(ah);

	return 0;
}

static void ath9k_hw_init_bb(struct ath_hw *ah,
			     struct ath9k_channel *chan)
{
	u32 synthDelay;

	synthDelay = REG_READ(ah, AR_PHY_RX_DELAY) & AR_PHY_RX_DELAY_DELAY;
	if (IS_CHAN_B(chan))
		synthDelay = (4 * synthDelay) / 22;
	else
		synthDelay /= 10;

	REG_WRITE(ah, AR_PHY_ACTIVE, AR_PHY_ACTIVE_EN);

	udelay(synthDelay + BASE_ACTIVATE_DELAY);
}

static void ath9k_hw_init_qos(struct ath_hw *ah)
{
	REG_WRITE(ah, AR_MIC_QOS_CONTROL, 0x100aa);
	REG_WRITE(ah, AR_MIC_QOS_SELECT, 0x3210);

	REG_WRITE(ah, AR_QOS_NO_ACK,
		  SM(2, AR_QOS_NO_ACK_TWO_BIT) |
		  SM(5, AR_QOS_NO_ACK_BIT_OFF) |
		  SM(0, AR_QOS_NO_ACK_BYTE_OFF));

	REG_WRITE(ah, AR_TXOP_X, AR_TXOP_X_VAL);
	REG_WRITE(ah, AR_TXOP_0_3, 0xFFFFFFFF);
	REG_WRITE(ah, AR_TXOP_4_7, 0xFFFFFFFF);
	REG_WRITE(ah, AR_TXOP_8_11, 0xFFFFFFFF);
	REG_WRITE(ah, AR_TXOP_12_15, 0xFFFFFFFF);
}

static void ath9k_hw_init_pll(struct ath_hw *ah,
			      struct ath9k_channel *chan)
{
	u32 pll;

	if (AR_SREV_9100(ah)) {
		if (chan && IS_CHAN_5GHZ(chan))
			pll = 0x1450;
		else
			pll = 0x1458;
	} else {
		if (AR_SREV_9280_10_OR_LATER(ah)) {
			pll = SM(0x5, AR_RTC_9160_PLL_REFDIV);

			if (chan && IS_CHAN_HALF_RATE(chan))
				pll |= SM(0x1, AR_RTC_9160_PLL_CLKSEL);
			else if (chan && IS_CHAN_QUARTER_RATE(chan))
				pll |= SM(0x2, AR_RTC_9160_PLL_CLKSEL);

			if (chan && IS_CHAN_5GHZ(chan)) {
				pll |= SM(0x28, AR_RTC_9160_PLL_DIV);


				if (AR_SREV_9280_20(ah)) {
					if (((chan->channel % 20) == 0)
					    || ((chan->channel % 10) == 0))
						pll = 0x2850;
					else
						pll = 0x142c;
				}
			} else {
				pll |= SM(0x2c, AR_RTC_9160_PLL_DIV);
			}

		} else if (AR_SREV_9160_10_OR_LATER(ah)) {

			pll = SM(0x5, AR_RTC_9160_PLL_REFDIV);

			if (chan && IS_CHAN_HALF_RATE(chan))
				pll |= SM(0x1, AR_RTC_9160_PLL_CLKSEL);
			else if (chan && IS_CHAN_QUARTER_RATE(chan))
				pll |= SM(0x2, AR_RTC_9160_PLL_CLKSEL);

			if (chan && IS_CHAN_5GHZ(chan))
				pll |= SM(0x50, AR_RTC_9160_PLL_DIV);
			else
				pll |= SM(0x58, AR_RTC_9160_PLL_DIV);
		} else {
			pll = AR_RTC_PLL_REFDIV_5 | AR_RTC_PLL_DIV2;

			if (chan && IS_CHAN_HALF_RATE(chan))
				pll |= SM(0x1, AR_RTC_PLL_CLKSEL);
			else if (chan && IS_CHAN_QUARTER_RATE(chan))
				pll |= SM(0x2, AR_RTC_PLL_CLKSEL);

			if (chan && IS_CHAN_5GHZ(chan))
				pll |= SM(0xa, AR_RTC_PLL_DIV);
			else
				pll |= SM(0xb, AR_RTC_PLL_DIV);
		}
	}
	REG_WRITE(ah, AR_RTC_PLL_CONTROL, pll);

	udelay(RTC_PLL_SETTLE_DELAY);

	REG_WRITE(ah, AR_RTC_SLEEP_CLK, AR_RTC_FORCE_DERIVED_CLK);
}

static void ath9k_hw_init_chain_masks(struct ath_hw *ah)
{
	int rx_chainmask, tx_chainmask;

	rx_chainmask = ah->rxchainmask;
	tx_chainmask = ah->txchainmask;

	switch (rx_chainmask) {
	case 0x5:
		REG_SET_BIT(ah, AR_PHY_ANALOG_SWAP,
			    AR_PHY_SWAP_ALT_CHAIN);
	case 0x3:
		if (((ah)->hw_version.macVersion <= AR_SREV_VERSION_9160)) {
			REG_WRITE(ah, AR_PHY_RX_CHAINMASK, 0x7);
			REG_WRITE(ah, AR_PHY_CAL_CHAINMASK, 0x7);
			break;
		}
	case 0x1:
	case 0x2:
	case 0x7:
		REG_WRITE(ah, AR_PHY_RX_CHAINMASK, rx_chainmask);
		REG_WRITE(ah, AR_PHY_CAL_CHAINMASK, rx_chainmask);
		break;
	default:
		break;
	}

	REG_WRITE(ah, AR_SELFGEN_MASK, tx_chainmask);
	if (tx_chainmask == 0x5) {
		REG_SET_BIT(ah, AR_PHY_ANALOG_SWAP,
			    AR_PHY_SWAP_ALT_CHAIN);
	}
	if (AR_SREV_9100(ah))
		REG_WRITE(ah, AR_PHY_ANALOG_SWAP,
			  REG_READ(ah, AR_PHY_ANALOG_SWAP) | 0x00000001);
}

static void ath9k_hw_init_interrupt_masks(struct ath_hw *ah,
					  enum nl80211_iftype opmode)
{
	ah->mask_reg = AR_IMR_TXERR |
		AR_IMR_TXURN |
		AR_IMR_RXERR |
		AR_IMR_RXORN |
		AR_IMR_BCNMISC;

	if (ah->config.intr_mitigation)
		ah->mask_reg |= AR_IMR_RXINTM | AR_IMR_RXMINTR;
	else
		ah->mask_reg |= AR_IMR_RXOK;

	ah->mask_reg |= AR_IMR_TXOK;

	if (opmode == NL80211_IFTYPE_AP)
		ah->mask_reg |= AR_IMR_MIB;

	REG_WRITE(ah, AR_IMR, ah->mask_reg);
	REG_WRITE(ah, AR_IMR_S2, REG_READ(ah, AR_IMR_S2) | AR_IMR_S2_GTT);

	if (!AR_SREV_9100(ah)) {
		REG_WRITE(ah, AR_INTR_SYNC_CAUSE, 0xFFFFFFFF);
		REG_WRITE(ah, AR_INTR_SYNC_ENABLE, AR_INTR_SYNC_DEFAULT);
		REG_WRITE(ah, AR_INTR_SYNC_MASK, 0);
	}
}

static bool ath9k_hw_set_ack_timeout(struct ath_hw *ah, u32 us)
{
	if (us > ath9k_hw_mac_to_usec(ah, MS(0xffffffff, AR_TIME_OUT_ACK))) {
		DPRINTF(ah->ah_sc, ATH_DBG_RESET, "bad ack timeout %u\n", us);
		ah->acktimeout = (u32) -1;
		return false;
	} else {
		REG_RMW_FIELD(ah, AR_TIME_OUT,
			      AR_TIME_OUT_ACK, ath9k_hw_mac_to_clks(ah, us));
		ah->acktimeout = us;
		return true;
	}
}

static bool ath9k_hw_set_cts_timeout(struct ath_hw *ah, u32 us)
{
	if (us > ath9k_hw_mac_to_usec(ah, MS(0xffffffff, AR_TIME_OUT_CTS))) {
		DPRINTF(ah->ah_sc, ATH_DBG_RESET, "bad cts timeout %u\n", us);
		ah->ctstimeout = (u32) -1;
		return false;
	} else {
		REG_RMW_FIELD(ah, AR_TIME_OUT,
			      AR_TIME_OUT_CTS, ath9k_hw_mac_to_clks(ah, us));
		ah->ctstimeout = us;
		return true;
	}
}

static bool ath9k_hw_set_global_txtimeout(struct ath_hw *ah, u32 tu)
{
	if (tu > 0xFFFF) {
		DPRINTF(ah->ah_sc, ATH_DBG_XMIT,
			"bad global tx timeout %u\n", tu);
		ah->globaltxtimeout = (u32) -1;
		return false;
	} else {
		REG_RMW_FIELD(ah, AR_GTXTO, AR_GTXTO_TIMEOUT_LIMIT, tu);
		ah->globaltxtimeout = tu;
		return true;
	}
}

static void ath9k_hw_init_user_settings(struct ath_hw *ah)
{
	DPRINTF(ah->ah_sc, ATH_DBG_RESET, "ah->misc_mode 0x%x\n",
		ah->misc_mode);

	if (ah->misc_mode != 0)
		REG_WRITE(ah, AR_PCU_MISC,
			  REG_READ(ah, AR_PCU_MISC) | ah->misc_mode);
	if (ah->slottime != (u32) -1)
		ath9k_hw_setslottime(ah, ah->slottime);
	if (ah->acktimeout != (u32) -1)
		ath9k_hw_set_ack_timeout(ah, ah->acktimeout);
	if (ah->ctstimeout != (u32) -1)
		ath9k_hw_set_cts_timeout(ah, ah->ctstimeout);
	if (ah->globaltxtimeout != (u32) -1)
		ath9k_hw_set_global_txtimeout(ah, ah->globaltxtimeout);
}

const char *ath9k_hw_probe(u16 vendorid, u16 devid)
{
	return vendorid == ATHEROS_VENDOR_ID ?
		ath9k_hw_devname(devid) : NULL;
}

void ath9k_hw_detach(struct ath_hw *ah)
{
	if (!AR_SREV_9100(ah))
		ath9k_hw_ani_disable(ah);

	ath9k_hw_rf_free(ah);
	ath9k_hw_setpower(ah, ATH9K_PM_FULL_SLEEP);
	kfree(ah);
	ah = NULL;
}

/*******/
/* INI */
/*******/

static void ath9k_hw_override_ini(struct ath_hw *ah,
				  struct ath9k_channel *chan)
{
	u32 val;

	if (AR_SREV_9271(ah)) {
		/*
		 * Enable spectral scan to solution for issues with stuck
		 * beacons on AR9271 1.0. The beacon stuck issue is not seeon on
		 * AR9271 1.1
		 */
		if (AR_SREV_9271_10(ah)) {
			val = REG_READ(ah, AR_PHY_SPECTRAL_SCAN) | AR_PHY_SPECTRAL_SCAN_ENABLE;
			REG_WRITE(ah, AR_PHY_SPECTRAL_SCAN, val);
		}
		else if (AR_SREV_9271_11(ah))
			/*
			 * change AR_PHY_RF_CTL3 setting to fix MAC issue
			 * present on AR9271 1.1
			 */
			REG_WRITE(ah, AR_PHY_RF_CTL3, 0x3a020001);
		return;
	}

	/*
	 * Set the RX_ABORT and RX_DIS and clear if off only after
	 * RXE is set for MAC. This prevents frames with corrupted
	 * descriptor status.
	 */
	REG_SET_BIT(ah, AR_DIAG_SW, (AR_DIAG_RX_DIS | AR_DIAG_RX_ABORT));

	if (AR_SREV_9280_10_OR_LATER(ah)) {
		val = REG_READ(ah, AR_PCU_MISC_MODE2) &
			       (~AR_PCU_MISC_MODE2_HWWAR1);

		if (AR_SREV_9287_10_OR_LATER(ah))
			val = val & (~AR_PCU_MISC_MODE2_HWWAR2);

		REG_WRITE(ah, AR_PCU_MISC_MODE2, val);
	}

	if (!AR_SREV_5416_20_OR_LATER(ah) ||
	    AR_SREV_9280_10_OR_LATER(ah))
		return;
	/*
	 * Disable BB clock gating
	 * Necessary to avoid issues on AR5416 2.0
	 */
	REG_WRITE(ah, 0x9800 + (651 << 2), 0x11);
}

static u32 ath9k_hw_def_ini_fixup(struct ath_hw *ah,
			      struct ar5416_eeprom_def *pEepData,
			      u32 reg, u32 value)
{
	struct base_eep_header *pBase = &(pEepData->baseEepHeader);

	switch (ah->hw_version.devid) {
	case AR9280_DEVID_PCI:
		if (reg == 0x7894) {
			DPRINTF(ah->ah_sc, ATH_DBG_EEPROM,
				"ini VAL: %x  EEPROM: %x\n", value,
				(pBase->version & 0xff));

			if ((pBase->version & 0xff) > 0x0a) {
				DPRINTF(ah->ah_sc, ATH_DBG_EEPROM,
					"PWDCLKIND: %d\n",
					pBase->pwdclkind);
				value &= ~AR_AN_TOP2_PWDCLKIND;
				value |= AR_AN_TOP2_PWDCLKIND &
					(pBase->pwdclkind << AR_AN_TOP2_PWDCLKIND_S);
			} else {
				DPRINTF(ah->ah_sc, ATH_DBG_EEPROM,
					"PWDCLKIND Earlier Rev\n");
			}

			DPRINTF(ah->ah_sc, ATH_DBG_EEPROM,
				"final ini VAL: %x\n", value);
		}
		break;
	}

	return value;
}

static u32 ath9k_hw_ini_fixup(struct ath_hw *ah,
			      struct ar5416_eeprom_def *pEepData,
			      u32 reg, u32 value)
{
	if (ah->eep_map == EEP_MAP_4KBITS)
		return value;
	else
		return ath9k_hw_def_ini_fixup(ah, pEepData, reg, value);
}

static void ath9k_olc_init(struct ath_hw *ah)
{
	u32 i;

	if (OLC_FOR_AR9287_10_LATER) {
		REG_SET_BIT(ah, AR_PHY_TX_PWRCTRL9,
				AR_PHY_TX_PWRCTRL9_RES_DC_REMOVAL);
		ath9k_hw_analog_shift_rmw(ah, AR9287_AN_TXPC0,
				AR9287_AN_TXPC0_TXPCMODE,
				AR9287_AN_TXPC0_TXPCMODE_S,
				AR9287_AN_TXPC0_TXPCMODE_TEMPSENSE);
		udelay(100);
	} else {
		for (i = 0; i < AR9280_TX_GAIN_TABLE_SIZE; i++)
			ah->originalGain[i] =
				MS(REG_READ(ah, AR_PHY_TX_GAIN_TBL1 + i * 4),
						AR_PHY_TX_GAIN);
		ah->PDADCdelta = 0;
	}
}

static u32 ath9k_regd_get_ctl(struct ath_regulatory *reg,
			      struct ath9k_channel *chan)
{
	u32 ctl = ath_regd_get_band_ctl(reg, chan->chan->band);

	if (IS_CHAN_B(chan))
		ctl |= CTL_11B;
	else if (IS_CHAN_G(chan))
		ctl |= CTL_11G;
	else
		ctl |= CTL_11A;

	return ctl;
}

static int ath9k_hw_process_ini(struct ath_hw *ah,
				struct ath9k_channel *chan,
				enum ath9k_ht_macmode macmode)
{
	struct ath_regulatory *regulatory = ath9k_hw_regulatory(ah);
	int i, regWrites = 0;
	struct ieee80211_channel *channel = chan->chan;
	u32 modesIndex, freqIndex;

	switch (chan->chanmode) {
	case CHANNEL_A:
	case CHANNEL_A_HT20:
		modesIndex = 1;
		freqIndex = 1;
		break;
	case CHANNEL_A_HT40PLUS:
	case CHANNEL_A_HT40MINUS:
		modesIndex = 2;
		freqIndex = 1;
		break;
	case CHANNEL_G:
	case CHANNEL_G_HT20:
	case CHANNEL_B:
		modesIndex = 4;
		freqIndex = 2;
		break;
	case CHANNEL_G_HT40PLUS:
	case CHANNEL_G_HT40MINUS:
		modesIndex = 3;
		freqIndex = 2;
		break;

	default:
		return -EINVAL;
	}

	REG_WRITE(ah, AR_PHY(0), 0x00000007);
	REG_WRITE(ah, AR_PHY_ADC_SERIAL_CTL, AR_PHY_SEL_EXTERNAL_RADIO);
	ah->eep_ops->set_addac(ah, chan);

	if (AR_SREV_5416_22_OR_LATER(ah)) {
		REG_WRITE_ARRAY(&ah->iniAddac, 1, regWrites);
	} else {
		struct ar5416IniArray temp;
		u32 addacSize =
			sizeof(u32) * ah->iniAddac.ia_rows *
			ah->iniAddac.ia_columns;

		memcpy(ah->addac5416_21,
		       ah->iniAddac.ia_array, addacSize);

		(ah->addac5416_21)[31 * ah->iniAddac.ia_columns + 1] = 0;

		temp.ia_array = ah->addac5416_21;
		temp.ia_columns = ah->iniAddac.ia_columns;
		temp.ia_rows = ah->iniAddac.ia_rows;
		REG_WRITE_ARRAY(&temp, 1, regWrites);
	}

	REG_WRITE(ah, AR_PHY_ADC_SERIAL_CTL, AR_PHY_SEL_INTERNAL_ADDAC);

	for (i = 0; i < ah->iniModes.ia_rows; i++) {
		u32 reg = INI_RA(&ah->iniModes, i, 0);
		u32 val = INI_RA(&ah->iniModes, i, modesIndex);

		REG_WRITE(ah, reg, val);

		if (reg >= 0x7800 && reg < 0x78a0
		    && ah->config.analog_shiftreg) {
			udelay(100);
		}

		DO_DELAY(regWrites);
	}

	if (AR_SREV_9280(ah) || AR_SREV_9287_10_OR_LATER(ah))
		REG_WRITE_ARRAY(&ah->iniModesRxGain, modesIndex, regWrites);

	if (AR_SREV_9280(ah) || AR_SREV_9285_12_OR_LATER(ah) ||
	    AR_SREV_9287_10_OR_LATER(ah))
		REG_WRITE_ARRAY(&ah->iniModesTxGain, modesIndex, regWrites);

	for (i = 0; i < ah->iniCommon.ia_rows; i++) {
		u32 reg = INI_RA(&ah->iniCommon, i, 0);
		u32 val = INI_RA(&ah->iniCommon, i, 1);

		REG_WRITE(ah, reg, val);

		if (reg >= 0x7800 && reg < 0x78a0
		    && ah->config.analog_shiftreg) {
			udelay(100);
		}

		DO_DELAY(regWrites);
	}

	ath9k_hw_write_regs(ah, modesIndex, freqIndex, regWrites);

	if (AR_SREV_9280_20(ah) && IS_CHAN_A_5MHZ_SPACED(chan)) {
		REG_WRITE_ARRAY(&ah->iniModesAdditional, modesIndex,
				regWrites);
	}

	ath9k_hw_override_ini(ah, chan);
	ath9k_hw_set_regs(ah, chan, macmode);
	ath9k_hw_init_chain_masks(ah);

	if (OLC_FOR_AR9280_20_LATER)
		ath9k_olc_init(ah);

	ah->eep_ops->set_txpower(ah, chan,
				 ath9k_regd_get_ctl(regulatory, chan),
				 channel->max_antenna_gain * 2,
				 channel->max_power * 2,
				 min((u32) MAX_RATE_POWER,
				 (u32) regulatory->power_limit));

	if (!ath9k_hw_set_rf_regs(ah, chan, freqIndex)) {
		DPRINTF(ah->ah_sc, ATH_DBG_FATAL,
			"ar5416SetRfRegs failed\n");
		return -EIO;
	}

	return 0;
}

/****************************************/
/* Reset and Channel Switching Routines */
/****************************************/

static void ath9k_hw_set_rfmode(struct ath_hw *ah, struct ath9k_channel *chan)
{
	u32 rfMode = 0;

	if (chan == NULL)
		return;

	rfMode |= (IS_CHAN_B(chan) || IS_CHAN_G(chan))
		? AR_PHY_MODE_DYNAMIC : AR_PHY_MODE_OFDM;

	if (!AR_SREV_9280_10_OR_LATER(ah))
		rfMode |= (IS_CHAN_5GHZ(chan)) ?
			AR_PHY_MODE_RF5GHZ : AR_PHY_MODE_RF2GHZ;

	if (AR_SREV_9280_20(ah) && IS_CHAN_A_5MHZ_SPACED(chan))
		rfMode |= (AR_PHY_MODE_DYNAMIC | AR_PHY_MODE_DYN_CCK_DISABLE);

	REG_WRITE(ah, AR_PHY_MODE, rfMode);
}

static void ath9k_hw_mark_phy_inactive(struct ath_hw *ah)
{
	REG_WRITE(ah, AR_PHY_ACTIVE, AR_PHY_ACTIVE_DIS);
}

static inline void ath9k_hw_set_dma(struct ath_hw *ah)
{
	u32 regval;

	/*
	 * set AHB_MODE not to do cacheline prefetches
	*/
	regval = REG_READ(ah, AR_AHB_MODE);
	REG_WRITE(ah, AR_AHB_MODE, regval | AR_AHB_PREFETCH_RD_EN);

	/*
	 * let mac dma reads be in 128 byte chunks
	 */
	regval = REG_READ(ah, AR_TXCFG) & ~AR_TXCFG_DMASZ_MASK;
	REG_WRITE(ah, AR_TXCFG, regval | AR_TXCFG_DMASZ_128B);

	/*
	 * Restore TX Trigger Level to its pre-reset value.
	 * The initial value depends on whether aggregation is enabled, and is
	 * adjusted whenever underruns are detected.
	 */
	REG_RMW_FIELD(ah, AR_TXCFG, AR_FTRIG, ah->tx_trig_level);

	/*
	 * let mac dma writes be in 128 byte chunks
	 */
	regval = REG_READ(ah, AR_RXCFG) & ~AR_RXCFG_DMASZ_MASK;
	REG_WRITE(ah, AR_RXCFG, regval | AR_RXCFG_DMASZ_128B);

	/*
	 * Setup receive FIFO threshold to hold off TX activities
	 */
	REG_WRITE(ah, AR_RXFIFO_CFG, 0x200);

	/*
	 * reduce the number of usable entries in PCU TXBUF to avoid
	 * wrap around issues.
	 */
	if (AR_SREV_9285(ah)) {
		/* For AR9285 the number of Fifos are reduced to half.
		 * So set the usable tx buf size also to half to
		 * avoid data/delimiter underruns
		 */
		REG_WRITE(ah, AR_PCU_TXBUF_CTRL,
			  AR_9285_PCU_TXBUF_CTRL_USABLE_SIZE);
	} else if (!AR_SREV_9271(ah)) {
		REG_WRITE(ah, AR_PCU_TXBUF_CTRL,
			  AR_PCU_TXBUF_CTRL_USABLE_SIZE);
	}
}

static void ath9k_hw_set_operating_mode(struct ath_hw *ah, int opmode)
{
	u32 val;

	val = REG_READ(ah, AR_STA_ID1);
	val &= ~(AR_STA_ID1_STA_AP | AR_STA_ID1_ADHOC);
	switch (opmode) {
	case NL80211_IFTYPE_AP:
		REG_WRITE(ah, AR_STA_ID1, val | AR_STA_ID1_STA_AP
			  | AR_STA_ID1_KSRCH_MODE);
		REG_CLR_BIT(ah, AR_CFG, AR_CFG_AP_ADHOC_INDICATION);
		break;
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_MESH_POINT:
		REG_WRITE(ah, AR_STA_ID1, val | AR_STA_ID1_ADHOC
			  | AR_STA_ID1_KSRCH_MODE);
		REG_SET_BIT(ah, AR_CFG, AR_CFG_AP_ADHOC_INDICATION);
		break;
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_MONITOR:
		REG_WRITE(ah, AR_STA_ID1, val | AR_STA_ID1_KSRCH_MODE);
		break;
	}
}

static inline void ath9k_hw_get_delta_slope_vals(struct ath_hw *ah,
						 u32 coef_scaled,
						 u32 *coef_mantissa,
						 u32 *coef_exponent)
{
	u32 coef_exp, coef_man;

	for (coef_exp = 31; coef_exp > 0; coef_exp--)
		if ((coef_scaled >> coef_exp) & 0x1)
			break;

	coef_exp = 14 - (coef_exp - COEF_SCALE_S);

	coef_man = coef_scaled + (1 << (COEF_SCALE_S - coef_exp - 1));

	*coef_mantissa = coef_man >> (COEF_SCALE_S - coef_exp);
	*coef_exponent = coef_exp - 16;
}

static void ath9k_hw_set_delta_slope(struct ath_hw *ah,
				     struct ath9k_channel *chan)
{
	u32 coef_scaled, ds_coef_exp, ds_coef_man;
	u32 clockMhzScaled = 0x64000000;
	struct chan_centers centers;

	if (IS_CHAN_HALF_RATE(chan))
		clockMhzScaled = clockMhzScaled >> 1;
	else if (IS_CHAN_QUARTER_RATE(chan))
		clockMhzScaled = clockMhzScaled >> 2;

	ath9k_hw_get_channel_centers(ah, chan, &centers);
	coef_scaled = clockMhzScaled / centers.synth_center;

	ath9k_hw_get_delta_slope_vals(ah, coef_scaled, &ds_coef_man,
				      &ds_coef_exp);

	REG_RMW_FIELD(ah, AR_PHY_TIMING3,
		      AR_PHY_TIMING3_DSC_MAN, ds_coef_man);
	REG_RMW_FIELD(ah, AR_PHY_TIMING3,
		      AR_PHY_TIMING3_DSC_EXP, ds_coef_exp);

	coef_scaled = (9 * coef_scaled) / 10;

	ath9k_hw_get_delta_slope_vals(ah, coef_scaled, &ds_coef_man,
				      &ds_coef_exp);

	REG_RMW_FIELD(ah, AR_PHY_HALFGI,
		      AR_PHY_HALFGI_DSC_MAN, ds_coef_man);
	REG_RMW_FIELD(ah, AR_PHY_HALFGI,
		      AR_PHY_HALFGI_DSC_EXP, ds_coef_exp);
}

static bool ath9k_hw_set_reset(struct ath_hw *ah, int type)
{
	u32 rst_flags;
	u32 tmpReg;

	if (AR_SREV_9100(ah)) {
		u32 val = REG_READ(ah, AR_RTC_DERIVED_CLK);
		val &= ~AR_RTC_DERIVED_CLK_PERIOD;
		val |= SM(1, AR_RTC_DERIVED_CLK_PERIOD);
		REG_WRITE(ah, AR_RTC_DERIVED_CLK, val);
		(void)REG_READ(ah, AR_RTC_DERIVED_CLK);
	}

	REG_WRITE(ah, AR_RTC_FORCE_WAKE, AR_RTC_FORCE_WAKE_EN |
		  AR_RTC_FORCE_WAKE_ON_INT);

	if (AR_SREV_9100(ah)) {
		rst_flags = AR_RTC_RC_MAC_WARM | AR_RTC_RC_MAC_COLD |
			AR_RTC_RC_COLD_RESET | AR_RTC_RC_WARM_RESET;
	} else {
		tmpReg = REG_READ(ah, AR_INTR_SYNC_CAUSE);
		if (tmpReg &
		    (AR_INTR_SYNC_LOCAL_TIMEOUT |
		     AR_INTR_SYNC_RADM_CPL_TIMEOUT)) {
			REG_WRITE(ah, AR_INTR_SYNC_ENABLE, 0);
			REG_WRITE(ah, AR_RC, AR_RC_AHB | AR_RC_HOSTIF);
		} else {
			REG_WRITE(ah, AR_RC, AR_RC_AHB);
		}

		rst_flags = AR_RTC_RC_MAC_WARM;
		if (type == ATH9K_RESET_COLD)
			rst_flags |= AR_RTC_RC_MAC_COLD;
	}

	REG_WRITE(ah, AR_RTC_RC, rst_flags);
	udelay(50);

	REG_WRITE(ah, AR_RTC_RC, 0);
	if (!ath9k_hw_wait(ah, AR_RTC_RC, AR_RTC_RC_M, 0, AH_WAIT_TIMEOUT)) {
		DPRINTF(ah->ah_sc, ATH_DBG_RESET,
			"RTC stuck in MAC reset\n");
		return false;
	}

	if (!AR_SREV_9100(ah))
		REG_WRITE(ah, AR_RC, 0);

	ath9k_hw_init_pll(ah, NULL);

	if (AR_SREV_9100(ah))
		udelay(50);

	return true;
}

static bool ath9k_hw_set_reset_power_on(struct ath_hw *ah)
{
	REG_WRITE(ah, AR_RTC_FORCE_WAKE, AR_RTC_FORCE_WAKE_EN |
		  AR_RTC_FORCE_WAKE_ON_INT);

	if (!AR_SREV_9100(ah))
		REG_WRITE(ah, AR_RC, AR_RC_AHB);

	REG_WRITE(ah, AR_RTC_RESET, 0);
	udelay(2);

	if (!AR_SREV_9100(ah))
		REG_WRITE(ah, AR_RC, 0);

	REG_WRITE(ah, AR_RTC_RESET, 1);

	if (!ath9k_hw_wait(ah,
			   AR_RTC_STATUS,
			   AR_RTC_STATUS_M,
			   AR_RTC_STATUS_ON,
			   AH_WAIT_TIMEOUT)) {
		DPRINTF(ah->ah_sc, ATH_DBG_RESET, "RTC not waking up\n");
		return false;
	}

	ath9k_hw_read_revisions(ah);

	return ath9k_hw_set_reset(ah, ATH9K_RESET_WARM);
}

static bool ath9k_hw_set_reset_reg(struct ath_hw *ah, u32 type)
{
	REG_WRITE(ah, AR_RTC_FORCE_WAKE,
		  AR_RTC_FORCE_WAKE_EN | AR_RTC_FORCE_WAKE_ON_INT);

	switch (type) {
	case ATH9K_RESET_POWER_ON:
		return ath9k_hw_set_reset_power_on(ah);
	case ATH9K_RESET_WARM:
	case ATH9K_RESET_COLD:
		return ath9k_hw_set_reset(ah, type);
	default:
		return false;
	}
}

static void ath9k_hw_set_regs(struct ath_hw *ah, struct ath9k_channel *chan,
			      enum ath9k_ht_macmode macmode)
{
	u32 phymode;
	u32 enableDacFifo = 0;

	if (AR_SREV_9285_10_OR_LATER(ah))
		enableDacFifo = (REG_READ(ah, AR_PHY_TURBO) &
					 AR_PHY_FC_ENABLE_DAC_FIFO);

	phymode = AR_PHY_FC_HT_EN | AR_PHY_FC_SHORT_GI_40
		| AR_PHY_FC_SINGLE_HT_LTF1 | AR_PHY_FC_WALSH | enableDacFifo;

	if (IS_CHAN_HT40(chan)) {
		phymode |= AR_PHY_FC_DYN2040_EN;

		if ((chan->chanmode == CHANNEL_A_HT40PLUS) ||
		    (chan->chanmode == CHANNEL_G_HT40PLUS))
			phymode |= AR_PHY_FC_DYN2040_PRI_CH;

		if (ah->extprotspacing == ATH9K_HT_EXTPROTSPACING_25)
			phymode |= AR_PHY_FC_DYN2040_EXT_CH;
	}
	REG_WRITE(ah, AR_PHY_TURBO, phymode);

	ath9k_hw_set11nmac2040(ah, macmode);

	REG_WRITE(ah, AR_GTXTO, 25 << AR_GTXTO_TIMEOUT_LIMIT_S);
	REG_WRITE(ah, AR_CST, 0xF << AR_CST_TIMEOUT_LIMIT_S);
}

static bool ath9k_hw_chip_reset(struct ath_hw *ah,
				struct ath9k_channel *chan)
{
	if (AR_SREV_9280(ah) && ah->eep_ops->get_eeprom(ah, EEP_OL_PWRCTRL)) {
		if (!ath9k_hw_set_reset_reg(ah, ATH9K_RESET_POWER_ON))
			return false;
	} else if (!ath9k_hw_set_reset_reg(ah, ATH9K_RESET_WARM))
		return false;

	if (!ath9k_hw_setpower(ah, ATH9K_PM_AWAKE))
		return false;

	ah->chip_fullsleep = false;
	ath9k_hw_init_pll(ah, chan);
	ath9k_hw_set_rfmode(ah, chan);

	return true;
}

static bool ath9k_hw_channel_change(struct ath_hw *ah,
				    struct ath9k_channel *chan,
				    enum ath9k_ht_macmode macmode)
{
	struct ath_regulatory *regulatory = ath9k_hw_regulatory(ah);
	struct ieee80211_channel *channel = chan->chan;
	u32 synthDelay, qnum;

	for (qnum = 0; qnum < AR_NUM_QCU; qnum++) {
		if (ath9k_hw_numtxpending(ah, qnum)) {
			DPRINTF(ah->ah_sc, ATH_DBG_QUEUE,
				"Transmit frames pending on queue %d\n", qnum);
			return false;
		}
	}

	REG_WRITE(ah, AR_PHY_RFBUS_REQ, AR_PHY_RFBUS_REQ_EN);
	if (!ath9k_hw_wait(ah, AR_PHY_RFBUS_GRANT, AR_PHY_RFBUS_GRANT_EN,
			   AR_PHY_RFBUS_GRANT_EN, AH_WAIT_TIMEOUT)) {
		DPRINTF(ah->ah_sc, ATH_DBG_FATAL,
			"Could not kill baseband RX\n");
		return false;
	}

	ath9k_hw_set_regs(ah, chan, macmode);

	if (AR_SREV_9280_10_OR_LATER(ah)) {
		ath9k_hw_ar9280_set_channel(ah, chan);
	} else {
		if (!(ath9k_hw_set_channel(ah, chan))) {
			DPRINTF(ah->ah_sc, ATH_DBG_FATAL,
				"Failed to set channel\n");
			return false;
		}
	}

	ah->eep_ops->set_txpower(ah, chan,
			     ath9k_regd_get_ctl(regulatory, chan),
			     channel->max_antenna_gain * 2,
			     channel->max_power * 2,
			     min((u32) MAX_RATE_POWER,
			     (u32) regulatory->power_limit));

	synthDelay = REG_READ(ah, AR_PHY_RX_DELAY) & AR_PHY_RX_DELAY_DELAY;
	if (IS_CHAN_B(chan))
		synthDelay = (4 * synthDelay) / 22;
	else
		synthDelay /= 10;

	udelay(synthDelay + BASE_ACTIVATE_DELAY);

	REG_WRITE(ah, AR_PHY_RFBUS_REQ, 0);

	if (IS_CHAN_OFDM(chan) || IS_CHAN_HT(chan))
		ath9k_hw_set_delta_slope(ah, chan);

	if (AR_SREV_9280_10_OR_LATER(ah))
		ath9k_hw_9280_spur_mitigate(ah, chan);
	else
		ath9k_hw_spur_mitigate(ah, chan);

	if (!chan->oneTimeCalsDone)
		chan->oneTimeCalsDone = true;

	return true;
}

static void ath9k_hw_9280_spur_mitigate(struct ath_hw *ah, struct ath9k_channel *chan)
{
	int bb_spur = AR_NO_SPUR;
	int freq;
	int bin, cur_bin;
	int bb_spur_off, spur_subchannel_sd;
	int spur_freq_sd;
	int spur_delta_phase;
	int denominator;
	int upper, lower, cur_vit_mask;
	int tmp, newVal;
	int i;
	int pilot_mask_reg[4] = { AR_PHY_TIMING7, AR_PHY_TIMING8,
			  AR_PHY_PILOT_MASK_01_30, AR_PHY_PILOT_MASK_31_60
	};
	int chan_mask_reg[4] = { AR_PHY_TIMING9, AR_PHY_TIMING10,
			 AR_PHY_CHANNEL_MASK_01_30, AR_PHY_CHANNEL_MASK_31_60
	};
	int inc[4] = { 0, 100, 0, 0 };
	struct chan_centers centers;

	int8_t mask_m[123];
	int8_t mask_p[123];
	int8_t mask_amt;
	int tmp_mask;
	int cur_bb_spur;
	bool is2GHz = IS_CHAN_2GHZ(chan);

	memset(&mask_m, 0, sizeof(int8_t) * 123);
	memset(&mask_p, 0, sizeof(int8_t) * 123);

	ath9k_hw_get_channel_centers(ah, chan, &centers);
	freq = centers.synth_center;

	ah->config.spurmode = SPUR_ENABLE_EEPROM;
	for (i = 0; i < AR_EEPROM_MODAL_SPURS; i++) {
		cur_bb_spur = ah->eep_ops->get_spur_channel(ah, i, is2GHz);

		if (is2GHz)
			cur_bb_spur = (cur_bb_spur / 10) + AR_BASE_FREQ_2GHZ;
		else
			cur_bb_spur = (cur_bb_spur / 10) + AR_BASE_FREQ_5GHZ;

		if (AR_NO_SPUR == cur_bb_spur)
			break;
		cur_bb_spur = cur_bb_spur - freq;

		if (IS_CHAN_HT40(chan)) {
			if ((cur_bb_spur > -AR_SPUR_FEEQ_BOUND_HT40) &&
			    (cur_bb_spur < AR_SPUR_FEEQ_BOUND_HT40)) {
				bb_spur = cur_bb_spur;
				break;
			}
		} else if ((cur_bb_spur > -AR_SPUR_FEEQ_BOUND_HT20) &&
			   (cur_bb_spur < AR_SPUR_FEEQ_BOUND_HT20)) {
			bb_spur = cur_bb_spur;
			break;
		}
	}

	if (AR_NO_SPUR == bb_spur) {
		REG_CLR_BIT(ah, AR_PHY_FORCE_CLKEN_CCK,
			    AR_PHY_FORCE_CLKEN_CCK_MRC_MUX);
		return;
	} else {
		REG_CLR_BIT(ah, AR_PHY_FORCE_CLKEN_CCK,
			    AR_PHY_FORCE_CLKEN_CCK_MRC_MUX);
	}

	bin = bb_spur * 320;

	tmp = REG_READ(ah, AR_PHY_TIMING_CTRL4(0));

	newVal = tmp | (AR_PHY_TIMING_CTRL4_ENABLE_SPUR_RSSI |
			AR_PHY_TIMING_CTRL4_ENABLE_SPUR_FILTER |
			AR_PHY_TIMING_CTRL4_ENABLE_CHAN_MASK |
			AR_PHY_TIMING_CTRL4_ENABLE_PILOT_MASK);
	REG_WRITE(ah, AR_PHY_TIMING_CTRL4(0), newVal);

	newVal = (AR_PHY_SPUR_REG_MASK_RATE_CNTL |
		  AR_PHY_SPUR_REG_ENABLE_MASK_PPM |
		  AR_PHY_SPUR_REG_MASK_RATE_SELECT |
		  AR_PHY_SPUR_REG_ENABLE_VIT_SPUR_RSSI |
		  SM(SPUR_RSSI_THRESH, AR_PHY_SPUR_REG_SPUR_RSSI_THRESH));
	REG_WRITE(ah, AR_PHY_SPUR_REG, newVal);

	if (IS_CHAN_HT40(chan)) {
		if (bb_spur < 0) {
			spur_subchannel_sd = 1;
			bb_spur_off = bb_spur + 10;
		} else {
			spur_subchannel_sd = 0;
			bb_spur_off = bb_spur - 10;
		}
	} else {
		spur_subchannel_sd = 0;
		bb_spur_off = bb_spur;
	}

	if (IS_CHAN_HT40(chan))
		spur_delta_phase =
			((bb_spur * 262144) /
			 10) & AR_PHY_TIMING11_SPUR_DELTA_PHASE;
	else
		spur_delta_phase =
			((bb_spur * 524288) /
			 10) & AR_PHY_TIMING11_SPUR_DELTA_PHASE;

	denominator = IS_CHAN_2GHZ(chan) ? 44 : 40;
	spur_freq_sd = ((bb_spur_off * 2048) / denominator) & 0x3ff;

	newVal = (AR_PHY_TIMING11_USE_SPUR_IN_AGC |
		  SM(spur_freq_sd, AR_PHY_TIMING11_SPUR_FREQ_SD) |
		  SM(spur_delta_phase, AR_PHY_TIMING11_SPUR_DELTA_PHASE));
	REG_WRITE(ah, AR_PHY_TIMING11, newVal);

	newVal = spur_subchannel_sd << AR_PHY_SFCORR_SPUR_SUBCHNL_SD_S;
	REG_WRITE(ah, AR_PHY_SFCORR_EXT, newVal);

	cur_bin = -6000;
	upper = bin + 100;
	lower = bin - 100;

	for (i = 0; i < 4; i++) {
		int pilot_mask = 0;
		int chan_mask = 0;
		int bp = 0;
		for (bp = 0; bp < 30; bp++) {
			if ((cur_bin > lower) && (cur_bin < upper)) {
				pilot_mask = pilot_mask | 0x1 << bp;
				chan_mask = chan_mask | 0x1 << bp;
			}
			cur_bin += 100;
		}
		cur_bin += inc[i];
		REG_WRITE(ah, pilot_mask_reg[i], pilot_mask);
		REG_WRITE(ah, chan_mask_reg[i], chan_mask);
	}

	cur_vit_mask = 6100;
	upper = bin + 120;
	lower = bin - 120;

	for (i = 0; i < 123; i++) {
		if ((cur_vit_mask > lower) && (cur_vit_mask < upper)) {

			/* workaround for gcc bug #37014 */
			volatile int tmp_v = abs(cur_vit_mask - bin);

			if (tmp_v < 75)
				mask_amt = 1;
			else
				mask_amt = 0;
			if (cur_vit_mask < 0)
				mask_m[abs(cur_vit_mask / 100)] = mask_amt;
			else
				mask_p[cur_vit_mask / 100] = mask_amt;
		}
		cur_vit_mask -= 100;
	}

	tmp_mask = (mask_m[46] << 30) | (mask_m[47] << 28)
		| (mask_m[48] << 26) | (mask_m[49] << 24)
		| (mask_m[50] << 22) | (mask_m[51] << 20)
		| (mask_m[52] << 18) | (mask_m[53] << 16)
		| (mask_m[54] << 14) | (mask_m[55] << 12)
		| (mask_m[56] << 10) | (mask_m[57] << 8)
		| (mask_m[58] << 6) | (mask_m[59] << 4)
		| (mask_m[60] << 2) | (mask_m[61] << 0);
	REG_WRITE(ah, AR_PHY_BIN_MASK_1, tmp_mask);
	REG_WRITE(ah, AR_PHY_VIT_MASK2_M_46_61, tmp_mask);

	tmp_mask = (mask_m[31] << 28)
		| (mask_m[32] << 26) | (mask_m[33] << 24)
		| (mask_m[34] << 22) | (mask_m[35] << 20)
		| (mask_m[36] << 18) | (mask_m[37] << 16)
		| (mask_m[48] << 14) | (mask_m[39] << 12)
		| (mask_m[40] << 10) | (mask_m[41] << 8)
		| (mask_m[42] << 6) | (mask_m[43] << 4)
		| (mask_m[44] << 2) | (mask_m[45] << 0);
	REG_WRITE(ah, AR_PHY_BIN_MASK_2, tmp_mask);
	REG_WRITE(ah, AR_PHY_MASK2_M_31_45, tmp_mask);

	tmp_mask = (mask_m[16] << 30) | (mask_m[16] << 28)
		| (mask_m[18] << 26) | (mask_m[18] << 24)
		| (mask_m[20] << 22) | (mask_m[20] << 20)
		| (mask_m[22] << 18) | (mask_m[22] << 16)
		| (mask_m[24] << 14) | (mask_m[24] << 12)
		| (mask_m[25] << 10) | (mask_m[26] << 8)
		| (mask_m[27] << 6) | (mask_m[28] << 4)
		| (mask_m[29] << 2) | (mask_m[30] << 0);
	REG_WRITE(ah, AR_PHY_BIN_MASK_3, tmp_mask);
	REG_WRITE(ah, AR_PHY_MASK2_M_16_30, tmp_mask);

	tmp_mask = (mask_m[0] << 30) | (mask_m[1] << 28)
		| (mask_m[2] << 26) | (mask_m[3] << 24)
		| (mask_m[4] << 22) | (mask_m[5] << 20)
		| (mask_m[6] << 18) | (mask_m[7] << 16)
		| (mask_m[8] << 14) | (mask_m[9] << 12)
		| (mask_m[10] << 10) | (mask_m[11] << 8)
		| (mask_m[12] << 6) | (mask_m[13] << 4)
		| (mask_m[14] << 2) | (mask_m[15] << 0);
	REG_WRITE(ah, AR_PHY_MASK_CTL, tmp_mask);
	REG_WRITE(ah, AR_PHY_MASK2_M_00_15, tmp_mask);

	tmp_mask = (mask_p[15] << 28)
		| (mask_p[14] << 26) | (mask_p[13] << 24)
		| (mask_p[12] << 22) | (mask_p[11] << 20)
		| (mask_p[10] << 18) | (mask_p[9] << 16)
		| (mask_p[8] << 14) | (mask_p[7] << 12)
		| (mask_p[6] << 10) | (mask_p[5] << 8)
		| (mask_p[4] << 6) | (mask_p[3] << 4)
		| (mask_p[2] << 2) | (mask_p[1] << 0);
	REG_WRITE(ah, AR_PHY_BIN_MASK2_1, tmp_mask);
	REG_WRITE(ah, AR_PHY_MASK2_P_15_01, tmp_mask);

	tmp_mask = (mask_p[30] << 28)
		| (mask_p[29] << 26) | (mask_p[28] << 24)
		| (mask_p[27] << 22) | (mask_p[26] << 20)
		| (mask_p[25] << 18) | (mask_p[24] << 16)
		| (mask_p[23] << 14) | (mask_p[22] << 12)
		| (mask_p[21] << 10) | (mask_p[20] << 8)
		| (mask_p[19] << 6) | (mask_p[18] << 4)
		| (mask_p[17] << 2) | (mask_p[16] << 0);
	REG_WRITE(ah, AR_PHY_BIN_MASK2_2, tmp_mask);
	REG_WRITE(ah, AR_PHY_MASK2_P_30_16, tmp_mask);

	tmp_mask = (mask_p[45] << 28)
		| (mask_p[44] << 26) | (mask_p[43] << 24)
		| (mask_p[42] << 22) | (mask_p[41] << 20)
		| (mask_p[40] << 18) | (mask_p[39] << 16)
		| (mask_p[38] << 14) | (mask_p[37] << 12)
		| (mask_p[36] << 10) | (mask_p[35] << 8)
		| (mask_p[34] << 6) | (mask_p[33] << 4)
		| (mask_p[32] << 2) | (mask_p[31] << 0);
	REG_WRITE(ah, AR_PHY_BIN_MASK2_3, tmp_mask);
	REG_WRITE(ah, AR_PHY_MASK2_P_45_31, tmp_mask);

	tmp_mask = (mask_p[61] << 30) | (mask_p[60] << 28)
		| (mask_p[59] << 26) | (mask_p[58] << 24)
		| (mask_p[57] << 22) | (mask_p[56] << 20)
		| (mask_p[55] << 18) | (mask_p[54] << 16)
		| (mask_p[53] << 14) | (mask_p[52] << 12)
		| (mask_p[51] << 10) | (mask_p[50] << 8)
		| (mask_p[49] << 6) | (mask_p[48] << 4)
		| (mask_p[47] << 2) | (mask_p[46] << 0);
	REG_WRITE(ah, AR_PHY_BIN_MASK2_4, tmp_mask);
	REG_WRITE(ah, AR_PHY_MASK2_P_61_45, tmp_mask);
}

static void ath9k_hw_spur_mitigate(struct ath_hw *ah, struct ath9k_channel *chan)
{
	int bb_spur = AR_NO_SPUR;
	int bin, cur_bin;
	int spur_freq_sd;
	int spur_delta_phase;
	int denominator;
	int upper, lower, cur_vit_mask;
	int tmp, new;
	int i;
	int pilot_mask_reg[4] = { AR_PHY_TIMING7, AR_PHY_TIMING8,
			  AR_PHY_PILOT_MASK_01_30, AR_PHY_PILOT_MASK_31_60
	};
	int chan_mask_reg[4] = { AR_PHY_TIMING9, AR_PHY_TIMING10,
			 AR_PHY_CHANNEL_MASK_01_30, AR_PHY_CHANNEL_MASK_31_60
	};
	int inc[4] = { 0, 100, 0, 0 };

	int8_t mask_m[123];
	int8_t mask_p[123];
	int8_t mask_amt;
	int tmp_mask;
	int cur_bb_spur;
	bool is2GHz = IS_CHAN_2GHZ(chan);

	memset(&mask_m, 0, sizeof(int8_t) * 123);
	memset(&mask_p, 0, sizeof(int8_t) * 123);

	for (i = 0; i < AR_EEPROM_MODAL_SPURS; i++) {
		cur_bb_spur = ah->eep_ops->get_spur_channel(ah, i, is2GHz);
		if (AR_NO_SPUR == cur_bb_spur)
			break;
		cur_bb_spur = cur_bb_spur - (chan->channel * 10);
		if ((cur_bb_spur > -95) && (cur_bb_spur < 95)) {
			bb_spur = cur_bb_spur;
			break;
		}
	}

	if (AR_NO_SPUR == bb_spur)
		return;

	bin = bb_spur * 32;

	tmp = REG_READ(ah, AR_PHY_TIMING_CTRL4(0));
	new = tmp | (AR_PHY_TIMING_CTRL4_ENABLE_SPUR_RSSI |
		     AR_PHY_TIMING_CTRL4_ENABLE_SPUR_FILTER |
		     AR_PHY_TIMING_CTRL4_ENABLE_CHAN_MASK |
		     AR_PHY_TIMING_CTRL4_ENABLE_PILOT_MASK);

	REG_WRITE(ah, AR_PHY_TIMING_CTRL4(0), new);

	new = (AR_PHY_SPUR_REG_MASK_RATE_CNTL |
	       AR_PHY_SPUR_REG_ENABLE_MASK_PPM |
	       AR_PHY_SPUR_REG_MASK_RATE_SELECT |
	       AR_PHY_SPUR_REG_ENABLE_VIT_SPUR_RSSI |
	       SM(SPUR_RSSI_THRESH, AR_PHY_SPUR_REG_SPUR_RSSI_THRESH));
	REG_WRITE(ah, AR_PHY_SPUR_REG, new);

	spur_delta_phase = ((bb_spur * 524288) / 100) &
		AR_PHY_TIMING11_SPUR_DELTA_PHASE;

	denominator = IS_CHAN_2GHZ(chan) ? 440 : 400;
	spur_freq_sd = ((bb_spur * 2048) / denominator) & 0x3ff;

	new = (AR_PHY_TIMING11_USE_SPUR_IN_AGC |
	       SM(spur_freq_sd, AR_PHY_TIMING11_SPUR_FREQ_SD) |
	       SM(spur_delta_phase, AR_PHY_TIMING11_SPUR_DELTA_PHASE));
	REG_WRITE(ah, AR_PHY_TIMING11, new);

	cur_bin = -6000;
	upper = bin + 100;
	lower = bin - 100;

	for (i = 0; i < 4; i++) {
		int pilot_mask = 0;
		int chan_mask = 0;
		int bp = 0;
		for (bp = 0; bp < 30; bp++) {
			if ((cur_bin > lower) && (cur_bin < upper)) {
				pilot_mask = pilot_mask | 0x1 << bp;
				chan_mask = chan_mask | 0x1 << bp;
			}
			cur_bin += 100;
		}
		cur_bin += inc[i];
		REG_WRITE(ah, pilot_mask_reg[i], pilot_mask);
		REG_WRITE(ah, chan_mask_reg[i], chan_mask);
	}

	cur_vit_mask = 6100;
	upper = bin + 120;
	lower = bin - 120;

	for (i = 0; i < 123; i++) {
		if ((cur_vit_mask > lower) && (cur_vit_mask < upper)) {

			/* workaround for gcc bug #37014 */
			volatile int tmp_v = abs(cur_vit_mask - bin);

			if (tmp_v < 75)
				mask_amt = 1;
			else
				mask_amt = 0;
			if (cur_vit_mask < 0)
				mask_m[abs(cur_vit_mask / 100)] = mask_amt;
			else
				mask_p[cur_vit_mask / 100] = mask_amt;
		}
		cur_vit_mask -= 100;
	}

	tmp_mask = (mask_m[46] << 30) | (mask_m[47] << 28)
		| (mask_m[48] << 26) | (mask_m[49] << 24)
		| (mask_m[50] << 22) | (mask_m[51] << 20)
		| (mask_m[52] << 18) | (mask_m[53] << 16)
		| (mask_m[54] << 14) | (mask_m[55] << 12)
		| (mask_m[56] << 10) | (mask_m[57] << 8)
		| (mask_m[58] << 6) | (mask_m[59] << 4)
		| (mask_m[60] << 2) | (mask_m[61] << 0);
	REG_WRITE(ah, AR_PHY_BIN_MASK_1, tmp_mask);
	REG_WRITE(ah, AR_PHY_VIT_MASK2_M_46_61, tmp_mask);

	tmp_mask = (mask_m[31] << 28)
		| (mask_m[32] << 26) | (mask_m[33] << 24)
		| (mask_m[34] << 22) | (mask_m[35] << 20)
		| (mask_m[36] << 18) | (mask_m[37] << 16)
		| (mask_m[48] << 14) | (mask_m[39] << 12)
		| (mask_m[40] << 10) | (mask_m[41] << 8)
		| (mask_m[42] << 6) | (mask_m[43] << 4)
		| (mask_m[44] << 2) | (mask_m[45] << 0);
	REG_WRITE(ah, AR_PHY_BIN_MASK_2, tmp_mask);
	REG_WRITE(ah, AR_PHY_MASK2_M_31_45, tmp_mask);

	tmp_mask = (mask_m[16] << 30) | (mask_m[16] << 28)
		| (mask_m[18] << 26) | (mask_m[18] << 24)
		| (mask_m[20] << 22) | (mask_m[20] << 20)
		| (mask_m[22] << 18) | (mask_m[22] << 16)
		| (mask_m[24] << 14) | (mask_m[24] << 12)
		| (mask_m[25] << 10) | (mask_m[26] << 8)
		| (mask_m[27] << 6) | (mask_m[28] << 4)
		| (mask_m[29] << 2) | (mask_m[30] << 0);
	REG_WRITE(ah, AR_PHY_BIN_MASK_3, tmp_mask);
	REG_WRITE(ah, AR_PHY_MASK2_M_16_30, tmp_mask);

	tmp_mask = (mask_m[0] << 30) | (mask_m[1] << 28)
		| (mask_m[2] << 26) | (mask_m[3] << 24)
		| (mask_m[4] << 22) | (mask_m[5] << 20)
		| (mask_m[6] << 18) | (mask_m[7] << 16)
		| (mask_m[8] << 14) | (mask_m[9] << 12)
		| (mask_m[10] << 10) | (mask_m[11] << 8)
		| (mask_m[12] << 6) | (mask_m[13] << 4)
		| (mask_m[14] << 2) | (mask_m[15] << 0);
	REG_WRITE(ah, AR_PHY_MASK_CTL, tmp_mask);
	REG_WRITE(ah, AR_PHY_MASK2_M_00_15, tmp_mask);

	tmp_mask = (mask_p[15] << 28)
		| (mask_p[14] << 26) | (mask_p[13] << 24)
		| (mask_p[12] << 22) | (mask_p[11] << 20)
		| (mask_p[10] << 18) | (mask_p[9] << 16)
		| (mask_p[8] << 14) | (mask_p[7] << 12)
		| (mask_p[6] << 10) | (mask_p[5] << 8)
		| (mask_p[4] << 6) | (mask_p[3] << 4)
		| (mask_p[2] << 2) | (mask_p[1] << 0);
	REG_WRITE(ah, AR_PHY_BIN_MASK2_1, tmp_mask);
	REG_WRITE(ah, AR_PHY_MASK2_P_15_01, tmp_mask);

	tmp_mask = (mask_p[30] << 28)
		| (mask_p[29] << 26) | (mask_p[28] << 24)
		| (mask_p[27] << 22) | (mask_p[26] << 20)
		| (mask_p[25] << 18) | (mask_p[24] << 16)
		| (mask_p[23] << 14) | (mask_p[22] << 12)
		| (mask_p[21] << 10) | (mask_p[20] << 8)
		| (mask_p[19] << 6) | (mask_p[18] << 4)
		| (mask_p[17] << 2) | (mask_p[16] << 0);
	REG_WRITE(ah, AR_PHY_BIN_MASK2_2, tmp_mask);
	REG_WRITE(ah, AR_PHY_MASK2_P_30_16, tmp_mask);

	tmp_mask = (mask_p[45] << 28)
		| (mask_p[44] << 26) | (mask_p[43] << 24)
		| (mask_p[42] << 22) | (mask_p[41] << 20)
		| (mask_p[40] << 18) | (mask_p[39] << 16)
		| (mask_p[38] << 14) | (mask_p[37] << 12)
		| (mask_p[36] << 10) | (mask_p[35] << 8)
		| (mask_p[34] << 6) | (mask_p[33] << 4)
		| (mask_p[32] << 2) | (mask_p[31] << 0);
	REG_WRITE(ah, AR_PHY_BIN_MASK2_3, tmp_mask);
	REG_WRITE(ah, AR_PHY_MASK2_P_45_31, tmp_mask);

	tmp_mask = (mask_p[61] << 30) | (mask_p[60] << 28)
		| (mask_p[59] << 26) | (mask_p[58] << 24)
		| (mask_p[57] << 22) | (mask_p[56] << 20)
		| (mask_p[55] << 18) | (mask_p[54] << 16)
		| (mask_p[53] << 14) | (mask_p[52] << 12)
		| (mask_p[51] << 10) | (mask_p[50] << 8)
		| (mask_p[49] << 6) | (mask_p[48] << 4)
		| (mask_p[47] << 2) | (mask_p[46] << 0);
	REG_WRITE(ah, AR_PHY_BIN_MASK2_4, tmp_mask);
	REG_WRITE(ah, AR_PHY_MASK2_P_61_45, tmp_mask);
}

static void ath9k_enable_rfkill(struct ath_hw *ah)
{
	REG_SET_BIT(ah, AR_GPIO_INPUT_EN_VAL,
		    AR_GPIO_INPUT_EN_VAL_RFSILENT_BB);

	REG_CLR_BIT(ah, AR_GPIO_INPUT_MUX2,
		    AR_GPIO_INPUT_MUX2_RFSILENT);

	ath9k_hw_cfg_gpio_input(ah, ah->rfkill_gpio);
	REG_SET_BIT(ah, AR_PHY_TEST, RFSILENT_BB);
}

int ath9k_hw_reset(struct ath_hw *ah, struct ath9k_channel *chan,
		    bool bChannelChange)
{
	u32 saveLedState;
	struct ath_softc *sc = ah->ah_sc;
	struct ath9k_channel *curchan = ah->curchan;
	u32 saveDefAntenna;
	u32 macStaId1;
	u64 tsf = 0;
	int i, rx_chainmask, r;

	ah->extprotspacing = sc->ht_extprotspacing;
	ah->txchainmask = sc->tx_chainmask;
	ah->rxchainmask = sc->rx_chainmask;

	if (!ath9k_hw_setpower(ah, ATH9K_PM_AWAKE))
		return -EIO;

	if (curchan && !ah->chip_fullsleep)
		ath9k_hw_getnf(ah, curchan);

	if (bChannelChange &&
	    (ah->chip_fullsleep != true) &&
	    (ah->curchan != NULL) &&
	    (chan->channel != ah->curchan->channel) &&
	    ((chan->channelFlags & CHANNEL_ALL) ==
	     (ah->curchan->channelFlags & CHANNEL_ALL)) &&
	     !(AR_SREV_9280(ah) || IS_CHAN_A_5MHZ_SPACED(chan) ||
	     IS_CHAN_A_5MHZ_SPACED(ah->curchan))) {

		if (ath9k_hw_channel_change(ah, chan, sc->tx_chan_width)) {
			ath9k_hw_loadnf(ah, ah->curchan);
			ath9k_hw_start_nfcal(ah);
			return 0;
		}
	}

	saveDefAntenna = REG_READ(ah, AR_DEF_ANTENNA);
	if (saveDefAntenna == 0)
		saveDefAntenna = 1;

	macStaId1 = REG_READ(ah, AR_STA_ID1) & AR_STA_ID1_BASE_RATE_11B;

	/* For chips on which RTC reset is done, save TSF before it gets cleared */
	if (AR_SREV_9280(ah) && ah->eep_ops->get_eeprom(ah, EEP_OL_PWRCTRL))
		tsf = ath9k_hw_gettsf64(ah);

	saveLedState = REG_READ(ah, AR_CFG_LED) &
		(AR_CFG_LED_ASSOC_CTL | AR_CFG_LED_MODE_SEL |
		 AR_CFG_LED_BLINK_THRESH_SEL | AR_CFG_LED_BLINK_SLOW);

	ath9k_hw_mark_phy_inactive(ah);

	if (AR_SREV_9271(ah) && ah->htc_reset_init) {
		REG_WRITE(ah,
			  AR9271_RESET_POWER_DOWN_CONTROL,
			  AR9271_RADIO_RF_RST);
		udelay(50);
	}

	if (!ath9k_hw_chip_reset(ah, chan)) {
		DPRINTF(ah->ah_sc, ATH_DBG_FATAL, "Chip reset failed\n");
		return -EINVAL;
	}

	if (AR_SREV_9271(ah) && ah->htc_reset_init) {
		ah->htc_reset_init = false;
		REG_WRITE(ah,
			  AR9271_RESET_POWER_DOWN_CONTROL,
			  AR9271_GATE_MAC_CTL);
		udelay(50);
	}

	/* Restore TSF */
	if (tsf && AR_SREV_9280(ah) && ah->eep_ops->get_eeprom(ah, EEP_OL_PWRCTRL))
		ath9k_hw_settsf64(ah, tsf);

	if (AR_SREV_9280_10_OR_LATER(ah))
		REG_SET_BIT(ah, AR_GPIO_INPUT_EN_VAL, AR_GPIO_JTAG_DISABLE);

	if (AR_SREV_9287_12_OR_LATER(ah)) {
		/* Enable ASYNC FIFO */
		REG_SET_BIT(ah, AR_MAC_PCU_ASYNC_FIFO_REG3,
				AR_MAC_PCU_ASYNC_FIFO_REG3_DATAPATH_SEL);
		REG_SET_BIT(ah, AR_PHY_MODE, AR_PHY_MODE_ASYNCFIFO);
		REG_CLR_BIT(ah, AR_MAC_PCU_ASYNC_FIFO_REG3,
				AR_MAC_PCU_ASYNC_FIFO_REG3_SOFT_RESET);
		REG_SET_BIT(ah, AR_MAC_PCU_ASYNC_FIFO_REG3,
				AR_MAC_PCU_ASYNC_FIFO_REG3_SOFT_RESET);
	}
	r = ath9k_hw_process_ini(ah, chan, sc->tx_chan_width);
	if (r)
		return r;

	/* Setup MFP options for CCMP */
	if (AR_SREV_9280_20_OR_LATER(ah)) {
		/* Mask Retry(b11), PwrMgt(b12), MoreData(b13) to 0 in mgmt
		 * frames when constructing CCMP AAD. */
		REG_RMW_FIELD(ah, AR_AES_MUTE_MASK1, AR_AES_MUTE_MASK1_FC_MGMT,
			      0xc7ff);
		ah->sw_mgmt_crypto = false;
	} else if (AR_SREV_9160_10_OR_LATER(ah)) {
		/* Disable hardware crypto for management frames */
		REG_CLR_BIT(ah, AR_PCU_MISC_MODE2,
			    AR_PCU_MISC_MODE2_MGMT_CRYPTO_ENABLE);
		REG_SET_BIT(ah, AR_PCU_MISC_MODE2,
			    AR_PCU_MISC_MODE2_NO_CRYPTO_FOR_NON_DATA_PKT);
		ah->sw_mgmt_crypto = true;
	} else
		ah->sw_mgmt_crypto = true;

	if (IS_CHAN_OFDM(chan) || IS_CHAN_HT(chan))
		ath9k_hw_set_delta_slope(ah, chan);

	if (AR_SREV_9280_10_OR_LATER(ah))
		ath9k_hw_9280_spur_mitigate(ah, chan);
	else
		ath9k_hw_spur_mitigate(ah, chan);

	ah->eep_ops->set_board_values(ah, chan);

	ath9k_hw_decrease_chain_power(ah, chan);

	REG_WRITE(ah, AR_STA_ID0, get_unaligned_le32(ah->macaddr));
	REG_WRITE(ah, AR_STA_ID1, get_unaligned_le16(ah->macaddr + 4)
		  | macStaId1
		  | AR_STA_ID1_RTS_USE_DEF
		  | (ah->config.
		     ack_6mb ? AR_STA_ID1_ACKCTS_6MB : 0)
		  | ah->sta_id1_defaults);
	ath9k_hw_set_operating_mode(ah, ah->opmode);

	REG_WRITE(ah, AR_BSSMSKL, get_unaligned_le32(sc->bssidmask));
	REG_WRITE(ah, AR_BSSMSKU, get_unaligned_le16(sc->bssidmask + 4));

	REG_WRITE(ah, AR_DEF_ANTENNA, saveDefAntenna);

	REG_WRITE(ah, AR_BSS_ID0, get_unaligned_le32(sc->curbssid));
	REG_WRITE(ah, AR_BSS_ID1, get_unaligned_le16(sc->curbssid + 4) |
		  ((sc->curaid & 0x3fff) << AR_BSS_ID1_AID_S));

	REG_WRITE(ah, AR_ISR, ~0);

	REG_WRITE(ah, AR_RSSI_THR, INIT_RSSI_THR);

	if (AR_SREV_9280_10_OR_LATER(ah))
		ath9k_hw_ar9280_set_channel(ah, chan);
	else
		if (!(ath9k_hw_set_channel(ah, chan)))
			return -EIO;

	for (i = 0; i < AR_NUM_DCU; i++)
		REG_WRITE(ah, AR_DQCUMASK(i), 1 << i);

	ah->intr_txqs = 0;
	for (i = 0; i < ah->caps.total_queues; i++)
		ath9k_hw_resettxqueue(ah, i);

	ath9k_hw_init_interrupt_masks(ah, ah->opmode);
	ath9k_hw_init_qos(ah);

	if (ah->caps.hw_caps & ATH9K_HW_CAP_RFSILENT)
		ath9k_enable_rfkill(ah);

	ath9k_hw_init_user_settings(ah);

	if (AR_SREV_9287_12_OR_LATER(ah)) {
		REG_WRITE(ah, AR_D_GBL_IFS_SIFS,
			  AR_D_GBL_IFS_SIFS_ASYNC_FIFO_DUR);
		REG_WRITE(ah, AR_D_GBL_IFS_SLOT,
			  AR_D_GBL_IFS_SLOT_ASYNC_FIFO_DUR);
		REG_WRITE(ah, AR_D_GBL_IFS_EIFS,
			  AR_D_GBL_IFS_EIFS_ASYNC_FIFO_DUR);

		REG_WRITE(ah, AR_TIME_OUT, AR_TIME_OUT_ACK_CTS_ASYNC_FIFO_DUR);
		REG_WRITE(ah, AR_USEC, AR_USEC_ASYNC_FIFO_DUR);

		REG_SET_BIT(ah, AR_MAC_PCU_LOGIC_ANALYZER,
			    AR_MAC_PCU_LOGIC_ANALYZER_DISBUG20768);
		REG_RMW_FIELD(ah, AR_AHB_MODE, AR_AHB_CUSTOM_BURST_EN,
			      AR_AHB_CUSTOM_BURST_ASYNC_FIFO_VAL);
	}
	if (AR_SREV_9287_12_OR_LATER(ah)) {
		REG_SET_BIT(ah, AR_PCU_MISC_MODE2,
				AR_PCU_MISC_MODE2_ENABLE_AGGWEP);
	}

	REG_WRITE(ah, AR_STA_ID1,
		  REG_READ(ah, AR_STA_ID1) | AR_STA_ID1_PRESERVE_SEQNUM);

	ath9k_hw_set_dma(ah);

	REG_WRITE(ah, AR_OBS, 8);

	if (ah->config.intr_mitigation) {
		REG_RMW_FIELD(ah, AR_RIMT, AR_RIMT_LAST, 500);
		REG_RMW_FIELD(ah, AR_RIMT, AR_RIMT_FIRST, 2000);
	}

	ath9k_hw_init_bb(ah, chan);

	if (!ath9k_hw_init_cal(ah, chan))
		return -EIO;

	rx_chainmask = ah->rxchainmask;
	if ((rx_chainmask == 0x5) || (rx_chainmask == 0x3)) {
		REG_WRITE(ah, AR_PHY_RX_CHAINMASK, rx_chainmask);
		REG_WRITE(ah, AR_PHY_CAL_CHAINMASK, rx_chainmask);
	}

	REG_WRITE(ah, AR_CFG_LED, saveLedState | AR_CFG_SCLK_32KHZ);

	/*
	 * For big endian systems turn on swapping for descriptors
	 */
	if (AR_SREV_9100(ah)) {
		u32 mask;
		mask = REG_READ(ah, AR_CFG);
		if (mask & (AR_CFG_SWRB | AR_CFG_SWTB | AR_CFG_SWRG)) {
			DPRINTF(ah->ah_sc, ATH_DBG_RESET,
				"CFG Byte Swap Set 0x%x\n", mask);
		} else {
			mask =
				INIT_CONFIG_STATUS | AR_CFG_SWRB | AR_CFG_SWTB;
			REG_WRITE(ah, AR_CFG, mask);
			DPRINTF(ah->ah_sc, ATH_DBG_RESET,
				"Setting CFG 0x%x\n", REG_READ(ah, AR_CFG));
		}
	} else {
		/* Configure AR9271 target WLAN */
                if (AR_SREV_9271(ah))
			REG_WRITE(ah, AR_CFG, AR_CFG_SWRB | AR_CFG_SWTB);
#ifdef __BIG_ENDIAN
                else
			REG_WRITE(ah, AR_CFG, AR_CFG_SWTD | AR_CFG_SWRD);
#endif
	}

	if (ah->ah_sc->sc_flags & SC_OP_BTCOEX_ENABLED)
		ath9k_hw_btcoex_enable(ah);

	return 0;
}

/************************/
/* Key Cache Management */
/************************/

bool ath9k_hw_keyreset(struct ath_hw *ah, u16 entry)
{
	u32 keyType;

	if (entry >= ah->caps.keycache_size) {
		DPRINTF(ah->ah_sc, ATH_DBG_FATAL,
			"keychache entry %u out of range\n", entry);
		return false;
	}

	keyType = REG_READ(ah, AR_KEYTABLE_TYPE(entry));

	REG_WRITE(ah, AR_KEYTABLE_KEY0(entry), 0);
	REG_WRITE(ah, AR_KEYTABLE_KEY1(entry), 0);
	REG_WRITE(ah, AR_KEYTABLE_KEY2(entry), 0);
	REG_WRITE(ah, AR_KEYTABLE_KEY3(entry), 0);
	REG_WRITE(ah, AR_KEYTABLE_KEY4(entry), 0);
	REG_WRITE(ah, AR_KEYTABLE_TYPE(entry), AR_KEYTABLE_TYPE_CLR);
	REG_WRITE(ah, AR_KEYTABLE_MAC0(entry), 0);
	REG_WRITE(ah, AR_KEYTABLE_MAC1(entry), 0);

	if (keyType == AR_KEYTABLE_TYPE_TKIP && ATH9K_IS_MIC_ENABLED(ah)) {
		u16 micentry = entry + 64;

		REG_WRITE(ah, AR_KEYTABLE_KEY0(micentry), 0);
		REG_WRITE(ah, AR_KEYTABLE_KEY1(micentry), 0);
		REG_WRITE(ah, AR_KEYTABLE_KEY2(micentry), 0);
		REG_WRITE(ah, AR_KEYTABLE_KEY3(micentry), 0);

	}

	return true;
}

bool ath9k_hw_keysetmac(struct ath_hw *ah, u16 entry, const u8 *mac)
{
	u32 macHi, macLo;

	if (entry >= ah->caps.keycache_size) {
		DPRINTF(ah->ah_sc, ATH_DBG_FATAL,
			"keychache entry %u out of range\n", entry);
		return false;
	}

	if (mac != NULL) {
		macHi = (mac[5] << 8) | mac[4];
		macLo = (mac[3] << 24) |
			(mac[2] << 16) |
			(mac[1] << 8) |
			mac[0];
		macLo >>= 1;
		macLo |= (macHi & 1) << 31;
		macHi >>= 1;
	} else {
		macLo = macHi = 0;
	}
	REG_WRITE(ah, AR_KEYTABLE_MAC0(entry), macLo);
	REG_WRITE(ah, AR_KEYTABLE_MAC1(entry), macHi | AR_KEYTABLE_VALID);

	return true;
}

bool ath9k_hw_set_keycache_entry(struct ath_hw *ah, u16 entry,
				 const struct ath9k_keyval *k,
				 const u8 *mac)
{
	const struct ath9k_hw_capabilities *pCap = &ah->caps;
	u32 key0, key1, key2, key3, key4;
	u32 keyType;

	if (entry >= pCap->keycache_size) {
		DPRINTF(ah->ah_sc, ATH_DBG_FATAL,
			"keycache entry %u out of range\n", entry);
		return false;
	}

	switch (k->kv_type) {
	case ATH9K_CIPHER_AES_OCB:
		keyType = AR_KEYTABLE_TYPE_AES;
		break;
	case ATH9K_CIPHER_AES_CCM:
		if (!(pCap->hw_caps & ATH9K_HW_CAP_CIPHER_AESCCM)) {
			DPRINTF(ah->ah_sc, ATH_DBG_ANY,
				"AES-CCM not supported by mac rev 0x%x\n",
				ah->hw_version.macRev);
			return false;
		}
		keyType = AR_KEYTABLE_TYPE_CCM;
		break;
	case ATH9K_CIPHER_TKIP:
		keyType = AR_KEYTABLE_TYPE_TKIP;
		if (ATH9K_IS_MIC_ENABLED(ah)
		    && entry + 64 >= pCap->keycache_size) {
			DPRINTF(ah->ah_sc, ATH_DBG_ANY,
				"entry %u inappropriate for TKIP\n", entry);
			return false;
		}
		break;
	case ATH9K_CIPHER_WEP:
		if (k->kv_len < WLAN_KEY_LEN_WEP40) {
			DPRINTF(ah->ah_sc, ATH_DBG_ANY,
				"WEP key length %u too small\n", k->kv_len);
			return false;
		}
		if (k->kv_len <= WLAN_KEY_LEN_WEP40)
			keyType = AR_KEYTABLE_TYPE_40;
		else if (k->kv_len <= WLAN_KEY_LEN_WEP104)
			keyType = AR_KEYTABLE_TYPE_104;
		else
			keyType = AR_KEYTABLE_TYPE_128;
		break;
	case ATH9K_CIPHER_CLR:
		keyType = AR_KEYTABLE_TYPE_CLR;
		break;
	default:
		DPRINTF(ah->ah_sc, ATH_DBG_FATAL,
			"cipher %u not supported\n", k->kv_type);
		return false;
	}

	key0 = get_unaligned_le32(k->kv_val + 0);
	key1 = get_unaligned_le16(k->kv_val + 4);
	key2 = get_unaligned_le32(k->kv_val + 6);
	key3 = get_unaligned_le16(k->kv_val + 10);
	key4 = get_unaligned_le32(k->kv_val + 12);
	if (k->kv_len <= WLAN_KEY_LEN_WEP104)
		key4 &= 0xff;

	/*
	 * Note: Key cache registers access special memory area that requires
	 * two 32-bit writes to actually update the values in the internal
	 * memory. Consequently, the exact order and pairs used here must be
	 * maintained.
	 */

	if (keyType == AR_KEYTABLE_TYPE_TKIP && ATH9K_IS_MIC_ENABLED(ah)) {
		u16 micentry = entry + 64;

		/*
		 * Write inverted key[47:0] first to avoid Michael MIC errors
		 * on frames that could be sent or received at the same time.
		 * The correct key will be written in the end once everything
		 * else is ready.
		 */
		REG_WRITE(ah, AR_KEYTABLE_KEY0(entry), ~key0);
		REG_WRITE(ah, AR_KEYTABLE_KEY1(entry), ~key1);

		/* Write key[95:48] */
		REG_WRITE(ah, AR_KEYTABLE_KEY2(entry), key2);
		REG_WRITE(ah, AR_KEYTABLE_KEY3(entry), key3);

		/* Write key[127:96] and key type */
		REG_WRITE(ah, AR_KEYTABLE_KEY4(entry), key4);
		REG_WRITE(ah, AR_KEYTABLE_TYPE(entry), keyType);

		/* Write MAC address for the entry */
		(void) ath9k_hw_keysetmac(ah, entry, mac);

		if (ah->misc_mode & AR_PCU_MIC_NEW_LOC_ENA) {
			/*
			 * TKIP uses two key cache entries:
			 * Michael MIC TX/RX keys in the same key cache entry
			 * (idx = main index + 64):
			 * key0 [31:0] = RX key [31:0]
			 * key1 [15:0] = TX key [31:16]
			 * key1 [31:16] = reserved
			 * key2 [31:0] = RX key [63:32]
			 * key3 [15:0] = TX key [15:0]
			 * key3 [31:16] = reserved
			 * key4 [31:0] = TX key [63:32]
			 */
			u32 mic0, mic1, mic2, mic3, mic4;

			mic0 = get_unaligned_le32(k->kv_mic + 0);
			mic2 = get_unaligned_le32(k->kv_mic + 4);
			mic1 = get_unaligned_le16(k->kv_txmic + 2) & 0xffff;
			mic3 = get_unaligned_le16(k->kv_txmic + 0) & 0xffff;
			mic4 = get_unaligned_le32(k->kv_txmic + 4);

			/* Write RX[31:0] and TX[31:16] */
			REG_WRITE(ah, AR_KEYTABLE_KEY0(micentry), mic0);
			REG_WRITE(ah, AR_KEYTABLE_KEY1(micentry), mic1);

			/* Write RX[63:32] and TX[15:0] */
			REG_WRITE(ah, AR_KEYTABLE_KEY2(micentry), mic2);
			REG_WRITE(ah, AR_KEYTABLE_KEY3(micentry), mic3);

			/* Write TX[63:32] and keyType(reserved) */
			REG_WRITE(ah, AR_KEYTABLE_KEY4(micentry), mic4);
			REG_WRITE(ah, AR_KEYTABLE_TYPE(micentry),
				  AR_KEYTABLE_TYPE_CLR);

		} else {
			/*
			 * TKIP uses four key cache entries (two for group
			 * keys):
			 * Michael MIC TX/RX keys are in different key cache
			 * entries (idx = main index + 64 for TX and
			 * main index + 32 + 96 for RX):
			 * key0 [31:0] = TX/RX MIC key [31:0]
			 * key1 [31:0] = reserved
			 * key2 [31:0] = TX/RX MIC key [63:32]
			 * key3 [31:0] = reserved
			 * key4 [31:0] = reserved
			 *
			 * Upper layer code will call this function separately
			 * for TX and RX keys when these registers offsets are
			 * used.
			 */
			u32 mic0, mic2;

			mic0 = get_unaligned_le32(k->kv_mic + 0);
			mic2 = get_unaligned_le32(k->kv_mic + 4);

			/* Write MIC key[31:0] */
			REG_WRITE(ah, AR_KEYTABLE_KEY0(micentry), mic0);
			REG_WRITE(ah, AR_KEYTABLE_KEY1(micentry), 0);

			/* Write MIC key[63:32] */
			REG_WRITE(ah, AR_KEYTABLE_KEY2(micentry), mic2);
			REG_WRITE(ah, AR_KEYTABLE_KEY3(micentry), 0);

			/* Write TX[63:32] and keyType(reserved) */
			REG_WRITE(ah, AR_KEYTABLE_KEY4(micentry), 0);
			REG_WRITE(ah, AR_KEYTABLE_TYPE(micentry),
				  AR_KEYTABLE_TYPE_CLR);
		}

		/* MAC address registers are reserved for the MIC entry */
		REG_WRITE(ah, AR_KEYTABLE_MAC0(micentry), 0);
		REG_WRITE(ah, AR_KEYTABLE_MAC1(micentry), 0);

		/*
		 * Write the correct (un-inverted) key[47:0] last to enable
		 * TKIP now that all other registers are set with correct
		 * values.
		 */
		REG_WRITE(ah, AR_KEYTABLE_KEY0(entry), key0);
		REG_WRITE(ah, AR_KEYTABLE_KEY1(entry), key1);
	} else {
		/* Write key[47:0] */
		REG_WRITE(ah, AR_KEYTABLE_KEY0(entry), key0);
		REG_WRITE(ah, AR_KEYTABLE_KEY1(entry), key1);

		/* Write key[95:48] */
		REG_WRITE(ah, AR_KEYTABLE_KEY2(entry), key2);
		REG_WRITE(ah, AR_KEYTABLE_KEY3(entry), key3);

		/* Write key[127:96] and key type */
		REG_WRITE(ah, AR_KEYTABLE_KEY4(entry), key4);
		REG_WRITE(ah, AR_KEYTABLE_TYPE(entry), keyType);

		/* Write MAC address for the entry */
		(void) ath9k_hw_keysetmac(ah, entry, mac);
	}

	return true;
}

bool ath9k_hw_keyisvalid(struct ath_hw *ah, u16 entry)
{
	if (entry < ah->caps.keycache_size) {
		u32 val = REG_READ(ah, AR_KEYTABLE_MAC1(entry));
		if (val & AR_KEYTABLE_VALID)
			return true;
	}
	return false;
}

/******************************/
/* Power Management (Chipset) */
/******************************/

static void ath9k_set_power_sleep(struct ath_hw *ah, int setChip)
{
	REG_SET_BIT(ah, AR_STA_ID1, AR_STA_ID1_PWR_SAV);
	if (setChip) {
		REG_CLR_BIT(ah, AR_RTC_FORCE_WAKE,
			    AR_RTC_FORCE_WAKE_EN);
		if (!AR_SREV_9100(ah))
			REG_WRITE(ah, AR_RC, AR_RC_AHB | AR_RC_HOSTIF);

		REG_CLR_BIT(ah, (AR_RTC_RESET),
			    AR_RTC_RESET_EN);
	}
}

static void ath9k_set_power_network_sleep(struct ath_hw *ah, int setChip)
{
	REG_SET_BIT(ah, AR_STA_ID1, AR_STA_ID1_PWR_SAV);
	if (setChip) {
		struct ath9k_hw_capabilities *pCap = &ah->caps;

		if (!(pCap->hw_caps & ATH9K_HW_CAP_AUTOSLEEP)) {
			REG_WRITE(ah, AR_RTC_FORCE_WAKE,
				  AR_RTC_FORCE_WAKE_ON_INT);
		} else {
			REG_CLR_BIT(ah, AR_RTC_FORCE_WAKE,
				    AR_RTC_FORCE_WAKE_EN);
		}
	}
}

static bool ath9k_hw_set_power_awake(struct ath_hw *ah, int setChip)
{
	u32 val;
	int i;

	if (setChip) {
		if ((REG_READ(ah, AR_RTC_STATUS) &
		     AR_RTC_STATUS_M) == AR_RTC_STATUS_SHUTDOWN) {
			if (ath9k_hw_set_reset_reg(ah,
					   ATH9K_RESET_POWER_ON) != true) {
				return false;
			}
		}
		if (AR_SREV_9100(ah))
			REG_SET_BIT(ah, AR_RTC_RESET,
				    AR_RTC_RESET_EN);

		REG_SET_BIT(ah, AR_RTC_FORCE_WAKE,
			    AR_RTC_FORCE_WAKE_EN);
		udelay(50);

		for (i = POWER_UP_TIME / 50; i > 0; i--) {
			val = REG_READ(ah, AR_RTC_STATUS) & AR_RTC_STATUS_M;
			if (val == AR_RTC_STATUS_ON)
				break;
			udelay(50);
			REG_SET_BIT(ah, AR_RTC_FORCE_WAKE,
				    AR_RTC_FORCE_WAKE_EN);
		}
		if (i == 0) {
			DPRINTF(ah->ah_sc, ATH_DBG_FATAL,
				"Failed to wakeup in %uus\n", POWER_UP_TIME / 20);
			return false;
		}
	}

	REG_CLR_BIT(ah, AR_STA_ID1, AR_STA_ID1_PWR_SAV);

	return true;
}

static bool ath9k_hw_setpower_nolock(struct ath_hw *ah,
				     enum ath9k_power_mode mode)
{
	int status = true, setChip = true;
	static const char *modes[] = {
		"AWAKE",
		"FULL-SLEEP",
		"NETWORK SLEEP",
		"UNDEFINED"
	};

	if (ah->power_mode == mode)
		return status;

	DPRINTF(ah->ah_sc, ATH_DBG_RESET, "%s -> %s\n",
		modes[ah->power_mode], modes[mode]);

	switch (mode) {
	case ATH9K_PM_AWAKE:
		status = ath9k_hw_set_power_awake(ah, setChip);
		break;
	case ATH9K_PM_FULL_SLEEP:
		ath9k_set_power_sleep(ah, setChip);
		ah->chip_fullsleep = true;
		break;
	case ATH9K_PM_NETWORK_SLEEP:
		ath9k_set_power_network_sleep(ah, setChip);
		break;
	default:
		DPRINTF(ah->ah_sc, ATH_DBG_FATAL,
			"Unknown power mode %u\n", mode);
		return false;
	}
	ah->power_mode = mode;

	return status;
}

bool ath9k_hw_setpower(struct ath_hw *ah, enum ath9k_power_mode mode)
{
	unsigned long flags;
	bool ret;

	spin_lock_irqsave(&ah->ah_sc->sc_pm_lock, flags);
	ret = ath9k_hw_setpower_nolock(ah, mode);
	spin_unlock_irqrestore(&ah->ah_sc->sc_pm_lock, flags);

	return ret;
}

void ath9k_ps_wakeup(struct ath_softc *sc)
{
	unsigned long flags;

	spin_lock_irqsave(&sc->sc_pm_lock, flags);
	if (++sc->ps_usecount != 1)
		goto unlock;

	ath9k_hw_setpower_nolock(sc->sc_ah, ATH9K_PM_AWAKE);

 unlock:
	spin_unlock_irqrestore(&sc->sc_pm_lock, flags);
}

void ath9k_ps_restore(struct ath_softc *sc)
{
	unsigned long flags;

	spin_lock_irqsave(&sc->sc_pm_lock, flags);
	if (--sc->ps_usecount != 0)
		goto unlock;

	if (sc->ps_enabled &&
	    !(sc->sc_flags & (SC_OP_WAIT_FOR_BEACON |
			      SC_OP_WAIT_FOR_CAB |
			      SC_OP_WAIT_FOR_PSPOLL_DATA |
			      SC_OP_WAIT_FOR_TX_ACK)))
		ath9k_hw_setpower_nolock(sc->sc_ah, ATH9K_PM_NETWORK_SLEEP);

 unlock:
	spin_unlock_irqrestore(&sc->sc_pm_lock, flags);
}

/*
 * Helper for ASPM support.
 *
 * Disable PLL when in L0s as well as receiver clock when in L1.
 * This power saving option must be enabled through the SerDes.
 *
 * Programming the SerDes must go through the same 288 bit serial shift
 * register as the other analog registers.  Hence the 9 writes.
 */
void ath9k_hw_configpcipowersave(struct ath_hw *ah, int restore, int power_off)
{
	u8 i;
	u32 val;

	if (ah->is_pciexpress != true)
		return;

	/* Do not touch SerDes registers */
	if (ah->config.pcie_powersave_enable == 2)
		return;

	/* Nothing to do on restore for 11N */
	if (!restore) {
		if (AR_SREV_9280_20_OR_LATER(ah)) {
			/*
			 * AR9280 2.0 or later chips use SerDes values from the
			 * initvals.h initialized depending on chipset during
			 * ath9k_hw_init()
			 */
			for (i = 0; i < ah->iniPcieSerdes.ia_rows; i++) {
				REG_WRITE(ah, INI_RA(&ah->iniPcieSerdes, i, 0),
					  INI_RA(&ah->iniPcieSerdes, i, 1));
			}
		} else if (AR_SREV_9280(ah) &&
			   (ah->hw_version.macRev == AR_SREV_REVISION_9280_10)) {
			REG_WRITE(ah, AR_PCIE_SERDES, 0x9248fd00);
			REG_WRITE(ah, AR_PCIE_SERDES, 0x24924924);

			/* RX shut off when elecidle is asserted */
			REG_WRITE(ah, AR_PCIE_SERDES, 0xa8000019);
			REG_WRITE(ah, AR_PCIE_SERDES, 0x13160820);
			REG_WRITE(ah, AR_PCIE_SERDES, 0xe5980560);

			/* Shut off CLKREQ active in L1 */
			if (ah->config.pcie_clock_req)
				REG_WRITE(ah, AR_PCIE_SERDES, 0x401deffc);
			else
				REG_WRITE(ah, AR_PCIE_SERDES, 0x401deffd);

			REG_WRITE(ah, AR_PCIE_SERDES, 0x1aaabe40);
			REG_WRITE(ah, AR_PCIE_SERDES, 0xbe105554);
			REG_WRITE(ah, AR_PCIE_SERDES, 0x00043007);

			/* Load the new settings */
			REG_WRITE(ah, AR_PCIE_SERDES2, 0x00000000);

		} else {
			REG_WRITE(ah, AR_PCIE_SERDES, 0x9248fc00);
			REG_WRITE(ah, AR_PCIE_SERDES, 0x24924924);

			/* RX shut off when elecidle is asserted */
			REG_WRITE(ah, AR_PCIE_SERDES, 0x28000039);
			REG_WRITE(ah, AR_PCIE_SERDES, 0x53160824);
			REG_WRITE(ah, AR_PCIE_SERDES, 0xe5980579);

			/*
			 * Ignore ah->ah_config.pcie_clock_req setting for
			 * pre-AR9280 11n
			 */
			REG_WRITE(ah, AR_PCIE_SERDES, 0x001defff);

			REG_WRITE(ah, AR_PCIE_SERDES, 0x1aaabe40);
			REG_WRITE(ah, AR_PCIE_SERDES, 0xbe105554);
			REG_WRITE(ah, AR_PCIE_SERDES, 0x000e3007);

			/* Load the new settings */
			REG_WRITE(ah, AR_PCIE_SERDES2, 0x00000000);
		}

		udelay(1000);

		/* set bit 19 to allow forcing of pcie core into L1 state */
		REG_SET_BIT(ah, AR_PCIE_PM_CTRL, AR_PCIE_PM_CTRL_ENA);

		/* Several PCIe massages to ensure proper behaviour */
		if (ah->config.pcie_waen) {
			val = ah->config.pcie_waen;
			if (!power_off)
				val &= (~AR_WA_D3_L1_DISABLE);
		} else {
			if (AR_SREV_9285(ah) || AR_SREV_9271(ah) ||
			    AR_SREV_9287(ah)) {
				val = AR9285_WA_DEFAULT;
				if (!power_off)
					val &= (~AR_WA_D3_L1_DISABLE);
			} else if (AR_SREV_9280(ah)) {
				/*
				 * On AR9280 chips bit 22 of 0x4004 needs to be
				 * set otherwise card may disappear.
				 */
				val = AR9280_WA_DEFAULT;
				if (!power_off)
					val &= (~AR_WA_D3_L1_DISABLE);
			} else
				val = AR_WA_DEFAULT;
		}

		REG_WRITE(ah, AR_WA, val);
	}

	if (power_off) {
		/*
		 * Set PCIe workaround bits
		 * bit 14 in WA register (disable L1) should only
		 * be set when device enters D3 and be cleared
		 * when device comes back to D0.
		 */
		if (ah->config.pcie_waen) {
			if (ah->config.pcie_waen & AR_WA_D3_L1_DISABLE)
				REG_SET_BIT(ah, AR_WA, AR_WA_D3_L1_DISABLE);
		} else {
			if (((AR_SREV_9285(ah) || AR_SREV_9271(ah) ||
			      AR_SREV_9287(ah)) &&
			     (AR9285_WA_DEFAULT & AR_WA_D3_L1_DISABLE)) ||
			    (AR_SREV_9280(ah) &&
			     (AR9280_WA_DEFAULT & AR_WA_D3_L1_DISABLE))) {
				REG_SET_BIT(ah, AR_WA, AR_WA_D3_L1_DISABLE);
			}
		}
	}
}

/**********************/
/* Interrupt Handling */
/**********************/

bool ath9k_hw_intrpend(struct ath_hw *ah)
{
	u32 host_isr;

	if (AR_SREV_9100(ah))
		return true;

	host_isr = REG_READ(ah, AR_INTR_ASYNC_CAUSE);
	if ((host_isr & AR_INTR_MAC_IRQ) && (host_isr != AR_INTR_SPURIOUS))
		return true;

	host_isr = REG_READ(ah, AR_INTR_SYNC_CAUSE);
	if ((host_isr & AR_INTR_SYNC_DEFAULT)
	    && (host_isr != AR_INTR_SPURIOUS))
		return true;

	return false;
}

bool ath9k_hw_getisr(struct ath_hw *ah, enum ath9k_int *masked)
{
	u32 isr = 0;
	u32 mask2 = 0;
	struct ath9k_hw_capabilities *pCap = &ah->caps;
	u32 sync_cause = 0;
	bool fatal_int = false;

	if (!AR_SREV_9100(ah)) {
		if (REG_READ(ah, AR_INTR_ASYNC_CAUSE) & AR_INTR_MAC_IRQ) {
			if ((REG_READ(ah, AR_RTC_STATUS) & AR_RTC_STATUS_M)
			    == AR_RTC_STATUS_ON) {
				isr = REG_READ(ah, AR_ISR);
			}
		}

		sync_cause = REG_READ(ah, AR_INTR_SYNC_CAUSE) &
			AR_INTR_SYNC_DEFAULT;

		*masked = 0;

		if (!isr && !sync_cause)
			return false;
	} else {
		*masked = 0;
		isr = REG_READ(ah, AR_ISR);
	}

	if (isr) {
		if (isr & AR_ISR_BCNMISC) {
			u32 isr2;
			isr2 = REG_READ(ah, AR_ISR_S2);
			if (isr2 & AR_ISR_S2_TIM)
				mask2 |= ATH9K_INT_TIM;
			if (isr2 & AR_ISR_S2_DTIM)
				mask2 |= ATH9K_INT_DTIM;
			if (isr2 & AR_ISR_S2_DTIMSYNC)
				mask2 |= ATH9K_INT_DTIMSYNC;
			if (isr2 & (AR_ISR_S2_CABEND))
				mask2 |= ATH9K_INT_CABEND;
			if (isr2 & AR_ISR_S2_GTT)
				mask2 |= ATH9K_INT_GTT;
			if (isr2 & AR_ISR_S2_CST)
				mask2 |= ATH9K_INT_CST;
			if (isr2 & AR_ISR_S2_TSFOOR)
				mask2 |= ATH9K_INT_TSFOOR;
		}

		isr = REG_READ(ah, AR_ISR_RAC);
		if (isr == 0xffffffff) {
			*masked = 0;
			return false;
		}

		*masked = isr & ATH9K_INT_COMMON;

		if (ah->config.intr_mitigation) {
			if (isr & (AR_ISR_RXMINTR | AR_ISR_RXINTM))
				*masked |= ATH9K_INT_RX;
		}

		if (isr & (AR_ISR_RXOK | AR_ISR_RXERR))
			*masked |= ATH9K_INT_RX;
		if (isr &
		    (AR_ISR_TXOK | AR_ISR_TXDESC | AR_ISR_TXERR |
		     AR_ISR_TXEOL)) {
			u32 s0_s, s1_s;

			*masked |= ATH9K_INT_TX;

			s0_s = REG_READ(ah, AR_ISR_S0_S);
			ah->intr_txqs |= MS(s0_s, AR_ISR_S0_QCU_TXOK);
			ah->intr_txqs |= MS(s0_s, AR_ISR_S0_QCU_TXDESC);

			s1_s = REG_READ(ah, AR_ISR_S1_S);
			ah->intr_txqs |= MS(s1_s, AR_ISR_S1_QCU_TXERR);
			ah->intr_txqs |= MS(s1_s, AR_ISR_S1_QCU_TXEOL);
		}

		if (isr & AR_ISR_RXORN) {
			DPRINTF(ah->ah_sc, ATH_DBG_INTERRUPT,
				"receive FIFO overrun interrupt\n");
		}

		if (!AR_SREV_9100(ah)) {
			if (!(pCap->hw_caps & ATH9K_HW_CAP_AUTOSLEEP)) {
				u32 isr5 = REG_READ(ah, AR_ISR_S5_S);
				if (isr5 & AR_ISR_S5_TIM_TIMER)
					*masked |= ATH9K_INT_TIM_TIMER;
			}
		}

		*masked |= mask2;
	}

	if (AR_SREV_9100(ah))
		return true;

	if (isr & AR_ISR_GENTMR) {
		u32 s5_s;

		s5_s = REG_READ(ah, AR_ISR_S5_S);
		if (isr & AR_ISR_GENTMR) {
			ah->intr_gen_timer_trigger =
				MS(s5_s, AR_ISR_S5_GENTIMER_TRIG);

			ah->intr_gen_timer_thresh =
				MS(s5_s, AR_ISR_S5_GENTIMER_THRESH);

			if (ah->intr_gen_timer_trigger)
				*masked |= ATH9K_INT_GENTIMER;

		}
	}

	if (sync_cause) {
		fatal_int =
			(sync_cause &
			 (AR_INTR_SYNC_HOST1_FATAL | AR_INTR_SYNC_HOST1_PERR))
			? true : false;

		if (fatal_int) {
			if (sync_cause & AR_INTR_SYNC_HOST1_FATAL) {
				DPRINTF(ah->ah_sc, ATH_DBG_ANY,
					"received PCI FATAL interrupt\n");
			}
			if (sync_cause & AR_INTR_SYNC_HOST1_PERR) {
				DPRINTF(ah->ah_sc, ATH_DBG_ANY,
					"received PCI PERR interrupt\n");
			}
			*masked |= ATH9K_INT_FATAL;
		}
		if (sync_cause & AR_INTR_SYNC_RADM_CPL_TIMEOUT) {
			DPRINTF(ah->ah_sc, ATH_DBG_INTERRUPT,
				"AR_INTR_SYNC_RADM_CPL_TIMEOUT\n");
			REG_WRITE(ah, AR_RC, AR_RC_HOSTIF);
			REG_WRITE(ah, AR_RC, 0);
			*masked |= ATH9K_INT_FATAL;
		}
		if (sync_cause & AR_INTR_SYNC_LOCAL_TIMEOUT) {
			DPRINTF(ah->ah_sc, ATH_DBG_INTERRUPT,
				"AR_INTR_SYNC_LOCAL_TIMEOUT\n");
		}

		REG_WRITE(ah, AR_INTR_SYNC_CAUSE_CLR, sync_cause);
		(void) REG_READ(ah, AR_INTR_SYNC_CAUSE_CLR);
	}

	return true;
}

enum ath9k_int ath9k_hw_set_interrupts(struct ath_hw *ah, enum ath9k_int ints)
{
	u32 omask = ah->mask_reg;
	u32 mask, mask2;
	struct ath9k_hw_capabilities *pCap = &ah->caps;

	DPRINTF(ah->ah_sc, ATH_DBG_INTERRUPT, "0x%x => 0x%x\n", omask, ints);

	if (omask & ATH9K_INT_GLOBAL) {
		DPRINTF(ah->ah_sc, ATH_DBG_INTERRUPT, "disable IER\n");
		REG_WRITE(ah, AR_IER, AR_IER_DISABLE);
		(void) REG_READ(ah, AR_IER);
		if (!AR_SREV_9100(ah)) {
			REG_WRITE(ah, AR_INTR_ASYNC_ENABLE, 0);
			(void) REG_READ(ah, AR_INTR_ASYNC_ENABLE);

			REG_WRITE(ah, AR_INTR_SYNC_ENABLE, 0);
			(void) REG_READ(ah, AR_INTR_SYNC_ENABLE);
		}
	}

	mask = ints & ATH9K_INT_COMMON;
	mask2 = 0;

	if (ints & ATH9K_INT_TX) {
		if (ah->txok_interrupt_mask)
			mask |= AR_IMR_TXOK;
		if (ah->txdesc_interrupt_mask)
			mask |= AR_IMR_TXDESC;
		if (ah->txerr_interrupt_mask)
			mask |= AR_IMR_TXERR;
		if (ah->txeol_interrupt_mask)
			mask |= AR_IMR_TXEOL;
	}
	if (ints & ATH9K_INT_RX) {
		mask |= AR_IMR_RXERR;
		if (ah->config.intr_mitigation)
			mask |= AR_IMR_RXMINTR | AR_IMR_RXINTM;
		else
			mask |= AR_IMR_RXOK | AR_IMR_RXDESC;
		if (!(pCap->hw_caps & ATH9K_HW_CAP_AUTOSLEEP))
			mask |= AR_IMR_GENTMR;
	}

	if (ints & (ATH9K_INT_BMISC)) {
		mask |= AR_IMR_BCNMISC;
		if (ints & ATH9K_INT_TIM)
			mask2 |= AR_IMR_S2_TIM;
		if (ints & ATH9K_INT_DTIM)
			mask2 |= AR_IMR_S2_DTIM;
		if (ints & ATH9K_INT_DTIMSYNC)
			mask2 |= AR_IMR_S2_DTIMSYNC;
		if (ints & ATH9K_INT_CABEND)
			mask2 |= AR_IMR_S2_CABEND;
		if (ints & ATH9K_INT_TSFOOR)
			mask2 |= AR_IMR_S2_TSFOOR;
	}

	if (ints & (ATH9K_INT_GTT | ATH9K_INT_CST)) {
		mask |= AR_IMR_BCNMISC;
		if (ints & ATH9K_INT_GTT)
			mask2 |= AR_IMR_S2_GTT;
		if (ints & ATH9K_INT_CST)
			mask2 |= AR_IMR_S2_CST;
	}

	DPRINTF(ah->ah_sc, ATH_DBG_INTERRUPT, "new IMR 0x%x\n", mask);
	REG_WRITE(ah, AR_IMR, mask);
	mask = REG_READ(ah, AR_IMR_S2) & ~(AR_IMR_S2_TIM |
					   AR_IMR_S2_DTIM |
					   AR_IMR_S2_DTIMSYNC |
					   AR_IMR_S2_CABEND |
					   AR_IMR_S2_CABTO |
					   AR_IMR_S2_TSFOOR |
					   AR_IMR_S2_GTT | AR_IMR_S2_CST);
	REG_WRITE(ah, AR_IMR_S2, mask | mask2);
	ah->mask_reg = ints;

	if (!(pCap->hw_caps & ATH9K_HW_CAP_AUTOSLEEP)) {
		if (ints & ATH9K_INT_TIM_TIMER)
			REG_SET_BIT(ah, AR_IMR_S5, AR_IMR_S5_TIM_TIMER);
		else
			REG_CLR_BIT(ah, AR_IMR_S5, AR_IMR_S5_TIM_TIMER);
	}

	if (ints & ATH9K_INT_GLOBAL) {
		DPRINTF(ah->ah_sc, ATH_DBG_INTERRUPT, "enable IER\n");
		REG_WRITE(ah, AR_IER, AR_IER_ENABLE);
		if (!AR_SREV_9100(ah)) {
			REG_WRITE(ah, AR_INTR_ASYNC_ENABLE,
				  AR_INTR_MAC_IRQ);
			REG_WRITE(ah, AR_INTR_ASYNC_MASK, AR_INTR_MAC_IRQ);


			REG_WRITE(ah, AR_INTR_SYNC_ENABLE,
				  AR_INTR_SYNC_DEFAULT);
			REG_WRITE(ah, AR_INTR_SYNC_MASK,
				  AR_INTR_SYNC_DEFAULT);
		}
		DPRINTF(ah->ah_sc, ATH_DBG_INTERRUPT, "AR_IMR 0x%x IER 0x%x\n",
			 REG_READ(ah, AR_IMR), REG_READ(ah, AR_IER));
	}

	return omask;
}

/*******************/
/* Beacon Handling */
/*******************/

void ath9k_hw_beaconinit(struct ath_hw *ah, u32 next_beacon, u32 beacon_period)
{
	int flags = 0;

	ah->beacon_interval = beacon_period;

	switch (ah->opmode) {
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_MONITOR:
		REG_WRITE(ah, AR_NEXT_TBTT_TIMER, TU_TO_USEC(next_beacon));
		REG_WRITE(ah, AR_NEXT_DMA_BEACON_ALERT, 0xffff);
		REG_WRITE(ah, AR_NEXT_SWBA, 0x7ffff);
		flags |= AR_TBTT_TIMER_EN;
		break;
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_MESH_POINT:
		REG_SET_BIT(ah, AR_TXCFG,
			    AR_TXCFG_ADHOC_BEACON_ATIM_TX_POLICY);
		REG_WRITE(ah, AR_NEXT_NDP_TIMER,
			  TU_TO_USEC(next_beacon +
				     (ah->atim_window ? ah->
				      atim_window : 1)));
		flags |= AR_NDP_TIMER_EN;
	case NL80211_IFTYPE_AP:
		REG_WRITE(ah, AR_NEXT_TBTT_TIMER, TU_TO_USEC(next_beacon));
		REG_WRITE(ah, AR_NEXT_DMA_BEACON_ALERT,
			  TU_TO_USEC(next_beacon -
				     ah->config.
				     dma_beacon_response_time));
		REG_WRITE(ah, AR_NEXT_SWBA,
			  TU_TO_USEC(next_beacon -
				     ah->config.
				     sw_beacon_response_time));
		flags |=
			AR_TBTT_TIMER_EN | AR_DBA_TIMER_EN | AR_SWBA_TIMER_EN;
		break;
	default:
		DPRINTF(ah->ah_sc, ATH_DBG_BEACON,
			"%s: unsupported opmode: %d\n",
			__func__, ah->opmode);
		return;
		break;
	}

	REG_WRITE(ah, AR_BEACON_PERIOD, TU_TO_USEC(beacon_period));
	REG_WRITE(ah, AR_DMA_BEACON_PERIOD, TU_TO_USEC(beacon_period));
	REG_WRITE(ah, AR_SWBA_PERIOD, TU_TO_USEC(beacon_period));
	REG_WRITE(ah, AR_NDP_PERIOD, TU_TO_USEC(beacon_period));

	beacon_period &= ~ATH9K_BEACON_ENA;
	if (beacon_period & ATH9K_BEACON_RESET_TSF) {
		beacon_period &= ~ATH9K_BEACON_RESET_TSF;
		ath9k_hw_reset_tsf(ah);
	}

	REG_SET_BIT(ah, AR_TIMER_MODE, flags);
}

void ath9k_hw_set_sta_beacon_timers(struct ath_hw *ah,
				    const struct ath9k_beacon_state *bs)
{
	u32 nextTbtt, beaconintval, dtimperiod, beacontimeout;
	struct ath9k_hw_capabilities *pCap = &ah->caps;

	REG_WRITE(ah, AR_NEXT_TBTT_TIMER, TU_TO_USEC(bs->bs_nexttbtt));

	REG_WRITE(ah, AR_BEACON_PERIOD,
		  TU_TO_USEC(bs->bs_intval & ATH9K_BEACON_PERIOD));
	REG_WRITE(ah, AR_DMA_BEACON_PERIOD,
		  TU_TO_USEC(bs->bs_intval & ATH9K_BEACON_PERIOD));

	REG_RMW_FIELD(ah, AR_RSSI_THR,
		      AR_RSSI_THR_BM_THR, bs->bs_bmissthreshold);

	beaconintval = bs->bs_intval & ATH9K_BEACON_PERIOD;

	if (bs->bs_sleepduration > beaconintval)
		beaconintval = bs->bs_sleepduration;

	dtimperiod = bs->bs_dtimperiod;
	if (bs->bs_sleepduration > dtimperiod)
		dtimperiod = bs->bs_sleepduration;

	if (beaconintval == dtimperiod)
		nextTbtt = bs->bs_nextdtim;
	else
		nextTbtt = bs->bs_nexttbtt;

	DPRINTF(ah->ah_sc, ATH_DBG_BEACON, "next DTIM %d\n", bs->bs_nextdtim);
	DPRINTF(ah->ah_sc, ATH_DBG_BEACON, "next beacon %d\n", nextTbtt);
	DPRINTF(ah->ah_sc, ATH_DBG_BEACON, "beacon period %d\n", beaconintval);
	DPRINTF(ah->ah_sc, ATH_DBG_BEACON, "DTIM period %d\n", dtimperiod);

	REG_WRITE(ah, AR_NEXT_DTIM,
		  TU_TO_USEC(bs->bs_nextdtim - SLEEP_SLOP));
	REG_WRITE(ah, AR_NEXT_TIM, TU_TO_USEC(nextTbtt - SLEEP_SLOP));

	REG_WRITE(ah, AR_SLEEP1,
		  SM((CAB_TIMEOUT_VAL << 3), AR_SLEEP1_CAB_TIMEOUT)
		  | AR_SLEEP1_ASSUME_DTIM);

	if (pCap->hw_caps & ATH9K_HW_CAP_AUTOSLEEP)
		beacontimeout = (BEACON_TIMEOUT_VAL << 3);
	else
		beacontimeout = MIN_BEACON_TIMEOUT_VAL;

	REG_WRITE(ah, AR_SLEEP2,
		  SM(beacontimeout, AR_SLEEP2_BEACON_TIMEOUT));

	REG_WRITE(ah, AR_TIM_PERIOD, TU_TO_USEC(beaconintval));
	REG_WRITE(ah, AR_DTIM_PERIOD, TU_TO_USEC(dtimperiod));

	REG_SET_BIT(ah, AR_TIMER_MODE,
		    AR_TBTT_TIMER_EN | AR_TIM_TIMER_EN |
		    AR_DTIM_TIMER_EN);

	/* TSF Out of Range Threshold */
	REG_WRITE(ah, AR_TSFOOR_THRESHOLD, bs->bs_tsfoor_threshold);
}

/*******************/
/* HW Capabilities */
/*******************/

void ath9k_hw_fill_cap_info(struct ath_hw *ah)
{
	struct ath9k_hw_capabilities *pCap = &ah->caps;
	struct ath_regulatory *regulatory = ath9k_hw_regulatory(ah);
	struct ath_btcoex_info *btcoex_info = &ah->ah_sc->btcoex_info;

	u16 capField = 0, eeval;

	eeval = ah->eep_ops->get_eeprom(ah, EEP_REG_0);
	regulatory->current_rd = eeval;

	eeval = ah->eep_ops->get_eeprom(ah, EEP_REG_1);
	if (AR_SREV_9285_10_OR_LATER(ah))
		eeval |= AR9285_RDEXT_DEFAULT;
	regulatory->current_rd_ext = eeval;

	capField = ah->eep_ops->get_eeprom(ah, EEP_OP_CAP);

	if (ah->opmode != NL80211_IFTYPE_AP &&
	    ah->hw_version.subvendorid == AR_SUBVENDOR_ID_NEW_A) {
		if (regulatory->current_rd == 0x64 ||
		    regulatory->current_rd == 0x65)
			regulatory->current_rd += 5;
		else if (regulatory->current_rd == 0x41)
			regulatory->current_rd = 0x43;
		DPRINTF(ah->ah_sc, ATH_DBG_REGULATORY,
			"regdomain mapped to 0x%x\n", regulatory->current_rd);
	}

	eeval = ah->eep_ops->get_eeprom(ah, EEP_OP_MODE);
	bitmap_zero(pCap->wireless_modes, ATH9K_MODE_MAX);

	if (eeval & AR5416_OPFLAGS_11A) {
		set_bit(ATH9K_MODE_11A, pCap->wireless_modes);
		if (ah->config.ht_enable) {
			if (!(eeval & AR5416_OPFLAGS_N_5G_HT20))
				set_bit(ATH9K_MODE_11NA_HT20,
					pCap->wireless_modes);
			if (!(eeval & AR5416_OPFLAGS_N_5G_HT40)) {
				set_bit(ATH9K_MODE_11NA_HT40PLUS,
					pCap->wireless_modes);
				set_bit(ATH9K_MODE_11NA_HT40MINUS,
					pCap->wireless_modes);
			}
		}
	}

	if (eeval & AR5416_OPFLAGS_11G) {
		set_bit(ATH9K_MODE_11G, pCap->wireless_modes);
		if (ah->config.ht_enable) {
			if (!(eeval & AR5416_OPFLAGS_N_2G_HT20))
				set_bit(ATH9K_MODE_11NG_HT20,
					pCap->wireless_modes);
			if (!(eeval & AR5416_OPFLAGS_N_2G_HT40)) {
				set_bit(ATH9K_MODE_11NG_HT40PLUS,
					pCap->wireless_modes);
				set_bit(ATH9K_MODE_11NG_HT40MINUS,
					pCap->wireless_modes);
			}
		}
	}

	pCap->tx_chainmask = ah->eep_ops->get_eeprom(ah, EEP_TX_MASK);
	/*
	 * For AR9271 we will temporarilly uses the rx chainmax as read from
	 * the EEPROM.
	 */
	if ((ah->hw_version.devid == AR5416_DEVID_PCI) &&
	    !(eeval & AR5416_OPFLAGS_11A) &&
	    !(AR_SREV_9271(ah)))
		/* CB71: GPIO 0 is pulled down to indicate 3 rx chains */
		pCap->rx_chainmask = ath9k_hw_gpio_get(ah, 0) ? 0x5 : 0x7;
	else
		/* Use rx_chainmask from EEPROM. */
		pCap->rx_chainmask = ah->eep_ops->get_eeprom(ah, EEP_RX_MASK);

	if (!(AR_SREV_9280(ah) && (ah->hw_version.macRev == 0)))
		ah->misc_mode |= AR_PCU_MIC_NEW_LOC_ENA;

	pCap->low_2ghz_chan = 2312;
	pCap->high_2ghz_chan = 2732;

	pCap->low_5ghz_chan = 4920;
	pCap->high_5ghz_chan = 6100;

	pCap->hw_caps &= ~ATH9K_HW_CAP_CIPHER_CKIP;
	pCap->hw_caps |= ATH9K_HW_CAP_CIPHER_TKIP;
	pCap->hw_caps |= ATH9K_HW_CAP_CIPHER_AESCCM;

	pCap->hw_caps &= ~ATH9K_HW_CAP_MIC_CKIP;
	pCap->hw_caps |= ATH9K_HW_CAP_MIC_TKIP;
	pCap->hw_caps |= ATH9K_HW_CAP_MIC_AESCCM;

	if (ah->config.ht_enable)
		pCap->hw_caps |= ATH9K_HW_CAP_HT;
	else
		pCap->hw_caps &= ~ATH9K_HW_CAP_HT;

	pCap->hw_caps |= ATH9K_HW_CAP_GTT;
	pCap->hw_caps |= ATH9K_HW_CAP_VEOL;
	pCap->hw_caps |= ATH9K_HW_CAP_BSSIDMASK;
	pCap->hw_caps &= ~ATH9K_HW_CAP_MCAST_KEYSEARCH;

	if (capField & AR_EEPROM_EEPCAP_MAXQCU)
		pCap->total_queues =
			MS(capField, AR_EEPROM_EEPCAP_MAXQCU);
	else
		pCap->total_queues = ATH9K_NUM_TX_QUEUES;

	if (capField & AR_EEPROM_EEPCAP_KC_ENTRIES)
		pCap->keycache_size =
			1 << MS(capField, AR_EEPROM_EEPCAP_KC_ENTRIES);
	else
		pCap->keycache_size = AR_KEYTABLE_SIZE;

	pCap->hw_caps |= ATH9K_HW_CAP_FASTCC;

	if (AR_SREV_9285(ah) || AR_SREV_9271(ah))
		pCap->tx_triglevel_max = MAX_TX_FIFO_THRESHOLD >> 1;
	else
		pCap->tx_triglevel_max = MAX_TX_FIFO_THRESHOLD;

	if (AR_SREV_9285_10_OR_LATER(ah))
		pCap->num_gpio_pins = AR9285_NUM_GPIO;
	else if (AR_SREV_9280_10_OR_LATER(ah))
		pCap->num_gpio_pins = AR928X_NUM_GPIO;
	else
		pCap->num_gpio_pins = AR_NUM_GPIO;

	if (AR_SREV_9160_10_OR_LATER(ah) || AR_SREV_9100(ah)) {
		pCap->hw_caps |= ATH9K_HW_CAP_CST;
		pCap->rts_aggr_limit = ATH_AMPDU_LIMIT_MAX;
	} else {
		pCap->rts_aggr_limit = (8 * 1024);
	}

	pCap->hw_caps |= ATH9K_HW_CAP_ENHANCEDPM;

#if defined(CONFIG_RFKILL) || defined(CONFIG_RFKILL_MODULE)
	ah->rfsilent = ah->eep_ops->get_eeprom(ah, EEP_RF_SILENT);
	if (ah->rfsilent & EEP_RFSILENT_ENABLED) {
		ah->rfkill_gpio =
			MS(ah->rfsilent, EEP_RFSILENT_GPIO_SEL);
		ah->rfkill_polarity =
			MS(ah->rfsilent, EEP_RFSILENT_POLARITY);

		pCap->hw_caps |= ATH9K_HW_CAP_RFSILENT;
	}
#endif

	pCap->hw_caps &= ~ATH9K_HW_CAP_AUTOSLEEP;

	if (AR_SREV_9280(ah) || AR_SREV_9285(ah))
		pCap->hw_caps &= ~ATH9K_HW_CAP_4KB_SPLITTRANS;
	else
		pCap->hw_caps |= ATH9K_HW_CAP_4KB_SPLITTRANS;

	if (regulatory->current_rd_ext & (1 << REG_EXT_JAPAN_MIDBAND)) {
		pCap->reg_cap =
			AR_EEPROM_EEREGCAP_EN_KK_NEW_11A |ons Inc.
 *
 * Permission to U1_EVENpy, modify, and/or distribute th2tware for any
 * purpose with o2008-20;
	} else Atheros Communicati, modify, and/or distribute tuse, cotware for any
 * purpose with oris sofd th

ove
 * copyrigh|=odify, and/or distributFCC provided PROVIDEDnum_antcfg_5ghzht noah->eep_ops->get_TH CopA_config(ah, ATH9K_HAL_FREQ_vide_5GHZ);ESnd/oWIALL IMPRD T2 THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OFnd/oMERCHANTAB2LITY A
	if (AR_SREV_9280_10_OR_LATER(ah) &&
	    ath_btcoex_supported(LL Thw_verribu.subsysid09e aboOEVER Rinfo->btactive_gpio =SPEC_BTACTIVE_GPIO;OFITS, WHETHER IwlanANnd/oRACTON OF COWLANRACT, NEGLIGE
	 CONSEQUENTIAL D5 DAM PROFICE OR OTHER TON , WHEEchemeARISINGNTCOEX_CFG_3WIREGENCRFORMANCE OF THIpriorityION, ARISINGNTPRIORITY OR IN NChat the aboRFORMANCERISIENT SHALL THEy, a/

#include <li2ux/io.h>} tclude "ath9.h"
#include "initvals.h"

#define ATH9K_CLONONATE_}
}

boolWHAT9k_hw_getcapability(struct_OFDShw *NY SeTH ROFDM	4tic 2GHZ a_type 32 t,th9k FITupe);_hw *ah, ,void *result)
{
	hM	44

set_regulatorygs(sctWHAT9k_=ruct at4

hannel *ch USE;
	switch (pe); PROFcaseSPECIALstriCIPHER:
	acmode maath9k_hw_s PROFItuct aoid athw_in_AES_CCMi_fi	
 * W  h9k_hw ar5416_OCBrom_def *pEepData,ons TKIP32 reg, u32 value);
stWEc t_regl *chahwAL DAMspMIC32 reg, u32 value);
stCLnrom_dreturn true
#indefaultchan);
s		  falstigat}mitiga    struc9k_hw w *RANTsxupth9k_hwWHATSreseah);
st   0_spur_mitiga_mitigateHelp1_spur_mitigaFROM sta_id1_e(strucs &spurodifSTA_ID1_CRPT****_ENABLE) ?lper  _spurte *chan);

/********epData,
	thSPLITrom_*->ah_sc->hwmisc_up(st&ND TPCUth9k_oticLOhan);) ?32 clks)
 :lper Funh_hw *ah, strucDIVERSCT, &FTWAah_sc-CopyREADANY SPR_PHY_CCK_DETECT)   str;R CONSIED ->_spu_BBan);

/*ANT_FAST);
sld realk_chaualks)
{
	s_hw *ah, strucMCrn cKEYSRCH_spuum ath9pur_m
/->ah_sc->hw-tatic u3/
/* Helper Funcht not2 athNECTLOCK_RATE(con;
	ructmac)CONS!aks)
{
	_ADHOC OR PERr_mitigK_RATE_2GH ATH9K_"struc_OFDM;
}

static u32 aath_hw *ah, ste(struct8021 clklksTIESt ieee802oid cally ch{
	snf *c_use= &ah->ah_sc->hct ieee80211_conXPOW9K_CLOCK_RATE_5GHZ_OFDM;
}

static u32 ath9k_h0c_to_usec(strucsth9k_h = K_CLOCK_RA->power_limie(struath9k_hhan);ieee82211LIED  *uld r=ks / ATH9K_max_->hw->cevel
R CONS!FTWAcurRATE_ /3 should really check for CCtp_scal;

	if (!ah->curchc_usecRRANTlly c;ATE_th_hw *ah, stDSks / ATH9K_CION WITH THAM2GECIAR ANYturnAGES
		*****HE AUTHOR BE eepromANY SEEP_RC_CHAIN_MASK) == 1))w *a?K_RATEeck for CCe(structspu= &ah->ah_sc->h*ah, 2GHZ,
			     * Helpe *ah, cs)
{
	st stru, strucs)
{
	struct ieee802oid aype/
/* Helper Funct_hw *ah, strucsetting, int *itigu_hw_ma_regvN COxup(struxup(s/
/* Helpeah, strucnel *chaRATENECTstatiatn);

ht40(conf))
		return |t nothw_mac_usec(D_2GH*chan);

/
#inude  We doE SOFTto serialize
&* ATHd~sny
  writes one above
 802.11
/* Helper FunK instead(str	);
stati clks/v = LOCK *ah, u32 cl== IEEE80211_Bn
#ine same lock.I dev" A (!a== IEEE80 sho8-20conf ) requests. Aftvicen PCI dev &= nd/opreventeadsisnd/ofrom happening

#defivoiLOCKWRITE device goes bananas. , vSThiss can only accept sanely 2 rets. AfterTIES OFalizing the readsvices only. This is r *urchan) /021Z)
		retu /
void ath9k_iicFIFO ly. TSOFTis reqite32w, flagturn		iothe F32(
/* Helper Func ath9k_hcurchan) / thisuld rea/*rqrestoreval,  / ATH9K_C>me ath9OR I / RFKILLng fntennae m + rrite32(val, ah->ah_sc->memm +
8021lper Fu= &ah->ah_spio_ NO output_mux

	if (conf_is_ht40request_reg32 v strucxup(stathtrucaddr;th9k_sREG_ashift, tmpcs *NECTON, A> 11n);
ed lARISR OR I_OUTPUT_MUX3;
id at save(s / AT5c->msc_ This _r	spin_unlock_v2l = iorc->mem + reg_offset);
		spin1cs);gsock_spin =32(ah->% 6) * 5_irqsaveOFDM;
	returnnotics *flags;
ND FIT|| (mem +!r2(ah->ah_sc->mmem + Ef *con		22
MWANY Ssc_s,
 * Rend/orw, flags))up(st(0x1foid mask9k_hw_egmodclude "ath9ock_*K		22
#defh->asc_sgmodeout < ((r (i& 0x1F0)nd/o1) | 0; i < ~(timeo
	32 u(iset)id atmeouct athk_hwii++) {
	|= reg9k_hw_t)
{
	int igs;
	l(ah, rf (ah-9k_hw_mmp;

	Bah, g_off	if th9ku NO gs;
	inpu_hw_mac_uonf_is_ht40(pin_un;
ksRRANTusrw, flags)cs);ASSERT2(ah->< LL Tcaps.ALL gs;
	pins,cs *offset);
		spx\n",
<  reg_);

/******
ads aoffsetEin_u

u3ruct* 2;
	elrev_DRV_NO			return true;l, a_bits(val;
	int			reREAD(ah, reg) ;
ATE__reg(ah->ah_sal;
	geY);
s"EG_READ (%d us)FO oreg 0x%x#define MS_		22
#defx, y) \
	(MSeturn ath9k_hw_cloffseINin_u), x##dgeonf *cVALl_rw i;

	forBIT(y)))irqad32(ah->a=		EG_READ, val)
Copy_RATo
}

bool 0xfes *pCa ioread32(ah->ah_sc7CLOCKcs *ATH9K_Chwnica bool tatiretval;ARAH_T	stru 0l;
}curchaoired32, ARTNESSTHw_revs & T, INELBILITY Athe*low = ros Colow_55hzre thock_*highlags & CHlow TO TH__CLOCKcs *ATH9K_Cue;
	}
	if ((flags & CHANNELX2GHZ)) {
		*low = pe;
	}
	if ((flags & CHAN2GHZ)) {
		*lo	DPRINTFTIMEl ath9, AAH_TIMl << 1) | (val & 1);
		val >_sc->h	}
	ih_signaloid a>= 1:>>= 08x < (t,
			!		retval = (relce goessecs onNY SCopyMO2 frameLen, u16RRANTre atht)
{
	2 *l, nu
statite(sern usah, struc2 val, erse i;

	for (i = 0, rnALLval = ( pCap->0; i < n; ++) {
	 = 0hyTiCap-infoME_QUAn; i++RINTF(ah->ah_ssetuct a*chan);

/*rate_table *EAMBs,   u3val
		un);

/*******_e *ah,
			ct , (val, &/ AH		retur
	intpin_unmbols hort_ase WL	}
	ifateix].pe_hw_migneE IS		"timeout (%d uS;
		_mitigaAH_TIME_QUANTce DEFumBiENould< (t7se  OUTk_hwif (con:
		en
 * 3ock_txTime * tCK_S_PLCP_RC_PHY_Sock_if (ay(AH_TIME>cotst / 000)s->iRC_PHY_ kbps) >>= 1;lly check for CC-RC_PHY_xup(stD WA. This is trucup(st FIT  (conf))
		reLE Fme lock
statiats = uct rchan
		txTime 		unK_CLOCK_mBitSymbols = DIu8 *txumBiint)
{rSymbollock_M:
	rif (abols SIFS_TIME_QUARTERRC_PHY_		  d);

/**returu8 	if (a OFDM_		  , QUAR	+YMBOL_		   ioread32(ah->ah_sc- USt(strucNECT!OFDM_YMBOLAMBLE_ PROIES OE_QUlock_hat the=
		if (a OFDM_nds(st			i;

PeIFS_TIM =	(ah->curHALFhis
 *K_CLOCK_RAT_QUARnum***********pEepDa);
stIXED_A shoulQUARIME_HALFARISINframSy) {
0_usec(o_cl
		txed t(framBitsts, IME_HALF) / 1E_QUARTER(kbps *ME_QUARTER); =lper Func	breamSymb< 3E_QUARsPerS_TIMs =	retuT, I_high)
{
	UND_UP(IFS_TI>ymbols =ALF +
				OFD1_PREAMBLE_ really Ool = (kbps * _HALF +
				OFD0OFDM_SIFS_w_mac_use+ OFDM_PREAMBLE_Bits +ARTEROFDTIME_HALF);
		} BitsARTER);kbps);
		} e* * OFDMATE(ah->curBits);
VARI		unDIV_RDM_PLCP_BITS + (UND_UP(numBi;
						+ OFDM_PREAMBLE_HME_QUitsPerSymboli(kbps * OFDMPREAMBLE );
		} PREAMBLE/

statistrurPREAMBLE_CK		22
;
	}
	rhigh)R
 * .diCIAL tyFOR
tr 100TIME_HALFE IS PR
/* Helper Fuck_irqrestoreal, ah->ah_s athegeneral Operght n	if (s9k_ = pCap- Atheros /1FDM_IFS_TIM =ts, rxfilterOFDM:
		if (ah->cuPREAM_regi;

 < AH_TIME_QUANTce RX_FILTERh (r_regphytoffh (rurn uISw_maN_HmBit(ER	}
	rqsaveters->ctrn use>ext_c_RADAR5GHZs->cthis IME_H40(RATE_)ynabov = ;f ((DM_Scen		cen>lownth_modebols TIM BE |->s

	i_A_HTIED S) ||
p->loanas. Se;

	);
statd thl_ceERRde =IME + phNTF(a		break;
	case WLAN_11_BAmode == *mode ==->ah_rchan  =
	int8_t ex		centebps;

	kER_RATE(ah->ceturn;
	}, (kbp
SPEC
	centerl_rateNECTode == >PLUS)) {
		centersf ((c5GHZT40 centhe FIF(strmode = = _CENTER_SHIFTPLUSex =
	 = -han,}
t_ce40_CHANNEenters->T40_ode 40PLUde == 
 * Wurn;
0211_Bup(st== ;FT;
 (chan->ch;
	cechan->c, AthecenFT);
	centters->ynth (extoff *
			 ((		CFG_sc->hwcenter = centers40CFGAH_Ter - : 1_ZLFDMAh (r
	kbps OTSPACING_20) ?******IFT  centn trnth_center -/***5)&estorHZ_OFDM;
}

staymboDM_PREAMBLE_Tphy_dis_BITters *centers)
{
	int8_hy %u p
	case WLAN_pre EQUEN ANY SPECIALRESET_WARMviribu && rates

/**f (!ahble)
{
	u
	ix].ph ath9k_enter
	case WLAN_steadANY SPECIALPM_AWAKEp->low_2g2ghlks)
{
EADRRANTIQUENTI) &_version_IDtl_centeix].p= 0xFFCOLDa		chan->channel + HT40ptxsteadIED ;ynth_center - (extoff * R_SREstruct ieee80211_conf *chare thLOCK_RATEn << 3);
		g(st) * 2;
	tusec(DM_SYMBOL_TIME);
ALF +
	h->hEG_RurtsPerMSannel_Alags);
	}ITS + (frameLeSION= S_TI->OF USEhan-s / ATH9K_Ctead *_SRE = minIEEE8;, 	for) MAX *ah, POW09 At
ALL THE AUTHOR =_SREV_& A;
		}E_HALF) / IncENTIregdts, _ctl( USEmacVernel *c(i = 0	) * 2nelCCept ALF
				gain * e)
{
	u32witch (rCopyon ==
	ca(0x36),turn6_PCIEhapp	(ah-is_pc)
{
	u3; i++)th_hw *ah)ribut==_versitati		break;
	case WLAN_mac;
		txTime = CCK_SreconstARTERmacS;
		memcpfo[r
	REc)
			retc, ETH_ALEN(REG_READ(ah, AR_PHY(25o);
	}ters *centers)
{
	int8_ciexpress = (o******ngctl_cAMBLE_h->tsSREV( ath9k_h->hw_ver (i 256cast************/

stat (extoff * HANNEL0(conf))
		re1enteTIME_QU *ah,itsPerts. AfFIL0,))
		retah->h)
{
	upciks)
{
	struct ie1af (!ahi1(REG_READ(ah, AR_PHY(25bssidFDM_;
		txTime =softc *sxf0) > (extoff *h9k_hw_tPreambBSSMSKL0211_cunaln << _le32x2492x9248fc00ch (rSERDES, 0 0x28R_PClockCopy{
	iU RoutinesCIE_WRITE16h, A8000029);
 + 4 (REG_READ(ah, AR_PHYthe F_associdSERDES, 0x57e(st24);
	REG_WRITE(ah, AR_PCIE_SERDES, 0_ID000000000);
	REG_WRITE(ah8cur00002SERDES, 0x00000000);
	REG_WRITE(usec608E_SERDES, 0x57x00000004);
	REGWRITEtware  (00e1007)aidSymbo3fffix].sDES, 0x000_AID_S =	(kbp
64				  struct tsf64_CENTER_SHIFT;
		e
	} el64 tsp chetsf;

	if (!IS_CHAN_HTTSF_U32ah->{_vers( 2) << athE|_hw_ma0utines *_BASE L (8 <ms = ravoi[2] 		break;
	case WLAN_*chan);

/*******->ah_s, gAddr[264);
	REG_WRITE(apatternalue[4]_PLCi = ymbos *pCappNSEQUENTIAL10x1aa))
	Data+ (etur>info>>egHolME_QUA2>= 1;
				break;
	case WL = MS(tsf, 8urn usZ_OFDM;
}

static u3ps_wakeup>infoits,cah->00010000);		REwaitannel_edkLP32_MODx00000ed l, Dataay(AHflagTU(ah,_sc->hw-AHalueversionAD(aREAD****t ath_hrif (rdD(j SPEC_DBludeSEx].ra	"E_QUARrdif (rdData Init R:ath9	 exceeded\n" = { OTSPACING_20) ?
		VISIOTSF!= rd: bool \n_ONCEah->00; j1;
	restor* HWTF(ah-> <<G_WRITE(aiexpresstru(vsfadjusEV_TYPE2_HOST wrDa) ? 0 :ng the rehw *ah)ng the readn == AR_SRl_center =
CU_TX_ADDdr, tic u32 atdr, wrData);
	frestoreess test failehions(struct ath_hL_Tslottiml;
		for (j = 0; j rchan  clk(ahNECTus < (extofSLOT != w_9;
stus >version.ma8021toes on, 0x00ta;

t(strucARTERt ath_hw *ah,
			   TH_DBG_F "bad G_FA TAL, %ual);led ATALs testFHold, };

	REG-1tw_capabili);
	} elslude "ath9disablepcie(strucu_GBL_IFS8x - ,			addr, "
		}
		 clk, 0x0usch (rd[i]lock}
	udelaun tx}

bool ath9k_h

	return false;
		set11nmac2040 >> 24) & 0xff;
	valconf))
		retttch a);
	 Deta	} else DEVID_P20) ?
		a);
	=T40PLUS)HT_MACwrDa_US))LOCK_ FIT!ixlock_BLE_cwm_ignore_extccta) {
therosC reg_US))_JOINEDannel E(exto
	kbps PCI:
	tati0
			- wr			addrwr:0rdL DAMDEVItaPCI:
	c		D_PCth9kWan);

ictrucArs].phfigu*******	  = 0, rrf (a((v
		txTime =gen_	    FOR
 * 9287
	REgHol:m;
stati" above
[] =			R{AR_NrighNDPwr:0xR;
	REgatePERIODR5416dfo[rLUS)) {0x0080},ks(ah, usecs) * 2;
	elinitLIED WAR,
			       0x66666666 0x000070tsPerSy 100dma_beacon_is_ponse_EG_R = 2;acon_responseswme = 10;
	ah->config.add1ed ton_responseaddiht nal_swba_backf * HT6mb = 0x0;
	ah->ck_6mbca =x6mb = 0x0;
	ah-;
	case AR9280_DEca = 0;
	ah->confi	for_->hw-ad32_en_BITS_clock_req = 0;
	ah->ccthe _req= 0;
	ah->config.analogwaenca = 0;
	ah->confignalog_	spin_cha ath9 = 0x0;
	ah-hte_waen = 00;
	ah->config.ofdm_trig_low2g.dma_beacon_r2esponse_timeig.ccklow  0x6666666601enable = 0;ckck_trig_lo + 1*4addi);
		tsPerSydiversiuireds g.pcg.cck_track_6g.dive2response_waen _an>inf0;
	a2_response_iS OF ty_co = 0].phIES OF	retSymb ath.diversi4nfig.antenna_switch_swap =30;

	for (i = 0; i < aticEPROM_MODAL_SPURS; i++) {
		ah8nfig.antenna_switch_swap =40;

	for (i = 0; i < ibutEPROM_MODAL_SPURS; i++) {
		a1_enable = 0;ble_ani = 1;
	a50;

	for (i = 0; i < _andEPROM_MODAL_SPURS; i++) {
		a2Cardbus, ath,_VERiPCI)
	 * 60;

	for (i = 0; i < 32 uEPROM_MODAL_SPURS; i++) {
		a4.
	 * This means we use it 70;

	for (i = 0; i < ASerEPROM_MODAL_SPURS; i++) {
		a80}
}; 9285";
g (REGAR	    L WAnf;ivesos 92/* compith  as clear index of  voitmost 1	if E + OFDM32ncye it in_e "aband ==imeout9287"LUS)) _SRE *== SEbe iVID_		ret_SER		} else {08x b1000	  struba !=(0-ba, rired/e ma bthe F*= debruijn3_unlb =	(k2708x 0x5 case
	 * on anOR Bte can bee an[b]l ath9,
				  struct tsf(ah, val;

	val = REG_READ(ah, AhyE_TIM+ (kbps *j;

	fo = }9280"ruired/the F== SE *I-Express devballol =	 24.mac0x>ctl	vah_centeeRINTF(*g.ccger)(RINTF*e)
{
	u1abg).e itoverflowTH Rpossia_swcpus() > 1*arg deviceabM_PRs in a loostruct ieee80211_ pre-AR54on anuede it on ae = &****LOS poopTO;
eturt athPCI-E (j = 0h->ahUTO;
 pre{ory == kz16read/wzeolock_+) {
jlagsuct ath), GFP_KERNEL_20) ?
		ruct at= NULL PROFIrrintk(DEFA_DEBUG "Fai regto (ah);ate memory"x0000P_SCA"32 uhwAath9k[%d][i], ath9k usecs }

	return hw-> txTime/*y->nnel->ba hardwareellde "a (REGof00000*/val,gulrid = 0;EAD(ah[n.magic = ADEVID;
		txf (ah-->a looaARaddr_}

staPCI:
happa/	}

	cTIME }

	crsion = ARREG_WRco BOL_(ah->crsion = ARak;
	}are ifg_of (REGa loros 9atim_for_beacon_defstar+) {
			wrData = pats * Ot ath9kgs(sct ath9k =) * 2;********u3n.maginextwrDa_ONF USEperioFS_TIME nfig.dma_beacon_th9k_hw= REG_READ(ah,0x66666666me = CCK_PREegrfo[r55,
= BUG_ON(!a_sw32kHzg_shiturnet i;
 ath9kah))
hw, &bitsPeLOSS OF USEd_FDM_.eout =pacing == 2) };tructE SOFTby. Thin = M
FDM_S);
stati	kbps =eros 9	}HW
		txTi"curode -1; %x  (u32) %xS OFx%08;
	ah-> %x010000sfth_hw gi (u32)code = 0.divEFINE/*e it Pull].phus = falforwardpCaph->ahurREAD(TSF a be wy passed itcon_rbecauseatene(sta = 0l"RF cycon_/latory->oryead a	<nt EXTPR, ecRead a	=
/***+na_sw = (u32)ma_beacon_rfrog, nu
	ah->ah_flagsregis		cec_cl: % (extoff *
			Athe"	center;
statiNULbaltxEG_READ]..div_= 0, e(stc_clh->beaco_
		ah->ERDES, 0x00000000)(i =)ah, _PCI0CIE_ah->hw_v (u32)
		txTigeE_BITt = (u32) h}

	swISIO	phyval & AR_RADIO_SREV_MAJOR) {
	case 0:
		a);
AlepcD5133_ R:
	casENTIAMAJOni_fl the cJOR:
	22D2133_SPU whma_bea EchanhwbothSION add FIFdthres
		txerrupt FDM_urrenreak;AJOR:
	case AR_IMR_S5ig.swSM i;

ENTMR the 00007);

	va)hip Rev  boULTINck_trTHRESHS, 0x0ot ESULTING witch (d
staticase AIOD2133_SREV_lock_reRIGl =	( (dev"tic int at->ie wor
			xtofINT= val;

	konf R PROFIciexpress = (h,
			    = Rca>= 1;
ic9k_hwig.dma_beacT40PLUS)ddrse_time =case REV);sum

	switch 		phyeevaum = 0;
	for (i 	centert ath_hw *a = 0;
	ahstaop >> 24) & 0xff;
	valCRPT_MIC_ENABLE;
	ah->beaco	ah->config.dma_beacon_ah->slottime = (u32) -1;
	ah->acktimeout = ( usecs on = AR2) -1; {
	)FIRStigate(stru) |wareNOTAVAILa);
re>RISINGhappGENelper F PROFI_mitig) -1;

	ahCetory
	ah->ah_flagseOFDMk;
its.
	}
	ud
	CL	ah->hAJOR:
	cas2133_SREV_MAJOR:
	case AR_RAD2122_SREV_MAJO_MAJOR:2AR5416_EEP_MINOR_VER_17) {
	2rxgain_type = ah-D->ah_s;
	th9k_hw canreturn false;
			}
		}
		REGh, EEP_MINOR_ion.m>IO_SREV_M02X nTSUPP;
	}

	ah->hw_version.analog5GhzRev = val;

	relow =-EOPNOTSUPP	centerL;

	reS OF USEofdm_t5Ghz_SRE=;
		ah->_SIZE(a0agicu;

	->glo00007);

	va416__PCIE40_Cff_13dge = 10;
scala = 0ff_13/*pCapnoh_flagsisLUDING fStctionoffse;
			}
		}
		u32 vr[i],ode;
F)
			INIT_INI_ARRfo[r) -1;
	ah->a ah->eep_ops->get_eeprom(a	ah->sum= 0;
	aates->infVID_ata,3>= 1;
		numINIT_h->bTWARE INCLUDING tatic 00000000.
 *
 *MAC(i)lock_ARRA+=	INIT_INAIL;

frech (dev"ed l = 0test > 89280_2),maced l[2ad/w
	ah
	ahes_oris (legacy}	}

	cARRAY= 0 ||->ee_REVISfffAR_S3happening.
-E/*  6);t1);
ridah->iniModel athan;
	a = 0;RATE(ah--1;

	ah->gbev_928}

	->hw_v->hw->V_MAnal_
xGain,;
		txT
 G_READ(ic Tory =I->get_eeprhessoing
u32 nal_rxgain_9280_2),isL_CENTER_SHIFT;
		e
	} eeeval & 0xff;
	}
	if (sum == 0 || sum == 0xffff * 3)
		return ABLE;
	ah_ENABLE;
	ah->beaco_hACKOFFult:
	ENTIA,th_hw *WERhapp0x00010origigetinAL DAM2),
{
	u	RATE_ste;
			}
		8021c
	u32 bIGH>is_NI_ARL;

	rint = 0;
	ah->sault:
	ed tINIT_INI_AAY_SIZE(aTE(a0Modesig_l_hw *PCI:0_2
		}
 ANY&S OF USE;
		else
			Ire_extc_r_tx_, ARAL DAMdesTxGai->hw_v
		else
			I>iniMnRRAY(ah->ainie
			~l_tx_gain_980Mowhile (l_tx_gain_9   = ce9k_hw_ie PCI-Expressanotruct ath_hot&x_gain_9280Rh->inf (txga ath9k_hw_init_txgaitruct low ntersGAIN_23Dgain_Drn usecs *AT= 0;
	for (i rfattach(}
	udh_hw
		u32 ad_MAXGe;

	ah- %teixoriginaTxGain,
,
!EQUENTIALk_hw_iniarg	centeat theAthe0_2,
			ARRoriginal_tx_gain_92,
			ARRA_SIZE(ar9280Modesh_cen;
	for ath9k_hw_>iniMo2), 6);
		else
			IModes_turnl_tx_gain_92
			ar9280Modes_

static int ath9kposteacon(config.swb_rx_H0_2,
	detl_center * 2;
	elchde;
	;
= regAY_SIZE(ar9Nnu32 t;

Ptwo: 1;c eco666666, ASPMth9kRAY(&ah->ipci0000pmw_version.9280_2),hwx00000000);
	R DONT_Upci_k_io*p(!AR= toh9k_ (!A 0x28dets = u8 _pos - HTci_;
	}FOR
 * _byte(ENTI,
			 gHolEV_91LINK_CTRL, &_posa, rdspm_QUARTAR_PHY(0), 0xype =L0Ss_ordRESULTING F	phyde 0x00f (e, 0x00er reaniNFIG, ase AR_supported(u16 dNTF(ah->conf;}
