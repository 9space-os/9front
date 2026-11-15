/*
 * nfront: Realtek's PCI Express card readers (RTS5209, 5227, 5229,
 * 522A, 5249, 525A; RTL8402, 8411, 8411B), an SD host for sdmmc
 * (port/sdmmc.c: the cards' protocol, #S/sdM0).  The chip runs a queue
 * of register reads and writes from memory (the command buffer: a
 * command, its response read back into the buffer) and moves a
 * transfer's data by DMA (ADMA descriptors); both end with the TRANS_OK
 * interrupt.  Its DMA reaches the first 4GB only: the data goes through
 * a bounce buffer there.  Driven as OpenBSD's rtsx does it
 * (sys/dev/ic/rtsx.c,v 1.22, rtsxreg.h,v 1.5, sys/dev/pci/rtsx_pci.c,v
 * 1.16):
 *
 * Copyright (c) 2006 Uwe Stuehler <uwe@openbsd.org>
 * Copyright (c) 2012 Stefan Sperling <stsp@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
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
#include "u.h"
#include "../port/lib.h"
#include "../port/error.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/pci.h"
#include "../port/sd.h"

/* the host's registers (32 bit) */
enum {
	Hcbar		= 0x00,		/* command buffer address */
	Hcbctlr		= 0x04,
	Hdbar		= 0x08,		/* data buffer address */
	Hdbctlr		= 0x0C,
	Haimr		= 0x10,		/* the chip's registers: address, data */
	Bipr		= 0x14,		/* interrupts pending */
	Bier		= 0x18,		/* interrupts enabled */

	StartCmd	= 1<<31,
	HwAutoRsp	= 1<<30,
	StopCmd		= 1<<28,
	TrigDma		= 1<<31,
	DmaRead		= 1<<29,
	StopDma		= 1<<28,
	AdmaMode	= 2<<26,

	HaimrBusy	= 0x80000000,
	HaimrWrite	= 0x40000000,

	SdExist		= 1<<16,
	SdInt		= 1<<25,
	TransFailInt	= 1<<28,
	TransOkInt	= 1<<29,

	/* command buffer entries */
	ReadReg		= 0,
	WriteReg	= 1,
	CheckReg	= 2,

	/* ADMA descriptors */
	SgValid		= 0x01,
	SgEnd		= 0x02,
	SgTransData	= 2<<4,
};

/* the chip's registers (8 bit, through Haimr and the command buffer) */
enum {
	Fpdctl		= 0xFC00,
	  SscPowerDown	= 0x01,
	ClkCtl		= 0xFC02,
	  ClkLowFreq	= 0x01,
	ClkDiv		= 0xFC03,
	  ClkDiv2	= 0x02,
	  ClkDiv4	= 0x03,
	  ClkDiv8	= 0x04,
	SscDivN0	= 0xFC0F,
	SscCtl1		= 0xFC11,
	  Rstb		= 0x80,
	  Ssc8xEn	= 0x40,
	  SscSel4m	= 0x10,
	SscCtl2		= 0xFC12,
	  SscDepthMask	= 0x07,
	Rcctl		= 0xFC14,
	  RcctlF2m	= 0x01,
	OltLedCtl	= 0xFC1E,
	  OltLedPeriod	= 0x02,
	  OltLedAutoblink	= 0x08,
	GpioCtl		= 0xFC1F,
	  GpioLedOn	= 0x02,
	CardClkSource	= 0xFC2E,
	  CrcFixClk	= 0x00,
	  Sd30VarClk0	= 1<<2,
	  SampleVarClk1	= 2<<4,

	PpbufBase2	= 0xFA00,

