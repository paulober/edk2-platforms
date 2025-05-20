/**
 * @file SimpleNetwork.c
 * @author Paul Oberosler (paul@paulober.dev)
 * 
 * Based on Silicon/Broadcom/Drivers/Net/BcmGenetDxe/SimpleNetwork.c
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DmaLib.h>
#include <Library/NetLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/SimpleNetwork.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/TimerLib.h>

#include "Rp1EthernetDxe.h"


/**
 * @brief Changes the state of a network interface from "stopped" to "started".
 * 
 * @param This Protocol instance pointer.
 * 
 * @retval EFI_SUCCESS           The network interface was started.
 * @retval EFI_ALREADY_STARTED   The network interface is already in the started state.
 * @retval EFI_INVALID_PARAMETER One or more of the parameters has an unsupported value.
 * @retval EFI_DEVICE_ERROR      The network interface is not in the right (stopped) state.
 */
STATIC
EFI_STATUS
EFIAPI
MacbSimpleNetworkStart (
  IN EFI_SIMPLE_NETWORK_PROTOCOL *This
  )
{
  MACB_PRIVATE_DATA *Macb;

  ASSERT (This != NULL);

  Macb = MACB_PRIVATE_DATA_FROM_SNP_THIS (This);
  if (Macb->SnpMode.State == EfiSimpleNetworkStarted) {
      return EFI_ALREADY_STARTED;
  } else if (Macb->SnpMode.State != EfiSimpleNetworkStopped) {
      return EFI_DEVICE_ERROR;
  }

  MacbResumeTxRx (Macb);

  Macb->SnpMode.State = EfiSimpleNetworkStarted;
  return EFI_SUCCESS;
}

/**
 * @brief Changes the state of a network interface from "started" to "stopped".
 * 
 * @param This Protocol instance pointer.
 * @retval EFI_SUCCESS           The network interface was stopped.
 * @retval EFI_NOT_STARTED       The network interface is already in the stopped state.
 * @retval EFI_INVALID_PARAMETER One or more of the parameters has an unsupported value.
 * @retval EFI_DEVICE_ERROR      The network interface is not in the right (started) state.
 */
STATIC
EFI_STATUS
EFIAPI
MacbSimpleNetworkStop (
  IN EFI_SIMPLE_NETWORK_PROTOCOL *This
  )
{
  MACB_PRIVATE_DATA *Macb;

  ASSERT (This != NULL);

  Macb = MACB_PRIVATE_DATA_FROM_SNP_THIS (This);
  if (Macb->SnpMode.State == EfiSimpleNetworkStopped) {
      return EFI_ALREADY_STARTED;
  } else if (Macb->SnpMode.State != EfiSimpleNetworkStarted) {
      return EFI_DEVICE_ERROR;
  }

  MacbHaltTxRx (Macb);

  Macb->SnpMode.State = EfiSimpleNetworkStopped;
  return EFI_SUCCESS;
}

