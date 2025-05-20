/**
 * @file MacbUtil.c
 * @author Paul Oberosler (paul@paulober.dev)
 * 
 * @copyright Copyright (c) 2025
 * 
 * @brief Utility functions for the Cadence MACB Ethernet driver.
 */

#include <Uefi.h>
#include <Library/DebugLib.h>
#include <Library/DmaLib.h>
#include <Library/IoLib.h>
#include <Library/NetLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Rp1GpioFunction.h>
#include <Library/Rp1ClocksLib.h>
#include <Library/CacheMaintenanceLib.h>

#include "Rp1EthernetDxe.h"

STATIC
UINT32
MacbMmioRead (
  IN MACB_PRIVATE_DATA    *Macb,
  IN UINT32               Offset
  )
{
  ASSERT((Offset & 3) == 0);

  return MmioRead32 (Macb->RegBase + Offset);
}

STATIC
VOID
MacbMmioWrite (
  IN MACB_PRIVATE_DATA    *Macb,
  IN UINT32               Offset,
  IN UINT32               Data
  )
{
  ASSERT ((Offset & 3) == 0);

  MemoryFence ();
  MmioWrite32 (Macb->RegBase + Offset, Data);
}

STATIC
VOID
MacbMmioOr (
  IN MACB_PRIVATE_DATA    *Macb,
  IN UINT32               Offset,
  IN UINT32               Data
  )
{
  ASSERT ((Offset & 3) == 0);

  MemoryFence ();
  MmioOr32 (Macb->RegBase + Offset, Data);
}

STATIC
VOID
MacbMmioAnd (
  IN MACB_PRIVATE_DATA    *Macb,
  IN UINT32               Offset,
  IN UINT32               Data
  )
{
  ASSERT ((Offset & 3) == 0);

  MemoryFence ();
  MmioAnd32 (Macb->RegBase + Offset, Data);
}

/**
 * @brief Perform a MACB PHY register read.
 * 
 * @param Macb[in]     Pointer to MACB_PRIVATE_DATA instance.
 * @param Reg[in]      PHY register.
 * @param Data[out]    Pointer to register data read.
 * @retval EFI_SUCCESS       Data read successfully.
 */
EFI_STATUS
EFIAPI
MacbMdioRead (
  IN MACB_PRIVATE_DATA *Macb,
  IN UINT8 Reg,
  OUT UINT16 *Data
  )
{
  UINT32 Value;

  // Using older MDIO Clause 22
  Value = MACB_BF(SOF, MACB_MAN_C22_SOF)  |
          MACB_BF(RW, MACB_MAN_C22_READ)  |
          MACB_BF(PHYA, 0x1)  |  // TODO: read from FDT
          MACB_BF(REGA, Reg)  |
          MACB_BF(CODE, MACB_MAN_C22_CODE);
  MacbMmioWrite (Macb, MACB_MAN, Value);

  // Wait for the operation to complete
  // TODO: maybe use idle function
  while (!MACB_BFEXT (IDLE, MacbMmioRead (Macb, MACB_NSR)))
      gBS->Stall (MACB_MDIO_POLL_IVAL);

  Value = MacbMmioRead (Macb, MACB_MAN);
  *Data = (UINT16)(MACB_BFEXT(DATA, Value));

  return EFI_SUCCESS;
}

/**
 * @brief Perform a MACB PHY register write.
 * 
 * @param Macb[in]      Pointer to MACB_PRIVATE_DATA instance.
 * @param Reg[in]       PHY register
 * @param Data[in]      Pointer to register data to write.
 * 
 * @retval EFI_STATUS   Data written successfully.
 */
EFI_STATUS
EFIAPI
MacbMdioWrite (
  IN MACB_PRIVATE_DATA *Macb,
  IN UINT8 Reg,
  IN UINT16 Data
  )
{
  UINT32  Value;

  // Using older MDIO Clause 22
  Value = MACB_BF(SOF, MACB_MAN_C22_SOF)
          | MACB_BF(RW, MACB_MAN_C22_WRITE)
          | MACB_BF(PHYA, 0x1)  // TODO: read from FDT
          | MACB_BF(REGA, Reg)
          | MACB_BF(CODE, MACB_MAN_C22_CODE)
          | MACB_BF(DATA, Data);
  MacbMmioWrite (Macb, MACB_MAN, Value);

  // Wait for the operation to complete
  while (!MACB_BFEXT (IDLE, MacbMmioRead (Macb, MACB_NSR)))
      gBS->Stall (MACB_MDIO_POLL_IVAL);

  return EFI_SUCCESS;
}

#define ENSURE_CLK_ENABLED(ClkId, ClkVar) \
  Status = Rp1IsClockEnabled (Macb->Dev->BaseAddress, ClkId, &ClkVar); \
  ASSERT_EFI_ERROR (Status); \
  if (!ClkVar) { \
    Status = Rp1ClockEnable (Macb->Dev->BaseAddress, ClkId); \
    ASSERT_EFI_ERROR (Status); \
  }

VOID
MacbSetupClocks (
  IN MACB_PRIVATE_DATA *Macb
  )
{
  EFI_STATUS  Status;
  BOOLEAN     IsClkEthEnabled, IsClkEthTsuEnabled, IsClkSysEnabled;
  
  DEBUG ((DEBUG_INFO,
      "%a: Setting up RP1 clocks for Ethernet.\n", __func__));
  
  ENSURE_CLK_ENABLED (RP1_CLK_ETH,     IsClkEthEnabled);
  ENSURE_CLK_ENABLED (RP1_CLK_ETH_TSU, IsClkEthTsuEnabled);
  ENSURE_CLK_ENABLED (RP1_CLK_SYS,     IsClkSysEnabled);

  #ifdef DEBUG
  // Verify that is worked
  Status = Rp1IsClockEnabled (Macb->Dev->BaseAddress, RP1_CLK_ETH, &IsClkEthEnabled);
  ASSERT_EFI_ERROR (Status);
  Status = Rp1IsClockEnabled (Macb->Dev->BaseAddress, RP1_CLK_ETH_TSU, &IsClkEthTsuEnabled);
  ASSERT_EFI_ERROR (Status);
  // PCLK and HCLK = CLK_SYS
  Status = Rp1IsClockEnabled (Macb->Dev->BaseAddress, RP1_CLK_SYS, &IsClkSysEnabled);
  ASSERT_EFI_ERROR (Status);

  if (!IsClkEthEnabled || !IsClkEthTsuEnabled || !IsClkSysEnabled) {
      DEBUG ((DEBUG_ERROR,
          "%a: Failed to enable clocks for Ethernet.\n", __func__));
  } else {
      DEBUG ((DEBUG_INFO,
          "%a: Clocks for Ethernet enabled successfully.\n", __func__));
  }
  #endif
}