	CardPwrCtl	= 0xFD50,
	  SdPartialPwrOn	= 0x01,
	  SdPwrOff	= 0x03,
	  PmosStrg800mA	= 0x10,
	CardShareMode	= 0xFD52,
	  CardShare48Sd	= 0x04,
	CardStop	= 0xFD54,
	  SdStop	= 0x04,
	  SdClrErr	= 0x40,
	CardOe		= 0xFD55,
	  XdOutputEn	= 0x02,
	  SdOutputEn	= 0x04,
	  MsOutputEn	= 0x08,
	CardAutoBlink	= 0xFD56,
	  LedBlinkEn	= 0x08,
	  LedBlinkSpeed	= 0x05,
	CardGpioDir	= 0xFD57,
	CardGpio	= 0xFD58,
	  CardGpioLedOff	= 0x01,
	CardDataSource	= 0xFD5B,
	  RingBuffer	= 0x00,
	  PingpongBuffer	= 0x01,
	CardSelect	= 0xFD5C,
	  SdModSel	= 0x02,
	CardPullCtl1	= 0xFD60,
	CardPullCtl2	= 0xFD61,
	CardPullCtl3	= 0xFD62,
	  PullCtlEnable12	= 0xAA,
	  PullCtlDisable12	= 0x55,
	  PullCtlEnable3	= 0xE9,
	  PullCtlDisable3	= 0xD5,
	  PullCtlEnable3TypeC	= 0xD9,
	  PullCtlDisable3TypeC	= 0xE5,
	CardClkEn	= 0xFD69,
	  XdClkEn	= 0x02,
	  SdClkEn	= 0x04,
	  MsClkEn	= 0x08,
	  SpiClkEn	= 0x10,
	  CardClkEnAll	= XdClkEn|SdClkEn|MsClkEn|SpiClkEn,

	SdCfg1		= 0xFDA0,
	  BusWidth1	= 0x00,
	  BusWidth4	= 0x01,
	  BusWidth8	= 0x02,
	  BusWidthMask	= 0x03,
	  SdModeMask	= 0x0C,
	  ClkDivide128	= 0x80,
	  ClkDivideMask	= 0xC0,
	SdCfg2		= 0xFDA1,
	  RspTypeR0	= 0x04,
	  RspTypeR1	= 0x01,
	  RspTypeR1b	= 0x09,
	  RspTypeR2	= 0x02,
	  RspTypeR3	= 0x05,
	  RspTypeR6	= 0x01,
	  RspTypeR7	= 0x01,
	  NoCalculateCrc7	= 0x80,
	  NoCheckWaitCrcTo	= 0x20,
	  NoCheckCrc7	= 0x04,
	SdStat1		= 0xFDA3,
	  SdCrcErr	= 0x80|0x40|0x20,
	SdBusStat	= 0xFDA5,
	  SdClkForceStop	= 0x40,
	SdSamplePointCtl	= 0xFDA7,
	  Sd20RxSelMask	= 0x08,
	SdPushPointCtl	= 0xFDA8,
	  Sd20TxNegEdge	= 0x00,
	SdCmd0		= 0xFDA9,
	SdCmd1		= 0xFDAA,
	SdCmd2		= 0xFDAB,
	SdCmd3		= 0xFDAC,
	SdCmd4		= 0xFDAD,
	SdCmd5		= 0xFDAE,
	SdByteCntL	= 0xFDAF,
	SdByteCntH	= 0xFDB0,
	SdBlockCntL	= 0xFDB1,
	SdBlockCntH	= 0xFDB2,
	SdTransfer	= 0xFDB3,
	  TmAutoWrite3	= 0x01,
	  TmAutoRead3	= 0x05,
	  TmCmdRsp	= 0x08,
	  SdStatIdle	= 0x20,
	  SdTransferEnd	= 0x40,
	  SdTransferStart	= 0x80,

	Irqstat0	= 0xFE21,
	  LinkReadyInt	= 0x20,
	  DmaDoneInt	= 0x80,
	Dmatc0		= 0xFE28,
	Dmatc1		= 0xFE29,
	Dmatc2		= 0xFE2A,
	Dmatc3		= 0xFE2B,
	Dmactl		= 0xFE2C,
	  DmaEn		= 0x01,
	  DmaDirToCard	= 0x00,
	  DmaDirFromCard	= 0x02,
	  Dma512	= 2<<4,
	  DmaPackSizeMask	= 0x30,
	  DmaRst	= 0x80,
	Rbctl		= 0xFE34,
	  RbFlush	= 0x80,
	PhyRwctl	= 0xFE3C,
	  PhyBusy	= 0x80,
	  PhyWrite	= 0x01,
	PhyData0	= 0xFE3D,
	PhyData1	= 0xFE3E,
	PhyAddr		= 0xFE3F,
	Petxcfg		= 0xFE49,
	  PetxcfgClkreqPin	= 0x08,
	ChangeLinkState	= 0xFE5B,
	  ForceRstCoreEn	= 0x02,
	  NonStickyRstNDbg	= 0x08,
	  MacPhyRstNDbg	= 0x10,
	PerstGlitchWidth	= 0xFE5C,
	Sd30DriveSel	= 0xFE5E,
	  Sd30DriveSel3v3	= 0x01,
	HostSleepState	= 0xFE60,
	  HostEnterS1	= 0x01,
	  HostEnterS3	= 0x02,
	NftsTxCtrl	= 0xFE72,
	  IntReadClr	= 0x02,
	PwrGateCtrl	= 0xFE75,
	  Ldo3318Vcc1	= 0x02,
	  Ldo3318Vcc2	= 0x04,
	  Ldo3318Suspend	= 0x04,
	  Ldo3318Off	= 0x06,
	LdoPwrSel	= 0xFE78,
	  LdoPwrSel3v3	= 0x01,
	  LdoPwrSelDv33	= 0x03,
	DummyReg	= 0xFE90,
	LdoVccCfg1	= 0xFF73,
	  LdoVccTuneMask	= 0x07,
	  LdoVcc3v3	= 0x07,
};

