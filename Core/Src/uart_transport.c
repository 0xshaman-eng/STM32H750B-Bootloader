/**
  ******************************************************************************
  * @file    uart_transport.c
  * @author  Shaman Karunaratne
  * @brief   Interrupt-driven, ring-buffered UART transport implementation.
  ******************************************************************************
  * @details
  * Mirrors usb_transport.c: a single-producer / single-consumer ring buffer.
  *  - Producer: USART3 RXNE interrupt, via UART_Transport_RxByte().
  *  - Consumer: main loop, via UART_Receive().
  *
  * Lock-free by design: exactly one producer and one consumer, and 32-bit
  * index reads/writes are atomic on Cortex-M7.
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "uart_transport.h"
#include "usart.h"   /* declares huart3 */

/** @addtogroup UART_TRANSPORT
  * @{
  */

/* Private defines -----------------------------------------------------------*/

/** RX ring size in bytes. Sized above the largest single read (265-byte data
 *  packet) with headroom; matches the USB ring buffer for symmetry. */
#define UART_RX_RING_SIZE  2048

/* Private variables ---------------------------------------------------------*/

static volatile uint8_t  rx_ring[UART_RX_RING_SIZE]; /**< RX ring storage. */
static volatile uint32_t rx_head = 0; /**< Write index, owned by ISR (producer). */
static volatile uint32_t rx_tail = 0; /**< Read index, owned by main loop (consumer). */

/* Exported functions --------------------------------------------------------*/

/**
  * @brief  One-time UART transport setup.
  * @retval None
  */
void UART_Transport_Init(void)
{
    rx_head = 0;
    rx_tail = 0;

    /* Clear any stale error flags left over from before reset (overrun,
     * framing, noise). On H7 these are cleared by writing to ICR. */
    __HAL_UART_CLEAR_OREFLAG(&huart3);
    __HAL_UART_CLEAR_FEFLAG(&huart3);
    __HAL_UART_CLEAR_NEFLAG(&huart3);

    /* Enable the "RX register not empty" interrupt. From now on, every
     * arriving byte triggers USART3_IRQHandler, which calls
     * UART_Transport_RxByte() to stash the byte in the ring buffer. */
    __HAL_UART_ENABLE_IT(&huart3, UART_IT_RXNE);
}

/**
  * @brief  Push one received byte into the RX ring buffer (ISR context).
  * @note   If the buffer is full the byte is dropped rather than
  *         overwriting unread data.
  * @param  byte  Received byte.
  * @retval None
  */
void UART_Transport_RxByte(uint8_t byte)
{
    uint32_t next = (rx_head + 1u) % UART_RX_RING_SIZE;
    if (next != rx_tail)   /* drop byte if buffer full rather than corrupt */
    {
        rx_ring[rx_head] = byte;
        rx_head = next;
    }
}

/**
  * @brief  Number of unread bytes in the RX ring buffer.
  * @retval Byte count.
  */
static uint32_t rx_available(void)
{
    return (rx_head - rx_tail + UART_RX_RING_SIZE) % UART_RX_RING_SIZE;
}

/**
  * @brief  Number of bytes available in the RX ring buffer.
  * @retval Byte count ready to be read.
  */
uint32_t UART_Available(void)
{
    return rx_available();
}

/**
  * @brief  Blocking receive with timeout, reading from the RX ring buffer.
  * @param  buf      Destination buffer.
  * @param  len      Number of bytes to receive.
  * @param  timeout  Timeout in ms; HAL_MAX_DELAY blocks forever.
  * @retval HAL_OK or HAL_TIMEOUT.
  */
HAL_StatusTypeDef UART_Receive(uint8_t *buf, uint16_t len, uint32_t timeout)
{
    uint32_t start = HAL_GetTick();
    uint16_t received = 0;

    while (received < len)
    {
        if (rx_available() > 0)
        {
            buf[received++] = rx_ring[rx_tail];
            rx_tail = (rx_tail + 1u) % UART_RX_RING_SIZE;
        }
        else
        {
            if ((timeout != HAL_MAX_DELAY) &&
                ((HAL_GetTick() - start) >= timeout))
            {
                return HAL_TIMEOUT;
            }
        }
    }
    return HAL_OK;
}

/**
  * @brief  Blocking transmit with timeout on USART3.
  * @param  buf      Source buffer.
  * @param  len      Number of bytes to send.
  * @param  timeout  Timeout in ms.
  * @retval Status returned by HAL_UART_Transmit().
  */
HAL_StatusTypeDef UART_Transmit(uint8_t *buf, uint16_t len, uint32_t timeout)
{
    /* Straight blocking HAL call; TX needs no buffering. */
    return HAL_UART_Transmit(&huart3, buf, len, timeout);
}

/**
  * @}
  */