/**
  Resets a network adapter and allocates the transmit and receive buffers
  required by the network interface; optionally, also requests allocation
  of additional transmit and receive buffers.

  @param  This              The protocol instance pointer.
  @param  ExtraRxBufferSize The size, in bytes, of the extra receive buffer space
                            that the driver should allocate for the network interface.
                            Some network interfaces will not be able to use the extra
                            buffer, and the caller will not know if it is actually
                            being used.
  @param  ExtraTxBufferSize The size, in bytes, of the extra transmit buffer space
                            that the driver should allocate for the network interface.
                            Some network interfaces will not be able to use the extra
                            buffer, and the caller will not know if it is actually
                            being used.

  @retval EFI_SUCCESS           The network interface was initialized.
  @retval EFI_NOT_STARTED       The network interface has not been started.
  @retval EFI_OUT_OF_RESOURCES  There was not enough memory for the transmit and
                                receive buffers.
  @retval EFI_INVALID_PARAMETER One or more of the parameters has an unsupported value.
  @retval EFI_DEVICE_ERROR      The network inteface is not in the right (started) state.
  @retval EFI_DEVICE_ERROR      PHY register read/write error.
  @retval EFI_TIMEOUT           PHY reset time-out.
  @retval EFI_NOT_FOUND         No PHY detected.
  @retval EFI_UNSUPPORTED       This function is not supported by the network interface.

**/
STATIC
EFI_STATUS
EFIAPI
MacbSimpleNetworkInitialize (
  IN EFI_SIMPLE_NETWORK_PROTOCOL *This,
  IN UINTN ExtraRxBufferSize, OPTIONAL
  IN UINTN ExtraTxBufferSize OPTIONAL
  )
{
  EFI_STATUS Status;
  MACB_PRIVATE_DATA *Macb;

  ASSERT (This != NULL);

  Macb = MACB_PRIVATE_DATA_FROM_SNP_THIS (This);
  if (Macb->SnpMode.State == EfiSimpleNetworkStopped) {
      return EFI_NOT_STARTED;
  } else if (Macb->SnpMode.State != EfiSimpleNetworkStarted) {
      return EFI_DEVICE_ERROR;
  }

  // === RESET + START AUTO-NEGOTIATION ===
  Status = MacbResetPhy (Macb);
  if (EFI_ERROR(Status)) {
      DEBUG ((DEBUG_ERROR,
          "%a: PHY reset failed: %r\n", __func__, Status));
      return Status;
  }

  // === MAC SETUP ===
  gBS->Stall (10);

  Status = MacbWaitForLink (Macb);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR,
      "%a: No link detected: %r\n", __func__, Status));
  } else {
    Macb->SnpMode.MediaPresent = TRUE;
  }

  //gBS->Stall (2 * 1000 * 1000); // 2s

  Macb->SnpMode.State = EfiSimpleNetworkInitialized;
  DEBUG ((DEBUG_INFO,
      "%a: Network interface initialized\n", __func__));
  return EFI_SUCCESS;
}

/**
  Resets a network adapter and re-initializes it with the parameters that were
  provided in the previous call to Initialize().

  @param  This                 The protocol instance pointer.
  @param  ExtendedVerification Indicates that the driver may perform a more
                               exhaustive verification operation of the device
                               during reset.

  @retval EFI_SUCCESS           The network interface was reset.
  @retval EFI_NOT_STARTED       The network interface has not been started.
  @retval EFI_INVALID_PARAMETER One or more of the parameters has an unsupported value.
  @retval EFI_DEVICE_ERROR      The network inteface is not in the right (initialized) state.
  @retval EFI_UNSUPPORTED       This function is not supported by the network interface.

**/
STATIC
EFI_STATUS
EFIAPI
MacbSimpleNetworkReset (
  IN EFI_SIMPLE_NETWORK_PROTOCOL *This,
  IN BOOLEAN                     ExtendedVerification
  )
{
  EFI_STATUS Status;
  MACB_PRIVATE_DATA *Macb;

  ASSERT (This != NULL);

  Macb = MACB_PRIVATE_DATA_FROM_SNP_THIS (This);
  if (Macb->SnpMode.State == EfiSimpleNetworkStopped) {
      return EFI_NOT_STARTED;
  } else if (Macb->SnpMode.State != EfiSimpleNetworkInitialized) {
      return EFI_DEVICE_ERROR;
  }

  Status = MacbResetPhy (Macb);
  if (EFI_ERROR(Status)) {
      return Status;
  }

  Status = MacbWaitForLink (Macb);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR,
      "%a: No link detected after reset: %r\n", __func__, Status));
    Macb->SnpMode.MediaPresent = FALSE;
  } else {
    Macb->SnpMode.MediaPresent = TRUE;
  }

  DEBUG ((DEBUG_INFO,
      "%a: Network interface reset\n", __func__));
  return EFI_SUCCESS;
}

/**
  Resets a network adapter and leaves it in a state that is safe for
  another driver to initialize.

  @param  This Protocol instance pointer.

  @retval EFI_SUCCESS           The network interface was shutdown.
  @retval EFI_NOT_STARTED       The network interface has not been started.
  @retval EFI_INVALID_PARAMETER One or more of the parameters has an unsupported value.
  @retval EFI_DEVICE_ERROR      The network inteface is not in the right (initialized) state.
  @retval EFI_UNSUPPORTED       This function is not supported by the network interface.

**/
STATIC
EFI_STATUS
EFIAPI
MacbSimpleNetworkShutdown (
  IN EFI_SIMPLE_NETWORK_PROTOCOL *This
  )
{
  MACB_PRIVATE_DATA  *Macb;

  ASSERT (This != NULL);
  DEBUG ((DEBUG_INFO,
    "%a: Shutting down network interface\n", __func__));

  Macb = MACB_PRIVATE_DATA_FROM_SNP_THIS (This);
  if (Macb->SnpMode.State == EfiSimpleNetworkStopped) {
    return EFI_NOT_STARTED;
  } else if (Macb->SnpMode.State != EfiSimpleNetworkInitialized) {
    return EFI_DEVICE_ERROR;
  }

  MacbFullStop (Macb);

  Macb->SnpMode.State = EfiSimpleNetworkStarted;
  DEBUG ((DEBUG_INFO,
    "%a: Network interface shutdown\n", __func__));
  return EFI_SUCCESS;
}

