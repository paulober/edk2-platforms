/** @file
 *
 *  Copyright (c) 2025, Paul Oberosler <paul@paulober.dev>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef __RP1_GPIO_DEVICE_PROTOCOL_H__
#define __RP1_GPIO_DEVICE_PROTOCOL_H__

#include <Uefi/UefiBaseType.h>

#define RP1_GPIO_DEVICE_PROTOCOL_GUID \
  {0x2232ace6, 0x9bfd, 0x4cf5, { 0xbb, 0x17, 0x2a, 0xf0, 0x04, 0xd8, 0x85, 0x9c }}

typedef struct {
  EFI_PHYSICAL_ADDRESS  GpioBase;
  EFI_PHYSICAL_ADDRESS  RioBase;
  EFI_PHYSICAL_ADDRESS  PadsBase;
} RP1_GPIO_DEVICE_PROTOCOL;

extern EFI_GUID gRp1GpioDeviceProtocolGuid;

#endif // __RP1_GPIO_DEVICE_PROTOCOL_H__