enum {
	/* flags */
	F5209		= 1<<0,
	F5229		= 1<<1,
	F5229TypeC	= 1<<2,
	F525A		= 1<<3,

	Ncmd		= 256,		/* command buffer entries */
	Nbounce		= SDmaxio,
	Nseg		= 0x80000,	/* an ADMA descriptor's bytes at most */
	Ndesc		= Nbounce/Nseg,
};

typedef struct Ctlr Ctlr;
struct Ctlr {
	Lock;
	Rendez	r;
	Pcidev	*pdev;
	u32int	*regs;
	int	flags;
	int	present;	/* a card in the slot */
	int	change;		/* inserted or removed since */
	u32int	status;		/* TRANS_OK, TRANS_FAIL from the interrupt */

	u32int	*cmdbuf;	/* Ncmd entries, below 4GB */
	uvlong	*desc;		/* Ndesc ADMA descriptors */
	uchar	*bounce;	/* Nbounce bytes */

	/* the transfer iosetup describes */
	int	write;
	uchar	*buf;
	int	bsize;
	int	bcount;
};

static u32int
rd(Ctlr *c, int r)
{
	return ((volatile u32int*)c->regs)[r/4];
}

static void
wr(Ctlr *c, int r, u32int v)
{
	((volatile u32int*)c->regs)[r/4] = v;
}

/* a chip register: -1 on timeout */
static int
rdreg(Ctlr *c, int addr)
{
	u32int v;
	int i;

	wr(c, Haimr, HaimrBusy | (addr & 0x3FFF)<<16);
	for(i = 0; i < 1024; i++){
		v = rd(c, Haimr);
		if((v & HaimrBusy) == 0)
			return v & 0xFF;
	}
	return -1;
}

static int
wrreg(Ctlr *c, int addr, int mask, int val)
{
	u32int v;
	int i;

	wr(c, Haimr, HaimrBusy | HaimrWrite | (addr & 0x3FFF)<<16 | (mask & 0xFF)<<8 | (val & 0xFF));
	for(i = 0; i < 1024; i++){
		v = rd(c, Haimr);
		if((v & HaimrBusy) == 0)
			return (v & 0xFF) == (val & 0xFF) ? 0 : -1;
	}
	return -1;
}

/* as OpenBSD's RTSX_WRITE, RTSX_SET, RTSX_CLR: return -1 if it fails */
#define	REGWR(c, a, v)	if(wrreg(c, a, 0xFF, v) < 0) return -1
#define	REGSET(c, a, b)	if(wrreg(c, a, b, 0xFF) < 0) return -1
#define	REGCLR(c, a, b)	if(wrreg(c, a, b, 0) < 0) return -1

static int
wrphy(Ctlr *c, int addr, int val)
{
	int i, v;

	REGWR(c, PhyData0, val);
	REGWR(c, PhyData1, val>>8);
	REGWR(c, PhyAddr, addr);
	REGWR(c, PhyRwctl, PhyBusy|PhyWrite);
	for(i = 0; i < 100000; i++){
		if((v = rdreg(c, PhyRwctl)) < 0)
			return -1;
		if((v & PhyBusy) == 0)
			return 0;
	}
	return -1;
}

