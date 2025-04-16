/** @file
 *
 *  Copyright (c) 2025, Paul Oberosler <paul@paulober.dev>
 *  Copyright (c) 2023, Mario Bălănică <mariobalanica02@gmail.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef __BOARD_INFO_LIB_H__
#define __BOARD_INFO_LIB_H__

EFI_STATUS
EFIAPI
BoardInfoGetRevisionCode (
  OUT   UINT32  *RevisionCode
  );

EFI_STATUS
EFIAPI
BoardInfoGetSerialNumber (
  OUT   UINT64  *SerialNumber
  );

EFI_STATUS
EFIAPI
BoardInfoGetPinctrl (
  OUT CONST CHAR8 **Pinctrl
  );

#endif /* __BOARD_INFO_LIB_H__ */
