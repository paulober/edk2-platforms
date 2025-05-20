/** @file
 * 
 *  Copyright (c) 2025, Paul Oberosler <paul@paulober.dev>
 * 
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 * 
 */

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/NetLib.h>
#include <Library/Rp1ClocksLib.h>
#include <Protocol/RpiFirmware.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/Rp1EthernetPlatformDevice.h>
#include <Guid/EventGroup.h>

#include "Rp1EthernetDxe.h"

STATIC EFI_EVENT                        mProtocolNotifyEvent;
STATIC VOID                             *mProtocolNotifyEventRegistration;
STATIC RASPBERRY_PI_FIRMWARE_PROTOCOL   *mFwProtocol;

/**
 * @brief Tests to see if this driver supports a given controller.
 * 
 * @param This[in]                 A pointer to the EFI_DRIVER_BINDING_PROTOCOL instance.  
 * @param ControllerHandle[in]     The handle of the controller to test.
 * @param RemainingDevicePath[in]  The remaining device path. (Ignored - this is not a bus driver.) 
 * @return STATIC 
 */
STATIC
EFI_STATUS
EFIAPI
MacbDriverBindingSupported (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   ControllerHandle,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath
  )
{
  RP1_ETHERNET_PLATFORM_DEVICE_PROTOCOL   *Dev;
  EFI_STATUS                              Status;

  // connect to the non-discoverable device
  Status = gBS->OpenProtocol (
    ControllerHandle,
    &gRp1EthernetPlatformDeviceProtocolGuid,
    (VOID **)&Dev,
    This->DriverBindingHandle,
    ControllerHandle,
    EFI_OPEN_PROTOCOL_BY_DRIVER);
  if (EFI_ERROR (Status)) {
      return EFI_UNSUPPORTED;
  }
  DEBUG ((DEBUG_INFO,
      "%a: Found Ethernet device at 0x%lx\n", __func__, Dev->BaseAddress));

  gBS->CloseProtocol (ControllerHandle,
      &gRp1EthernetPlatformDeviceProtocolGuid,
      This->DriverBindingHandle,
      ControllerHandle);
  return EFI_SUCCESS;
}

/**
 * @brief Callback function to shut down the network device at ExitBootServices
 * 
 * @param Event     Pointer to this event
 * @param Context   Event handler private data
 * @return STATIC 
 */
STATIC
VOID
EFIAPI
MacbNotifyExitBootServices(
  EFI_EVENT     Event,
  VOID          *Context
  )
{
  MACB_PRIVATE_DATA *Macb;
  Macb = (MACB_PRIVATE_DATA *)Context;

  // TODO: maybe disable interrupts here
  MacbFullStop (Macb);
}

/**
 * @brief Changes the state of a network interface from "stopped" to "started".
 * 
 * @param This Protocol instance pointer.
 * @param ControllerHandle Handle of the controller to start.
 * @param OPTIONAL RemainingDevicePath The remaining portion of the device path.
 * @return STATIC 
 */