static int
chipinit(Ctlr *c)
{
	u32int status;
	int v;

	if(c->flags & F5229){
		if((v = rdreg(c, DummyReg)) < 0)
			return -1;
		switch(v & 0x0F){
		case 0:	/* versions A, B, D */
		case 1:
		case 3:
			break;
		case 2:
			c->flags |= F5229TypeC;
			break;
		default:
			print("rtsx: unknown ic %.2ux\n", v);
			return -1;
		}
	}

	/* interrupts cleared by writing them back */
	REGCLR(c, NftsTxCtrl, IntReadClr);
	status = rd(c, Bipr);
	wr(c, Bipr, status);
	c->present = (status & SdExist) != 0;
	wr(c, Bier, TransOkInt | TransFailInt | SdInt);

	/* the SSC clock */
	REGCLR(c, Fpdctl, SscPowerDown);
	microdelay(200);

	/* OpenBSD: "magic numbers from linux driver" */
	if(wrphy(c, 0x00, (c->flags & F5209) ? 0xB966 : 0xBA42) < 0){
		print("rtsx: cannot write phy register\n");
		return -1;
	}
	REGSET(c, ClkDiv, 0x07);

	/* no sleep, no card clock */
	REGCLR(c, HostSleepState, HostEnterS1|HostEnterS3);
	REGCLR(c, CardClkEn, CardClkEnAll);

	REGCLR(c, ChangeLinkState, ForceRstCoreEn|NonStickyRstNDbg|0x04);
	REGWR(c, Sd30DriveSel, Sd30DriveSel3v3);

	REGWR(c, SscCtl1, Ssc8xEn|SscSel4m);
	REGWR(c, SscCtl2, 0x12);

	REGSET(c, ChangeLinkState, MacPhyRstNDbg);
	REGSET(c, Irqstat0, LinkReadyInt);
	REGWR(c, PerstGlitchWidth, 0x80);

	/* RC oscillator at 400K, the clock requested (CLKREQ low) */
	REGCLR(c, Rcctl, RcctlF2m);
	REGSET(c, Petxcfg, PetxcfgClkreqPin);

	/* the LED's GPIO */
	if(c->flags & F5209){
		REGWR(c, CardGpio, 0x03);
		REGWR(c, CardGpioDir, 0x03);
	} else {
		REGSET(c, GpioCtl, GpioLedOn);
		/* LDO3318 from DV33 to 3V3 */
		REGCLR(c, LdoPwrSel, LdoPwrSelDv33);
		REGSET(c, LdoPwrSel, LdoPwrSel3v3);
		REGSET(c, OltLedCtl, OltLedPeriod);
	}
	return 0;
}

static int
stopclock(Ctlr *c)
{
	REGCLR(c, CardClkEn, CardClkEnAll);
	REGSET(c, SdBusStat, SdClkForceStop);
	return 0;
}

/*
 * PwrGateCtrl: on the 5209 a mask of the power gates disabled, on the
 * others of those enabled
 */
static int
poweroff(Ctlr *c)
{
	int disable3;

	if(stopclock(c) < 0)
		return -1;
	REGCLR(c, CardOe, XdOutputEn|SdOutputEn|MsOutputEn);
	disable3 = PullCtlDisable3;
	if(c->flags & F5209){
		REGSET(c, PwrGateCtrl, Ldo3318Off);
	} else {
		REGCLR(c, PwrGateCtrl, Ldo3318Vcc1|Ldo3318Vcc2);
		if(c->flags & F5229TypeC)
			disable3 = PullCtlDisable3TypeC;
	}
	REGSET(c, CardPwrCtl, SdPwrOff);
	REGCLR(c, CardPwrCtl, PmosStrg800mA);
	REGWR(c, CardPullCtl1, PullCtlDisable12);
	REGWR(c, CardPullCtl2, PullCtlDisable12);
	REGWR(c, CardPullCtl3, disable3);
	return 0;
}

