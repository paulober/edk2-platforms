/** @file

  Copyright (c) 2025, Paul Oberosler <paul@paulober.dev>

  Based on Silicon/Broadcom/Drivers/Net/BcmGenetDxe/AdapterInfo.c

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>

#include "Rp1EthernetDxe.h"

STATIC
EFI_STATUS
EFIAPI
MacbAipGetInformation (
  IN  EFI_ADAPTER_INFORMATION_PROTOCOL  *This,
  IN  EFI_GUID                          *InformationType,
  OUT VOID                              **InformationBlock,
  OUT UINTN                             *InformationBlockSize
  )
{
  EFI_ADAPTER_INFO_MEDIA_STATE  *AdapterInfo;
  MACB_PRIVATE_DATA             *Macb;

  if (This == NULL || InformationBlock == NULL ||
      InformationBlockSize == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (!CompareGuid (InformationType, &gEfiAdapterInfoMediaStateGuid)) {
    return EFI_UNSUPPORTED;
  }

  AdapterInfo = AllocateZeroPool (sizeof (EFI_ADAPTER_INFO_MEDIA_STATE));
  if (AdapterInfo == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  *InformationBlock = AdapterInfo;
  *InformationBlockSize = sizeof (EFI_ADAPTER_INFO_MEDIA_STATE);

  Macb = MACB_PRIVATE_DATA_FROM_AIP_THIS (This);
  AdapterInfo->MediaState = MacbCheckLink (Macb);

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
MacbAipSetInformation (
  IN  EFI_ADAPTER_INFORMATION_PROTOCOL  *This,
  IN  EFI_GUID                          *InformationType,
  IN  VOID                              *InformationBlock,
  IN  UINTN                             InformationBlockSize
  )
{
  if (This == NULL || InformationBlock == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (CompareGuid (InformationType, &gEfiAdapterInfoMediaStateGuid)) {
    return EFI_WRITE_PROTECTED;
  }

  return EFI_UNSUPPORTED;
}

STATIC
EFI_STATUS
EFIAPI
MacbAipGetSupportedTypes (
  IN  EFI_ADAPTER_INFORMATION_PROTOCOL  *This,
  OUT EFI_GUID                          **InfoTypesBuffer,
  OUT UINTN                             *InfoTypesBufferCount
  )
{
  EFI_GUID    *Guid;

  if (This == NULL || InfoTypesBuffer == NULL ||
      InfoTypesBufferCount == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Guid = AllocatePool (sizeof *Guid);
  if (Guid == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  CopyGuid (Guid, &gEfiAdapterInfoMediaStateGuid);

  *InfoTypesBuffer      = Guid;
  *InfoTypesBufferCount = 1;

  return EFI_SUCCESS;
}

CONST EFI_ADAPTER_INFORMATION_PROTOCOL gMacbAdapterInfoTemplate = {
  MacbAipGetInformation,
  MacbAipSetInformation,
  MacbAipGetSupportedTypes,
};