STATIC
EFI_STATUS
EFIAPI
MacbDriverBindingStart(
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   ControllerHandle,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath   OPTIONAL
  )
{
  EFI_STATUS          Status;
  MACB_PRIVATE_DATA   *Macb;

  // allocate resources
  Macb = AllocateZeroPool (sizeof (MACB_PRIVATE_DATA));
  if (Macb == NULL) {
    DEBUG ((DEBUG_ERROR,
        "%a: Couldn't allocate private data\n", __func__));
    return EFI_OUT_OF_RESOURCES;
  }
  // Maybe just zero memory
  Macb->TxIndex = 0;
  Macb->RxIndex = 0;
  Macb->TxCleanIndex = 0;
  Macb->TxQueued = 0;

  Status = gBS->OpenProtocol (
    ControllerHandle,
    &gRp1EthernetPlatformDeviceProtocolGuid,
    (VOID **)&Macb->Dev,
    This->DriverBindingHandle,
    ControllerHandle,
    EFI_OPEN_PROTOCOL_BY_DRIVER
    );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR,
      "%a: Couldn't open protocol: %r\n", __func__, Status));
    goto FreeDevice;
  }

  MacbSetupClocks (Macb);

  Status = gBS->LocateProtocol (
      &gRp1GpioProtocolGuid,
      NULL,
      (VOID **)&Macb->Gpio
  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR,
        "%a: Couldn't locate RP1 GPIO protocol: %r\n", __func__, Status));
    goto FreeDevice;
  }
  // TODO: maybe move into gpiodxe to setup initial state of all pin that are active low
  // because it is active low
  Macb->Gpio->Write (MACB_PHY_RESET_PIN, TRUE);

  Macb->Signature = MACB_DRIVER_SIGNATURE;
  Macb->RegBase = Macb->Dev->BaseAddress;

  ASSERT (MacbSupports64BitDma (Macb));
  Status = MacbDmaAlloc(Macb);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR,
        "%a: Couldn't allocate DMA buffers: %r\n", __func__, Status));
    goto FreeDevice;
  }
  
  EfiInitializeLock (&Macb->Lock, TPL_CALLBACK);
  CopyMem (&Macb->Snp, &gMacbSimpleNetworkTemplate, sizeof Macb->Snp);
  CopyMem (&Macb->Aip, &gMacbAdapterInfoTemplate, sizeof Macb->Aip);

  Macb->Snp.Mode = &Macb->SnpMode;
  Macb->SnpMode.State = EfiSimpleNetworkStopped;
  Macb->SnpMode.HwAddressSize = NET_ETHER_ADDR_LEN;
  Macb->SnpMode.MediaHeaderSize = sizeof (ETHER_HEAD);
  //Macb->SnpMode.MaxPacketSize = MAX_ETHERNET_PKT_SIZE;
  Macb->SnpMode.MaxPacketSize = EFI_PAGE_SIZE;
  ASSERT (sizeof (ETHER_HEAD) == ETHERNET_HEADER_LEN);
  Macb->SnpMode.NvRamSize = 0;
  Macb->SnpMode.NvRamAccessSize = 0;
  Macb->SnpMode.ReceiveFilterMask = EFI_SIMPLE_NETWORK_RECEIVE_UNICAST
      | EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST
      | EFI_SIMPLE_NETWORK_RECEIVE_PROMISCUOUS;
  Macb->SnpMode.ReceiveFilterSetting = EFI_SIMPLE_NETWORK_RECEIVE_UNICAST
      | EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST;
  Macb->SnpMode.MaxMCastFilterCount = 0;
  Macb->SnpMode.MCastFilterCount = 0;
  Macb->SnpMode.IfType = NET_IFTYPE_ETHERNET;
  Macb->SnpMode.MacAddressChangeable = TRUE;
  Macb->SnpMode.MultipleTxSupported = FALSE;
  Macb->SnpMode.MediaPresentSupported = TRUE;
  Macb->SnpMode.MediaPresent = FALSE;
  Macb->Caps = MACB_CAPS_GIGABIT_MODE_AVAILABLE;// MACB_CAPS_CLK_HW_CHG
      //MACB_CAPS_GEM_HAS_PTP | MACB_CAPS_JUMBO |

  SetMem (&Macb->SnpMode.BroadcastAddress, sizeof (EFI_MAC_ADDRESS), 0xff);

  CopyMem (&Macb->SnpMode.PermanentAddress, &Macb->Dev->MacAddress,
      sizeof(EFI_MAC_ADDRESS));
  CopyMem (&Macb->SnpMode.CurrentAddress, &Macb->Dev->MacAddress,
      sizeof(EFI_MAC_ADDRESS));

  Status = gBS->CreateEventEx (
    EVT_NOTIFY_SIGNAL, 
    TPL_CALLBACK,
    MacbNotifyExitBootServices,
    Macb,
    &gEfiEventExitBootServicesGuid,
    &Macb->ExitBootServicesEvent
    );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR,
        "%a: Couldn't create event: %r\n", __func__, Status));
    goto FreeDevice;
  }

  Status = gBS->InstallMultipleProtocolInterfaces (
    &ControllerHandle,
    &gEfiSimpleNetworkProtocolGuid,       &Macb->Snp,
    &gEfiAdapterInformationProtocolGuid,  &Macb->Aip,
    NULL
    );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR,
      "%a: Couldn't install protocol interfaces: %r\n",
      __func__, Status));
    gBS->CloseProtocol (
      ControllerHandle,
      &gRp1EthernetPlatformDeviceProtocolGuid,
      This->DriverBindingHandle,
      ControllerHandle
      );
    goto FreeEvent;
  }

  Macb->ControllerHandle = ControllerHandle;
  // MacbConfigureCaps (Macb);
  return EFI_SUCCESS;

FreeEvent:
  gBS->CloseEvent (Macb->ExitBootServicesEvent);
FreeDevice:
  DEBUG ((DEBUG_WARN, "%a: Returning %r\n", __func__, Status));
  FreePool (Macb);
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
MacbDriverBindingStop(
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   ControllerHandle,
  IN UINTN                        NumberOfChildren,
  IN EFI_HANDLE                   *ChildHandleBuffer   OPTIONAL
  )
{
  EFI_STATUS                  Status;
  EFI_SIMPLE_NETWORK_PROTOCOL *SnpProtocol;
  MACB_PRIVATE_DATA           *Macb;

  Status = gBS->HandleProtocol (ControllerHandle,
      &gEfiSimpleNetworkProtocolGuid,
      (VOID **)&SnpProtocol);
  ASSERT_EFI_ERROR (Status);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Macb = MACB_PRIVATE_DATA_FROM_SNP_THIS (SnpProtocol);

  ASSERT (Macb->ControllerHandle == ControllerHandle);

  Status = gBS->UninstallMultipleProtocolInterfaces (ControllerHandle,
    &gEfiSimpleNetworkProtocolGuid,       &Macb->Snp,
    &gEfiAdapterInformationProtocolGuid,  &Macb->Aip,
    NULL);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->CloseEvent (Macb->ExitBootServicesEvent);
  ASSERT_EFI_ERROR (Status);

  MacbDmaFree (Macb);

  Status = gBS->CloseProtocol (ControllerHandle,
    &gRp1EthernetPlatformDeviceProtocolGuid,
    This->DriverBindingHandle,
    ControllerHandle);
  ASSERT_EFI_ERROR (Status);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  FreePool (Macb);
  return EFI_SUCCESS;
}

