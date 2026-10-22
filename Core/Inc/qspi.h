/**
  ******************************************************************************
  * @file    qspi.h
  * @author  Shaman Karunaratne
  * @date    Mar 11, 2026
  * @brief   Bootloader-side QSPI flash operations (MT25TL01G, dual-flash mode).
  ******************************************************************************
  * @details
  * Implemented in qspi.c. Addresses are offsets into the 128 MB QSPI flash
  * (not CPU addresses at 0x90000000). Erase and write operations abort
  * memory-mapped mode first; call QSPI_EnableMemoryMappedMode() again
  * before reading through 0x90000000.
  *
  * All functions return MEMORY_OK on success or MEMORY_ERROR on failure.
  ******************************************************************************
  */

#ifndef INC_QSPI_H_
#define INC_QSPI_H_

/** @addtogroup QSPI
  * @{
  */

/**
  * @brief  Erase one 64 KB sector.
  * @param  SectorAddress  Flash offset inside the sector to erase.
  */
uint32_t QSPI_EraseSector(uint32_t SectorAddress);

/**
  * @brief  Erase one 4 KB subsector.
  * @param  SubSectorAddress  Flash offset inside the subsector to erase.
  */
uint32_t QSPI_EraseSubSector(uint32_t SubSectorAddress);

/**
  * @brief  Put the QSPI flash in memory-mapped (XiP / read) mode at 0x90000000.
  */
uint32_t QSPI_EnableMemoryMappedMode(void);

/**
  * @brief  Leave memory-mapped mode (aborts the ongoing QSPI transfer).
  */
uint32_t QSPI_DisableMemoryMappedMode(void);

/**
  * @brief  Program up to one 256-byte page.
  * @param  pData      Source data.
  * @param  WriteAddr  Flash offset to write to.
  * @param  Size       Bytes to write (max 256, one page).
  */
uint32_t QSPI_WritePage(uint8_t* pData, uint32_t WriteAddr, uint32_t Size);

/**
  * @}
  */

#endif /* INC_QSPI_H_ */