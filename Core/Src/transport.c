/**
  ******************************************************************************
  * @file    transport.c
  * @author  Shaman Karunaratne
  * @brief   Generic transport dispatcher implementation.
  * @see     transport.h for the design rationale.
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "transport.h"
#include "uart_transport.h"
#include "usb_transport.h"

/** @addtogroup TRANSPORT
  * @{
  */

/* Private variables ---------------------------------------------------------*/

/** Transport selected by Transport_SetActive(); read by every dispatch. */
static volatile ActiveTransport_t active_transport = TRANSPORT_NONE;

/* Exported functions --------------------------------------------------------*/

/**
  * @brief  One-time setup for all transports.
  * @retval None
  */
void Transport_Init(void)
{
    active_transport = TRANSPORT_NONE;
    UART_Transport_Init();
    USB_Transport_Init();
}

/**
  * @brief  Select the transport used for all subsequent I/O.
  * @param  transport  Transport to make active.
  * @retval None
  */
void Transport_SetActive(ActiveTransport_t transport)
{
    active_transport = transport;
}

/**
  * @brief  Get the currently active transport.
  * @retval Active transport, or ::TRANSPORT_NONE if not yet decided.
  */
ActiveTransport_t Transport_GetActive(void)
{
    return active_transport;
}

/**
  * @brief  Blocking receive with timeout on the active transport.
  * @param  buf      Destination buffer.
  * @param  len      Number of bytes to receive.
  * @param  timeout  Timeout in ms; HAL_MAX_DELAY blocks forever.
  * @retval HAL_OK, HAL_TIMEOUT, or HAL_ERROR if no transport is active.
  */
HAL_StatusTypeDef Transport_Receive(uint8_t *buf, uint16_t len, uint32_t timeout)
{
    switch (active_transport)
    {
        case TRANSPORT_UART:
            return UART_Receive(buf, len, timeout);
        case TRANSPORT_USB:
            return VCP_Receive(buf, len, timeout);
        default:
            /* Transport_SetActive() was never called -- this indicates a
             * bug in the caller (e.g. the dual-listen STARTUP loop was
             * skipped). Fail loudly rather than silently picking one. */
            return HAL_ERROR;
    }
}

/**
  * @brief  Blocking transmit with timeout on the active transport.
  * @param  buf      Source buffer.
  * @param  len      Number of bytes to send.
  * @param  timeout  Timeout in ms.
  * @retval Status from the underlying transport, or HAL_ERROR if no
  *         transport is active.
  */
HAL_StatusTypeDef Transport_Transmit(uint8_t *buf, uint16_t len, uint32_t timeout)
{
    switch (active_transport)
    {
        case TRANSPORT_UART:
            return UART_Transmit(buf, len, timeout);
        case TRANSPORT_USB:
            return VCP_Transmit(buf, len, timeout);
        default:
            return HAL_ERROR;
    }
}

/**
  * @}
  */
