/**
  ******************************************************************************
  * @file    firmware_protocol.h
  * @author  Shaman Karunaratne
  * @brief   Firmware update protocol: opcodes, error codes, packet layouts,
  *          state machine types and helper prototypes.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  ******************************************************************************
  * @details
  * @par Session flow (host -> bootloader)
  *  -# STARTUP byte (::OPCODE_STARTUP) selects the transport.
  *  -# One HeaderPacket_t      -> ACK / NACK.
  *  -# N   DataPacket_t        -> ACK / NACK per packet.
  *  -# One FooterPacket_t      -> ACK / NACK after SHA-256 verification.
  *
  * All multi-byte fields are little-endian. Every packet ends in a CRC-32
  * computed with CRC32_Calculate() over all preceding bytes.
  ******************************************************************************
  */

#ifndef __FIRMWARE_PROTOCOL_H
#define __FIRMWARE_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include <stdint.h>

/** @defgroup FW_PROTOCOL Firmware Update Protocol
  * @{
  */

/** @defgroup FW_PROTOCOL_Opcodes Protocol Opcodes
  * @{
  */
#define OPCODE_STARTUP      0xAA    /**< Startup / sync byte sent by host. */
#define OPCODE_HEADER       0x01    /**< Header packet. */
#define OPCODE_DATA         0x02    /**< Data packet. */
#define OPCODE_FOOTER       0x03    /**< Footer packet. */
#define OPCODE_ACK          0x0A    /**< Acknowledgment. */
#define OPCODE_NACK         0x15    /**< Negative acknowledgment. */
#define OPCODE_DEBUG        0x0D    /**< Debug message (bootloader to host only). */
/**
  * @}
  */

/** @defgroup FW_PROTOCOL_Errors NACK Error Codes
  * @{
  */
/* Header packet errors (0x01 - 0x0F) */
#define ERROR_HEADER_UART_RX        0x01    /**< Receive error/timeout for header. */
#define ERROR_HEADER_CRC            0x02    /**< Header CRC validation failed. */
#define ERROR_HEADER_ERASE          0x03    /**< Flash erase error during header processing. */

/* Data packet errors (0x10 - 0x1F) */
#define ERROR_DATA_NOT_READY        0x10    /**< Not in correct state to receive data. */
#define ERROR_DATA_UART_RX          0x11    /**< Receive error/timeout for data. */
#define ERROR_DATA_SEQ_NUM          0x12    /**< Sequence number mismatch (also used to request resume). */
#define ERROR_DATA_CRC              0x13    /**< Data packet CRC validation failed. */
#define ERROR_DATA_LENGTH           0x14    /**< Invalid data length. */
#define ERROR_DATA_FLASH_WRITE      0x15    /**< Flash write error. */

/* Footer packet errors (0x20 - 0x2F) */
#define ERROR_FOOTER_NOT_READY      0x20    /**< Not in correct state to receive footer. */
#define ERROR_FOOTER_UART_RX        0x21    /**< Receive error/timeout for footer. */
#define ERROR_FOOTER_CRC            0x22    /**< Footer CRC validation failed. */
#define ERROR_FOOTER_PACKET_COUNT   0x23    /**< Total packet count mismatch. */
#define ERROR_FOOTER_MEM_MAPPED     0x24    /**< Failed to enable memory-mapped mode. */
#define ERROR_FOOTER_SHA256         0x25    /**< SHA-256 verification failed. */
#define ERROR_FOOTER_FLASH_WRITE    0x26    /**< Flash write error during Bank 2 -> Bank 1 copy. */
#define ERROR_FOOTER_ERASE          0x27    /**< Flash erase error during Bank 1 erase. */
/**
  * @}
  */

/** @defgroup FW_PROTOCOL_Sizes Packet Sizes
  * @{
  */