STATIC
VOID
MacbSetTxClock (
  IN MACB_PRIVATE_DATA *Macb,
  IN GENERIC_PHY_SPEED Speed
  )
{
  EFI_STATUS  Status;
  UINT32      DesiredRateHz = 0;

  switch (Speed) {
  case PHY_SPEED_10:
      DesiredRateHz = 2500000;
      break;
  case PHY_SPEED_100:
      DesiredRateHz = 25000000;
      break;
  case PHY_SPEED_1000:
      DesiredRateHz = 125000000;
      break;
  default:
      DEBUG ((DEBUG_WARN, "MACB: Unsupported PHY speed: %u\n", Speed));
      break;
  }

  if (DesiredRateHz != 0) {
    Status = Rp1EthSetClockRate (
      Macb->RegBase,
      DesiredRateHz
    );
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_WARN, "MACB: Failed to set Tx clock rate: %r\n", Status));
    } else {
      DEBUG ((DEBUG_INFO, "MACB: Tx clock rate set to %u Hz\n", DesiredRateHz));
    }
  }
}

STATIC
VOID
GemSetDma (
  IN MACB_PRIVATE_DATA *Macb
  )
{
  MemoryFence();

  // Now give the GEM the ring pointers
  MacbMmioWrite (Macb, MACB_TBQP, (UINT32)(Macb->TxRingDma));
  MacbMmioWrite (Macb, MACB_RBQP, (UINT32)(Macb->RxRingDma));

  // Because of ADDR64
  MacbMmioWrite (Macb, MACB_TBQPH, (UINT32)(Macb->TxRingDma >> 32));
  MacbMmioWrite (Macb, MACB_RBQPH, (UINT32)(Macb->RxRingDma >> 32));
}

STATIC UINT8 ZeroAddr[NET_ETHER_ADDR_LEN] = { 0 };

/**
 * @brief Process a PHY link speed change (e.g. with MAC layer).
 * 
 * @param Macb[in]    Pointer to MACB_PRIVATE_DATA instance
 * @param Speed[in]   Speed setting
 * @param Duplex[in]  Duplex setting
 * @return VOID 
 */
VOID
EFIAPI
MacbPhyConfigure (
  IN MACB_PRIVATE_DATA *Macb,
  IN GENERIC_PHY_SPEED Speed,
  IN GENERIC_PHY_DUPLEX Duplex
  )
{
  UINT32  Reg;

  // Interrupts need to be enabled for status bits to work
  Reg = MACB_INT_IER_NORMAL_BITS | MACB_INT_IER_ERR_BITS;
  MacbMmioWrite (Macb, MACB_IER, Reg);

  // Tell the GEM about the ring pointers
  GemSetDma (Macb);

  // read current config
  Reg = MacbMmioRead (Macb, GEM_NCFGR);

  // clear existing speed and duplex settings
  Reg &= ~(MACB_BIT (SPD) | MACB_BIT (FD));
  Reg &= ~(MACB_BIT (PAE) | GEM_BIT(GBE));

  // set duplex
  if (Duplex == PHY_DUPLEX_FULL) {
    Reg |= MACB_BIT (FD);
  }

  // Make sure pause frames are disabled
  Reg &= ~MACB_BIT (PAE);

  switch (Speed)
  {
    case PHY_SPEED_100:
      Reg |= MACB_BIT (SPD);
      break;
    case PHY_SPEED_1000:
      Reg |= GEM_BIT(GBE);
      break;
    default:
      break;
  }

  // write back the new config
  MacbMmioWrite (Macb, GEM_NCFGR, Reg);
  // Set the Tx clock rate
  MacbSetTxClock (Macb, Speed);

  // Set Mac-Address
  if (CompareMem (
    Macb->SnpMode.CurrentAddress.Addr, 
    ZeroAddr, 
    NET_ETHER_ADDR_LEN) != 0) {
    MacbSetMacAddress (Macb, &Macb->SnpMode.CurrentAddress);
  }

  MemoryFence();

  // enable rx and tx
  MacbEnableTxRx (Macb);

  DEBUG((DEBUG_INFO, 
    "MACB: link configured: %u Mbps, %a duplex\n",
    Speed,
    (Duplex == PHY_DUPLEX_FULL) ? "full" : "half"));
}

#if DEBUG
STATIC
VOID
MacbDebugPhyId (
  IN MACB_PRIVATE_DATA *Macb
  )
{
  EFI_STATUS  Status;
  UINT16      Msb, Lsb, Bctrl;

  Msb = Lsb = 0;

  Status = MacbMdioRead (Macb, GENERIC_PHY_BMCR, &Bctrl);
  ASSERT_EFI_ERROR (Status);

  Status = MacbMdioRead (Macb, GENERIC_PHY_PHYIDR1, &Msb);
  ASSERT_EFI_ERROR (Status);

  Status = MacbMdioRead (Macb, GENERIC_PHY_PHYIDR2, &Lsb);
  ASSERT_EFI_ERROR (Status);

  DEBUG ((DEBUG_INFO, "MACB: PHY ID: %04x %04x with CTRL: %04x\n", Msb, Lsb, Bctrl));
}
#endif