static int
poweron(Ctlr *c)
{
	if(c->flags & F525A)
		if(wrreg(c, LdoVccCfg1, LdoVccTuneMask, LdoVcc3v3) < 0)
			return -1;

	/* the SD card */
	REGWR(c, CardSelect, SdModSel);
	REGWR(c, CardShareMode, CardShare48Sd);
	REGSET(c, CardClkEn, SdClkEn);

	REGWR(c, CardPullCtl1, PullCtlEnable12);
	REGWR(c, CardPullCtl2, PullCtlEnable12);
	REGWR(c, CardPullCtl3, (c->flags & F5229TypeC) ? PullCtlEnable3TypeC : PullCtlEnable3);

	/* the card's power in two steps (no current peak) */
	REGSET(c, CardPwrCtl, SdPartialPwrOn);
	if(c->flags & F5209)
		REGSET(c, PwrGateCtrl, Ldo3318Suspend);
	else
		REGSET(c, PwrGateCtrl, Ldo3318Vcc1);
	microdelay(200);
	REGCLR(c, CardPwrCtl, SdPwrOff);
	if(c->flags & F5209)
		REGCLR(c, PwrGateCtrl, Ldo3318Off);
	else
		REGSET(c, PwrGateCtrl, Ldo3318Vcc2);

	REGWR(c, CardOe, SdOutputEn);
	return 0;
}

static int
buswidth(Ctlr *c, int w)
{
	return wrreg(c, SdCfg1, BusWidthMask, w == 8 ? BusWidth8 : w == 4 ? BusWidth4 : BusWidth1);
}

/* the SD clock: 400kHz, 25MHz, 50MHz (rounded down) */
static int
setclock(Ctlr *c, int hz)
{
	int n, div;

	if(hz == 0)
		return stopclock(c);
	if(hz >= 50000000){
		n = 100;
		div = ClkDiv2;
		REGCLR(c, SdCfg1, ClkDivideMask);
	} else if(hz >= 25000000){
		n = 100;
		div = ClkDiv4;
		REGCLR(c, SdCfg1, ClkDivideMask);
	} else {
		n = 80;
		div = ClkDiv8;
		REGSET(c, SdCfg1, ClkDivide128);
	}

	/* SD 2.0 mode */
	REGCLR(c, SdCfg1, SdModeMask);
	REGSET(c, ClkCtl, ClkLowFreq);
	REGWR(c, CardClkSource, CrcFixClk|Sd30VarClk0|SampleVarClk1);
	REGCLR(c, SdSamplePointCtl, Sd20RxSelMask);
	REGWR(c, SdPushPointCtl, Sd20TxNegEdge);
	REGWR(c, ClkDiv, div<<4 | 7);
	REGCLR(c, SscCtl1, Rstb);
	REGCLR(c, SscCtl2, SscDepthMask);
	REGWR(c, SscDivN0, n);
	REGSET(c, SscCtl1, Rstb);
	microdelay(100);
	REGCLR(c, ClkCtl, ClkLowFreq);
	return 0;
}

static void
softreset(Ctlr *c)
{
	wr(c, Hcbctlr, StopCmd);
	wrreg(c, CardStop, SdStop|SdClrErr, SdStop|SdClrErr);
	wr(c, Hdbctlr, StopDma);
	wrreg(c, Dmactl, DmaRst, DmaRst);
	wrreg(c, Rbctl, RbFlush, RbFlush);
}

static void
led(Ctlr *c, int on)
{
	if(c->flags & F5209){
		if(on){
			wrreg(c, CardGpio, CardGpioLedOff, 0);
			wrreg(c, CardAutoBlink, 0xFF, LedBlinkEn|LedBlinkSpeed);
		} else {
			wrreg(c, CardAutoBlink, LedBlinkEn, 0);
			wrreg(c, CardGpio, 0xFF, CardGpioLedOff);
		}
	} else {
		if(on){
			wrreg(c, GpioCtl, GpioLedOn, 0xFF);
			wrreg(c, OltLedCtl, OltLedAutoblink, 0xFF);
		} else {
			wrreg(c, OltLedCtl, OltLedAutoblink, 0);
			wrreg(c, GpioCtl, GpioLedOn, 0);
		}
	}
}

/* power the slot on (a card in it): at enable and after a change; -1 */
static int
reset(Ctlr *c)
{
	c->change = 0;
	if(chipinit(c) < 0 || poweroff(c) < 0)
		return -1;
	microdelay(200);
	if(c->present)
		if(poweron(c) < 0 || buswidth(c, 1) < 0 || setclock(c, 400000) < 0)
			return -1;
	led(c, c->present);
	return 0;
}