/**
  Manages the receive filters of a network interface.

  @param  This             The protocol instance pointer.
  @param  Enable           A bit mask of receive filters to enable on the network interface.
  @param  Disable          A bit mask of receive filters to disable on the network interface.
  @param  ResetMCastFilter Set to TRUE to reset the contents of the multicast receive
                           filters on the network interface to their default values.
  @param  McastFilterCnt   Number of multicast HW MAC addresses in the new
                           MCastFilter list. This value must be less than or equal to
                           the MCastFilterCnt field of EFI_SIMPLE_NETWORK_MODE. This
                           field is optional if ResetMCastFilter is TRUE.
  @param  MCastFilter      A pointer to a list of new multicast receive filter HW MAC
                           addresses. This list will replace any existing multicast
                           HW MAC address list. This field is optional if
                           ResetMCastFilter is TRUE.

  @retval EFI_SUCCESS           The multicast receive filter list was updated.
  @retval EFI_NOT_STARTED       The network interface has not been started.
  @retval EFI_INVALID_PARAMETER One or more of the parameters has an unsupported value.
  @retval EFI_DEVICE_ERROR      The network inteface is not in the right (initialized) state.
  @retval EFI_UNSUPPORTED       This function is not supported by the network interface.

**/
STATIC
EFI_STATUS
EFIAPI
MacbSimpleNetworkReceiveFilters (
  IN EFI_SIMPLE_NETWORK_PROTOCOL *This,
  IN UINT32                      Enable,
  IN UINT32                      Disable,
  IN BOOLEAN                     ResetMCastFilter,
  IN UINTN                       MCastFilterCnt, OPTIONAL
  IN EFI_MAC_ADDRESS             *MCastFilter    OPTIONAL
  )
{
  MACB_PRIVATE_DATA  *Macb;
  EFI_TPL             OldTpl;

  ASSERT (This != NULL);

  OldTpl = gBS->RaiseTPL (TPL_CALLBACK);

  Macb = MACB_PRIVATE_DATA_FROM_SNP_THIS (This);
  if (((Enable | Disable) & ~Macb->SnpMode.ReceiveFilterMask) != 0 ||
      (!ResetMCastFilter && MCastFilterCnt > Macb->SnpMode.MaxMCastFilterCount)) {
    gBS->RestoreTPL (OldTpl);
    return EFI_INVALID_PARAMETER;
  }
  if (Macb->SnpMode.State == EfiSimpleNetworkStopped) {
    gBS->RestoreTPL (OldTpl);
    return EFI_NOT_STARTED;
  } else if (Macb->SnpMode.State != EfiSimpleNetworkInitialized) {
    gBS->RestoreTPL (OldTpl);
    return EFI_DEVICE_ERROR;
  }

  MacbHaltTxRx (Macb);
  gBS->Stall (10);

  MacbSetPromisc (Macb,
    (Enable & ~Disable & EFI_SIMPLE_NETWORK_RECEIVE_PROMISCUOUS) != 0);

  MacbEnableBroadcastFilter (Macb,
    (Enable & ~Disable & EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST) != 0);

  MacbSetMcast (
    Macb,
    ResetMCastFilter,
    ((Enable & ~Disable & EFI_SIMPLE_NETWORK_RECEIVE_MULTICAST) != 0)
      && (MCastFilter != NULL)
      && (MCastFilterCnt > 0),
    MCastFilterCnt,
    MCastFilter
  );

  gBS->Stall (10);
  MacbResumeTxRx (Macb);

  gBS->RestoreTPL (OldTpl);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
MacbSimpleNetworkStationAddress (
  IN EFI_SIMPLE_NETWORK_PROTOCOL *This,
  IN BOOLEAN                     Reset,
  IN EFI_MAC_ADDRESS             *New    OPTIONAL
  )
{
  MACB_PRIVATE_DATA  *Macb;
  EFI_TPL             OldTpl;
  
  OldTpl = gBS->RaiseTPL (TPL_CALLBACK);

  if (This == NULL || This->Mode == NULL) {
    gBS->RestoreTPL (OldTpl);
    return EFI_INVALID_PARAMETER;
  } else if (Reset == TRUE && New == NULL) {
    gBS->RestoreTPL (OldTpl);
    return EFI_INVALID_PARAMETER;
  }

  Macb = MACB_PRIVATE_DATA_FROM_SNP_THIS (This);
  if (Macb->SnpMode.State == EfiSimpleNetworkStopped) {
    gBS->RestoreTPL (OldTpl);
    return EFI_NOT_STARTED;
  } else if (Macb->SnpMode.State != EfiSimpleNetworkInitialized) {
    gBS->RestoreTPL (OldTpl);
    return EFI_DEVICE_ERROR;
  }

  if (Reset) {
    CopyMem (&This->Mode->CurrentAddress, &This->Mode->PermanentAddress,
      sizeof (This->Mode->CurrentAddress));
  } else {
    CopyMem (&This->Mode->CurrentAddress, New,
      sizeof (This->Mode->CurrentAddress));
  }
  MacbSetMacAddress (Macb, &Macb->SnpMode.CurrentAddress);

  gBS->RestoreTPL (OldTpl);
  DEBUG ((DEBUG_INFO,
    "%a: Station address updated\n", __func__));
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
MacbSimpleNetworkStatistics (
  IN EFI_SIMPLE_NETWORK_PROTOCOL *This,
  IN BOOLEAN                     Reset,
  IN OUT UINTN                   *StatisticsSize, OPTIONAL
  OUT EFI_NETWORK_STATISTICS     *StatisticsTable OPTIONAL
  )
{
    // TODO: implement
    return EFI_UNSUPPORTED;
}

STATIC
EFI_STATUS
EFIAPI
MacbSimpleNetworkNvData (
  IN EFI_SIMPLE_NETWORK_PROTOCOL *This,
  IN BOOLEAN                     ReadWrite,
  IN UINTN                       Offset,
  IN UINTN                       BufferSize,
  IN OUT VOID                    *Buffer
  )
{
    return EFI_UNSUPPORTED;
}

/**
  Reads the current interrupt status and recycled transmit buffer status from
  a network interface.

  @param  This            The protocol instance pointer.
  @param  InterruptStatus A pointer to the bit mask of the currently active interrupts
                          If this is NULL, the interrupt status will not be read from
                          the device. If this is not NULL, the interrupt status will
                          be read from the device. When the  interrupt status is read,
                          it will also be cleared. Clearing the transmit  interrupt
                          does not empty the recycled transmit buffer array.
  @param  TxBuf           Recycled transmit buffer address. The network interface will
                          not transmit if its internal recycled transmit buffer array
                          is full. Reading the transmit buffer does not clear the
                          transmit interrupt. If this is NULL, then the transmit buffer
                          status will not be read. If there are no transmit buffers to
                          recycle and TxBuf is not NULL, * TxBuf will be set to NULL.

  @retval EFI_SUCCESS           The status of the network interface was retrieved.
  @retval EFI_NOT_STARTED       The network interface has not been started.
  @retval EFI_INVALID_PARAMETER One or more of the parameters has an unsupported value.
  @retval EFI_DEVICE_ERROR      The network inteface is not in the right (initialized) state.
  @retval EFI_UNSUPPORTED       This function is not supported by the network interface.

**/
STATIC
EFI_STATUS
EFIAPI
MacbSimpleNetworkGetStatus (
  IN EFI_SIMPLE_NETWORK_PROTOCOL *This,
  OUT UINT32                     *InterruptStatus, OPTIONAL
  OUT VOID                       **TxBuf           OPTIONAL
  )
{
  MACB_PRIVATE_DATA     *Macb;
  UINT32                 Index;
  GEM_DMA64_TX_DESC     *TxDesc;
  EFI_STATUS             Status;
  EFI_TPL                OldTpl;

  ASSERT (This != NULL);

  OldTpl = gBS->RaiseTPL (TPL_CALLBACK);

  Macb = MACB_PRIVATE_DATA_FROM_SNP_THIS (This);
  if (Macb->SnpMode.State == EfiSimpleNetworkStopped) {
    gBS->RestoreTPL (OldTpl);
    return EFI_NOT_STARTED;
  } else if (Macb->SnpMode.State != EfiSimpleNetworkInitialized) {
    gBS->RestoreTPL (OldTpl);
    return EFI_DEVICE_ERROR;
  }

  Status = MacbCheckLink (Macb);
  if (EFI_ERROR (Status)) {
    Macb->SnpMode.MediaPresent = FALSE;
  } else {
    Macb->SnpMode.MediaPresent = TRUE;
  }

  if (TxBuf != NULL) {
    if (!MacbTxPending (Macb)) {
      // No TX buffer to recycle
      *TxBuf = NULL;
      goto CheckInterrupts;
    }
    DEBUG ((DEBUG_INFO,
      "%a: Recycling TX buffer\n", __func__));

    // buffer to recycle
    // Check the oldest TX descriptor to see if it's done
    Index = Macb->TxCleanIndex % RP1_NET_RING_DESC_COUNT;
    TxDesc = &Macb->TxRing[Index];

    // Transmission completed → return buffer to caller
    // TODO: unmap buffer if used

    // Return original buffer passed to SNP Transmit()
    *TxBuf = Macb->TxOriginalUserBuffer[Index];
    Macb->TxOriginalUserBuffer[Index] = NULL;

    // Reset / Prepare descriptor for next use
    TxDesc->Status = GEM_TX_STATUS_CPU_OWNS_BUF | GEM_TX_STATUS_LAST_BUF;
    if (Index == RP1_NET_RING_DESC_COUNT - 1) {
      TxDesc->Status |= GEM_TX_STATUS_RING_WRAP;
    }
    //TxDesc->AddrLo = 0;
    //TxDesc->AddrHi = 0;
    TxDesc->Pad = 0;

    Macb->TxCleanIndex = Index + 1;
    // faster than modulo
    if (Macb->TxCleanIndex >= RP1_NET_RING_DESC_COUNT) {
      Macb->TxCleanIndex -= RP1_NET_RING_DESC_COUNT;
    }
    Macb->TxQueued--;
  }

CheckInterrupts:
  if (InterruptStatus != NULL) {
    *InterruptStatus = 0;
    if (MacbRxAvailable (Macb)) {
      *InterruptStatus |= EFI_SIMPLE_NETWORK_RECEIVE_INTERRUPT;
    }
    if (MacbTxPending (Macb)) {
      *InterruptStatus |= EFI_SIMPLE_NETWORK_TRANSMIT_INTERRUPT;
    }
  }

  gBS->RestoreTPL (OldTpl);
  return EFI_SUCCESS;
}

/**
  Places a packet in the transmit queue of a network interface.

  @param  This       The protocol instance pointer.
  @param  HeaderSize The size, in bytes, of the media header to be filled in by
                     the Transmit() function. If HeaderSize is non-zero, then it
                     must be equal to This->Mode->MediaHeaderSize and the DestAddr
                     and Protocol parameters must not be NULL.
  @param  BufferSize The size, in bytes, of the entire packet (media header and
                     data) to be transmitted through the network interface.
  @param  Buffer     A pointer to the packet (media header followed by data) to be
                     transmitted. This parameter cannot be NULL. If HeaderSize is zero,
                     then the media header in Buffer must already be filled in by the
                     caller. If HeaderSize is non-zero, then the media header will be
                     filled in by the Transmit() function.
  @param  SrcAddr    The source HW MAC address. If HeaderSize is zero, then this parameter
                     is ignored. If HeaderSize is non-zero and SrcAddr is NULL, then
                     This->Mode->CurrentAddress is used for the source HW MAC address.
  @param  DestAddr   The destination HW MAC address. If HeaderSize is zero, then this
                     parameter is ignored.
  @param  Protocol   The type of header to build. If HeaderSize is zero, then this
                     parameter is ignored. See RFC 1700, section "Ether Types", for
                     examples.

  @retval EFI_SUCCESS           The packet was placed on the transmit queue.
  @retval EFI_NOT_STARTED       The network interface has not been started.
  @retval EFI_NOT_READY         The network interface is too busy to accept this transmit request.
  @retval EFI_BUFFER_TOO_SMALL  The BufferSize parameter is too small.
  @retval EFI_INVALID_PARAMETER One or more of the parameters has an unsupported value.
  @retval EFI_DEVICE_ERROR      The network inteface is not in the right (initialized) state.
  @retval EFI_UNSUPPORTED       This function is not supported by the network interface.

**/
STATIC
EFI_STATUS
EFIAPI
MacbSimpleNetworkTransmit (
    IN EFI_SIMPLE_NETWORK_PROTOCOL *This,
    IN UINTN                       HeaderSize,
    IN UINTN                       BufferSize,
    IN VOID                        *Buffer,
    IN EFI_MAC_ADDRESS             *SrcAddr,  OPTIONAL
    IN EFI_MAC_ADDRESS             *DestAddr, OPTIONAL
    IN UINT16                      *Protocol  OPTIONAL
  )
{
  MACB_PRIVATE_DATA     *Macb;
  UINT32                 Index;
  GEM_DMA64_TX_DESC     *TxDesc;
  EFI_PHYSICAL_ADDRESS   DmaAddr;
  EFI_STATUS             Status;
  UINT8                 *Frame;
  EFI_TPL                OldTpl;

  OldTpl = gBS->RaiseTPL (TPL_CALLBACK);
  Frame = Buffer;

  if (This == NULL || Buffer == NULL || BufferSize == 0) {
    gBS->RestoreTPL (OldTpl);
    return EFI_INVALID_PARAMETER;
  }

  Macb = MACB_PRIVATE_DATA_FROM_SNP_THIS (This);
  if (Macb->SnpMode.State == EfiSimpleNetworkStopped) {
    gBS->RestoreTPL (OldTpl);
    return EFI_NOT_STARTED;
  } else if (Macb->SnpMode.State != EfiSimpleNetworkInitialized) {
    gBS->RestoreTPL (OldTpl);
    return EFI_DEVICE_ERROR;
  }

  ASSERT (Macb->SnpMode.MediaPresent);
  if (HeaderSize != 0 && (HeaderSize != Macb->SnpMode.MediaHeaderSize 
      || DestAddr == NULL 
      || Protocol == NULL)) {
    gBS->RestoreTPL (OldTpl);
    return EFI_INVALID_PARAMETER;
  }

  if (BufferSize < Macb->SnpMode.MediaHeaderSize) {
    gBS->RestoreTPL (OldTpl);
    return EFI_BUFFER_TOO_SMALL;
  }

  if (HeaderSize != 0) {
    if (HeaderSize != Macb->SnpMode.MediaHeaderSize) {
      return EFI_INVALID_PARAMETER;
    }
    if (DestAddr == NULL || Protocol == NULL) {
      return EFI_INVALID_PARAMETER;
    }
  }

  Status = EfiAcquireLockOrFail (&Macb->Lock);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR,
      "%a: Couldn't get lock: %r\n", __func__, Status));
    gBS->RestoreTPL (OldTpl);
    return EFI_ACCESS_DENIED;
  }

  Status = MacbTxRingEnsureAvailable (Macb);
  if (EFI_ERROR (Status)) {
    EfiReleaseLock (&Macb->Lock);
    gBS->RestoreTPL (OldTpl);
    return EFI_NOT_READY;
  }

  Index = Macb->TxIndex;
  TxDesc = &Macb->TxRing[Index];

  if (!(TxDesc->Status & GEM_TX_STATUS_CPU_OWNS_BUF)) {
    DEBUG ((DEBUG_ERROR,
      "%a: TX queue full - Current index: %d\n", __func__, Macb->TxIndex));
    EfiReleaseLock (&Macb->Lock);
    gBS->RestoreTPL (OldTpl);
    return EFI_NOT_READY;
  }

  if (HeaderSize != 0) {
    CopyMem (&Frame[0], &DestAddr->Addr[0], NET_ETHER_ADDR_LEN);
    CopyMem (&Frame[6], &SrcAddr->Addr[0], NET_ETHER_ADDR_LEN);
    Frame[12] = (*Protocol & 0xFF00) >> 8;
    Frame[13] = *Protocol & 0xFF;
  }

  // TODO: just Map the user's buffer and save a copymem
  // and some mem
  Macb->TxOriginalUserBuffer[Index] = Frame;
  VOID *BufferAddr = (VOID *) ((UINT8 *) Macb->TxBuffer + (Index * RP1_NET_BUFFER_SIZE));
  DmaAddr = Macb->TxBufferDma + (Index * RP1_NET_BUFFER_SIZE);
  CopyMem(BufferAddr, Frame, BufferSize);

  ASSERT (BufferSize >= 60 && BufferSize <= RP1_NET_BUFFER_SIZE);
  (void) WriteBackDataCacheRange (
    BufferAddr,
    BufferSize
  );

  // Prepare descriptor
  TxDesc->Status |= (BufferSize & GEM_TX_STATUS_LEN_MASK);
  // TxDesc->Status |= MACB_BIT (TX_LAST);
  // Update buffer availability bit to move the ownership to the HW
  TxDesc->Status &= ~GEM_TX_STATUS_CPU_OWNS_BUF;
  WriteBackDataCacheRange(TxDesc, sizeof(GEM_DMA64_TX_DESC));

  DEBUG ((DEBUG_VERBOSE,
    "%a: TX desc: AddrLo: 0x%08X AddrHi: 0x%08X Status: 0x%08X\n",
    __func__, TxDesc->AddrLo, TxDesc->AddrHi, TxDesc->Status));

  Status = MacbPauseUntilIdle (Macb);
  if (EFI_ERROR (Status)) {
    EfiReleaseLock (&Macb->Lock);
    gBS->RestoreTPL (OldTpl);
    return EFI_NOT_READY;
  }

  // Trigger transmission
  MacbTriggerTx (Macb);

  // Advance TX index
  Macb->TxIndex = (Index + 1) % RP1_NET_RING_DESC_COUNT;
  Macb->TxQueued++;

  EfiReleaseLock (&Macb->Lock);
  gBS->RestoreTPL (OldTpl);
  DEBUG ((DEBUG_VERBOSE,
    "%a: Packet transmitted\n", __func__));
  return EFI_SUCCESS;
}


