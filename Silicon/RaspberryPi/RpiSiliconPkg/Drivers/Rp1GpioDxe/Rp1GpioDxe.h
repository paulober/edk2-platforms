/** @file
 *
 *  Copyright (c) 2025, Paul Oberosler <paul@paulober.dev>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef __RP1_GPIO_DXE_H__
#define __RP1_GPIO_DXE_H__

#include <Protocol/DriverBinding.h>
#include <Protocol/ComponentName.h>
#include <Protocol/ComponentName2.h>
#include <Protocol/Rp1GpioDevice.h>
#include <Protocol/Rp1Gpio.h>

#include <Rp1.h>

#define RP1_GPIO_DXE_DRIVER_VERSION 0x1

#define RP1_RW_OFFSET			0x0000
#define RP1_XOR_OFFSET			0x1000
#define RP1_SET_OFFSET			0x2000
#define RP1_CLR_OFFSET			0x3000

#define RP1_GPIO_STATUS			0x0000
#define RP1_GPIO_CTRL			0x0004

#define RP1_GPIO_PCIE_INTE		0x011c
#define RP1_GPIO_PCIE_INTS		0x0124

#define RP1_GPIO_EVENTS_SHIFT_RAW	20
#define RP1_GPIO_STATUS_FALLING		BIT(20)
#define RP1_GPIO_STATUS_RISING		BIT(21)
#define RP1_GPIO_STATUS_LOW		BIT(22)
#define RP1_GPIO_STATUS_HIGH		BIT(23)

#define RP1_GPIO_EVENTS_SHIFT_FILTERED	24
#define RP1_GPIO_STATUS_F_FALLING	BIT(24)
#define RP1_GPIO_STATUS_F_RISING	BIT(25)
#define RP1_GPIO_STATUS_F_LOW		BIT(26)
#define RP1_GPIO_STATUS_F_HIGH		BIT(27)

#define RP1_GPIO_CTRL_FUNCSEL_LSB	0
#define RP1_GPIO_CTRL_FUNCSEL_MASK	0x0000001f
#define RP1_GPIO_CTRL_OUTOVER_LSB	12
#define RP1_GPIO_CTRL_OUTOVER_MASK	0x00003000
#define RP1_GPIO_CTRL_OEOVER_LSB	14
#define RP1_GPIO_CTRL_OEOVER_MASK	0x0000c000
#define RP1_GPIO_CTRL_INOVER_LSB	16
#define RP1_GPIO_CTRL_INOVER_MASK	0x00030000
#define RP1_GPIO_CTRL_IRQEN_FALLING	BIT(20)
#define RP1_GPIO_CTRL_IRQEN_RISING	BIT(21)
#define RP1_GPIO_CTRL_IRQEN_LOW		BIT(22)
#define RP1_GPIO_CTRL_IRQEN_HIGH	BIT(23)
#define RP1_GPIO_CTRL_IRQEN_F_FALLING	BIT(24)
#define RP1_GPIO_CTRL_IRQEN_F_RISING	BIT(25)
#define RP1_GPIO_CTRL_IRQEN_F_LOW	BIT(26)
#define RP1_GPIO_CTRL_IRQEN_F_HIGH	BIT(27)
#define RP1_GPIO_CTRL_IRQRESET		BIT(28)
#define RP1_GPIO_CTRL_IRQOVER_LSB	30
#define RP1_GPIO_CTRL_IRQOVER_MASK	0xc0000000

#define RP1_INT_EDGE_FALLING		BIT(0)
#define RP1_INT_EDGE_RISING		BIT(1)
#define RP1_INT_LEVEL_LOW		BIT(2)
#define RP1_INT_LEVEL_HIGH		BIT(3)
#define RP1_INT_MASK			0xf

#define RP1_INT_EDGE_BOTH		(RP1_INT_EDGE_FALLING |	\
					 RP1_INT_EDGE_RISING)
#define RP1_PUD_OFF			0
#define RP1_PUD_DOWN			1
#define RP1_PUD_UP			2

#define RP1_DIR_OUTPUT			0
#define RP1_DIR_INPUT			1

#define RP1_OUTOVER_PERI		0
#define RP1_OUTOVER_INVPERI		1
#define RP1_OUTOVER_LOW			2
#define RP1_OUTOVER_HIGH		3

#define RP1_OEOVER_PERI			0
#define RP1_OEOVER_INVPERI		1
#define RP1_OEOVER_DISABLE		2
#define RP1_OEOVER_ENABLE		3

#define RP1_INOVER_PERI			0
#define RP1_INOVER_INVPERI		1
#define RP1_INOVER_LOW			2
#define RP1_INOVER_HIGH			3

#define RP1_RIO_OUT			0x00
#define RP1_RIO_OE			0x04
#define RP1_RIO_IN			0x08

#define RP1_PAD_SLEWFAST_MASK		0x00000001
#define RP1_PAD_SLEWFAST_LSB		0
#define RP1_PAD_SCHMITT_MASK		0x00000002
#define RP1_PAD_SCHMITT_LSB		1
#define RP1_PAD_PULL_MASK		0x0000000c
#define RP1_PAD_PULL_LSB		2
#define RP1_PAD_DRIVE_MASK		0x00000030
#define RP1_PAD_DRIVE_LSB		4
#define RP1_PAD_IN_ENABLE_MASK		0x00000040
#define RP1_PAD_IN_ENABLE_LSB		6
#define RP1_PAD_OUT_DISABLE_MASK	0x00000080
#define RP1_PAD_OUT_DISABLE_LSB		7

#define RP1_PAD_DRIVE_2MA		0x00000000
#define RP1_PAD_DRIVE_4MA		0x00000010
#define RP1_PAD_DRIVE_8MA		0x00000020
#define RP1_PAD_DRIVE_12MA		0x00000030

//
// Bitfield helpers (compatible with Broadcom-style "start:end" from TRM)
//

#define FLD_GET(Reg, Field) (((Reg) & (Field ## _MASK)) >> (Field ## _LSB))
#define FLD_SET(Reg, Field, Val) Reg = (((Reg) & ~(Field ## _MASK)) | ((Val) << (Field ## _LSB)))

typedef struct {
  UINT8 MinPin;
  UINT8 PinCount;
  UINTN GpioOffset;
  UINTN InteOffset;
  UINTN IntsOffset;
  UINTN RioOffset;
  UINTN PadsOffset;
} RP1_IOBANK_DESC;

STATIC CONST RP1_IOBANK_DESC mRp1IoBanks[3] = {
  {
    .MinPin     = 0,
    .PinCount   = 28,
    .GpioOffset = 0,
    .InteOffset = 0x011c,
    .IntsOffset = 0x0124,
    .RioOffset  = 0,
    .PadsOffset = 0x0004,
  },
  {
    .MinPin     = 28,
    .PinCount   = 6,
    .GpioOffset = RP1_IO_BANK1_BASE - RP1_IO_BANK0_BASE,
    .InteOffset = 0x411c,
    .IntsOffset = 0x4124,
    .RioOffset  = RP1_SYS_RIO1_BASE - RP1_SYS_RIO0_BASE,
    .PadsOffset = RP1_PADS_BANK1_BASE - RP1_PADS_BANK0_BASE + 0x0004,
  },
  {
    .MinPin     = 34,
    .PinCount   = 20,
    .GpioOffset = RP1_IO_BANK2_BASE - RP1_IO_BANK0_BASE,
    .InteOffset = 0x811c,
    .IntsOffset = 0x8124,
    .RioOffset  = RP1_SYS_RIO2_BASE - RP1_SYS_RIO0_BASE,
    .PadsOffset = RP1_PADS_BANK2_BASE - RP1_PADS_BANK0_BASE + 0x0004,
  }
};

#endif // __RP1_GPIO_DXE_H__