static void
interrupt(Ureg*, void *a)
{
	Ctlr *c = a;
	u32int on, status;

	on = rd(c, Bier);
	status = rd(c, Bipr);
	wr(c, Bipr, status);
	if((on & status) == 0 || status == 0xFFFFFFFF)
		return;
	ilock(c);
	if(status & SdInt){
		c->present = (status & SdExist) != 0;
		c->change = 1;
	}
	if(status & (TransOkInt|TransFailInt)){
		c->status |= status & (TransOkInt|TransFailInt);
		wakeup(&c->r);
	}
	iunlock(c);
}

static int
done(void *a)
{
	return ((Ctlr*)a)->status != 0;
}

/* TRANS_OK within ms, or error */
static void
waitintr(Ctlr *c, int ms)
{
	u32int status;

	if(!waserror()){
		tsleep(&c->r, done, c, ms);
		poperror();
	}
	ilock(c);
	status = c->status;
	c->status = 0;
	iunlock(c);
	if(status == 0){
		softreset(c);
		error("rtsx: timeout");
	}
	if(!c->present)
		error("no card");
	if(status & TransFailInt){
		softreset(c);
		error(Eio);
	}
}

static void
hostcmd(Ctlr *c, int *n, int cmd, int reg, int mask, int data)
{
	if(*n >= Ncmd)
		panic("rtsx: command buffer");
	c->cmdbuf[(*n)++] = (u32int)(cmd & 3)<<30 | (u32int)(reg & 0x3FFF)<<16 | (mask & 0xFF)<<8 | (data & 0xFF);
}

static void
runcmds(Ctlr *c, int n, int ms)
{
	ilock(c);
	c->status = 0;
	iunlock(c);
	coherence();
	wr(c, Hcbar, PADDR(c->cmdbuf));
	wr(c, Hcbctlr, (n*4 & 0x00FFFFFF) | StartCmd | HwAutoRsp);
	waitintr(c, ms);
}

/*
 * the data after a command's response (the transfer modes AUTO_READ3,
 * AUTO_WRITE3: the command is sent, sdmmc stops a multiple block one),
 * through the bounce buffer
 */
static void
xfer(Ctlr *c)
{
	int n, len, i, o, cfg2, tmode, dir;
	u32int pa;

	len = c->bsize * c->bcount;
	cfg2 = NoCheckWaitCrcTo;	/* and CHECK_CRC16, NO_WAIT_BUSY_END, RSP_LEN_0: 0 */
	if(c->write){
		dir = DmaDirToCard;
		tmode = TmAutoWrite3;
		cfg2 |= NoCalculateCrc7 | NoCheckCrc7;
		memmove(c->bounce, c->buf, len);
	} else {
		dir = DmaDirFromCard;
		tmode = TmAutoRead3;
	}

	n = 0;
	hostcmd(c, &n, WriteReg, SdCfg2, 0xFF, cfg2);
	hostcmd(c, &n, WriteReg, SdByteCntL, 0xFF, c->bsize);
	hostcmd(c, &n, WriteReg, SdByteCntH, 0xFF, c->bsize>>8);
	hostcmd(c, &n, WriteReg, SdBlockCntL, 0xFF, c->bcount);
	hostcmd(c, &n, WriteReg, SdBlockCntH, 0xFF, c->bcount>>8);
	hostcmd(c, &n, WriteReg, CardDataSource, 0x01, RingBuffer);
	hostcmd(c, &n, WriteReg, Irqstat0, DmaDoneInt, DmaDoneInt);
	hostcmd(c, &n, WriteReg, Dmatc3, 0xFF, len>>24);
	hostcmd(c, &n, WriteReg, Dmatc2, 0xFF, len>>16);
	hostcmd(c, &n, WriteReg, Dmatc1, 0xFF, len>>8);
	hostcmd(c, &n, WriteReg, Dmatc0, 0xFF, len);
	hostcmd(c, &n, WriteReg, Dmactl, 0x03|DmaPackSizeMask, dir|DmaEn|Dma512);
	hostcmd(c, &n, WriteReg, SdTransfer, 0xFF, tmode|SdTransferStart);
	hostcmd(c, &n, CheckReg, SdTransfer, SdTransferEnd, SdTransferEnd);

	/* the queue runs; its TRANS_OK comes after the data's */
	ilock(c);
	c->status = 0;
	iunlock(c);
	coherence();
	wr(c, Hcbar, PADDR(c->cmdbuf));
	wr(c, Hcbctlr, (n*4 & 0x00FFFFFF) | StartCmd | HwAutoRsp);

	/* the data: ADMA descriptors over the bounce buffer */
	pa = PADDR(c->bounce);
	for(i = o = 0; o < len; i++, o += Nseg){
		n = len - o > Nseg ? Nseg : len - o;
		c->desc[i] = (uvlong)(pa + o)<<32 | (uvlong)(n & 0x00FFFFFF)<<12
			| SgValid | SgTransData | (o + n >= len ? SgEnd : 0);
	}
	coherence();
	wr(c, Hdbar, PADDR(c->desc));
	wr(c, Hdbctlr, AdmaMode | TrigDma | (c->write ? 0 : DmaRead));
	if(waserror()){
		if((n = rdreg(c, SdStat1)) >= 0 && (n & SdCrcErr) != 0)
			print("rtsx: CRC error\n");
		nexterror();
	}
	waitintr(c, 10000);
	poperror();
}

