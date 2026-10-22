/**
  ******************************************************************************
  * @file    uart_transport.h
  * @author  Shaman Karunaratne
  * @brief   Interrupt-driven, ring-buffered UART transport on USART3.
  ******************************************************************************
  * @details
  * Function signatures match usb_transport.h so the generic dispatcher
  * (transport.c) treats UART and USB identically.
  *
  * @par Receive path
  * Interrupt-driven: the USART3 RXNE interrupt fires on every incoming byte
  * and pushes it into a ring buffer. UART_Receive() reads from that buffer
  * with a blocking-with-timeout contract, mirroring usb_transport.c. Bytes are
  * captured by the ISR even if the main loop is briefly preempted (e.g. by
  * USB interrupts during high-speed bursts), so nothing is lost.
  *
  * @par Transmit path
  * Plain blocking HAL call.
  ******************************************************************************
  */

#ifndef UART_TRANSPORT_H
#define UART_TRANSPORT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32h7xx_hal.h"
#include <stdint.h>

/** @defgroup UART_TRANSPORT UART Transport
  * @brief    USART3 backend for the transport dispatcher.
  * @{
  */

/* Exported functions --------------------------------------------------------*/

/**
  * @brief  One-time UART transport setup.
  * @note   Transport_Init() calls this for you. Resets the ring buffer,
  *         clears stale overrun/framing/noise flags and enables the USART3
  *         RXNE interrupt so bytes start being captured.
  * @retval None
  */
void UART_Transport_Init(void);

/**
  * @brief  Blocking receive with timeout, reading from the RX ring buffer.
  * @note   Same contract as VCP_Receive().
  * @param  buf      Destination buffer (at least @p len bytes).
  * @param  len      Number of bytes to receive.
  * @param  timeout  Timeout in ms; HAL_MAX_DELAY blocks forever.
  * @retval HAL_OK       @p len bytes were received.
  * @retval HAL_TIMEOUT  Timeout expired first.
  */
HAL_StatusTypeDef UART_Receive(uint8_t *buf, uint16_t len, uint32_t timeout);

/**
  * @brief  Blocking transmit with timeout on USART3.
  * @param  buf      Source buffer.
  * @param  len      Number of bytes to send.
  * @param  timeout  Timeout in ms.
  * @retval Status returned by HAL_UART_Transmit().
  */
HAL_StatusTypeDef UART_Transmit(uint8_t *buf, uint16_t len, uint32_t timeout);

/**
  * @brief  Number of bytes available in the RX ring buffer.
  * @note   Non-blocking peek, mirroring VCP_Available().
  * @retval Byte count ready to be read.
  */
uint32_t UART_Available(void);

/**
  * @brief  Push one received byte into the RX ring buffer.
  * @warning Call ONLY from USART3_IRQHandler(). Not for general use.
  * @param  byte  Byte read from the USART3 RDR register.
  * @retval None
  */
void UART_Transport_RxByte(uint8_t byte);

/**
  * @}
  */

#ifdef __cplusplus
}
#endif

#endif /* UART_TRANSPORT_H */