#define HEADER_PACKET_SIZE  41      /**< 1 + 4 + 32 + 4 bytes. */
#define DATA_PACKET_SIZE    265     /**< 1 + 2 + 2 + 256 + 4 bytes. */
#define FOOTER_PACKET_SIZE  8       /**< 1 + 2 + 1 + 4 bytes. */
#define ACK_PACKET_SIZE     4       /**< 1 + 1 + 2 bytes. */
#define NACK_PACKET_SIZE    5       /**< 1 + 1 + 2 + 1 bytes. */

#define DATA_CHUNK_SIZE     256     /**< Payload bytes per data packet. */
/**
  * @}
  */

/* Exported types ------------------------------------------------------------*/

/**
  * @brief  Top-level bootloader state (main loop in main.c).
  */
typedef enum {
    MASTER_STATE_READY = 0,     /**< Listening for STARTUP on UART and USB. */
    MASTER_STATE_BOOT,          /**< Jump to application in Bank 1. */
    MASTER_STATE_BOOT_UPDATE,   /**< Run the firmware update protocol. */
    MASTER_STATE_ERROR          /**< Unrecoverable error; blink LED. */
} MasterState_t;

/**
  * @brief  Firmware update state machine (Firmware_Update_Protocol()).
  */
typedef enum {
    UPDATE_STATE_IDLE = 0,          /**< Waiting for a header opcode. */
    UPDATE_STATE_HEADER_RECEIVED,   /**< Header opcode seen; receive rest, erase Bank 2. */
    UPDATE_STATE_READY,             /**< Accepting data packets / footer. */
    UPDATE_STATE_FOOTER_RECEIVED,   /**< Footer opcode seen; verify count and SHA-256. */
    UPDATE_STATE_UPDATE_DONE,       /**< Bank 2 verified; erase Bank 1. */
    UPDATE_STATE_BANK1_READY,       /**< Bank 1 erased; copy Bank 2 -> Bank 1. */
    UPDATE_STATE_FIRMWARE_UPDATED,  /**< Copy done; mark Bank 1 valid and reset. */
    UPDATE_STATE_ERROR              /**< Unrecoverable error. */
} UpdateState_t;

/**
  * @brief  Runtime context of an update session. Persisted inside
  *         BootloaderNVConfig_t so an interrupted update can resume.
  */
typedef struct {
    UpdateState_t state;                          /**< Current update state. */
    uint32_t firmware_size;                       /**< Image size from header, bytes. */
    uint32_t total_bytes_written;                 /**< Bytes written to Bank 2 so far. */
    uint16_t expected_seq_number;                 /**< Next data packet sequence number. */
    uint16_t total_packets_received;              /**< Data packets accepted so far. */
    uint32_t flash_write_address;                 /**< Next QSPI write address. */
    uint8_t received_sha256[32];                  /**< SHA-256 from header. */
    uint8_t rx_packet_buffer[DATA_PACKET_SIZE];   /**< Scratch packet buffer. */
} BootloaderContext_t;

#include "sha256.h"

/** Marks a valid BootloaderNVConfig_t record in QSPI flash. */
#define NV_CONFIG_MAGIC_WORD 0x5AA5C33C

/**
  * @brief  Non-volatile bootloader configuration, stored ping-pong in the last
  *         two QSPI subsectors.
  */
typedef struct __attribute__((packed)) {
    uint32_t magic_word;          /**< ::NV_CONFIG_MAGIC_WORD when valid. */
    uint32_t commit_counter;      /**< Incremented on every write; newest copy wins the ping-pong tie-break. */

    /* Bank status */
    uint32_t bank1_status;        /**< 1 = valid, 0 = invalid. */
    uint32_t bank2_status;        /**< 1 = valid, 0 = invalid. */

    /* Resume capability */
    uint32_t update_in_progress;  /**< 1 while an update is partially written. */
    BootloaderContext_t boot_ctx; /**< Full context (seq num, bytes written, etc.). */

    /* Integrity */
    uint32_t crc32;               /**< CRC-32 of all preceding fields. MUST BE LAST. */
} BootloaderNVConfig_t;

