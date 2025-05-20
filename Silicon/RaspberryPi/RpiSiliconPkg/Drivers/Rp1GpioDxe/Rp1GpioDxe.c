/** @file
 *
 *  Copyright (c) 2025, Paul Oberosler <paul@paulober.dev>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/IoLib.h>
#include <Rp1.h>
#include <Rp1GpioFunction.h>

#include "Rp1GpioDxe.h"

STATIC RP1_GPIO_DEVICE_PROTOCOL   *mRp1DeviceProto;


STATIC 
CONST 
RP1_IOBANK_DESC*
Rp1GetBank (UINT8 Pin)
{
  for (UINTN i = 0; i < ARRAY_SIZE(mRp1IoBanks); ++i) {
    if (Pin >= mRp1IoBanks[i].MinPin && Pin < mRp1IoBanks[i].MinPin + mRp1IoBanks[i].PinCount)
      return &mRp1IoBanks[i];
  }
  return NULL;
}

STATIC
UINTN
CalculateGpioAddress (
  IN const RP1_IOBANK_DESC* Bank,
  IN UINT8 Pin,
  IN UINTN Offset
  )
{
  return mRp1DeviceProto->GpioBase
    + Bank->GpioOffset 
    + (Pin - Bank->MinPin) * sizeof(UINT32) * 2 
    + Offset;
}

STATIC
UINTN
CalculatePadAddress (
  IN const RP1_IOBANK_DESC* Bank,
  IN UINT8 Pin
  )
{
  return mRp1DeviceProto->PadsBase
    + Bank->PadsOffset 
    + (Pin - Bank->MinPin) * sizeof(UINT32);
}

////////////////////////////////
// GPIO MMIO helper functions //
////////////////////////////////

STATIC
VOID
GpioPadUpdate (
  IN UINT8 Pin,
  IN UINT32 Clr,
  IN UINT32 Set
  )
{
  const RP1_IOBANK_DESC* Bank;
  UINTN Address;

  Bank = Rp1GetBank (Pin);
  if (Bank == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid pin %d\n", __func__, Pin));
    return;
  }
  Address = CalculatePadAddress(Bank, Pin);

  MmioAndThenOr32 (Address, ~Clr, Set);
}

STATIC
VOID
GpioInputEnable (
  IN UINT8 Pin,
  IN BOOLEAN Enable
  )
{
  GpioPadUpdate (
    Pin, 
    RP1_PAD_IN_ENABLE_MASK, 
    Enable 
      ? RP1_PAD_IN_ENABLE_MASK
      : 0
    );
}

STATIC
VOID
GpioOutputEnable (
  IN UINT8 Pin,
  IN BOOLEAN Enable
  )
{
  GpioPadUpdate (
    Pin, 
    RP1_PAD_OUT_DISABLE_MASK, 
    Enable 
      ? 0 
      : RP1_PAD_OUT_DISABLE_MASK
    );
}

STATIC
UINT8
GpioGetFsel (
  IN UINT8 Pin
  )
{
  const RP1_IOBANK_DESC* Bank;
  UINTN Address;
  UINT32 Reg, OeOver, Fsel;

  Bank = Rp1GetBank (Pin);
  if (Bank == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid pin %d\n", __func__, Pin));
    return RP1_FSEL_NONE_HW;
  }
  Address = CalculateGpioAddress(Bank, Pin, RP1_GPIO_CTRL);

  Reg = MmioRead32 (Address);
  OeOver = FLD_GET (Reg, RP1_GPIO_CTRL_OEOVER);
  Fsel = FLD_GET (Reg, RP1_GPIO_CTRL_FUNCSEL);

  if (OeOver != RP1_OEOVER_PERI || Fsel >= RP1_FSEL_COUNT) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid function select %d\n", __func__, Fsel));
    return RP1_FSEL_NONE;
  }

  return (UINT8)Fsel;
}

STATIC
VOID
GpioSetFsel (
  IN UINT8 Pin,
  IN UINT8 Fsel
  )
{
  const RP1_IOBANK_DESC* Bank;
  UINTN Address;
  UINT32 Reg;

  Bank = Rp1GetBank (Pin);
  if (Bank == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid pin %d\n", __func__, Pin));
    return;
  }
  Address = CalculateGpioAddress(Bank, Pin, RP1_GPIO_CTRL);

  Reg = MmioRead32 (Address);
  if (Fsel >= RP1_FSEL_COUNT) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid function select %d\n", __func__, Fsel));
    return;
  }

  GpioInputEnable (Pin, TRUE);
  GpioOutputEnable (Pin, TRUE);

  if (Fsel == RP1_FSEL_NONE) {
    FLD_SET (Reg, RP1_GPIO_CTRL_OEOVER, RP1_OEOVER_DISABLE);
  } else {
    FLD_SET (Reg, RP1_GPIO_CTRL_OUTOVER, RP1_OUTOVER_PERI);
    FLD_SET (Reg, RP1_GPIO_CTRL_OEOVER, RP1_OEOVER_PERI);
  }
  FLD_SET (Reg, RP1_GPIO_CTRL_FUNCSEL, Fsel);
  MmioWrite32 (Address, Reg);
}

STATIC
UINT8
GpioGetDir (
  IN UINT8 Pin
  )
{
  const RP1_IOBANK_DESC* Bank;
  UINTN Address;
  UINT32 Reg;

  Bank = Rp1GetBank (Pin);
  if (Bank == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid pin %d\n", __func__, Pin));
    return RP1_DIR_OUTPUT;
  }
  Address = mRp1DeviceProto->RioBase + Bank->RioOffset + RP1_RIO_OE;
  Reg = MmioRead32 (Address);

  if (!(Reg & (1 << (Pin - Bank->MinPin)))) {
    return RP1_DIR_INPUT;
  } else {
    return RP1_DIR_OUTPUT;
  }
}

STATIC
VOID
GpioSetDir (
  IN UINT8 Pin,
  IN BOOLEAN IsInput
  )
{
  const RP1_IOBANK_DESC* Bank;
  UINTN Address;
  UINT32 Offset;

  Bank = Rp1GetBank (Pin);
  if (Bank == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid pin %d\n", __func__, Pin));
    return;
  }
  Offset = IsInput ? RP1_CLR_OFFSET : RP1_SET_OFFSET;
  Address = mRp1DeviceProto->RioBase + Bank->RioOffset + RP1_RIO_OE + Offset;

  MmioWrite32 (Address, 1 << (Pin - Bank->MinPin));
}

STATIC
UINT8
GpioGetValue (
  IN UINT8 Pin
  )
{
  const RP1_IOBANK_DESC* Bank;
  UINTN Address;
  UINT32 Reg;

  Bank = Rp1GetBank (Pin);
  if (Bank == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid pin %d\n", __func__, Pin));
    return 0;
  }
  Address = mRp1DeviceProto->RioBase + Bank->RioOffset + RP1_RIO_IN;
  Reg = MmioRead32 (Address);

  return !!(Reg & (1 << (Pin - Bank->MinPin)));
}

STATIC
VOID
GpioSetValue (
  IN UINT8 Pin,
  IN UINT8 Value
  )
{
  const RP1_IOBANK_DESC* Bank;
  UINTN Address;
  UINT32 Offset;

  Bank = Rp1GetBank (Pin);
  if (Bank == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid pin %d\n", __func__, Pin));
    return;
  }
  Offset = Value ? RP1_SET_OFFSET : RP1_CLR_OFFSET;
  Address = mRp1DeviceProto->RioBase + Bank->RioOffset + RP1_RIO_OUT + Offset;

  // Assume Pin is already configured as output
  MmioWrite32 (Address, 1 << (Pin - Bank->MinPin));
}

STATIC
VOID
GpioClear (
  IN UINT8 Pin
  )
{
  const RP1_IOBANK_DESC* Bank;
  UINTN Address;

  Bank = Rp1GetBank (Pin);
  if (Bank == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid pin %d\n", __func__, Pin));
    return;
  }

  Address = mRp1DeviceProto->RioBase + Bank->RioOffset + RP1_RIO_OUT + RP1_CLR_OFFSET;
  MmioWrite32 (Address, 1 << (Pin - Bank->MinPin));
}

////////////////////////
// Protocol functions //
////////////////////////

EFI_STATUS
EFIAPI
Rp1GpioGetFunction (
  IN UINT8 Pin,
  OUT UINT8 *Function
  )
{
  ASSERT (Pin < RP1_GPIO_COUNT);
  *Function = GpioGetFsel (Pin);

  if (*Function == RP1_FSEL_NONE_HW) {
    *Function = RP1_FSEL_NONE;
    return EFI_INVALID_PARAMETER;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
Rp1GpioSetFunction (
  IN UINT8 Pin,
  IN UINT8 Function
  )
{
  ASSERT (Pin < RP1_GPIO_COUNT);
  GpioSetFsel (Pin, Function);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
Rp1GpioGetPull (
  IN UINT8 Pin,
  OUT RP1_GPIO_PIN_PULL *Pull
  )
{
  ASSERT (Pin < RP1_GPIO_COUNT);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
Rp1GpioSetPull (
  IN UINT8 Pin,
  IN RP1_GPIO_PIN_PULL Pull
  )
{
  ASSERT (Pin < RP1_GPIO_COUNT);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
Rp1GpioRead (
  IN UINT8 Pin,
  OUT BOOLEAN *Value
  )
{
  ASSERT (Pin < RP1_GPIO_COUNT);

  *Value = GpioGetValue (Pin) ? TRUE : FALSE;
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
Rp1GpioWrite (
  IN UINT8 Pin,
  IN BOOLEAN Value
  )
{
  ASSERT (Pin < RP1_GPIO_COUNT);

  GpioSetValue (Pin, Value ? 1 : 0);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
Rp1GpioGetDirection (
  IN UINT8 Pin,
  OUT BOOLEAN *IsInput
  )
{
  UINT8 Fsel, Dir;

  ASSERT (Pin < RP1_GPIO_COUNT);
  Fsel = GpioGetFsel (Pin);
  if (Fsel != RP1_FSEL_GPIO)
    return EFI_UNSUPPORTED;
  Dir = GpioGetDir (Pin);
  *IsInput = (Dir == RP1_DIR_OUTPUT) ? FALSE : TRUE;
  
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
Rp1GpioSetDirection (
  IN UINT8 Pin,
  IN BOOLEAN Input
  )
{
  ASSERT (Pin < RP1_GPIO_COUNT);

  if (!Input) {
    GpioSetValue (Pin, Input ? 1 : 0);
  }
  GpioSetDir (Pin, Input);
  GpioSetFsel (Pin, RP1_FSEL_GPIO);

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
Rp1GpioClear (
  IN UINT8 Pin
  )
{
  ASSERT (Pin < RP1_GPIO_COUNT);

  GpioClear (Pin);
  return EFI_SUCCESS;
}

STATIC RP1_GPIO_PROTOCOL mRp1GpioProtocol = {
  Rp1GpioGetFunction,
  Rp1GpioSetFunction,
  Rp1GpioGetPull,
  Rp1GpioSetPull,
  Rp1GpioRead,
  Rp1GpioWrite,
  Rp1GpioGetDirection,
  Rp1GpioSetDirection,
  Rp1GpioClear
};

EFI_STATUS
EFIAPI
Rp1GpioDxeEntryPoint (
  IN EFI_HANDLE ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  EFI_STATUS                Status;

  // Open the device protocol installed by Rp1BusDxe
  Status = gBS->LocateProtocol (
    &gRp1GpioDeviceProtocolGuid,
    NULL,
    (VOID **)&mRp1DeviceProto
  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, 
      "%a: Couldn't open protocol: %r\n", __func__, Status));
    return Status;
  }

  // Install RP1_GPIO_PROTOCOL so other drivers can use it
  Status = gBS->InstallProtocolInterface (
    &ImageHandle,
    &gRp1GpioProtocolGuid,
    EFI_NATIVE_INTERFACE,
    &mRp1GpioProtocol
  );
  ASSERT_EFI_ERROR (Status);

  return EFI_SUCCESS;
}

