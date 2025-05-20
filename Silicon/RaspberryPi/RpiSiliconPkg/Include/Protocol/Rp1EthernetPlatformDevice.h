/**
 * @file Rp1EthernetPlatformDevice.h
 * @author Paul Oberosler (paul@paulober.dev)
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#ifndef __RP1_ETHERNET_PLATFORM_DEVICE_H__
#define __RP1_ETHERNET_PLATFORM_DEVICE_H__

#include <Uefi/UefiBaseType.h>

#define RP1_ETHERNET_PLATFORM_DEVICE_PROTOCOL_GUID \
  {0xfe2cf5cf, 0xfb21, 0x46ae, { 0xab, 0x3c, 0x18, 0xef, 0x82, 0xb8, 0x33, 0x0d }}

typedef struct {
    EFI_PHYSICAL_ADDRESS    BaseAddress;
    EFI_MAC_ADDRESS         MacAddress;
} RP1_ETHERNET_PLATFORM_DEVICE_PROTOCOL;

extern EFI_GUID gRp1EthernetPlatformDeviceProtocolGuid;

#endif // __RP1_ETHERNET_PLATFORM_DEVICE_H__
