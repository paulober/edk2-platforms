/** @file
 *
 *  Copyright (c) 2025, Paul Oberosler <paul@paulober.dev>
 *  Copyright (c) 2023, Mario Bălănică <mariobalanica02@gmail.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <IndustryStandard/Bcm2712.h>
#include <Library/Bcm2712GpioLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>

#define BCM2712_GIO_DATA_REG                0x4
#define BCM2712_GIO_IODIR_REG               0x8

#define BCM2712_GIO_BANK_SIZE               (8 * sizeof (UINT32))
#define BCM2712_GIO_MAX_PINS_PER_BANK       32

#define BCM2712_GIO_BANK_OFFSET(Pin)        ((Pin / BCM2712_GIO_MAX_PINS_PER_BANK) * BCM2712_GIO_BANK_SIZE)
#define BCM2712_GIO_REG_BIT(Pin)            (1 << Pin)

#define BCM2712_PINCTRL_FSEL_MASK           (BIT3 | BIT2 | BIT1 | BIT0)
#define BCM2712_PINCTRL_PULL_MASK           (BIT1 | BIT0)

#define PINCTRL_REG_UNUSED                  MAX_UINT16

#define BCM2712_SUN_TOP_CTRL_PROD_ID            0x1001504004ULL

typedef struct {
  UINT16   MuxReg;
  UINT8    MuxBit;
  UINT16   CtlReg;
  UINT8    CtlBit;
} BCM2712_PINCTRL_REGISTERS;

typedef struct {
  EFI_PHYSICAL_ADDRESS         GioBase;
  EFI_PHYSICAL_ADDRESS         PinctrlBase;
  BCM2712_PINCTRL_REGISTERS    *PinctrlRegisters;
  UINT32                       PinCount;
} BCM2712_GPIO_CONTROLLER;

STATIC BCM2712_PINCTRL_REGISTERS Bcm2712PinctrlGioRegisters[] = {
  // GPIO 0-4: BOOT
  [0] = { .MuxReg = 0x00, .MuxBit = 0,  .CtlReg = 0x1C, .CtlBit = 14 }, // GPIO_000
  [1] = { .MuxReg = 0x00, .MuxBit = 4,  .CtlReg = 0x1C, .CtlBit = 16 }, // 2712_BOOT_CS_N
  [2] = { .MuxReg = 0x00, .MuxBit = 8,  .CtlReg = 0x1C, .CtlBit = 18 }, // 2712_BOOT_MISO
  [3] = { .MuxReg = 0x00, .MuxBit = 12, .CtlReg = 0x1C, .CtlBit = 20 }, // 2712_BOOT_MOSI
  [4] = { .MuxReg = 0x00, .MuxBit = 16, .CtlReg = 0x1C, .CtlBit = 22 }, // 2712_BOOT_SCLK

  // GPIO 5–13: Unused
  [5 ... 13] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },

  [14] = { .MuxReg = 0x04, .MuxBit = 24, .CtlReg = 0x20, .CtlBit = 12 }, // PCIE_SDA
  [15] = { .MuxReg = 0x04, .MuxBit = 28, .CtlReg = 0x20, .CtlBit = 14 }, // PCIE_SCL

  // GPIO 16–19: Unused
  [16 ... 19] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },

  [20] = { .MuxReg = 0x08, .MuxBit = 16, .CtlReg = 0x20, .CtlBit = 24 }, // PWR_GPIO
  [21] = { .MuxReg = 0x08, .MuxBit = 20, .CtlReg = 0x20, .CtlBit = 26 }, // 2712_G21_FS

  // GPIO 22–23: Unused
  [22 ... 23] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },

  // GPIO 24–29: Bluetooth / Wi-Fi control
  [24] = { .MuxReg = 0x0C, .MuxBit = 0,  .CtlReg = 0x24, .CtlBit = 2 },  // BT_RTS
  [25] = { .MuxReg = 0x0C, .MuxBit = 4,  .CtlReg = 0x24, .CtlBit = 4 },  // BT_CTS
  [26] = { .MuxReg = 0x0C, .MuxBit = 8,  .CtlReg = 0x24, .CtlBit = 6 },  // BT_TXD
  [27] = { .MuxReg = 0x0C, .MuxBit = 12, .CtlReg = 0x24, .CtlBit = 8 },  // BT_RXD
  [28] = { .MuxReg = 0x0C, .MuxBit = 16, .CtlReg = 0x24, .CtlBit = 10 }, // WL_ON
  [29] = { .MuxReg = 0x0C, .MuxBit = 20, .CtlReg = 0x24, .CtlBit = 12 }, // BT_ON

  // GPIO 30–35: SDIO
  [30] = { .MuxReg = 0x0C, .MuxBit = 24, .CtlReg = 0x24, .CtlBit = 14 }, // WIFI_SDIO_CLK
  [31] = { .MuxReg = 0x0C, .MuxBit = 28, .CtlReg = 0x24, .CtlBit = 16 }, // WIFI_SDIO_CMD
  [32] = { .MuxReg = 0x10, .MuxBit = 0,  .CtlReg = 0x24, .CtlBit = 18 }, // WIFI_SDIO_D0
  [33] = { .MuxReg = 0x10, .MuxBit = 4,  .CtlReg = 0x24, .CtlBit = 20 }, // WIFI_SDIO_D1
  [34] = { .MuxReg = 0x10, .MuxBit = 8,  .CtlReg = 0x24, .CtlBit = 22 }, // WIFI_SDIO_D2
  [35] = { .MuxReg = 0x10, .MuxBit = 12, .CtlReg = 0x24, .CtlBit = 24 }, // WIFI_SDIO_D3
};

STATIC BCM2712_PINCTRL_REGISTERS Bcm2712PinctrlGioAonRegisters[] = {
  [0] = { .MuxReg = 0x00, .MuxBit = 0, .CtlReg = 0x1C, .CtlBit = 0 },  // GPIOAON_000
  [1] = { .MuxReg = 0x00, .MuxBit = 4, .CtlReg = 0x1C, .CtlBit = 2 },  // GPIOAON_001
  [2] = { .MuxReg = 0x00, .MuxBit = 8, .CtlReg = 0x1C, .CtlBit = 4 },  // GPIOAON_002
  [3] = { .MuxReg = 0x00, .MuxBit = 12, .CtlReg = 0x1C, .CtlBit = 6 }, // GPIOAON_003
  [4] = { .MuxReg = 0x00, .MuxBit = 16, .CtlReg = 0x1C, .CtlBit = 8 }, // GPIOAON_004
  [5] = { .MuxReg = 0x00, .MuxBit = 20, .CtlReg = 0x1C, .CtlBit = 10 }, // GPIOAON_005 (SD_DET?)
  [6] = { .MuxReg = 0x00, .MuxBit = 24, .CtlReg = 0x1C, .CtlBit = 12 }, // GPIOAON_006
  [7] = { .MuxReg = 0x00, .MuxBit = 28, .CtlReg = 0x1C, .CtlBit = 14 }, // GPIOAON_007
  [8] = { .MuxReg = 0x04, .MuxBit = 0,  .CtlReg = 0x20, .CtlBit = 0 },  // GPIOAON_008
  [9] = { .MuxReg = 0x04, .MuxBit = 4,  .CtlReg = 0x20, .CtlBit = 2 },  // GPIOAON_009
  [10] = { .MuxReg = 0x04, .MuxBit = 8,  .CtlReg = 0x20, .CtlBit = 4 },  // GPIOAON_010
  [11] = { .MuxReg = 0x04, .MuxBit = 12, .CtlReg = 0x20, .CtlBit = 6 },  // GPIOAON_011
  [12] = { .MuxReg = 0x04, .MuxBit = 16, .CtlReg = 0x20, .CtlBit = 8 },  // GPIOAON_012
  [13] = { .MuxReg = 0x04, .MuxBit = 20, .CtlReg = 0x20, .CtlBit = 10 }, // GPIOAON_013
  [14] = { .MuxReg = 0x04, .MuxBit = 24, .CtlReg = 0x20, .CtlBit = 12 }, // GPIOAON_014
  [15] = { .MuxReg = 0x04, .MuxBit = 28, .CtlReg = 0x20, .CtlBit = 14 }, // GPIOAON_015
  [16] = { .MuxReg = 0x08, .MuxBit = 0,  .CtlReg = 0x24, .CtlBit = 0 },  // GPIOAON_016
};

STATIC BCM2712_PINCTRL_REGISTERS Bcm2712D0PinctrlGioRegisters[] = {
  [0] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [1] = { .MuxReg = 0x00, .MuxBit = 0, .CtlReg = 0x10, .CtlBit = 5 },
  [2] = { .MuxReg = 0x00, .MuxBit = 4, .CtlReg = 0x10, .CtlBit = 6 },
  [3] = { .MuxReg = 0x00, .MuxBit = 8, .CtlReg = 0x10, .CtlBit = 7 },
  [4] = { .MuxReg = 0x00, .MuxBit = 12, .CtlReg = 0x10, .CtlBit = 8 },
  
  // GPIO 5–9: Unused
  [5 ... 9] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },

  [10] = { .MuxReg = 0x00, .MuxBit = 16, .CtlReg = 0x10, .CtlBit = 9 },
  [11] = { .MuxReg = 0x00, .MuxBit = 20, .CtlReg = 0x10, .CtlBit = 10 },
  [12] = { .MuxReg = 0x00, .MuxBit = 24, .CtlReg = 0x10, .CtlBit = 11 },
  [13] = { .MuxReg = 0x00, .MuxBit = 28, .CtlReg = 0x10, .CtlBit = 12 },
  [14] = { .MuxReg = 0x04, .MuxBit = 0,  .CtlReg = 0x10, .CtlBit = 13 },
  [15] = { .MuxReg = 0x04, .MuxBit = 4,  .CtlReg = 0x10, .CtlBit = 14 },

  // GPIO 16-17 unused
  [16] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },
  [17] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = PINCTRL_REG_UNUSED, .CtlBit = 0 },

  [18] = { .MuxReg = 0x04, .MuxBit = 8,  .CtlReg = 0x14, .CtlBit = 0 },
  [19] = { .MuxReg = 0x04, .MuxBit = 12, .CtlReg = 0x14, .CtlBit = 1 },
  [20] = { .MuxReg = 0x04, .MuxBit = 16, .CtlReg = 0x14, .CtlBit = 2 },
  [21] = { .MuxReg = 0x04, .MuxBit = 20, .CtlReg = 0x14, .CtlBit = 3 },
  [22] = { .MuxReg = 0x04, .MuxBit = 24, .CtlReg = 0x14, .CtlBit = 4 },
  [23] = { .MuxReg = 0x04, .MuxBit = 28, .CtlReg = 0x14, .CtlBit = 5 },
  [24] = { .MuxReg = 0x08, .MuxBit = 0,  .CtlReg = 0x14, .CtlBit = 6 },
  [25] = { .MuxReg = 0x08, .MuxBit = 4,  .CtlReg = 0x14, .CtlBit = 7 },
  [26] = { .MuxReg = 0x08, .MuxBit = 8,  .CtlReg = 0x14, .CtlBit = 8 },
  [27] = { .MuxReg = 0x08, .MuxBit = 12, .CtlReg = 0x14, .CtlBit = 9 },
  [28] = { .MuxReg = 0x08, .MuxBit = 16, .CtlReg = 0x14, .CtlBit = 10 },
  [29] = { .MuxReg = 0x08, .MuxBit = 20, .CtlReg = 0x14, .CtlBit = 11 },
  [30] = { .MuxReg = 0x08, .MuxBit = 24, .CtlReg = 0x14, .CtlBit = 12 },
  [31] = { .MuxReg = 0x08, .MuxBit = 28, .CtlReg = 0x14, .CtlBit = 13 },
  [32] = { .MuxReg = 0x0C, .MuxBit = 0,  .CtlReg = 0x14, .CtlBit = 14 },
  [33] = { .MuxReg = 0x0C, .MuxBit = 4,  .CtlReg = 0x18, .CtlBit = 0 },
  [34] = { .MuxReg = 0x0C, .MuxBit = 8,  .CtlReg = 0x18, .CtlBit = 1 },
  [35] = { .MuxReg = 0x0C, .MuxBit = 12, .CtlReg = 0x18, .CtlBit = 2 },
  [36] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = 0x18, .CtlBit = 3 },
  [37] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = 0x18, .CtlBit = 4 },
  [38] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = 0x18, .CtlBit = 5 },
  [39] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = 0x18, .CtlBit = 6 },
  [40] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = 0x18, .CtlBit = 7 },
  [41] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = 0x18, .CtlBit = 8 },
  [42] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = 0x18, .CtlBit = 9 },
  [43] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = 0x18, .CtlBit = 10 },
  [44] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = 0x18, .CtlBit = 11 },
  [45] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = 0x18, .CtlBit = 12 },
  [46] = { .MuxReg = PINCTRL_REG_UNUSED, .MuxBit = 0, .CtlReg = 0x18, .CtlBit = 13 },
};

STATIC BCM2712_PINCTRL_REGISTERS Bcm2712D0PinctrlGioAonRegisters[] = {
  [0] = { .MuxReg = 0x0C, .MuxBit = 0, .CtlReg = 0x14, .CtlBit = 9 },
  [1] = { .MuxReg = 0x0C, .MuxBit = 4, .CtlReg = 0x14, .CtlBit = 10 },
  [2] = { .MuxReg = 0x0C, .MuxBit = 8, .CtlReg = 0x14, .CtlBit = 11 },
  [3] = { .MuxReg = 0x0C, .MuxBit = 12, .CtlReg = 0x14, .CtlBit = 12 },
  [4] = { .MuxReg = 0x0C, .MuxBit = 16, .CtlReg = 0x14, .CtlBit = 13 },
  [5] = { .MuxReg = 0x0C, .MuxBit = 20, .CtlReg = 0x14, .CtlBit = 14 },
  [6] = { .MuxReg = 0x0C, .MuxBit = 24, .CtlReg = 0x18, .CtlBit = 0 },
  [8] = { .MuxReg = 0x0C, .MuxBit = 28, .CtlReg = 0x18, .CtlBit = 1 },
  [9] = { .MuxReg = 0x10, .MuxBit = 0, .CtlReg = 0x18, .CtlBit = 2 },
  [12] = { .MuxReg = 0x10, .MuxBit = 4, .CtlReg = 0x18, .CtlBit = 3 },
  [13] = { .MuxReg = 0x10, .MuxBit = 8, .CtlReg = 0x18, .CtlBit = 4 },
  [14] = { .MuxReg = 0x10, .MuxBit = 12, .CtlReg = 0x18, .CtlBit = 5 },
};

#define GPIOLIB_ASSERT_OR_FAIL(Expression, FailAction) \
  do { \
    ASSERT(Expression); \
    if (!(Expression)) { \
      FailAction; \
    } \
  } while (FALSE)

#define GPIOLIB_ASSERT_COMMON_PARAMS(Type, Pin, FailAction) \
  GPIOLIB_ASSERT_OR_FAIL ((Type < ARRAY_SIZE (Controllers)) \
                          && (Pin < Controllers[Type].PinCount), \
                          FailAction)


STATIC BOOLEAN                  GpioInitialized = FALSE;
STATIC BOOLEAN                  IsD0Revision = FALSE;
STATIC BCM2712_GPIO_CONTROLLER  Controllers[BCM2712_GIO_COUNT];

STATIC VOID
InitializeGpioIfNeeded (
  VOID
  )
{
  if (GpioInitialized) {
    return;
  }

  UINT32 SocRev   = MmioRead32 ((UINTN)BCM2712_SUN_TOP_CTRL_PROD_ID);
  UINT8  MajorRev = (SocRev >> 4) & 0xF; // bits [7:4]

  IsD0Revision = MajorRev == 3;

  DEBUG ((DEBUG_INFO, "GPIO: Detected BCM2712 with major revision %u variant\n", MajorRev));

  Controllers[0].GioBase = BCM2712_BRCMSTB_GIO_BASE;
  Controllers[0].PinctrlBase = BCM2712_PINCTRL_BASE;
  Controllers[0].PinctrlRegisters = IsD0Revision 
    ? Bcm2712D0PinctrlGioRegisters 
    : Bcm2712PinctrlGioRegisters;
  Controllers[0].PinCount = IsD0Revision
    ? ARRAY_SIZE (Bcm2712D0PinctrlGioRegisters)
    : ARRAY_SIZE (Bcm2712PinctrlGioRegisters);

  Controllers[1].GioBase = BCM2712_BRCMSTB_GIO_AON_BASE;
  Controllers[1].PinctrlBase = BCM2712_PINCTRL_AON_BASE;
  Controllers[1].PinctrlRegisters = IsD0Revision 
    ? Bcm2712D0PinctrlGioAonRegisters 
    : Bcm2712PinctrlGioAonRegisters;
  Controllers[1].PinCount = IsD0Revision
    ? ARRAY_SIZE (Bcm2712D0PinctrlGioAonRegisters)
    : ARRAY_SIZE (Bcm2712PinctrlGioAonRegisters);

  GpioInitialized = TRUE;
}

UINT8
EFIAPI
GpioGetFunction (
  IN  BCM2712_GPIO_TYPE               Type,
  IN  UINT8                           Pin
  )
{
  BCM2712_GPIO_CONTROLLER     *Controller;
  BCM2712_PINCTRL_REGISTERS   *Regs;
  UINT32                      Value = BCM2712_GPIO_ALT_COUNT;

  InitializeGpioIfNeeded ();
  GPIOLIB_ASSERT_COMMON_PARAMS (Type, Pin, return Value);

  Controller = &Controllers[Type];
  Regs = &Controller->PinctrlRegisters[Pin];

  GPIOLIB_ASSERT_OR_FAIL (Regs->MuxReg != PINCTRL_REG_UNUSED, return Value);

  Value = MmioRead32 (Controller->PinctrlBase + Regs->MuxReg);

  return (Value >> Regs->MuxBit) & BCM2712_PINCTRL_FSEL_MASK;
}

VOID
EFIAPI
GpioSetFunction (
  IN  BCM2712_GPIO_TYPE               Type,
  IN  UINT8                           Pin,
  IN  UINT8                           Function
  )
{
  BCM2712_GPIO_CONTROLLER     *Controller;
  BCM2712_PINCTRL_REGISTERS   *Regs;

  InitializeGpioIfNeeded ();
  GPIOLIB_ASSERT_COMMON_PARAMS (Type, Pin, return);

  Controller = &Controllers[Type];
  Regs = &Controller->PinctrlRegisters[Pin];

  GPIOLIB_ASSERT_OR_FAIL (Regs->MuxReg != PINCTRL_REG_UNUSED, return);

  MmioAndThenOr32 (Controller->PinctrlBase + Regs->MuxReg,
                   ~(BCM2712_PINCTRL_FSEL_MASK << Regs->MuxBit),
                   Function << Regs->MuxBit);
}

BCM2712_GPIO_PIN_PULL
EFIAPI
GpioGetPull (
  IN  BCM2712_GPIO_TYPE               Type,
  IN  UINT8                           Pin
  )
{
  BCM2712_GPIO_CONTROLLER     *Controller;
  BCM2712_PINCTRL_REGISTERS   *Regs;
  UINT32                      Value = BCM2712_GPIO_PIN_PULL_NONE;

  InitializeGpioIfNeeded ();
  GPIOLIB_ASSERT_COMMON_PARAMS (Type, Pin, return Value);

  Controller = &Controllers[Type];
  Regs = &Controller->PinctrlRegisters[Pin];

  GPIOLIB_ASSERT_OR_FAIL (Regs->CtlReg != PINCTRL_REG_UNUSED, return Value);

  Value = MmioRead32 (Controller->PinctrlBase + Regs->CtlReg);

  return (Value >> Regs->CtlBit) & BCM2712_PINCTRL_PULL_MASK;
}

VOID
EFIAPI
GpioSetPull (
  IN  BCM2712_GPIO_TYPE               Type,
  IN  UINT8                           Pin,
  IN  BCM2712_GPIO_PIN_PULL           Pull
  )
{
  BCM2712_GPIO_CONTROLLER     *Controller;
  BCM2712_PINCTRL_REGISTERS   *Regs;

  InitializeGpioIfNeeded ();
  GPIOLIB_ASSERT_COMMON_PARAMS (Type, Pin, return);

  Controller = &Controllers[Type];
  Regs = &Controller->PinctrlRegisters[Pin];

  GPIOLIB_ASSERT_OR_FAIL (Pull == BCM2712_GPIO_PIN_PULL_NONE
                          || Pull == BCM2712_GPIO_PIN_PULL_UP
                          || Pull == BCM2712_GPIO_PIN_PULL_DOWN,
                          return);

  GPIOLIB_ASSERT_OR_FAIL (Regs->CtlReg != PINCTRL_REG_UNUSED, return);

  MmioAndThenOr32 (Controller->PinctrlBase + Regs->CtlReg,
                   ~(BCM2712_PINCTRL_PULL_MASK << Regs->CtlBit),
                   Pull << Regs->CtlBit);
}

BOOLEAN
EFIAPI
GpioRead (
  IN  BCM2712_GPIO_TYPE               Type,
  IN  UINT8                           Pin
  )
{
  BCM2712_GPIO_CONTROLLER     *Controller;
  EFI_PHYSICAL_ADDRESS        BankReg;
  UINT32                      Value = FALSE;

  InitializeGpioIfNeeded ();
  GPIOLIB_ASSERT_COMMON_PARAMS (Type, Pin, return Value);

  Controller = &Controllers[Type];
  BankReg = Controller->GioBase + BCM2712_GIO_BANK_OFFSET (Pin);

  Value = MmioRead32 (BankReg + BCM2712_GIO_DATA_REG);

  return (Value & BCM2712_GIO_REG_BIT (Pin)) != 0;
}

VOID
EFIAPI
GpioWrite (
  IN  BCM2712_GPIO_TYPE               Type,
  IN  UINT8                           Pin,
  IN  BOOLEAN                         Value
  )
{
  BCM2712_GPIO_CONTROLLER     *Controller;
  EFI_PHYSICAL_ADDRESS        BankReg;

  InitializeGpioIfNeeded ();
  GPIOLIB_ASSERT_COMMON_PARAMS (Type, Pin, return);

  Controller = &Controllers[Type];
  BankReg = Controller->GioBase + BCM2712_GIO_BANK_OFFSET (Pin);

  if (Value) {
    MmioOr32 (BankReg + BCM2712_GIO_DATA_REG, BCM2712_GIO_REG_BIT (Pin));
  } else {
    MmioAnd32 (BankReg + BCM2712_GIO_DATA_REG, ~BCM2712_GIO_REG_BIT (Pin));
  }
}

BCM2712_GPIO_PIN_DIRECTION
EFIAPI
GpioGetDirection (
  IN  BCM2712_GPIO_TYPE               Type,
  IN  UINT8                           Pin
  )
{
  BCM2712_GPIO_CONTROLLER     *Controller;
  EFI_PHYSICAL_ADDRESS        BankReg;
  UINT32                      Value = BCM2712_GPIO_PIN_OUTPUT;

  InitializeGpioIfNeeded ();
  GPIOLIB_ASSERT_COMMON_PARAMS (Type, Pin, return Value);

  Controller = &Controllers[Type];
  BankReg = Controller->GioBase + BCM2712_GIO_BANK_OFFSET (Pin);

  Value = MmioRead32 (BankReg + BCM2712_GIO_IODIR_REG);

  if (Value & BCM2712_GIO_REG_BIT (Pin)) {
    return BCM2712_GPIO_PIN_INPUT;
  } else {
    return BCM2712_GPIO_PIN_OUTPUT;
  }
}

VOID
EFIAPI
GpioSetDirection (
  IN  BCM2712_GPIO_TYPE               Type,
  IN  UINT8                           Pin,
  IN  BCM2712_GPIO_PIN_DIRECTION      Direction
  )
{
  BCM2712_GPIO_CONTROLLER     *Controller;
  EFI_PHYSICAL_ADDRESS        BankReg;

  InitializeGpioIfNeeded ();
  GPIOLIB_ASSERT_COMMON_PARAMS (Type, Pin, return);
  GPIOLIB_ASSERT_OR_FAIL (Direction != BCM2712_GPIO_PIN_OUTPUT
                          || Direction != BCM2712_GPIO_PIN_INPUT,
                          return);

  Controller = &Controllers[Type];
  BankReg = Controller->GioBase + BCM2712_GIO_BANK_OFFSET (Pin);

  switch (Direction) {
    case BCM2712_GPIO_PIN_INPUT:
      MmioOr32 (BankReg + BCM2712_GIO_IODIR_REG, BCM2712_GIO_REG_BIT (Pin));
      break;
    case BCM2712_GPIO_PIN_OUTPUT:
      MmioAnd32 (BankReg + BCM2712_GIO_IODIR_REG, ~BCM2712_GIO_REG_BIT (Pin));
      break;
    default:
     break;
  }
}