/* Packet structures ---------------------------------------------------------*/

/**
  * @brief  Header packet: [Opcode(1) | FileSize(4) | SHA256(32) | CRC32(4)]
  */
typedef struct __attribute__((packed)) {
    uint8_t opcode;             /**< ::OPCODE_HEADER. */
    uint32_t file_size;         /**< Total firmware size in bytes. */
    uint8_t sha256_hash[32];    /**< SHA-256 hash of the firmware image. */
    uint32_t crc32;             /**< CRC-32 of opcode + file_size + sha256_hash. */
} HeaderPacket_t;

/**
  * @brief  Data packet: [Opcode(1) | SeqNum(2) | Length(2) | Data(256) | CRC32(4)]
  */
typedef struct __attribute__((packed)) {
    uint8_t opcode;             /**< ::OPCODE_DATA. */
    uint16_t seq_number;        /**< Sequence number, starting at 0. */
    uint16_t length;            /**< Valid payload bytes (1..256). */
    uint8_t data[256];          /**< Payload. */
    uint32_t crc32;             /**< CRC-32 of opcode + seq_number + length + data. */
} DataPacket_t;

/**
  * @brief  Footer packet: [Opcode(1) | TotalPackets(2) | Reserved(1) | CRC32(4)]
  */
typedef struct __attribute__((packed)) {
    uint8_t opcode;             /**< ::OPCODE_FOOTER. */
    uint16_t total_packets;     /**< Total number of data packets sent. */
    uint8_t reserved;           /**< Reserved, 0x00. */
    uint32_t crc32;             /**< CRC-32 of opcode + total_packets + reserved. */
} FooterPacket_t;

/**
  * @brief  ACK packet: [Opcode(1) | AckedOpcode(1) | SeqNum(2)]
  */
typedef struct __attribute__((packed)) {
    uint8_t opcode;             /**< ::OPCODE_ACK. */
    uint8_t acked_opcode;       /**< Opcode being acknowledged. */
    uint16_t seq_number;        /**< Sequence number (0 for header/footer). */
} ACKPacket_t;

/**
  * @brief  NACK packet: [Opcode(1) | NackedOpcode(1) | SeqNum(2) | ErrorCode(1)]
  */
typedef struct __attribute__((packed)) {
    uint8_t opcode;             /**< ::OPCODE_NACK. */
    uint8_t nacked_opcode;      /**< Opcode being rejected. */
    uint16_t seq_number;        /**< Sequence number (0 for header/footer). */
    uint8_t error_code;         /**< One of the @ref FW_PROTOCOL_Errors codes. */
} NACKPacket_t;

/* Exported functions --------------------------------------------------------*/

/**
  * @brief  Calculate CRC-32 using the STM32 hardware CRC peripheral.
  * @param  data    Pointer to data buffer.
  * @param  length  Length of data in bytes.
  * @retval CRC-32 value.
  */
uint32_t CRC32_Calculate(const uint8_t *data, uint32_t length);

/**
  * @brief  Send an ACK packet over the active transport.
  * @param  acked_opcode  Opcode being acknowledged.
  * @param  seq_number    Sequence number (0 for header/footer).
  * @retval None
  */
void Protocol_SendACK(uint8_t acked_opcode, uint16_t seq_number);

/**
  * @brief  Send a NACK packet over the active transport.
  * @param  nacked_opcode  Opcode being rejected.
  * @param  seq_number     Sequence number (0 for header/footer).
  * @param  error_code     One of the @ref FW_PROTOCOL_Errors codes.
  * @retval None
  */
void Protocol_SendNACK(uint8_t nacked_opcode, uint16_t seq_number, uint8_t error_code);

/**
  * @brief  Send a debug message over the active transport.
  * @param  message  Null-terminated string (max 255 characters sent).
  * @retval None
  */
void Protocol_SendDebug(const char *message);

/**
  * @}
  */

#ifdef __cplusplus
}
#endif

#endif /* __FIRMWARE_PROTOCOL_H */