EFI_STATUS
EFIAPI
MacbResetPhy (
  IN MACB_PRIVATE_DATA *Macb
  )
{
  EFI_STATUS  Status;
  UINT32      Reg;
  UINT16      PhyReg;

  // disable management port
  MacbMmioAnd (Macb, MACB_NCR, ~MACB_BIT(MPE));

  Macb->Gpio->SetDirection (MACB_PHY_RESET_PIN, FALSE);
  Macb->Gpio->Clear (MACB_PHY_RESET_PIN);
  gBS->Stall(20); // 20us
  Macb->Gpio->Write (MACB_PHY_RESET_PIN, TRUE);
  // TODO: load phy reset time from fdt
  // Wait for phy internal reset to complete
  gBS->Stall(50 * 1000); // 50ms

  // Set MDC clock divisor
  // Or use Reg = MacbMdcClkDiv (Macb);
  Reg = MacbMmioRead (Macb, MACB_NCFGR);
  Reg &= ~MACB_BIT (CLK);
  // TODO: dynamically chose based on PCLK
  Reg = GEM_BFINS (CLK, GEM_CLK_DIV96, Reg);
  MacbMmioWrite (Macb, MACB_NCFGR, Reg);

  // Reenable management port
  MacbMmioOr (Macb, MACB_NCR, MACB_BIT(MPE));
  #if DEBUG
  MacbDebugPhyId (Macb);
  #endif

  MacbInit (Macb);

  // Start link nogation
  PhyReg = 0 | GENERIC_PHY_BMCR_ANE | GENERIC_PHY_BMCR_RESTART_AN;
  Status = MacbMdioWrite (Macb, GENERIC_PHY_BMCR, PhyReg);
  ASSERT_EFI_ERROR (Status);

  Status = MacbMdioRead (Macb, GENERIC_PHY_BMCR, &PhyReg);
  ASSERT_EFI_ERROR (Status);

  return EFI_SUCCESS;
}

// TODO: return this:
/*
Returns the current media state status. MediaState can have any of the following values:
EFI_SUCCESS: There is media attached to the network adapter. EFI_NOT_READY: This detects a bounced state.
There was media attached to the network adapter, but it was removed and reattached. EFI_NO_MEDIA: There is
not any media attached to the network.
*/
EFI_STATUS
EFIAPI
MacbCheckLink (
  IN MACB_PRIVATE_DATA *Macb
  )
{
  UINT16 MiiStatus;
  EFI_STATUS Status;

  // Read the link status
  Status = MacbMdioRead (Macb, GENERIC_PHY_BMSR, &MiiStatus);
  ASSERT_EFI_ERROR (Status);

  if (MiiStatus & GENERIC_PHY_BMSR_LINK_STATUS) {
    return EFI_SUCCESS;
  }

  return EFI_NOT_READY;
}

EFI_STATUS
EFIAPI
MacbWaitForLink (
  IN MACB_PRIVATE_DATA *Macb
  )
{
  UINT16 MiiStatus, LinkReadyMask, Mii1000BaseTStatus, Lpa;
  UINT32 LinkReady;
  EFI_STATUS Status;
  GENERIC_PHY_SPEED Speed;
  GENERIC_PHY_DUPLEX Duplex;

  LinkReadyMask = GENERIC_PHY_BMSR_ANEG_COMPLETE | GENERIC_PHY_BMSR_LINK_STATUS;

  // TODO: not wait for ever!
  // Wait for link to be established
  do {
    Status = MacbMdioRead (Macb, GENERIC_PHY_BMSR, &MiiStatus);
    ASSERT_EFI_ERROR (Status);

    Status = MacbMdioRead (Macb, GENERIC_PHY_GBSR, &Mii1000BaseTStatus);
    ASSERT_EFI_ERROR (Status);

    Status = MacbMdioRead (Macb, GENERIC_PHY_ANLPAR, &Lpa);
    ASSERT_EFI_ERROR (Status);
    
    LinkReady = ((MiiStatus & LinkReadyMask) == LinkReadyMask);
    if (LinkReady) {
      if (Mii1000BaseTStatus & ((1 << 11) | (1 << 10))) {
        Speed = PHY_SPEED_1000;
        Duplex = (Mii1000BaseTStatus & (1 << 11)) ? PHY_DUPLEX_FULL : PHY_DUPLEX_HALF;
      } else {
        // check link partner ability
        // TODO: combine and move bit masks to header
        if (Lpa & (1 << 8)) {
          Speed = PHY_SPEED_100;
          Duplex = PHY_DUPLEX_FULL;
        }
        else if (Lpa & (1 << 7)) {
          Speed = PHY_SPEED_100;
          Duplex = PHY_DUPLEX_HALF;
        }
        else if (Lpa & (1 << 6)) {
          Speed = PHY_SPEED_10;
          Duplex = PHY_DUPLEX_FULL;
        }
        else if (Lpa & (1 << 5)) {
          Speed = PHY_SPEED_10;
          Duplex = PHY_DUPLEX_HALF;
        }
        else {
          // No link
          Speed = PHY_SPEED_NONE;
          Duplex = PHY_DUPLEX_HALF;
        }
      }

      if (Speed != PHY_SPEED_NONE) {
        DEBUG ((DEBUG_INFO, "MACB: Link established: %u Mbps, %a duplex\n",
          Speed,
          (Duplex == PHY_DUPLEX_FULL) ? "full" : "half"));
        MacbPhyConfigure (Macb, Speed, Duplex);
        return EFI_SUCCESS;
      } else {
        gBS->Stall (500); // 500us
      }
    }
  } while (TRUE);

  return EFI_NOT_READY;
}

BOOLEAN
MacbRxAvailable (
  IN MACB_PRIVATE_DATA *Macb
  )
{
  UINT32 Isr;

  // No clear necessary as clear-on-read
  Isr = MacbMmioRead (Macb, MACB_ISR);
  return (Isr & MACB_INT_RCOMP) != 0;
}

