/**
  ******************************************************************************
  * @file    transport.h
  * @author  Shaman Karunaratne
  * @brief   Generic transport dispatcher for the bootloader.
  ******************************************************************************
  * @details
  * This is the ONLY module in the project that knows both UART and USB exist.
  * uart_transport.c and usb_transport.c each know only their own hardware.
  * Firmware_Update_Protocol() and firmware_protocol.c call
  * Transport_Receive() / Transport_Transmit() exclusively and never call the
  * UART_xxx() or VCP_xxx() functions directly.
  *
  * @par Adding a new transport (e.g. OTA)
  *  -# Write ota_transport.h/.c providing OTA_Receive() / OTA_Transmit().
  *  -# Add one value to ::ActiveTransport_t.
  *  -# Add one branch to each switch in transport.c.
  *
  * Nothing else in the project needs to change.
  ******************************************************************************
  */

#ifndef TRANSPORT_H
#define TRANSPORT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32h7xx_hal.h"
#include <stdint.h>

/** @defgroup TRANSPORT Transport Dispatcher
  * @brief    Routes protocol I/O to the active physical transport.
  * @{
  */

/* Exported types ------------------------------------------------------------*/

/**
  * @brief  Physical transports the bootloader can communicate over.
  */
typedef enum
{
    TRANSPORT_NONE = 0,  /**< No transport selected yet. */
    TRANSPORT_UART,      /**< USART3, interrupt-driven ring buffer. */
    TRANSPORT_USB,       /**< USB OTG FS, CDC virtual COM port. */
} ActiveTransport_t;

/* Exported functions --------------------------------------------------------*/

/**
  * @brief  One-time setup for all transports.
  * @note   Call once at boot, after MX_USB_DEVICE_Init(). Internally calls
  *         UART_Transport_Init() and USB_Transport_Init() and resets the
  *         active transport to ::TRANSPORT_NONE.
  * @retval None
  */
void Transport_Init(void);

/**
  * @brief  Select the transport used for all subsequent I/O.
  * @note   Called with whichever transport delivered the STARTUP byte first
  *         (or is being resumed on). Every later Transport_Receive() /
  *         Transport_Transmit() call dispatches based on this.
  * @param  transport  Transport to make active.
  * @retval None
  */
void Transport_SetActive(ActiveTransport_t transport);

/**
  * @brief  Get the currently active transport.
  * @retval Active transport, or ::TRANSPORT_NONE if not yet decided.
  */
ActiveTransport_t Transport_GetActive(void);

/**
  * @brief  Blocking receive with timeout on the active transport.
  * @note   Same contract as HAL_UART_Receive() / VCP_Receive().
  * @param  buf      Destination buffer (at least @p len bytes).
  * @param  len      Number of bytes to receive.
  * @param  timeout  Timeout in ms; HAL_MAX_DELAY blocks forever.
  * @retval HAL_OK       @p len bytes were received.
  * @retval HAL_TIMEOUT  Timeout expired before @p len bytes arrived.
  * @retval HAL_ERROR    Transport_SetActive() has never been called
  *                      (programming error).
  */
HAL_StatusTypeDef Transport_Receive(uint8_t *buf, uint16_t len, uint32_t timeout);

/**
  * @brief  Blocking transmit with timeout on the active transport.
  * @param  buf      Source buffer.
  * @param  len      Number of bytes to send.
  * @param  timeout  Timeout in ms.
  * @retval HAL_OK on success, HAL_TIMEOUT / HAL_BUSY / HAL_ERROR otherwise.
  *         HAL_ERROR is also returned if no transport is active.
  */
HAL_StatusTypeDef Transport_Transmit(uint8_t *buf, uint16_t len, uint32_t timeout);

/**
  * @}
  */

#ifdef __cplusplus
}
#endif

#endif /* TRANSPORT_H */