static int
rsptype(SDiocmd *cmd)
{
	switch(cmd->resp){
	case 0:
		return RspTypeR0;
	case 1:
		return cmd->busy ? RspTypeR1b : RspTypeR1;
	case 2:
		return RspTypeR2;
	case 3:
		return RspTypeR3;
	case 6:
		return RspTypeR6;
	case 7:
		return RspTypeR7;
	}
	return RspTypeR1;
}

static int
rtsxcmd(SDio *io, SDiocmd *cmd, u32int arg, u32int *resp)
{
	Ctlr *c = io->aux;
	uchar *b;
	int n, r, type, i;

	if(c->change && reset(c) < 0)
		error(Eio);
	if(!c->present)
		error("no card");

	type = rsptype(cmd);
	n = 0;
	hostcmd(c, &n, WriteReg, SdCmd0, 0xFF, 0x40 | cmd->index);
	hostcmd(c, &n, WriteReg, SdCmd1, 0xFF, arg>>24);
	hostcmd(c, &n, WriteReg, SdCmd2, 0xFF, arg>>16);
	hostcmd(c, &n, WriteReg, SdCmd3, 0xFF, arg>>8);
	hostcmd(c, &n, WriteReg, SdCmd4, 0xFF, arg);
	hostcmd(c, &n, WriteReg, SdCfg2, 0xFF, type);
	hostcmd(c, &n, WriteReg, CardDataSource, 0x01, PingpongBuffer);
	hostcmd(c, &n, WriteReg, SdTransfer, 0xFF, TmCmdRsp|SdTransferStart);
	hostcmd(c, &n, CheckReg, SdTransfer, SdTransferEnd|SdStatIdle, SdTransferEnd|SdStatIdle);
	/* the response read back into the buffer, a byte per read after the check's */
	if(type == RspTypeR2){
		for(r = PpbufBase2 + 15; r > PpbufBase2; r--)
			hostcmd(c, &n, ReadReg, r, 0, 0);
		hostcmd(c, &n, ReadReg, SdCmd5, 0, 0);
	} else if(type != RspTypeR0){
		for(r = SdCmd0; r <= SdCmd4; r++)
			hostcmd(c, &n, ReadReg, r, 0, 0);
	}
	runcmds(c, n, 1000);

	b = (uchar*)c->cmdbuf;
	if(type == RspTypeR2){
		/*
		 * bytes 1 to 15: the response's bits 8 to 127 (no CRC);
		 * sdmmc's resp[] has bit i of the response at bit i
		 */
		resp[0] = b[1]<<8 | b[2]<<16 | b[3]<<24;
		for(i = 1; i < 4; i++)
			resp[i] = b[4*i] | b[4*i+1]<<8 | b[4*i+2]<<16 | b[4*i+3]<<24;
	} else if(type != RspTypeR0)
		/* bytes 2 to 5: the response's bits 39 to 8 (byte 1: the command) */
		resp[0] = b[2]<<24 | b[3]<<16 | b[4]<<8 | b[5];
	else
		resp[0] = 0;

	if(cmd->data != 0){
		if(c->buf == nil || c->bsize*c->bcount <= 0)
			error(Egreg);
		xfer(c);
	}
	return 0;
}