/**
 * @brief Check the RX descriptor for ownership and status.
 * 
 * @param Ring [in] Pointer to the RX descriptor ring.
 * @return INT16  Length of the received packet or error code.
 *               -1: No ownership
 *               -2: Invalid status
 */
STATIC
INT16
CheckDescriptor (
  GEM_DMA64_RX_DESC *Ring
  )
{
  UINT32 DescStatus;

  MemoryFence ();
  InvalidateDataCacheRange (Ring, sizeof(GEM_DMA64_RX_DESC));

  if ((Ring->AddrLo & GEM_RX_ADDR_LO_CPU_OWNS_BUF) == 0) {
    return -1;
  }
  DescStatus = Ring->Status;
  if (!(DescStatus & GEM_RX_STATUS_LEN_MASK)) {
    DEBUG ((DEBUG_ERROR,
      "MACB: RX descriptor status invalid: 0x%08x\n", DescStatus));
    return -2;
  }
  DescStatus &= (GEM_RX_STATUS_SOF | GEM_RX_STATUS_EOF);
  if (DescStatus ^ (GEM_RX_STATUS_SOF | GEM_RX_STATUS_EOF)) {
    DEBUG ((DEBUG_ERROR,
      "MACB: RX descriptor status invalid(2): 0x%08x\n", DescStatus));
    return -2;
  }

  return Ring->Status & GEM_RX_STATUS_LEN_MASK;
}

INT16
MacbRxPending (
  IN MACB_PRIVATE_DATA *Macb
  )
{
  INT16 BufLen;
  GEM_DMA64_RX_DESC *Ring;

  do {
    Ring = &Macb->RxRing[Macb->RxIndex];
    BufLen = CheckDescriptor (Ring);
    if (BufLen == -1) {
      break;
    } else if (BufLen == -2) {
      // reset descriptor
      Ring->AddrLo &= ~GEM_RX_ADDR_LO_CPU_OWNS_BUF;
      Ring->Status = 0;

      // optimized approach without modulo or branching
      // if desc count is power of 2
      //ASSERT (RP1_NET_RING_DESC_COUNT % 2 == 0);
      //Macb->RxIndex = (Macb->RxIndex + 1) & (RP1_NET_RING_DESC_COUNT - 1);
      Macb->RxIndex = (Macb->RxIndex + 1) % RP1_NET_RING_DESC_COUNT;
    }
  } while (BufLen < 0);

  return BufLen;
}

// TODO: maybe check TXUBR in ISR (if not cleared by check tx available thing)
BOOLEAN
MacbTxPending (
  IN MACB_PRIVATE_DATA *Macb
  )
{
  UINT32               Index;
  GEM_DMA64_TX_DESC   *TxDesc;

  Index = Macb->TxCleanIndex;
  ASSERT (Index < RP1_NET_RING_DESC_COUNT);
  TxDesc = &Macb->TxRing[Index];

  //InvalidateDataCacheRange (TxDesc, sizeof(GEM_DMA64_TX_DESC));

  // If GEM cleared CPU ownership, we can now reclaim it
  if ((TxDesc->Status & GEM_TX_STATUS_CPU_OWNS_BUF) == 0) {
    return TRUE;
  }
  return FALSE;
}

/**
 * @brief Set the station address.
 * 
 * @param Macb[in]    Pointer to MACB_PRIVATE_DATA instance
 * @param MacAddr[in] MAC address to set.
 * @return VOID 
 */
VOID
EFIAPI
MacbSetMacAddress(
  IN MACB_PRIVATE_DATA *Macb,
  IN EFI_MAC_ADDRESS *MacAddr
  )
{
  UINT32 Bottom;
  UINT16 Top;

  Bottom = ((UINT32)MacAddr->Addr[0] << 24)
            | ((UINT32)MacAddr->Addr[1] << 16)
            | ((UINT32)MacAddr->Addr[2] << 8)
            | ((UINT32)MacAddr->Addr[3]);

  Top = ((UINT16)MacAddr->Addr[4] << 8)
        | ((UINT16)MacAddr->Addr[5]);

  MacbMmioWrite (Macb, MACB_SA1B, Bottom);
  (void) MacbMmioRead (Macb, MACB_SA1B);  // read-after-write barrier
  MacbMmioWrite (Macb, MACB_SA1T, Top);

  Bottom = MacbMmioRead (Macb, MACB_SA1B);
  Top = MacbMmioRead (Macb, MACB_SA1T);

  DEBUG ((DEBUG_VERBOSE, 
    "MACB: MAC address set to %02x:%02x:%02x:%02x:%02x:%02x\n",
      (UINT8)(Bottom >> 24),
      (UINT8)(Bottom >> 16),
      (UINT8)(Bottom >> 8),
      (UINT8)(Bottom),
      (UINT8)(Top >> 8),
      (UINT8)(Top)));
}

BOOLEAN
MacbSupports64BitDma (
  IN MACB_PRIVATE_DATA *Macb
  )
{
  return GEM_BFEXT (DAW64, MacbMmioRead (Macb, GEM_DCFG6)) != 0;
}

/*STATIC
BOOLEAN
MacbHwIsGem (
  IN MACB_PRIVATE_DATA *Macb
  )
{
  UINT32 Id;

  Id = MacbMmioRead (Macb, MACB_MID);
  return MACB_BFEXT (IDNUM, Id) >= 0x2;
}*/

