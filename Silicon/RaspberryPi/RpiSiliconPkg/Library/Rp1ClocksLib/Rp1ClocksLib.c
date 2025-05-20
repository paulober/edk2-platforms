/** @file
 *
 *  Copyright (c) 2025, Paul Oberosler <paul@paulober.dev>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/BaseLib.h>
#include <Library/UefiLib.h>
#include <Library/TimerLib.h>

#include <Rp1.h>
#include <Library/Rp1ClocksLib.h>

#define DIV_U64_NEAREST(a, b) (((a) + ((b) / 2)) / (b))

STATIC
UINT32
RegRead(
  IN EFI_PHYSICAL_ADDRESS PeripheralBase,
  IN UINT32 Offset)
{
  ASSERT (PeripheralBase != 0);
  return MmioRead32(PeripheralBase + RP1_CLOCKS_MAIN_BASE + Offset);
}

STATIC
VOID
RegWrite (
  IN EFI_PHYSICAL_ADDRESS PeripheralBase,
  IN UINT32 Offset, 
  IN UINT32 Value)
{
  ASSERT (PeripheralBase != 0);
  MmioWrite32(PeripheralBase + RP1_CLOCKS_MAIN_BASE + Offset, Value);
}

EFI_STATUS
EFIAPI
Rp1ClockEnable (
  IN EFI_PHYSICAL_ADDRESS PeripheralBase,
  IN UINT32 ClockId
  )
{
  UINT32 RegOffset, Val;
  
  switch (ClockId) {
    case RP1_CLK_SYS:
      RegOffset = CLK_SYS_CTRL;
      break;
    case RP1_CLK_ETH:
      RegOffset = CLK_ETH_CTRL;
      break;
    case RP1_CLK_ETH_TSU:
      RegOffset = CLK_ETH_TSU_CTRL;
      break;
    default:
      return EFI_UNSUPPORTED;
  }

  Val = RegRead (PeripheralBase, RegOffset);
  Val |= CLK_CTRL_ENABLE;
  RegWrite (PeripheralBase, RegOffset, Val);

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
Rp1IsClockEnabled(
  IN EFI_PHYSICAL_ADDRESS PeripheralBase,
  IN UINT32 ClockId,
  OUT BOOLEAN *Enabled
  )
{
  if (Enabled == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  switch (ClockId) {
    case RP1_CLK_SYS:
      *Enabled = !!(RegRead (PeripheralBase, CLK_SYS_CTRL) & CLK_CTRL_ENABLE);
      break;
    case RP1_CLK_ETH:
      *Enabled = !!(RegRead (PeripheralBase, CLK_ETH_CTRL) & CLK_CTRL_ENABLE);
      break;
    case RP1_CLK_ETH_TSU:
      *Enabled = !!(RegRead (PeripheralBase, CLK_ETH_TSU_CTRL) & CLK_CTRL_ENABLE);
      break;
    default:
      DEBUG ((DEBUG_WARN, "Rp1IsClockEnabled: Unknown clock ID %u\n", ClockId));
      return EFI_UNSUPPORTED;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
Rp1EthGetClockRate (
  IN  EFI_PHYSICAL_ADDRESS PeripheralBase,
  OUT UINT64               *RateHz
  )
{
  UINT32 DivRaw;
  UINT32 Div;

  // Read divider register (assumes integer divider, no fractional support for CLK_ETH)
  // PWM GP and Video clocks have a different divider register for fractional dividers
  DivRaw = RegRead (PeripheralBase, CLK_ETH_DIV_INT);
  Div = DivRaw >> CLK_DIV_FRAC_BITS;

  if (Div == 0) {
    DEBUG ((DEBUG_WARN, "Rp1EthGetClockRate: Divider = 0 (invalid)\n"));
    return EFI_DEVICE_ERROR;
  }

  *RateHz = (UINT64)RP1_ETH_PARENT_RATE_HZ / Div;
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
Rp1SysGetClockRate (
  IN  EFI_PHYSICAL_ADDRESS PeripheralBase,
  OUT UINT64               *RateHz
  )
{
  UINT32 DivRaw;
  UINT32 Div;

  DivRaw = RegRead (PeripheralBase, CLK_SYS_DIV_INT);
  Div = DivRaw >> CLK_DIV_FRAC_BITS;

  *RateHz = (UINT64)RP1_CLK_SYS_RATE_HZ / Div;
  return EFI_SUCCESS;
}


UINT32
Rp1ClockChooseDiv (
  IN EFI_PHYSICAL_ADDRESS PeripheralBase,
  IN UINT32 Rate,
  IN UINT32 ParentRateHz
  )
{
  UINT32 Divider;

  /*
	 * Due to earlier rounding, calculated parent_rate may differ from
	 * expected value. Don't fail on a small discrepancy near unity divide.
	 */
  if (!Rate || Rate > ParentRateHz + (ParentRateHz / CLK_DIV_FRAC_BITS)) {
    return 0;
  }

  /*
	 * Always express div in fixed-point format for fractional division;
	 * If no fractional divider is present, the fraction part will be zero.
	 */
  // other behaivor for PWM GP or Video clocks (div_frac_reg)
  Divider = DIV_U64_NEAREST (ParentRateHz, Rate);
  Divider <<= CLK_DIV_FRAC_BITS;

  // clamp
  Divider = Divider < (1ull << CLK_DIV_FRAC_BITS) 
    ? (1ull << CLK_DIV_FRAC_BITS) 
    : (Divider > ((UINT64)DIV_INT_8BIT_MAX << CLK_DIV_FRAC_BITS) 
      ? ( (UINT64)DIV_INT_8BIT_MAX << CLK_DIV_FRAC_BITS) 
      : Divider);

  return Divider;
}

EFI_STATUS
EFIAPI
Rp1EthSetClockRate (
  IN EFI_PHYSICAL_ADDRESS PeripheralBase,
  IN UINT32               DesiredRateHz
  )
{
  UINT32 Divider;
  UINT32 Val;

  // Compute and clamp divider
  Divider = Rp1ClockChooseDiv (PeripheralBase, DesiredRateHz, RP1_ETH_PARENT_RATE_HZ);
  if (Divider == 0) {
    DEBUG ((DEBUG_WARN, "Rp1EthSetClockRate: Invalid divider for %u Hz\n", DesiredRateHz));
    return EFI_INVALID_PARAMETER;
  }

  RegWrite (PeripheralBase, CLK_ETH_DIV_INT, Divider >> CLK_DIV_FRAC_BITS);
  // special frac reg to set on pwm gp and video clks

  // Enable the clock
  Val = RegRead (PeripheralBase, CLK_ETH_CTRL);
  Val |= CLK_CTRL_ENABLE;
  RegWrite (PeripheralBase, CLK_ETH_CTRL, Val);

  return EFI_SUCCESS;
}
