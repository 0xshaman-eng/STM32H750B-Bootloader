/**
  ******************************************************************************
  * @file    firmware_protocol.c
  * @author  Shaman Karunaratne
  * @brief   Firmware update protocol helpers: CRC-32, ACK/NACK and debug
  *          messages. All output goes through the transport dispatcher.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "firmware_protocol.h"
#include "usart.h"
#include "crc.h"
#include "transport.h"

/** @addtogroup FW_PROTOCOL
  * @{
  */

/* Exported functions --------------------------------------------------------*/

/**
  * @brief  Calculate CRC-32 using the STM32 hardware CRC peripheral.
  * @param  data    Pointer to data buffer.
  * @param  length  Length of data in BYTES (not words).
  * @retval CRC-32 value.
  *
  * @details
  * Hardware CRC configuration (set in crc.c):
  *  - Polynomial:    0x04C11DB7 (standard CRC-32)
  *  - Initial value: 0xFFFFFFFF
  *  - Input format:  bytes (CRC_INPUTDATA_FORMAT_BYTES)
  *  - No input/output inversion
  *
  * Because the input format is bytes, HAL_CRC_Accumulate() takes @p length
  * as a byte count and internally (CRC_Handle_8()) packs 4 bytes per word and
  * handles any 1-3 trailing bytes, so any length works (37, 261, 4, ...).
  *
  * @note   The hardware result is returned as-is with no final XOR. This
  *         matches the host-side calculation (reference value 0xE91036AB).
  */
uint32_t CRC32_Calculate(const uint8_t *data, uint32_t length)
{
    uint32_t crc;

    /* Reset CRC calculation unit to initial value (0xFFFFFFFF) */
    __HAL_CRC_DR_RESET(&hcrc);

    /* 'length' is in bytes; HAL handles byte packing based on InputDataFormat */
    crc = HAL_CRC_Accumulate(&hcrc, (uint32_t*)data, length);

    /* Return CRC value directly - no XOR needed */
    return crc;
}

/**
  * @brief  Send an ACK packet over the active transport.
  * @param  acked_opcode  Opcode being acknowledged.
  * @param  seq_number    Sequence number (0 for header/footer).
  * @retval None
  */
void Protocol_SendACK(uint8_t acked_opcode, uint16_t seq_number)
{
    ACKPacket_t ack;

    ack.opcode = OPCODE_ACK;
    ack.acked_opcode = acked_opcode;
    ack.seq_number = seq_number;

    Transport_Transmit((uint8_t*)&ack, sizeof(ACKPacket_t), HAL_MAX_DELAY);
}

/**
  * @brief  Send a NACK packet over the active transport.
  * @param  nacked_opcode  Opcode being rejected.
  * @param  seq_number     Sequence number (0 for header/footer).
  * @param  error_code     One of the @ref FW_PROTOCOL_Errors codes.
  * @retval None
  */
void Protocol_SendNACK(uint8_t nacked_opcode, uint16_t seq_number, uint8_t error_code)
{
    NACKPacket_t nack;

    nack.opcode = OPCODE_NACK;
    nack.nacked_opcode = nacked_opcode;
    nack.seq_number = seq_number;
    nack.error_code = error_code;

    Transport_Transmit((uint8_t*)&nack, sizeof(NACKPacket_t), HAL_MAX_DELAY);
}

/**
  * @brief  Send a debug message over the active transport.
  * @details Sends ::OPCODE_DEBUG followed by the string including its null
  *          terminator. Length is capped at 255 characters; for longer
  *          strings 256 bytes are sent and no terminator is included.
  * @param  message  Null-terminated debug string.
  * @retval None
  */
void Protocol_SendDebug(const char *message)
{
    uint8_t opcode = OPCODE_DEBUG;

    /* Send DEBUG opcode */
    Transport_Transmit(&opcode, 1, HAL_MAX_DELAY);

    /* Measure string length (capped at 255) */
    uint16_t length = 0;
    while(message[length] != '\0' && length < 255)
    {
        length++;
    }

    /* Send the debug string INCLUDING null terminator */
    if(length > 0)
    {
        Transport_Transmit((uint8_t*)message, length + 1, HAL_MAX_DELAY);  // +1 for null terminator
    }
}

/**
  * @}
  */