/*
VOID
MacbConfigureCaps (
  IN MACB_PRIVATE_DATA *Macb
  )
{
  UINT32 Dcfg;

  if (!MacbHwIsGem (Macb))
      return;

  DEBUG ((DEBUG_INFO, "MACB: Configuring capabilities.\n"));

  Macb->Caps |= MACB_CAPS_MACB_IS_GEM;

  Dcfg = MacbMmioRead (Macb, GEM_DCFG1);
  if (GEM_BFEXT (IRQCOR, Dcfg) == 0) {
    Macb->Caps |= MACB_CAPS_ISR_CLEAR_ON_WRITE;
  }
  if (GEM_BFEXT (NO_PCS, Dcfg) == 0) {
    Macb->Caps |= MACB_CAPS_PCS;
  }
  
  Dcfg = MacbMmioRead (Macb, GEM_DCFG12);
  if (GEM_BFEXT (HIGH_SPEED, Dcfg) == 0) {
    Macb->Caps |= MACB_CAPS_HIGH_SPEED;
  }

  Dcfg = MacbMmioRead (Macb, GEM_DCFG2);
  if ((Dcfg & (GEM_BIT(RX_PKT_BUFF) | GEM_BIT(TX_PKT_BUFF))) == 0) {
    Macb->Caps |= MACB_CAPS_FIFO_MODE;
  }

  if (GemHasPtp (Macb)) {
    Dcfg = MacbMmioRead (Macb, GEM_DCFG5);
    if (!GEM_BFEXT (TSU, Dcfg)) {
      DEBUG ((DEBUG_ERROR, "MACB: GEM doesn't support hardware PTP.\n"));
    } else {
      Macb->CanHwDmaPtp = TRUE;
    }
  }

  Macb->CanHwDma64 = MacbSupports64BitDma (Macb);
  DEBUG ((DEBUG_INFO, "MACB: 64-bit DMA %s\n",
      Macb->CanHwDma64 ? "enabled" : "disabled"));
}*/

STATIC
UINT32
MacbDbw (
  IN MACB_PRIVATE_DATA *Macb
  )
{
  // TODO: use macro and or auto detection of bus width
  return (0x2 << 21);

  // TODO: doesn't work yet
  switch (GEM_BFEXT (DBWDEF, MacbMmioRead (Macb, GEM_DCFG1))) {
  case 4:
    DEBUG ((DEBUG_INFO, "MACB: 128-bit DMA bus width\n"));
    return GEM_BF (DBW, GEM_DBW128);
  case 2:
    DEBUG ((DEBUG_INFO, "MACB: 64-bit DMA bus width\n"));
    return GEM_BF (DBW, GEM_DBW64);
  default: // default to 1
    DEBUG ((DEBUG_INFO, "MACB: 32-bit DMA bus width\n"));
    return GEM_BF (DBW, GEM_DBW32);
  }
}

STATIC
VOID
MacbGemConfigureDma (
  IN MACB_PRIVATE_DATA *Macb
  )
{
  UINT32 DmaCfg;
  UINT32 RxBufferSize;
  
  RxBufferSize = RP1_NET_BUFFER_SIZE / 64;
  ASSERT (RxBufferSize <= 0xFF); // Fits into RXBS

  DmaCfg = 0;
  DmaCfg |= GEM_BF (RXBS, RxBufferSize);

  DmaCfg |= GEM_BIT(ADDR64);

  DmaCfg |= GEM_BIT (TXPBMS);
  DmaCfg |= GEM_BF (RXBMS, -1L); // or RXBMS 2

  // DMA burst size
  DmaCfg = GEM_BFINS (FBLDO, MACB_DMA_BURST_LEN, DmaCfg);

  MacbMmioWrite (Macb, GEM_DMACFG, DmaCfg);
  DEBUG ((DEBUG_INFO, "MACB: Configured DMA = 0x%08x\n", DmaCfg));
  (void) MacbMmioRead (Macb, GEM_DMACFG); // Serialise the write
}

STATIC
VOID
MacbGemInitAxi (
  IN MACB_PRIVATE_DATA *Macb
  )
{
  UINT32 Amp;
  
  Amp = 0;
  // AW2B-Fill is enabled on RP1 | TODO: maybe read from fdt
  //Amp = GEM_BFINS (AW2B_FILL, TRUE, Amp);
  Amp = GEM_BFINS (AW2W_MAX_PIPE, 8, Amp);
  Amp = GEM_BFINS (AR2R_MAX_PIPE, 8, Amp);

  MacbMmioWrite (Macb, GEM_AMP, Amp); // AXI Max Pipeline Offset
  DEBUG ((DEBUG_VERBOSE, "MACB: Configured AMP = 0x%08x\n", Amp));
  (void) MacbMmioRead (Macb, GEM_AMP); // Serialise the write
}

VOID
EFIAPI
MacbInit (
  IN MACB_PRIVATE_DATA *Macb
  )
{
  UINT32 Config;

  // Enable MP but disable RX/TX
  MacbMmioWrite (Macb, MACB_NCR, MACB_BIT(MPE));
  MacbMmioWrite (Macb, MACB_NCR,
    MACB_BIT(MPE) |   // Enable management port
    MACB_BIT(CLRSTAT) // Clear all stats registers
  );
  (void)MacbMmioRead (Macb, MACB_NCR); // Serialise the write

  // TODO: what does this do?
  Config = GEM_BIT (RGMII) | MACB_BIT (CLKEN);
  MacbMmioWrite (Macb, GEM_USRIO, Config);

  Config = GEM_BF (CLK, GEM_CLK_DIV96);
  Config |= MACB_BF (RBOF, 0); // ensure proper alignment of incoming packets
  Config |= MACB_BIT (BIG); // receive 1536 bytes frames (BIG)
  Config |= MacbDbw (Macb); // set bus width
  Config &= ~MACB_BIT (PAE); // disable pause
  Config |= MACB_BIT (DRFCS); // discard FCS
  MacbMmioWrite (Macb, MACB_NCFGR, Config);
  (void) MacbMmioRead (Macb, MACB_NCFGR); // Serialise the write
 
  MacbGemConfigureDma (Macb);
  GemSetDma (Macb);

  // No TSU insertion
  MacbMmioWrite (Macb, ETH_TX_BD_CONTROL_OFFSET, 0);
  MacbMmioWrite (Macb, ETH_RX_BD_CONTROL_OFFSET, 0);

  MacbGemInitAxi (Macb);
}

#define META_DATA_BYTES 2048
#define DESC_PAD_SIZE (sizeof (GEM_DMA64_TX_DESC) * 16)

