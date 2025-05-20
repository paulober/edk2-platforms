/** @file
 *
 *  Copyright (c) 2025, Paul Oberosler <paul@paulober.dev>
 *  Copyright (c) 2023, Mario Bălănică <mariobalanica02@gmail.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <Library/BoardInfoLib.h>
#include <Library/FdtPlatformLib.h>
#include <Library/DebugLib.h>
#include <libfdt.h>

EFI_STATUS
EFIAPI
BoardInfoGetRevisionCode (
  OUT   UINT32  *RevisionCode
  )
{
  VOID            *Fdt;
  INT32           Node;
  CONST VOID      *Property;
  INT32           Length;

  Fdt = FdtPlatformGetBase ();
  if (Fdt == NULL) {
    return EFI_NOT_FOUND;
  }

  Node = fdt_path_offset (Fdt, "/system");
  if (Node < 0) {
    return EFI_NOT_FOUND;
  }

  Property = fdt_getprop (Fdt, Node, "linux,revision", &Length);
  if (Property == NULL) {
    return EFI_NOT_FOUND;
  } else if (Length != sizeof (UINT32)) {
    return EFI_BAD_BUFFER_SIZE;
  }

  *RevisionCode = fdt32_to_cpu (*(UINT32 *) Property);

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BoardInfoGetSerialNumber (
  OUT   UINT64  *SerialNumber
  )
{
  VOID            *Fdt;
  INT32           Node;
  CONST VOID      *Property;
  INT32           Length;

  Fdt = FdtPlatformGetBase ();
  if (Fdt == NULL) {
    return EFI_NOT_FOUND;
  }

  Node = fdt_path_offset (Fdt, "/system");
  if (Node < 0) {
    return EFI_NOT_FOUND;
  }

  Property = fdt_getprop (Fdt, Node, "linux,serial", &Length);
  if (Property == NULL) {
    return EFI_NOT_FOUND;
  } else if (Length != sizeof (UINT64)) {
    return EFI_BAD_BUFFER_SIZE;
  }

  *SerialNumber = fdt64_to_cpu (*(UINT64 *) Property);

  return EFI_SUCCESS;
}

/**
  Get the pinctrl node compatible string.

  (only available on Pi5 series)
**/
EFI_STATUS
EFIAPI
BoardInfoGetPinctrl (
  OUT CONST CHAR8 **Pinctrl
  )
{
  VOID            *Fdt;
  INT32           SymNode, TargetNode;
  CONST VOID      *PinctrlPath;
  CONST VOID      *Compat;
  INT32           Length;

  Fdt = FdtPlatformGetBase ();
  if (Fdt == NULL) {
    return EFI_NOT_FOUND;
  }

  SymNode = fdt_path_offset (Fdt, "/__symbols__");
  if (SymNode < 0) {
    return EFI_NOT_FOUND;
  }

  PinctrlPath = fdt_getprop (Fdt, SymNode, "pinctrl", &Length);
  if (PinctrlPath == NULL || Length <= 0) {
    return EFI_NOT_FOUND;
  }

  TargetNode = fdt_path_offset (Fdt, PinctrlPath);
  if (TargetNode < 0) {
    return EFI_NOT_FOUND;
  }

  Compat = fdt_getprop (Fdt, TargetNode, "compatible", &Length);
  if (Compat == NULL) {
    return EFI_NOT_FOUND;
  } else if (Length < 0) {
    return EFI_NOT_FOUND;
  }

  *Pinctrl = (CHAR8 *) Compat;

  return EFI_SUCCESS;
}


/**
 * @brief Get the Mac Address of the ethernet node.
 * 
 * @param MacAddress[out] Pointer to the buffer to store the MAC address.
 *                        The buffer must be at least 6 bytes long.
 */
EFI_STATUS
EFIAPI
GetEthernetMacAddress (
  OUT UINT8 *MacAddress
  )
{
  EFI_STATUS    Status;
  VOID          *Fdt;
  INT32         Size;
  INTN          Node;
  CONST VOID    *Mac;

  if (MacAddress == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Fdt = FdtPlatformGetBase ();
  if (Fdt == NULL) {
    return EFI_NOT_FOUND;
  }

  Node = fdt_path_offset (Fdt, "ethernet0");
  if (Node < 0) {
    DEBUG ((DEBUG_ERROR, "%a: failed to locate 'ethernet0' alias\n", __func__));
    return EFI_NOT_FOUND;
  }

  Mac = fdt_getprop (Fdt, Node, "local-mac-address", &Size);
  if (EFI_ERROR (Status) || Size != 6) {
    DEBUG ((DEBUG_ERROR, "%a: failed to locate 'local-mac-address' property\n", __func__));
    return EFI_NOT_FOUND;
  }

  CopyMem (MacAddress, Mac, 6);

  return EFI_SUCCESS;
}
