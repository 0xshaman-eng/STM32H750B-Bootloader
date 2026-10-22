/**
  ******************************************************************************
  * @file    usb_transport.h
  * @author  Shaman Karunaratne
  * @brief   Blocking-style USB CDC (virtual COM port) transport.
  ******************************************************************************
  * @details
  * Drop-in replacement for HAL_UART_Receive() / HAL_UART_Transmit() in the
  * firmware update protocol:
  *
  * @code
  *   HAL_UART_Receive(&huart3, buf, len, timeout)   ->  VCP_Receive(buf, len, timeout)
  *   HAL_UART_Transmit(&huart3, buf, len, timeout)  ->  VCP_Transmit(buf, len, timeout)
  * @endcode
  *
  * Both functions keep the HAL_UART return semantics (HAL_OK / HAL_TIMEOUT /
  * HAL_ERROR), so the protocol state machine needs no other changes.
  ******************************************************************************
  */

#ifndef USB_TRANSPORT_H
#define USB_TRANSPORT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32h7xx_hal.h"
#include <stdint.h>

/** @defgroup USB_TRANSPORT USB CDC Transport
  * @brief    USB OTG FS CDC backend for the transport dispatcher.
  * @{
  */

/* Exported functions --------------------------------------------------------*/

/**
  * @brief  Reset the RX ring buffer.
  * @note   Call once after MX_USB_DEVICE_Init(). Transport_Init() calls this
  *         for you.
  * @retval None
  */
void USB_Transport_Init(void);

/**
  * @brief  Blocking receive with timeout, reading from the RX ring buffer.
  * @note   Mirrors HAL_UART_Receive() minus the UART handle.
  * @param  buf      Destination buffer (at least @p len bytes).
  * @param  len      Number of bytes to receive.
  * @param  timeout  Timeout in ms; HAL_MAX_DELAY blocks forever.
  * @retval HAL_OK       @p len bytes were received.
  * @retval HAL_TIMEOUT  Timeout expired first.
  */
HAL_StatusTypeDef VCP_Receive(uint8_t *buf, uint16_t len, uint32_t timeout);

/**
  * @brief  Blocking transmit with timeout over the CDC IN endpoint.
  * @note   Mirrors HAL_UART_Transmit() minus the UART handle. Retries while
  *         the endpoint reports busy.
  * @param  buf      Source buffer. Safe to reuse once this returns HAL_OK.
  * @param  len      Number of bytes to send.
  * @param  timeout  Timeout in ms.
  * @retval HAL_OK       Data handed to the USB stack.
  * @retval HAL_TIMEOUT  Endpoint stayed busy for @p timeout ms.
  * @retval HAL_ERROR    USB failure or device not enumerated.
  */
HAL_StatusTypeDef VCP_Transmit(uint8_t *buf, uint16_t len, uint32_t timeout);

/**
  * @brief  Number of bytes available in the RX ring buffer.
  * @note   Non-blocking peek, e.g. to poll for an opcode without blocking.
  * @retval Byte count ready to be read.
  */
uint32_t VCP_Available(void);

/**
  * @brief  Feed raw received bytes into the RX ring buffer.
  * @warning Call ONLY from CDC_Receive_FS() in usbd_cdc_if.c (USB OTG
  *          interrupt context).
  * @param  data  Received bytes (owned by the USB stack; copied out here).
  * @param  len   Number of bytes in this USB packet.
  * @retval None
  */
void USB_Transport_RxCallback(uint8_t *data, uint32_t len);

/**
  * @}
  */

#ifdef __cplusplus
}
#endif

#endif /* USB_TRANSPORT_H */