/**
 * @brief Allocate DMA Descs and the RX Buffer.
 * 
 * @param Macb[in]    Pointer to MACB_PRIVATE_DATA instance
 * @return EFI_STATUS Status
 */
EFI_STATUS
EFIAPI
MacbDmaAlloc(
  IN MACB_PRIVATE_DATA *Macb
  )
{
  EFI_STATUS             Status;
  UINTN                  DescRingSize, RxBufferSize, TxBufferSize, MainBufferSize;
  UINT8                  Index;
  GEM_DMA64_RX_DESC     *RxDesc;
  GEM_DMA64_TX_DESC     *TxDesc;
  EFI_PHYSICAL_ADDRESS   BufDma;

  ASSERT (sizeof (GEM_DMA64_RX_DESC) == 16);
  ASSERT (sizeof (GEM_DMA64_TX_DESC) == 16);

  Macb->RxRingSize = sizeof (GEM_DMA64_RX_DESC) * RP1_NET_RING_DESC_COUNT;
  Macb->TxRingSize = sizeof (GEM_DMA64_TX_DESC) * RP1_NET_RING_DESC_COUNT;
  // TODO: maybe meta data is not needed
  DescRingSize = 
    META_DATA_BYTES +
    Macb->RxRingSize +
    DESC_PAD_SIZE +
    Macb->TxRingSize +
    DESC_PAD_SIZE;
  RxBufferSize = (RP1_NET_BUFFER_SIZE * RP1_NET_RING_DESC_COUNT);
  TxBufferSize = (RP1_NET_BUFFER_SIZE * RP1_NET_RING_DESC_COUNT);
  MainBufferSize = DescRingSize +
    RxBufferSize +   // RX buffers
    TxBufferSize;    // TX buffers

  Status = DmaAllocateAlignedBuffer (
    EfiBootServicesData,
    EFI_SIZE_TO_PAGES (MainBufferSize),
    64, // TODO: or other alignment
    &Macb->DescRingBase
  );

  // TODO: necessary? and is this size enough because size to page and alignment
  ZeroMem (Macb->DescRingBase, MainBufferSize);
  Macb->RxRing = (GEM_DMA64_RX_DESC *)((UINT8 *)Macb->DescRingBase + META_DATA_BYTES);
  Macb->TxRing = (GEM_DMA64_TX_DESC *)(
    (UINT8 *)Macb->DescRingBase +
    META_DATA_BYTES +
    Macb->RxRingSize +
    DESC_PAD_SIZE
  );
  Macb->RxBuffer = (VOID *)((UINT8 *)Macb->TxRing + Macb->TxRingSize + DESC_PAD_SIZE);
  Macb->TxBuffer = (VOID *)((UINT8 *)Macb->RxBuffer + RxBufferSize);

  // Map both descriptor rings
  Status = DmaMap (
    MapOperationBusMasterCommonBuffer, //MapOperationBusMasterCommonBuffer
    Macb->DescRingBase,
    &DescRingSize,
    &Macb->DescRingDma,
    &Macb->DescRingMapping
  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "MACB: Failed to map RP1 ethernet DMA: %r\n", Status));
    DmaFreeBuffer (EFI_SIZE_TO_PAGES (MainBufferSize), Macb->DescRingBase);
    return Status;
  }
  Macb->RxRingDma = Macb->DescRingDma + META_DATA_BYTES;
  Macb->TxRingDma = Macb->RxRingDma + Macb->RxRingSize + DESC_PAD_SIZE;

  // Check we're still word-aligned for buffers to come
  //ASSERT (!(((UINTN)Macb->DescRingBase + DescRingSize) & 0x3));

  // Map rxbuffer
  Status = DmaMap (
    MapOperationBusMasterWrite,
    Macb->DescRingBase + DescRingSize,
    &RxBufferSize,
    &Macb->RxBufferDma,
    &Macb->RxBufferMapping
  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "MACB: Failed to map RX buffer: %r\n", Status));
    DmaUnmap (Macb->DescRingMapping);
    DmaFreeBuffer (EFI_SIZE_TO_PAGES (DescRingSize), Macb->DescRingBase);
    return Status;
  }
  ASSERT (!(Macb->RxBufferDma & 0x3));
  // TODO: assert out mapped buffer size

  // Setting up rx descriptor ring
  for (Index = 0; Index < RP1_NET_RING_DESC_COUNT; Index++) {
    RxDesc = &Macb->RxRing[Index];
    BufDma = Macb->RxBufferDma + (Index * RP1_NET_BUFFER_SIZE);
  
    RxDesc->AddrLo = (UINT32)BufDma;
    RxDesc->AddrHi = (UINT32)(BufDma >> 32);
    RxDesc->Status = 0;
    RxDesc->Pad = 0;

    if (Index == RP1_NET_RING_DESC_COUNT - 1) {
      RxDesc->AddrLo |= GEM_RX_ADDR_LO_RING_WRAP;
    }
  }

  // Map txbuffer
  Status = DmaMap (
    MapOperationBusMasterRead,
    Macb->DescRingBase + DescRingSize + RxBufferSize,
    &TxBufferSize,
    &Macb->TxBufferDma,
    &Macb->TxBufferMapping
  );
  ASSERT (!(Macb->TxBufferDma & 0x3));

  for (Index = 0; Index < RP1_NET_RING_DESC_COUNT; Index++) {
    TxDesc = &Macb->TxRing[Index];
    BufDma = Macb->TxBufferDma + (Index * RP1_NET_BUFFER_SIZE);
    
    // set later when transmitting
    TxDesc->AddrLo = (UINT32)BufDma;
    TxDesc->AddrHi = (UINT32)(BufDma >> 32);

    // CPU owns buffer and only one buffer per frame
    TxDesc->Status = GEM_TX_STATUS_CPU_OWNS_BUF | GEM_TX_STATUS_LAST_BUF;
    if (Index == RP1_NET_RING_DESC_COUNT - 1) {
      TxDesc->Status |= GEM_TX_STATUS_RING_WRAP; // Bit 1 = 1 for wrap
    }
    TxDesc->Pad = 0;
  }

  DEBUG ((DEBUG_INFO, "MACB: DMA buffer allocated\n"));
  return EFI_SUCCESS;
}