EFI_DRIVER_BINDING_PROTOCOL mMacbDriverBinding = {
  MacbDriverBindingSupported,
  MacbDriverBindingStart,
  MacbDriverBindingStop,
  MACB_VERSION,
  NULL,
  NULL
};

/**
 * @brief Protcol notify callback function for RP1 Ethernet Platform Device Protocol.
 * 
 * @param Event The event that is signaled.
 * @param Context A pointer to the context data.
 * @return STATIC 
 */
STATIC
VOID
EFIAPI
OnProtocolNotify(
  IN EFI_EVENT Event,
  IN VOID *Context
  )
{
  EFI_STATUS                              Status;
  EFI_HANDLE                              *HandleBuffer;
  RP1_ETHERNET_PLATFORM_DEVICE_PROTOCOL   *MacbDevice;
  UINTN                                   HandleCount;
  UINTN                                   Index;
  MACB_PRIVATE_DATA                       *Macb;

  Macb = (MACB_PRIVATE_DATA *)Context;

  while (TRUE) {
    Status = gBS->LocateHandleBuffer(ByRegisterNotify, NULL,
        mProtocolNotifyEventRegistration, &HandleCount, &HandleBuffer);
    if (EFI_ERROR(Status)) {
      break;
    }

    for (Index = 0; Index < HandleCount; Index++) {
      Status = gBS->HandleProtocol(
        HandleBuffer[Index],
        &gRp1EthernetPlatformDeviceProtocolGuid,
        (VOID **)&MacbDevice
        );
      ASSERT_EFI_ERROR(Status);

      MacbSetMacAddress (Macb, &MacbDevice->MacAddress);
    }

    FreePool(HandleBuffer);
  }
}

/**
 * @brief The entry point of the RP1 Cadence MACB Ethernet UEFI Driver.
 * 
 * @param ImageHandle The image handle of the UEFI Driver.
 * @param SystemTable A pointer to the EFI System Table.
 * @return EFI_STATUS 
 */
EFI_STATUS
EFIAPI
Rp1EthernetDxeEntryPoint (
  IN EFI_HANDLE ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  EFI_STATUS Status;
  
  Status = EfiLibInstallDriverBindingComponentName2 (
    ImageHandle,
    SystemTable,
    &mMacbDriverBinding,
    ImageHandle,
    &gMacbComponentName,
    &gMacbComponentName2
    );
  ASSERT_EFI_ERROR (Status);

  mProtocolNotifyEvent = EfiCreateProtocolNotifyEvent (
    &gRp1EthernetPlatformDeviceProtocolGuid,
    TPL_CALLBACK,
    OnProtocolNotify,
    NULL,
    &mProtocolNotifyEventRegistration
    );
  ASSERT (mProtocolNotifyEvent != NULL);

  DEBUG ((DEBUG_INIT | DEBUG_INFO,
      "Installed RP1 Cadence MACB Ethernet UEFI driver!\n"));

  Status = gBS->LocateProtocol (
    &gRaspberryPiFirmwareProtocolGuid,
    NULL,
    (VOID **)&mFwProtocol
    );
  ASSERT_EFI_ERROR (Status);

  return EFI_SUCCESS;
}

/**
 * @brief Unload function of RP1 Cadence MACB Ethernet UEFI Driver.
 * 
 * @param ImageHandle The allocated handle for the EFI image.
 * @return EFI_STATUS
 */
EFI_STATUS
EFIAPI
Rp1EthernetDxeUnloadImage (
  IN EFI_HANDLE ImageHandle
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  *HandleBuffer;
  UINTN       HandleCount, Index;

  Status = gBS->LocateHandleBuffer (ByProtocol,
    &gRp1EthernetPlatformDeviceProtocolGuid,
    NULL,
    &HandleCount,
    &HandleBuffer
    );
  if (EFI_ERROR (Status)) {
      return Status;
  }

  for (Index = 0; Index < HandleCount && !EFI_ERROR (Status); Index++) {
    Status = gBS->DisconnectController (
      HandleBuffer[Index],
      gImageHandle,
      NULL
      );
  }

  gBS->FreePool (HandleBuffer);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "%a: failed to disconnect all controllers - %r\n",
        __func__, Status));
    return Status;
  }

  Status = EfiLibUninstallDriverBindingComponentName2 (
    &mMacbDriverBinding,
    &gMacbComponentName,
    &gMacbComponentName2
  );
  ASSERT_EFI_ERROR (Status);

  return EFI_SUCCESS;
}
