/** @file
 *
 *  Copyright (c) 2025, Paul Oberosler <paul@paulober.dev>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef __RP1_GPIO_PROTOCOL_H__
#define __RP1_GPIO_PROTOCOL_H__

#include <Uefi/UefiBaseType.h>

#define RP1_GPIO_PROTOCOL_GUID \
  {0x2775d532, 0x1fc5, 0x4959, { 0xa8, 0x5b, 0x7c, 0xc7, 0x63, 0xa5, 0x72, 0x9f }}

typedef enum {
    RP1_GPIO_PIN_PULL_NONE  = 0,
    RP1_GPIO_PIN_PULL_DOWN  = 1,
    RP1_GPIO_PIN_PULL_UP    = 2
} RP1_GPIO_PIN_PULL;

typedef
EFI_STATUS
(EFIAPI *RP1_GPIO_GET_FUNCTION)(
    IN UINT8 Pin,
    OUT UINT8 *Function
);

typedef
EFI_STATUS
(EFIAPI *RP1_GPIO_SET_FUNCTION)(
    IN UINT8 Pin,
    IN UINT8 Function
);

typedef
EFI_STATUS
(EFIAPI *RP1_GPIO_GET_PULL)(
    IN UINT8 Pin,
    OUT RP1_GPIO_PIN_PULL *Pull
);

typedef
EFI_STATUS
(EFIAPI *RP1_GPIO_SET_PULL)(
    IN UINT8 Pin,
    IN RP1_GPIO_PIN_PULL Pull
);

typedef
EFI_STATUS
(EFIAPI *RP1_GPIO_READ)(
    IN UINT8 Pin,
    OUT BOOLEAN *Value
);

typedef
EFI_STATUS
(EFIAPI *RP1_GPIO_WRITE)(
    IN UINT8 Pin,
    IN BOOLEAN Value
);

typedef
EFI_STATUS
(EFIAPI *RP1_GPIO_GET_DIRECTION)(
    IN UINT8 Pin,
    OUT BOOLEAN *Direction
);

typedef
EFI_STATUS
(EFIAPI *RP1_GPIO_SET_DIRECTION)(
    IN UINT8 Pin,
    IN BOOLEAN Input
);

typedef
EFI_STATUS
(EFIAPI *RP1_GPIO_CLEAR)(
    IN UINT8 Pin
);

typedef struct {
    RP1_GPIO_GET_FUNCTION     GetFunction;
    RP1_GPIO_SET_FUNCTION     SetFunction;

    RP1_GPIO_GET_PULL         GetPull;
    RP1_GPIO_SET_PULL         SetPull;

    RP1_GPIO_READ             Read;
    RP1_GPIO_WRITE            Write;

    RP1_GPIO_GET_DIRECTION    GetDirection;
    RP1_GPIO_SET_DIRECTION    SetDirection;

    RP1_GPIO_CLEAR            Clear;
} RP1_GPIO_PROTOCOL;

extern EFI_GUID gRp1GpioProtocolGuid;

#endif // __RP1_GPIO_PROTOCOL_H__