/**
 * @brief Free DMA buffers for RX and TX rings and buffers.
 * 
 * @param Macb[in]    Pointer to MACB_PRIVATE_DATA instance
 * @return VOID 
 */
VOID
MacbDmaFree (
  IN MACB_PRIVATE_DATA *Macb
  )
{
  UINT32 DescRingSize, RxBufferSize, TxBufferSize, MainBufferSize;
  
  // TODO: already callculated in alloc
  // maybe define
  DescRingSize = 
    META_DATA_BYTES +
    Macb->RxRingSize +
    DESC_PAD_SIZE +
    Macb->TxRingSize +
    DESC_PAD_SIZE;
  RxBufferSize = (RP1_NET_BUFFER_SIZE * RP1_NET_RING_DESC_COUNT);
  TxBufferSize = (RP1_NET_BUFFER_SIZE * RP1_NET_RING_DESC_COUNT);
  MainBufferSize = DescRingSize +
    RxBufferSize +   // RX buffers
    TxBufferSize;    // TX buffers

  // Unmap TX Buffer
  DmaUnmap (Macb->TxBufferMapping);
  // Unmap RX Buffer
  DmaUnmap (Macb->RxBufferMapping);
  // Unmap Desc Ring
  DmaUnmap (Macb->DescRingMapping);
  // Free MainBuffer
  DmaFreeBuffer (EFI_SIZE_TO_PAGES (MainBufferSize), Macb->DescRingBase);
  Macb->DescRingBase = NULL;
  Macb->DescRingDma = 0;
  Macb->DescRingMapping = NULL;

  Macb->RxRing = NULL;
  Macb->TxRing = NULL;
  Macb->RxRingDma = 0;
  Macb->TxRingDma = 0;
  Macb->RxRingSize = 0;
  Macb->TxRingSize = 0;

  Macb->RxBuffer = NULL;
  Macb->TxBuffer = NULL;
  Macb->RxBufferDma = 0;
  Macb->TxBufferDma = 0;
}

/**
  Enable TX/RX.

  @param  Macb[in]  Pointer to MACB_PRIVATE_DATA

**/
VOID
MacbEnableTxRx (
  IN MACB_PRIVATE_DATA   *Macb
  )
{
  // Enable TX and RX in GEM_NCR
  MacbMmioOr (Macb, MACB_NCR, MACB_BIT(TE) | MACB_BIT(RE));
}

VOID
MacbDisableTxRx (
  IN MACB_PRIVATE_DATA   *Macb
  )
{
  DEBUG((DEBUG_INFO, "MACB: Disabling TX/RX\n"));
  // Disable Tx and Rx
  MacbMmioAnd (Macb, MACB_NCR, ~(MACB_BIT(RE) | MACB_BIT(TE)));
  (void) MacbMmioRead (Macb, MACB_NCR); // Serialise the write
}

VOID
MacbHaltTxRx (
  IN MACB_PRIVATE_DATA *Macb
  )
{
  UINT32 Reg;

  // Halt TX
  Reg = MacbMmioRead(Macb, MACB_NCR);
  Reg |= MACB_BIT(THALT);
  MacbMmioWrite(Macb, MACB_NCR, Reg);
  gBS->Stall(10);
  
  // TODO: maybe management port should be disabled instead of re and te
  // Disable RX and TX
  Reg &= ~(MACB_BIT(RE) | MACB_BIT(TE));
  MacbMmioWrite(Macb, MACB_NCR, Reg);

  // Allow hardware time to flush any in-flight TX
  gBS->Stall(10);
}

VOID
MacbResumeTxRx (
  IN MACB_PRIVATE_DATA *Macb
  )
{
  // Restore TX/RX (don't touch MPE unless MDIO needed)
  MacbMmioOr(Macb, MACB_NCR, MACB_BIT(RE) | MACB_BIT(TE));
  (void) MacbMmioRead(Macb, MACB_NCR); // Serialise the write
}

/**
  Enable the MAC filter for the Ethernet broadcast address

  @param  Macb[in]   Pointer to MACB_PRIVATE_DATA
  @param  Enable[in]  Promiscuous mode state

**/
VOID
MacbEnableBroadcastFilter (
  IN MACB_PRIVATE_DATA  *Macb,
  IN BOOLEAN            Enable
  )
{
  if (Enable) {
    DEBUG ((DEBUG_VERBOSE, "MACB: Enabling broadcast filter\n"));
    MacbMmioAnd(Macb, MACB_NCFGR, ~MACB_BIT(NBC));
  } else {
    DEBUG ((DEBUG_VERBOSE, "MACB: Disabling broadcast filter\n"));
    MacbMmioOr(Macb, MACB_NCFGR, MACB_BIT(NBC));
  }
  (void)MacbMmioRead (Macb, MACB_NCFGR); // Serialise the write
}

/**
  Change promiscuous mode state.

  @param  Macb[in]   Pointer to MACB_PRIVATE_DATA
  @param  Enable[in]  Promiscuous mode state

**/
VOID
MacbSetPromisc (
  IN MACB_PRIVATE_DATA  *Macb,
  IN BOOLEAN            Enable
  )
{
  if (Enable) {
    DEBUG ((DEBUG_VERBOSE, "MACB: Enabling promiscuous mode\n"));
    MacbMmioOr (Macb, MACB_NCFGR, MACB_BIT(CAF));
  } else {
    DEBUG ((DEBUG_VERBOSE, "MACB: Disabling promiscuous mode\n"));
    MacbMmioAnd (Macb, MACB_NCFGR, ~MACB_BIT(CAF));
  }
  (void)MacbMmioRead (Macb, MACB_NCFGR); // Serialise the write
}

