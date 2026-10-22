/**
  ******************************************************************************
  * @file    usb_transport.c
  * @author  Shaman Karunaratne
  * @brief   Blocking-style USB CDC transport implementation.
  ******************************************************************************
  * @details
  * @par Receive
  * CDC_Receive_FS() runs in USB OTG FS interrupt context and delivers data in
  * bursts of up to CDC_DATA_FS_MAX_PACKET_SIZE (64) bytes, unrelated to the
  * protocol's packet boundaries. A single-producer / single-consumer ring
  * buffer decouples the two:
  *  - Producer (ISR context): USB_Transport_RxCallback()
  *  - Consumer (main loop):   VCP_Receive()
  *
  * Lock-free by design: exactly one producer and one consumer, and 32-bit
  * index reads/writes are atomic on Cortex-M7.
  *
  * @par Transmit
  * VCP_Transmit() wraps CDC_Transmit_FS(), retrying while the endpoint
  * reports USBD_BUSY, up to the given timeout.
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "usb_transport.h"
#include "usbd_cdc_if.h"
#include <string.h>

/** @addtogroup USB_TRANSPORT
  * @{
  */

/* Private defines -----------------------------------------------------------*/

/** RX ring size in bytes. Comfortably above the largest single read
 *  (264 bytes of a data packet) with headroom for several queued USB packets
 *  while the main loop is busy with flash writes/erases. Increase if stalls
 *  are seen under heavy throughput. */
#define USB_RX_RING_SIZE  2048

/* Private variables ---------------------------------------------------------*/

static volatile uint8_t  rx_ring[USB_RX_RING_SIZE]; /**< RX ring storage. */
static volatile uint32_t rx_head = 0; /**< Write index, owned by USB ISR (producer). */
static volatile uint32_t rx_tail = 0; /**< Read index, owned by main loop (consumer). */

/* Exported functions --------------------------------------------------------*/

/**
  * @brief  Reset the RX ring buffer.
  * @retval None
  */
void USB_Transport_Init(void)
{
    rx_head = 0;
    rx_tail = 0;
}

/**
  * @brief  Feed raw received bytes into the RX ring buffer (USB IRQ context).
  * @note   If the buffer fills, the remaining bytes of this packet are
  *         dropped rather than overwriting unread data.
  * @param  data  Received bytes.
  * @param  len   Number of bytes received.
  * @retval None
  */
void USB_Transport_RxCallback(uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++)
    {
        uint32_t next = (rx_head + 1u) % USB_RX_RING_SIZE;
        if (next == rx_tail)
        {
            /* Ring full: the main loop is stuck (e.g. long flash erase).
             * Drop remaining bytes rather than corrupt the buffer. Increase
             * USB_RX_RING_SIZE or reduce time between VCP_Receive() calls
             * if this happens. */
            break;
        }
        rx_ring[rx_head] = data[i];
        rx_head = next;
    }
}

/**
  * @brief  Number of unread bytes in the RX ring buffer.
  * @retval Byte count.
  */
static uint32_t rx_available(void)
{
    return (rx_head - rx_tail + USB_RX_RING_SIZE) % USB_RX_RING_SIZE;
}

/**
  * @brief  Number of bytes available in the RX ring buffer.
  * @retval Byte count ready to be read.
  */
uint32_t VCP_Available(void)
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
HAL_StatusTypeDef VCP_Receive(uint8_t *buf, uint16_t len, uint32_t timeout)
{
    uint32_t start = HAL_GetTick();
    uint16_t received = 0;

    while (received < len)
    {
        if (rx_available() > 0)
        {
            buf[received++] = rx_ring[rx_tail];
            rx_tail = (rx_tail + 1u) % USB_RX_RING_SIZE;
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
  * @brief  Blocking transmit with timeout over the CDC IN endpoint.
  * @note   CDC_Transmit_FS() copies @p buf into the class driver's TX buffer
  *         before returning, so @p buf may be reused after HAL_OK.
  * @param  buf      Source buffer.
  * @param  len      Number of bytes to send.
  * @param  timeout  Timeout in ms.
  * @retval HAL_OK, HAL_TIMEOUT or HAL_ERROR.
  */
HAL_StatusTypeDef VCP_Transmit(uint8_t *buf, uint16_t len, uint32_t timeout)
{
    uint32_t start = HAL_GetTick();
    uint8_t  result;

    do
    {
        result = CDC_Transmit_FS(buf, len);

        if (result == USBD_OK)
        {
            return HAL_OK;
        }

        if (result != USBD_BUSY)
        {
            /* USBD_FAIL or device not configured/enumerated yet */
            return HAL_ERROR;
        }

        if ((HAL_GetTick() - start) >= timeout)
        {
            return HAL_TIMEOUT;
        }

    } while (result == USBD_BUSY);

    return HAL_ERROR;
}

/**
  * @}
  */