static void
rtsxiosetup(SDio *io, int write, void *buf, int bsize, int bcount)
{
	Ctlr *c = io->aux;

	if(bsize <= 0 || bcount <= 0 || bsize > 0xFFFF || bcount > 0xFFFF
	|| (vlong)bsize*bcount > Nbounce)
		error(Egreg);
	c->write = write;
	c->buf = buf;
	c->bsize = bsize;
	c->bcount = bcount;
}

/* the data moved in rtsxcmd already: a read's from the bounce buffer */
static void
rtsxio(SDio *io, int write, uchar *buf, int len)
{
	Ctlr *c = io->aux;

	if(len != c->bsize*c->bcount || write != c->write)
		error(Egreg);
	if(!write)
		memmove(buf, c->bounce, len);
	c->buf = nil;
}

static void
rtsxbus(SDio *io, int width, int speed)
{
	Ctlr *c = io->aux;

	if(width != 0 && buswidth(c, width) < 0)
		error(Eio);
	if(speed != 0 && setclock(c, speed) < 0)
		error(Eio);
}

static int
rtsxinquiry(SDio*, char *inquiry, int inqlen)
{
	return snprint(inquiry, inqlen, "Realtek Card Reader");
}

static void
rtsxenable(SDio *io)
{
	Ctlr *c = io->aux;

	if(reset(c) < 0)
		print("rtsx: %T: can't reset\n", c->pdev->tbdf);
	intrenable(c->pdev->intl, interrupt, c, c->pdev->tbdf, io->name);
}

/* DMA memory below 4GB, once */
static void*
low(int n, int align)
{
	void *v;

	if((v = xspanalloc(n, align, 0)) == nil)
		return nil;
	if(PADDR(v) + n > 0x100000000ULL){
		print("rtsx: no memory below 4GB for DMA\n");
		return nil;
	}
	return v;
}

static int
rtsxinit(SDio *io)
{
	static Pcidev *p;
	Ctlr *c;
	int flags, bar;

	while((p = pcimatch(p, 0x10EC, 0)) != nil){
		/* the undefined class only: some 5209 are also SDHCI, pmmc's */
		if(p->ccrb != 0xFF)
			continue;
		bar = 0;
		switch(p->did){
		case 0x5209:
			flags = F5209;
			break;
		case 0x5229:
		case 0x5249:
			flags = F5229;
			break;
		case 0x525A:
			flags = F525A;
			bar = 1;
			break;
		case 0x5227:
		case 0x522A:
		case 0x5286:	/* RTL8402 */
		case 0x5287:	/* RTL8411B */
		case 0x5289:	/* RTL8411 */
			flags = 0;
			break;
		default:
			continue;
		}
		if(pcicfgr32(p, 0x1C) & 0x10){	/* RTSX_CFG_PCI: RTSX_CFG_ASIC */
			print("rtsx: %T: no asic\n", p->tbdf);
			continue;
		}
		if(p->mem[bar].bar & 1 || p->mem[bar].size == 0){
			print("rtsx: %T: no registers\n", p->tbdf);
			continue;
		}
		if((c = malloc(sizeof(Ctlr))) == nil)
			return -1;
		c->regs = vmap(p->mem[bar].bar & ~0xF, p->mem[bar].size);
		if(c->regs == nil){
			print("rtsx: %T: can't map registers\n", p->tbdf);
			free(c);
			continue;
		}
		c->cmdbuf = low(Ncmd*4, 64);
		c->desc = low(Ndesc*8, 64);
		c->bounce = low(Nbounce, BY2PG);
		if(c->cmdbuf == nil || c->desc == nil || c->bounce == nil){
			print("rtsx: %T: no DMA memory\n", p->tbdf);
			vunmap(c->regs, p->mem[bar].size);
			free(c);
			return -1;	/* (xspanalloc'd memory is not freed) */
		}
		c->pdev = p;
		c->flags = flags;
		pcienable(p);
		pcisetbme(p);
		io->aux = c;
		print("rtsx: %T: RTS%.4ux\n", p->tbdf, p->did);
		return 1;	/* probe again for the next one */
	}
	return -1;
}

void
rtsxlink(void)
{
	static SDio io = {
		"rtsx",
		rtsxinit,
		rtsxenable,
		rtsxinquiry,
		rtsxcmd,
		rtsxiosetup,
		rtsxio,
		rtsxbus,
	};
	addmmcio(&io);
}