VOID
MacbSetMcast (
  IN MACB_PRIVATE_DATA  *Macb,
  IN BOOLEAN            ResetFilter,
  IN BOOLEAN            Enable,
  IN UINTN              MCastFilterCnt OPTIONAL,
  IN EFI_MAC_ADDRESS    *MCastFilter OPTIONAL
  )
{
  if (ResetFilter) {
    DEBUG ((DEBUG_VERBOSE, "MACB: Resetting multicast filter\n"));
    MacbMmioWrite (Macb, MACB_HRB, 0);
    MacbMmioWrite (Macb, MACB_HRT, 0);
    MacbMmioAnd (Macb, MACB_NCFGR, ~MACB_BIT (NCFGR_MTI));
  }
  MemoryFence ();

  if (Enable) {
    DEBUG ((DEBUG_WARN, 
      "MACB: Setting multicast filter isn't supported at the moment\n"));
  }
}

VOID
MacbTriggerTx (
  IN MACB_PRIVATE_DATA *Macb
  )
{
  MacbMmioOr (Macb, MACB_NCR, MACB_BIT (TSTART));
  gBS->Stall (10);  // TODO: maybe remove
}

EFI_STATUS
EFIAPI
MacbTxRingEnsureAvailable (
  IN MACB_PRIVATE_DATA *Macb
  )
{
  UINT32 IntStatus, TxStatus, TxQPtr;
  volatile GEM_DMA64_TX_DESC *CurTx;

  MemoryFence ();

  // Read & clear interrupt status (read-to-clear)
  IntStatus = MacbMmioRead (Macb, MACB_ISR);

  // Read transmit status (errors, txgo, etc.)
  TxStatus = MacbMmioRead (Macb, MACB_TSR);

  // Handle TX errors
  if (TxStatus & MACB_TX_STATUS_ERR_BITS) {
    DEBUG((DEBUG_WARN, "MACB TX error 0x%08x\n", TxStatus));
    MacbMmioWrite (Macb, MACB_TSR, MACB_TX_STATUS_ERR_BITS);  // Clear error bits
  }

  // Update TxCleanIndex (Consumer index)
  if (!(TxStatus & MACB_BIT (TGO))) {
    // TX Go bit is cleared: all packets sent
    Macb->TxCleanIndex = Macb->TxIndex;
    MacbMmioWrite (Macb, MACB_TSR, MACB_BIT (TXUBR)); // Clear Used Bit Read
  } else if (IntStatus & MACB_BIT (TXUBR)) {
    // TX Used Bit Read interrupt occurred
    Macb->TxCleanIndex = Macb->TxIndex;
    MacbMmioWrite (Macb, MACB_TSR, MACB_BIT (TXUBR)); // Clear Used Bit Read
  } else {
    // Update consumer index based on TX queue pointer
    TxQPtr = MacbMmioRead (Macb, MACB_TBQP);
    TxQPtr &= ~(sizeof(GEM_DMA64_TX_DESC) - 1); // = ~0xF

    if ((TxQPtr < Macb->TxRingDma) ||
        (TxQPtr >= Macb->TxRingDma + sizeof(GEM_DMA64_TX_DESC) * Macb->TxRingSize)) {
      DEBUG((DEBUG_ERROR, "MACB: Invalid TxQPtr 0x%08x\n", TxQPtr));
      return EFI_DEVICE_ERROR;
    }

    UINTN DescOffset = TxQPtr - Macb->TxRingDma;
    UINTN CurTxIdx = DescOffset / sizeof(GEM_DMA64_TX_DESC);

    CurTx = &Macb->TxRing[CurTxIdx];

    // Update consumer index
    // TODO: safer version because: will cause an infinite loop if CurTxIdx 
    // is ever inconsistent (e.g. memory corruption or DMA error) => bound the loop.
    while ((Macb->TxCleanIndex % Macb->TxRingSize) != CurTxIdx) {
        Macb->TxCleanIndex++;
    }

    DEBUG ((DEBUG_INFO, "MACB: Updated TxCleanIndex=%u TxIndex=%u TxQPtr=0x%lx CurTxIdx=%u\n",
           Macb->TxCleanIndex, Macb->TxIndex, (UINT64)TxQPtr, CurTxIdx));
  }

  // Handle interrupt errors
  if (IntStatus & MACB_INT_IER_ERR_BITS) {
    DEBUG((DEBUG_WARN, "MACB: Error Interrupts 0x%08x\n", IntStatus & MACB_INT_IER_ERR_BITS));
  }

  // RX ring exhausted?
  if (IntStatus & MACB_BIT (RXUBR)) {
    DEBUG((DEBUG_WARN, "MACB: RX ring exhausted\n"));
  }

  // Check if there is TX ring space
  if (!RING_BUF_SPACE (Macb->TxCleanIndex, Macb->TxIndex, Macb->TxRingSize)) {
    DEBUG((DEBUG_WARN, "MACB: TX ring full (no space)\n"));
    return EFI_NOT_READY;
  }

  return EFI_SUCCESS;
}

VOID
MacbFullStop (
  IN MACB_PRIVATE_DATA *Macb
  )
{
  MacbHaltTxRx (Macb);

  // Enable management port
  MacbMmioWrite (Macb, MACB_NCR, MACB_BIT (MPE));

  // Clear mac address
  MacbMmioWrite (Macb, MACB_SA1T, 0);
  MacbMmioWrite (Macb, MACB_SA1B, 0);
  Macb->TxIndex = 0;
  Macb->TxCleanIndex = 0;
  Macb->RxIndex = 0;
}

EFI_STATUS
EFIAPI
MacbPauseUntilIdle (
  IN MACB_PRIVATE_DATA *Macb
  )
{
  UINT8 i;
  i = 100;

  while ((MacbMmioRead (Macb, MACB_TSR) & MACB_BIT(TGO)) != 0 && i > 0) {
    gBS->Stall (MACB_TX_POLL_IVAL);
    i--;
  }

  return EFI_SUCCESS;
}