/**
  Receives a packet from a network interface.

  @param  This       The protocol instance pointer.
  @param  HeaderSize The size, in bytes, of the media header received on the network
                     interface. If this parameter is NULL, then the media header size
                     will not be returned.
  @param  BufferSize On entry, the size, in bytes, of Buffer. On exit, the size, in
                     bytes, of the packet that was received on the network interface.
  @param  Buffer     A pointer to the data buffer to receive both the media header and
                     the data.
  @param  SrcAddr    The source HW MAC address. If this parameter is NULL, the
                     HW MAC source address will not be extracted from the media
                     header.
  @param  DestAddr   The destination HW MAC address. If this parameter is NULL,
                     the HW MAC destination address will not be extracted from the
                     media header.
  @param  Protocol   The media header type. If this parameter is NULL, then the
                     protocol will not be extracted from the media header. See
                     RFC 1700 section "Ether Types" for examples.

  @retval  EFI_SUCCESS           The received data was stored in Buffer, and BufferSize has
                                 been updated to the number of bytes received.
  @retval  EFI_NOT_STARTED       The network interface has not been started.
  @retval  EFI_NOT_READY         No packets received.
  @retval  EFI_BUFFER_TOO_SMALL  The BufferSize parameter is too small.
  @retval  EFI_INVALID_PARAMETER One or more of the parameters has an unsupported value.
  @retval  EFI_DEVICE_ERROR      The network inteface is not in the right (initialized) state.
  @retval  EFI_UNSUPPORTED       This function is not supported by the network interface.

**/
STATIC
EFI_STATUS
EFIAPI
MacbSimpleNetworkReceive (
  IN     EFI_SIMPLE_NETWORK_PROTOCOL *This,
  OUT    UINTN                       *HeaderSize, OPTIONAL
  IN OUT UINTN                       *BufferSize,
  OUT    VOID                        *Buffer,
  OUT    EFI_MAC_ADDRESS             *SrcAddr,    OPTIONAL
  OUT    EFI_MAC_ADDRESS             *DestAddr,   OPTIONAL
  OUT    UINT16                      *Protocol    OPTIONAL
  )
{
  MACB_PRIVATE_DATA     *Macb;
  UINT32                 Index;
  GEM_DMA64_RX_DESC     *RxDesc;
  UINT8                 *RxBuf;
  UINTN                  Length;
  ETHER_HEAD            *Hdr;
  INT16                  RxPending;
  EFI_TPL                OldTpl;
  EFI_STATUS             Status;

  OldTpl = gBS->RaiseTPL (TPL_CALLBACK);

  if (This == NULL || BufferSize == NULL || Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Macb = MACB_PRIVATE_DATA_FROM_SNP_THIS (This);
  if (Macb->SnpMode.State == EfiSimpleNetworkStopped) {
    gBS->RestoreTPL (OldTpl);
    return EFI_NOT_STARTED;
  } else if (Macb->SnpMode.State != EfiSimpleNetworkInitialized) {
    gBS->RestoreTPL (OldTpl);
    return EFI_DEVICE_ERROR;
  }

  RxPending = MacbRxPending (Macb);
  if (RxPending < 0) {
    gBS->RestoreTPL (OldTpl);
    return EFI_NOT_READY;
  }
  ASSERT (RxPending != 0);
  Index = Macb->RxIndex;

  Status = EfiAcquireLockOrFail (&Macb->Lock);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Couldn't get lock: %r\n", __func__, Status));
    return EFI_ACCESS_DENIED;
  }

  RxDesc = &Macb->RxRing[Index];
  Length = (UINTN)RxPending;
  
  if (*BufferSize < Length) {
    RxDesc->AddrLo &= ~GEM_RX_ADDR_LO_CPU_OWNS_BUF; // clear CPU ownership
    RxDesc->Status = 0; // clear status
    Macb->RxIndex = (Macb->RxIndex + 1) % RP1_NET_RING_DESC_COUNT;

    EfiReleaseLock (&Macb->Lock);
    gBS->RestoreTPL (OldTpl);
    return EFI_BUFFER_TOO_SMALL;
  }

  // Copy received data
  RxBuf = (VOID *) ((UINT8 *) Macb->RxBuffer + (Index * RP1_NET_BUFFER_SIZE));
  InvalidateDataCacheRange (RxBuf, Length);

  Hdr = (ETHER_HEAD *)RxBuf;
  if (DestAddr != NULL) {
    CopyMem (&DestAddr->Addr[0], Hdr->DstMac, NET_ETHER_ADDR_LEN);
  }
  if (SrcAddr != NULL) {
    CopyMem (&SrcAddr->Addr[0], Hdr->SrcMac, NET_ETHER_ADDR_LEN);
  }
  if (Protocol != NULL) {
    // Network To Host Short (big endian → CPU endian)
    *Protocol = NTOHS (Hdr->EtherType);
  }
  if (HeaderSize != NULL) {
    *HeaderSize = Macb->SnpMode.MediaHeaderSize;
  }

  CopyMem (Buffer, RxBuf, Length);
  *BufferSize = Length;

  RxDesc->AddrLo &= ~GEM_RX_ADDR_LO_CPU_OWNS_BUF; // clear CPU ownership
  RxDesc->Status = 0;

  Macb->RxIndex = (Index + 1) % RP1_NET_RING_DESC_COUNT;
  
  EfiReleaseLock(&Macb->Lock);
  gBS->RestoreTPL (OldTpl);
  DEBUG ((DEBUG_VERBOSE,
    "%a: Packet received - length: %d\n", __func__, Length));
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
MacbSimpleNetworkMCastIPtoMAC (
    IN  EFI_SIMPLE_NETWORK_PROTOCOL *SimpleNetwork,
    IN  BOOLEAN                     IPv6,
    IN  EFI_IP_ADDRESS              *IP,
    OUT EFI_MAC_ADDRESS             *MAC
  )
{
    return EFI_UNSUPPORTED;
}

// Simple Network Protocol instance
CONST EFI_SIMPLE_NETWORK_PROTOCOL gMacbSimpleNetworkTemplate = {
    EFI_SIMPLE_NETWORK_PROTOCOL_REVISION,     // Revision
    MacbSimpleNetworkStart,                   // Start
    MacbSimpleNetworkStop,                    // Stop
    MacbSimpleNetworkInitialize,              // Initialize
    MacbSimpleNetworkReset,                   // Reset
    MacbSimpleNetworkShutdown,                // Shutdown
    MacbSimpleNetworkReceiveFilters,          // ReceiveFilters
    MacbSimpleNetworkStationAddress,          // StationAddress
    MacbSimpleNetworkStatistics,              // Statistics
    MacbSimpleNetworkMCastIPtoMAC,            // MCastIpToMac
    MacbSimpleNetworkNvData,                  // NvData
    MacbSimpleNetworkGetStatus,               // GetStatus
    MacbSimpleNetworkTransmit,                // Transmit
    MacbSimpleNetworkReceive,                 // Receive
    NULL,                                     // WaitForPacket
    NULL                                      // Mode
};
