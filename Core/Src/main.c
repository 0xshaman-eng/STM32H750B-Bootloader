/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @author         : Shaman Karunaratne
  * @brief          : Dual-bank QSPI bootloader with UART / USB CDC firmware
  *                   update.
  ******************************************************************************
  * @details
  * Boot flow:
  *  -# Initialise clocks, peripherals, QSPI and transports.
  *  -# Load the ping-pong NV configuration (Config_Init()).
  *  -# Listen on UART and USB for ::OPCODE_STARTUP for 5 s (forever if an
  *     update must be resumed). The first transport to deliver it wins.
  *  -# STARTUP received -> Firmware_Update_Protocol().
  *     Timeout             -> jump to the application in Bank 1.
  *
  * QSPI memory map (flash offsets):
  *  - Bank 1 (executed image): 0x00000000
  *  - Bank 2 (download area):  ::APP_OFFSET (0x04000000)
  *  - NV config A / B:         last two 4 KB subsectors
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "crc.h"
#include "dma2d.h"
#include "iwdg.h"
#include "ltdc.h"
#include "usart.h"
#include "usb_device.h"
#include "gpio.h"
#include "fmc.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "memory.h"
#include "qspi.h"
#include "sha256.h"
#include "firmware_protocol.h"
#include "usb_transport.h"
#include "uart_transport.h"       /* UART backend */
#include "transport.h"            /* Generic transport dispatcher */
#include <string.h>
#include <stdio.h>
#include "mt25tl01g.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/** Application entry point (reset handler) function pointer. */
typedef  void (*pFunction)(void);

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/** @name Bootloader version
  * @{ */
#define BOOTLOADER_VERSION_MAJOR 0
#define BOOTLOADER_VERSION_MINOR 1
#define BOOTLOADER_VERSION_PATCH 0
/** @} */

/** QSPI offset of Bank 2 (download area): 64 MB. Bank 1 starts at 0. */
#define APP_OFFSET 0x04000000

/** @name NV config locations (last two 4 KB subsectors of the 128 MB flash)
  * @{ */
#define NV_CONFIG_SUBSECTOR_SIZE   MT25TL01G_SUBSECTOR_SIZE
#define NV_CONFIG_SECTOR_A_ADDR (MT25TL01G_FLASH_SIZE - (2 * NV_CONFIG_SUBSECTOR_SIZE))
#define NV_CONFIG_SECTOR_B_ADDR (MT25TL01G_FLASH_SIZE - NV_CONFIG_SUBSECTOR_SIZE)
/** @} */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/** Application reset handler, read from the Bank 1 vector table. */
pFunction JumpToApplication;

/** Live state of the current firmware update session. */
static BootloaderContext_t boot_ctx = {
    .state = UPDATE_STATE_IDLE,
    .firmware_size = 0,
    .total_bytes_written = 0,
    .expected_seq_number = 0,
    .total_packets_received = 0,
    .flash_write_address = APP_OFFSET
};

/** RAM copy of the active NV configuration (see Config_Init() / Config_Write()). */
static BootloaderNVConfig_t nv_config = {0};

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
/* USER CODE BEGIN PFP */

static void CPU_CACHE_Enable(void);
static void CPU_CACHE_Disable(void);
static uint32_t Memory_Startup(void);
static void Firmware_Update_Protocol(void);
static void Cleanup_Protocol_State(void);
static uint8_t Copy_Firmware_Bank2_to_Bank1(uint32_t firmware_size);

static void Config_Init(void);
static void Config_Write(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

  /* Enable the CPU Cache */

  /* Enable I-Cache---------------------------------------------------------*/
  SCB_EnableICache();

  /* Enable D-Cache---------------------------------------------------------*/
  SCB_EnableDCache();

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
    uint8_t iwdg_reset = 0;
    // Check if IWDG1 caused the reset
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDG1RST)) {
        iwdg_reset = 1;
        __HAL_RCC_CLEAR_RESET_FLAGS();
    }

    /* Route HSI48 (RC48) to the USB_OTG_FS peripheral clock */
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USB;
    PeriphClkInitStruct.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_LTDC_Init();
  MX_DMA2D_Init();
  MX_FMC_Init();
  MX_CRC_Init();
  MX_USART3_UART_Init();
  /* NOTE: MX_IWDG1_Init() intentionally NOT called here -- CubeMX re-adds it
   * on every regeneration; it must only start inside Firmware_Update_Protocol().
   * Delete it again after any future "Generate Code". */
  MX_USB_DEVICE_Init();
  Transport_Init();   /* Inits both UART and USB ring buffers and enables UART RXNE IRQ */
  /* USER CODE BEGIN 2 */

  if(Memory_Startup() != HAL_OK)
	{
		Error_Handler();
	}
	
	/* Initialize the non-volatile configuration safely from QSPI */
	Config_Init();

	if (iwdg_reset) {
		nv_config.update_in_progress = 0;
		Config_Write();
	}

	MasterState_t master_state = MASTER_STATE_READY;

	while (1)
	{
		switch (master_state)
		{
			case MASTER_STATE_READY:
			{
				/* Blink LED to indicate ready state initially */
				for(int i = 0; i < 10; i++){
					HAL_GPIO_TogglePin(LD1_GPIO_Port, LD1_Pin);
					HAL_Delay(100);
				}
				
				uint8_t rx_byte = 0;
				uint32_t start_tick = HAL_GetTick();
				uint8_t update_triggered = 0;

				/* If an update was interrupted, there is no safe fallback to booting
				 * the application (bank1 may be invalid) -- so this boot MUST resume,
				 * meaning the wait below never times out for this case. */
				uint8_t must_resume = (nv_config.update_in_progress == 1) ||
				                       (nv_config.bank1_status == 0 && nv_config.bank2_status == 1);

				/* Dual-listen: poll both UART and USB every iteration. Whichever
				 * delivers the STARTUP byte first becomes the active transport for
				 * the rest of this session -- fresh update or resume alike. */
				while (must_resume || (HAL_GetTick() - start_tick) < 5000)
				{
				    ActiveTransport_t winner = TRANSPORT_NONE;

				    if (UART_Receive(&rx_byte, 1, 10) == HAL_OK && rx_byte == OPCODE_STARTUP)
				    {
				        winner = TRANSPORT_UART;
				    }
				    else if (VCP_Receive(&rx_byte, 1, 10) == HAL_OK && rx_byte == OPCODE_STARTUP)
				    {
				        winner = TRANSPORT_USB;
				    }

				    if (winner != TRANSPORT_NONE)
				    {
				        Transport_SetActive(winner);
				        update_triggered = 1;

				        char version_msg[128];
				        int msg_len = snprintf(version_msg, sizeof(version_msg), "\r\nBootloader Online\r\n Version %d.%d.%d\r\n", BOOTLOADER_VERSION_MAJOR, BOOTLOADER_VERSION_MINOR, BOOTLOADER_VERSION_PATCH);
				        Transport_Transmit((uint8_t*)version_msg, msg_len, 500);
				        break;
				    }
				}
				if (update_triggered)
				{
					master_state = MASTER_STATE_BOOT_UPDATE;
				}
				else
				{
					master_state = MASTER_STATE_BOOT;
				}
				break;
			}
			
			case MASTER_STATE_BOOT_UPDATE:
			{
				Firmware_Update_Protocol();
				break; /* Usually won't return, as update process does system reset on success */
			}
			
			case MASTER_STATE_BOOT:
			{
				if (nv_config.bank1_status == 0)
				{
					/* Cannot boot from invalid Bank 1, fallback to update mode forever */
					master_state = MASTER_STATE_BOOT_UPDATE;
					break;
				}

				#if (DATA_AREA == USE_EXTERNAL_SDRAM) || (CODE_AREA == USE_EXTERNAL_SDRAM)
					/* Configure MPU to allow non aligned access */
					MPU_Config();
				#endif

				/* Re-enable memory mapped mode to allow executing/reading from QSPI */
				QSPI_EnableMemoryMappedMode();

				HAL_UART_DeInit(&huart3);
				HAL_CRC_DeInit(&hcrc);
				/* Recommended open item: also cleanly stop USB before the jump:
				 *   USBD_Stop(&hUsbDeviceFS);
				 *   USBD_DeInit(&hUsbDeviceFS);
				 * (left commented pending validation on hardware) */

				/* Disable CPU L1 cache before jumping to the QSPI code execution */
				CPU_CACHE_Disable();

				/* Disable Systick interrupt */
				SysTick->CTRL = 0;

				__disable_irq();

				JumpToApplication = (pFunction)(*(__IO uint32_t*)(0x90000000 + 4)); // Reset vector at offset 4
				__set_MSP(*(__IO uint32_t*)(0x90000000)); // Set MSP to initial stack pointer from offset 0
				JumpToApplication(); // Jump to application
				break;
			}
			
			case MASTER_STATE_ERROR:
			default:
			{
				HAL_GPIO_TogglePin(LD1_GPIO_Port, LD1_Pin);
				HAL_Delay(100);
				break;
			}
		}
	}
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48|RCC_OSCILLATORTYPE_LSI
                              |RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 5;
  RCC_OscInitStruct.PLL.PLLN = 160;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */


/**
  * @brief  Initialize and configure Volatile and Non-Volatile memory.
  * @note   Which memories are started depends on DATA_AREA / CODE_AREA /
  *         BINARY_AREA in memory.h (QSPI only in the current configuration).
  * @retval HAL_OK (MEMORY_OK) on success, non-zero on any failure.
  */
uint32_t Memory_Startup(void)
{
  uint32_t status = HAL_OK;

  /* Volatile Memory Configuration */
#if (DATA_AREA == USE_EXTERNAL_SDRAM) || (CODE_AREA == USE_EXTERNAL_SDRAM)
  status |= SDRAM_Startup();
#endif

  /* Non-Volatile Memory Configuration */
#if (CODE_AREA == USE_QSPI) || (BINARY_AREA == USE_SPI_NOR)
  status |= QSPI_Startup(QSPI_MODE);
#endif

  #if (BINARY_AREA == USE_MMC)
  status |= MMC_Startup();
#endif

  return status;
}

/**
  * @brief  CPU L1-Cache enable.
  * @retval None
  */
static void CPU_CACHE_Enable(void)
{
  /* Enable I-Cache */
  SCB_EnableICache();

  /* Enable D-Cache */
  SCB_EnableDCache();
}

/**
  * @brief  CPU L1-Cache disable (required before jumping to the application).
  * @retval None
  */
static void CPU_CACHE_Disable(void)
{
  /* Disable I-Cache */
  SCB_DisableICache();

  /* Disable D-Cache */
  SCB_DisableDCache();
}

/**
  * @brief  Copy the verified firmware from Bank 2 to Bank 1, page by page.
  * @note   Bank 1 must already be erased. Each page is read through the
  *         memory-mapped window, then programmed with QSPI_WritePage(); the
  *         watchdog is refreshed per page.
  * @param  f_size  Size of the firmware to copy, in bytes.
  * @retval 0 on success, 1 on failure.
  */
static uint8_t Copy_Firmware_Bank2_to_Bank1(uint32_t f_size)
{
  QSPI_EnableMemoryMappedMode();
  
  uint8_t *src_ptr = (uint8_t*)(0x90000000 + APP_OFFSET);
  uint32_t dst_address = 0;
  uint32_t bytes_to_copy = f_size;
  uint8_t page_buffer[MT25TL01G_PAGE_SIZE];
  
  while(bytes_to_copy > 0)
  {
    uint32_t chunk_size = (bytes_to_copy > MT25TL01G_PAGE_SIZE) ? MT25TL01G_PAGE_SIZE : bytes_to_copy;
    memcpy(page_buffer, src_ptr, chunk_size);
    
    if(QSPI_WritePage(page_buffer, dst_address, chunk_size) != MEMORY_OK)
    {
      return 1; // Failed
    }
    
    QSPI_EnableMemoryMappedMode();
    
    src_ptr += chunk_size;
    dst_address += chunk_size;
    bytes_to_copy -= chunk_size;

    HAL_IWDG_Refresh(&hiwdg1); 

  }
  return 0; // Success
}

/**
  * @brief  Load the NV configuration from QSPI flash (ping-pong).
  * @details Reads copies A and B and validates each by magic word and CRC-32.
  *          If both are valid, the one with the higher commit_counter wins.
  *          If neither is valid (first boot), ::nv_config is reset to defaults.
  * @retval None
  */
static void Config_Init(void)
{
  BootloaderNVConfig_t config_a = {0};
  BootloaderNVConfig_t config_b = {0};
  uint8_t a_valid = 0, b_valid = 0;
  uint32_t config_size = sizeof(BootloaderNVConfig_t);
  
  /* Read Sector A */
  QSPI_EnableMemoryMappedMode();
  uint8_t *flash_ptr_a = (uint8_t*)(0x90000000 + NV_CONFIG_SECTOR_A_ADDR);
  memcpy(&config_a, flash_ptr_a, config_size);
  if (config_a.magic_word == NV_CONFIG_MAGIC_WORD && 
      CRC32_Calculate((uint8_t*)&config_a, config_size - 4) == config_a.crc32)
  {
    a_valid = 1;
  }
  
  /* Read Sector B */
  uint8_t *flash_ptr_b = (uint8_t*)(0x90000000 + NV_CONFIG_SECTOR_B_ADDR);
  memcpy(&config_b, flash_ptr_b, config_size);
  if (config_b.magic_word == NV_CONFIG_MAGIC_WORD && 
      CRC32_Calculate((uint8_t*)&config_b, config_size - 4) == config_b.crc32)
  {
    b_valid = 1;
  }
  
  if (a_valid && b_valid)
  {
    /* Resolve collision using commit_counter */
    if (config_a.commit_counter >= config_b.commit_counter)
      memcpy(&nv_config, &config_a, config_size);
    else
      memcpy(&nv_config, &config_b, config_size);
  }
  else if (a_valid)
  {
    memcpy(&nv_config, &config_a, config_size);
  }
  else if (b_valid)
  {
    memcpy(&nv_config, &config_b, config_size);
  }
  else
  {
    /* First boot or completely empty config */
    memset(&nv_config, 0, config_size);
    nv_config.magic_word = NV_CONFIG_MAGIC_WORD;
    nv_config.commit_counter = 0;
  }
  
  QSPI_DisableMemoryMappedMode();
}

/**
  * @brief  Write the NV configuration to QSPI flash (ping-pong).
  * @details Increments commit_counter, recomputes the CRC-32, then writes to
  *          copy A (odd counter) or copy B (even counter). The other copy is
  *          left intact, so a power loss mid-write never loses the last
  *          good configuration.
  * @retval None
  */
static void Config_Write(void)
{
  uint32_t active_addr;

  /* Alternate slots by counter parity: odd -> A, even -> B */

  nv_config.commit_counter++;
  nv_config.crc32 = CRC32_Calculate((uint8_t*)&nv_config, sizeof(BootloaderNVConfig_t) - 4);
  
  if (nv_config.commit_counter % 2 == 1)
  {
    active_addr = NV_CONFIG_SECTOR_A_ADDR;
  }
  else
  {
    active_addr = NV_CONFIG_SECTOR_B_ADDR;
  }
  
  QSPI_EraseSubSector(active_addr);
  /* Write chunks as page sizes */
  uint32_t bytes_to_write = sizeof(BootloaderNVConfig_t);
  uint32_t write_addr = active_addr;
  uint8_t *src_ptr = (uint8_t*)&nv_config;
  
  while (bytes_to_write > 0)
  {
    uint32_t chunk = (bytes_to_write > MT25TL01G_PAGE_SIZE) ? MT25TL01G_PAGE_SIZE : bytes_to_write;
    QSPI_WritePage(src_ptr, write_addr, chunk);
    bytes_to_write -= chunk;
    write_addr += chunk;
    src_ptr += chunk;
  }
}

/**
  * @brief  Reset the update session to idle and persist it.
  * @details Clears ::boot_ctx, copies it into ::nv_config, clears
  *          update_in_progress and calls Config_Write().
  * @retval None
  */
static void Cleanup_Protocol_State(void)
{
  boot_ctx.state = UPDATE_STATE_IDLE;
  boot_ctx.firmware_size = 0;
  boot_ctx.total_bytes_written = 0;
  boot_ctx.expected_seq_number = 0;
  boot_ctx.total_packets_received = 0;
  boot_ctx.flash_write_address = APP_OFFSET;
  memset(boot_ctx.received_sha256, 0, 32);
  memset(boot_ctx.rx_packet_buffer, 0, DATA_PACKET_SIZE);

  memcpy(&nv_config.boot_ctx, &boot_ctx, sizeof(BootloaderContext_t));
  nv_config.update_in_progress = 0; 
  Config_Write();
}

/**
  * @brief  Firmware update protocol: receive an image over the active
  *         transport (UART or USB), store it in Bank 2, verify it, then copy
  *         it to Bank 1.
  * @details
  * Starts IWDG1 on entry. Entry paths:
  *  - update_in_progress set: restore ::boot_ctx, re-erase ahead of the
  *    last write position and NACK with ::ERROR_DATA_SEQ_NUM so the host
  *    resumes from expected_seq_number.
  *  - Bank 1 invalid, Bank 2 valid: restore ::boot_ctx and continue the
  *    Bank 2 -> Bank 1 copy.
  *  - Otherwise: start fresh in ::UPDATE_STATE_IDLE.
  *
  * State flow: IDLE -> HEADER_RECEIVED -> READY (data loop) ->
  * FOOTER_RECEIVED -> UPDATE_DONE -> BANK1_READY -> FIRMWARE_UPDATED.
  * On success the MCU is reset and this function does not return.
  * @retval None
  */
static void Firmware_Update_Protocol(void)
{
  uint8_t opcode;
  HeaderPacket_t header_packet;
  DataPacket_t data_packet;
  FooterPacket_t footer_packet;
  uint32_t calculated_crc32;
  uint32_t sectors_to_erase;
  SHA256_CTX sha256_ctx;
  uint8_t calculated_sha256[32];

  /* Initialize and start Watchdog ONLY when firmware update is triggered */
  MX_IWDG1_Init();
  HAL_IWDG_Refresh(&hiwdg1);

  if (nv_config.update_in_progress == 1)
  {
    /* Resume from interrupted state */
    memcpy(&boot_ctx, &nv_config.boot_ctx, sizeof(BootloaderContext_t));
    
    /* Re-erase from where we left off writing (data past the last saved
     * checkpoint may be partially programmed) */
    uint32_t sector_address = APP_OFFSET + (boot_ctx.total_bytes_written);

    // change to do erase if only needed, not all stopped states needs an erase.
    for(uint32_t i = 0; i < 16; i++) // Erase up to 16 sectors (1MB) ahead to ensure we have a clean area to write next packets, adjust as needed
    {
      HAL_IWDG_Refresh(&hiwdg1); // Kick watchdog while resuming/erasing each sector
      if(QSPI_EraseSector(sector_address + (i * MT25TL01G_SECTOR_SIZE)) != MEMORY_OK)
      {
        Protocol_SendNACK(OPCODE_DATA, boot_ctx.expected_seq_number, ERROR_HEADER_ERASE);
        boot_ctx.state = UPDATE_STATE_ERROR;
        return;
      }
    }
    
    /* Request the host to start sending from expected sequence number */
    Protocol_SendNACK(OPCODE_DATA, boot_ctx.expected_seq_number, ERROR_DATA_SEQ_NUM);
  }
  else if (nv_config.bank1_status == 0 && nv_config.bank2_status == 1)
  {
    /* Bank 2 holds valid downloaded update, but Bank 1 wasn't fully copied/flashed */
    /* Start from UPDATE_DONE to re-erase Bank 1 and safely perform the copy phase */
    memcpy(&boot_ctx, &nv_config.boot_ctx, sizeof(BootloaderContext_t)); 
  }
  else
  {
    HAL_IWDG_Refresh(&hiwdg1);
    boot_ctx.state = UPDATE_STATE_IDLE;
  }

  /* Main protocol loop - continuously wait for packets */
  while(1)
  {
    /* Wait for a new opcode only in states that are actively listening for a new command */
    if (boot_ctx.state == UPDATE_STATE_IDLE || 
        boot_ctx.state == UPDATE_STATE_READY) 
    {
    	if(Transport_Receive(&opcode, 1, HAL_MAX_DELAY) != HAL_OK)
      {
        continue;
      }
    }

    switch(boot_ctx.state)
    {
      case UPDATE_STATE_IDLE:
      {
        if (opcode == OPCODE_HEADER)
        {
          boot_ctx.state = UPDATE_STATE_HEADER_RECEIVED;
          HAL_IWDG_Refresh(&hiwdg1);
        }
        else
        {
          /* Only NACK if not STARTUP syncing bytes occasionally re-received */
          if (opcode != OPCODE_STARTUP)
          {
            /* UNEXPECTED PACKET handling */
            Protocol_SendNACK(opcode, 0, ERROR_HEADER_UART_RX); /* Re-using error code or custom one */
          }
        }
        break;
      }
      
      case UPDATE_STATE_HEADER_RECEIVED:
      {
        /* Receive rest of header packet (file_size + sha256 + crc32 = 40 bytes) */
        header_packet.opcode = OPCODE_HEADER;
        if(Transport_Receive((uint8_t*)&header_packet.file_size, 40, 2000) != HAL_OK)
        {
          Protocol_SendNACK(OPCODE_HEADER, 0, ERROR_HEADER_UART_RX);
          boot_ctx.state = UPDATE_STATE_IDLE; 
          break;
        }

        Cleanup_Protocol_State();

        /* Validate CRC-32 */
        calculated_crc32 = CRC32_Calculate((uint8_t*)&header_packet, 37);
        if(calculated_crc32 != header_packet.crc32)
        {
          Protocol_SendNACK(OPCODE_HEADER, 0, ERROR_HEADER_CRC);
          boot_ctx.state = UPDATE_STATE_IDLE;
          break;
        }

        /* Store metadata */
        boot_ctx.firmware_size = header_packet.file_size;
        memcpy(boot_ctx.received_sha256, header_packet.sha256_hash, 32);

        /* Invalidate Bank 2 in NV Config BEFORE erasing it */
        nv_config.bank2_status = 0;
        Config_Write();

        /* Erase QSPI bank 2 */
        sectors_to_erase = (boot_ctx.firmware_size + MT25TL01G_SECTOR_SIZE - 1) / MT25TL01G_SECTOR_SIZE;
        uint8_t erase_error = 0;
        for(uint32_t i = 0; i < sectors_to_erase; i++)
        {
          HAL_IWDG_Refresh(&hiwdg1); // Kick watchdog while erasing each sector
          uint32_t sector_address = APP_OFFSET + (i * MT25TL01G_SECTOR_SIZE);
          if(QSPI_EraseSector(sector_address) != MEMORY_OK)
          {
            Protocol_SendNACK(OPCODE_HEADER, 0, ERROR_HEADER_ERASE);
            boot_ctx.state = UPDATE_STATE_ERROR;
            erase_error = 1;
            break;
          }
        }

        if(!erase_error)
        {
          boot_ctx.state = UPDATE_STATE_READY;
          HAL_IWDG_Refresh(&hiwdg1); // Header & Initial Erase complete
          
          /* Save initial non-volatile update state */
          nv_config.update_in_progress = 1;
          memcpy(&nv_config.boot_ctx, &boot_ctx, sizeof(BootloaderContext_t));
          Config_Write();
          
          Protocol_SendACK(OPCODE_HEADER, 0);
        }
        break;
      }
      
      case UPDATE_STATE_READY:
      {
        if (opcode == OPCODE_HEADER)
        {
          boot_ctx.state = UPDATE_STATE_HEADER_RECEIVED;
          HAL_IWDG_Refresh(&hiwdg1); 
        }
        else if (opcode == OPCODE_DATA)
        {
          data_packet.opcode = OPCODE_DATA;
          if(Transport_Receive((uint8_t*)&data_packet.seq_number, 264, 2000) != HAL_OK)
          {
            Protocol_SendNACK(OPCODE_DATA, data_packet.seq_number, ERROR_DATA_UART_RX);
            break;
          }

          /* Verify sequence number */
          if(data_packet.seq_number != boot_ctx.expected_seq_number)
          {
            Protocol_SendNACK(OPCODE_DATA, boot_ctx.expected_seq_number, ERROR_DATA_SEQ_NUM);
            break;
          }

          /* Validate CRC-32 */
          calculated_crc32 = CRC32_Calculate((uint8_t*)&data_packet, 261);
          if(calculated_crc32 != data_packet.crc32)
          {
            Protocol_SendNACK(OPCODE_DATA, data_packet.seq_number, ERROR_DATA_CRC);
            break;
          }

          /* Validate length */
          uint32_t remaining_bytes = boot_ctx.firmware_size - boot_ctx.total_bytes_written;
          if(data_packet.length == 0 || data_packet.length > 256 || data_packet.length > remaining_bytes)
          {
            Protocol_SendNACK(OPCODE_DATA, data_packet.seq_number, ERROR_DATA_LENGTH);
            Cleanup_Protocol_State();
            break;
          }

          /* Write data */
          if(QSPI_WritePage(data_packet.data, boot_ctx.flash_write_address, data_packet.length) != MEMORY_OK)
          {
            Protocol_SendNACK(OPCODE_DATA, data_packet.seq_number, ERROR_DATA_FLASH_WRITE);
            boot_ctx.state = UPDATE_STATE_ERROR; /* Unrecoverable error */
            nv_config.update_in_progress = 0;    /* Abort retry */
            Config_Write();
            break;
          }

          boot_ctx.flash_write_address += data_packet.length;
          boot_ctx.total_bytes_written += data_packet.length;
          boot_ctx.expected_seq_number++;
          boot_ctx.total_packets_received++;

          /* Periodically save resume context (every 16 sectors = 1MB) */
          if (boot_ctx.total_bytes_written % (MT25TL01G_SECTOR_SIZE * 16) == 0)
          {
            memcpy(&nv_config.boot_ctx, &boot_ctx, sizeof(BootloaderContext_t));
            Config_Write();
          }

          Protocol_SendACK(OPCODE_DATA, data_packet.seq_number);
          HAL_IWDG_Refresh(&hiwdg1); // Successful data write & ACK
        }
        else if (opcode == OPCODE_FOOTER)
        {
          boot_ctx.state = UPDATE_STATE_FOOTER_RECEIVED;
          HAL_IWDG_Refresh(&hiwdg1); 
        }
        else
        {
          Protocol_SendNACK(opcode, 0, ERROR_DATA_NOT_READY);
        }
        break;
      }
      
      case UPDATE_STATE_FOOTER_RECEIVED:
      {
        footer_packet.opcode = OPCODE_FOOTER;
        if(Transport_Receive((uint8_t*)&footer_packet.total_packets, 7, 1000) != HAL_OK)
        {
          Protocol_SendNACK(OPCODE_FOOTER, 0, ERROR_FOOTER_UART_RX);
          boot_ctx.state = UPDATE_STATE_READY;
          break;
        }

        calculated_crc32 = CRC32_Calculate((uint8_t*)&footer_packet, 4);
        if(calculated_crc32 != footer_packet.crc32)
        {
          Protocol_SendNACK(OPCODE_FOOTER, 0, ERROR_FOOTER_CRC);
          boot_ctx.state = UPDATE_STATE_READY;
          break;
        }

        if(footer_packet.total_packets != boot_ctx.total_packets_received)
        {
          Protocol_SendNACK(OPCODE_FOOTER, 0, ERROR_FOOTER_PACKET_COUNT);
          Cleanup_Protocol_State();
          break;
        }

        if(QSPI_EnableMemoryMappedMode() != MEMORY_OK)
        {
          Protocol_SendNACK(OPCODE_FOOTER, 0, ERROR_FOOTER_MEM_MAPPED);
          boot_ctx.state = UPDATE_STATE_ERROR;
          nv_config.update_in_progress = 0;
          Config_Write();
          break;
        }

        sha256_init(&sha256_ctx);
        uint8_t *flash_ptr = (uint8_t*)(0x90000000 + APP_OFFSET);
        uint32_t bytes_remaining = boot_ctx.firmware_size;

        while(bytes_remaining > 0)
        {
          uint32_t chunk_size = (bytes_remaining > 1024) ? 1024 : bytes_remaining;
          sha256_update(&sha256_ctx, flash_ptr, chunk_size);
          flash_ptr += chunk_size;
          bytes_remaining -= chunk_size;
          HAL_IWDG_Refresh(&hiwdg1);   /* keep watchdog fed during full-image hash */
        }
        sha256_final(&sha256_ctx, calculated_sha256);

        if(memcmp(calculated_sha256, boot_ctx.received_sha256, 32) == 0)
        {
          boot_ctx.state = UPDATE_STATE_UPDATE_DONE;
          HAL_IWDG_Refresh(&hiwdg1); // Successful Footer & hash validation

          Protocol_SendACK(OPCODE_FOOTER, 0);

          /* Update bank 2 status to valid and clear update_in_progress */
          memcpy(&nv_config.boot_ctx, &boot_ctx, sizeof(BootloaderContext_t));
          nv_config.bank2_status = 1;
          nv_config.update_in_progress = 0;
          Config_Write();
        }
        else
        {
          Cleanup_Protocol_State();
        }
        break;
      }
      
      case UPDATE_STATE_UPDATE_DONE:
      {
        QSPI_DisableMemoryMappedMode();
        
        /* Invalidate Bank 1 in NV Config BEFORE erasing it */
        nv_config.bank1_status = 0;
        Config_Write();
        
        uint32_t sectors_to_erase_bank1 = (boot_ctx.firmware_size + MT25TL01G_SECTOR_SIZE - 1) / MT25TL01G_SECTOR_SIZE;
        uint8_t erase_error = 0;
        
        for(uint32_t i = 0; i < sectors_to_erase_bank1; i++)
        {
          uint32_t sector_address = (i * MT25TL01G_SECTOR_SIZE);
          if(QSPI_EraseSector(sector_address) != MEMORY_OK)
          {
            Protocol_SendNACK(OPCODE_FOOTER, 0, ERROR_FOOTER_ERASE);
            boot_ctx.state = UPDATE_STATE_ERROR;
            erase_error = 1;
            break;
          }
          HAL_IWDG_Refresh(&hiwdg1); // Kick watchdog while erasing each sector
        }
        
        if (!erase_error)
        {
          boot_ctx.state = UPDATE_STATE_BANK1_READY;
          HAL_IWDG_Refresh(&hiwdg1);
        }
        break;
      }
      
      case UPDATE_STATE_BANK1_READY:
      {
        if(Copy_Firmware_Bank2_to_Bank1(boot_ctx.firmware_size) != 0)
        {
          Protocol_SendNACK(OPCODE_FOOTER, 0, ERROR_FOOTER_FLASH_WRITE);
          boot_ctx.state = UPDATE_STATE_ERROR;
          /* Copy failed, keep Bank1 invalid */
        }
        else
        {
          boot_ctx.state = UPDATE_STATE_FIRMWARE_UPDATED;
          HAL_IWDG_Refresh(&hiwdg1);
        }
        break;
      }
      
      case UPDATE_STATE_FIRMWARE_UPDATED:
      {
        /* Complete: mark Bank 1 valid */
        memcpy(&nv_config.boot_ctx, &boot_ctx, sizeof(BootloaderContext_t));
        nv_config.bank1_status = 1;
        Config_Write();
        
        Cleanup_Protocol_State();

        for(int i = 0; i < 20; i++)
        {
          HAL_GPIO_TogglePin(LD1_GPIO_Port, LD1_Pin);
          HAL_Delay(50);
        }
        HAL_Delay(500);

        NVIC_SystemReset();
        break;
      }
      
      case UPDATE_STATE_ERROR:
      {
        /* Error state, blink forever or handle accordingly */
        HAL_GPIO_TogglePin(LD1_GPIO_Port, LD1_Pin);
        HAL_Delay(250);
        break;
      }
      
      default:
        break;
    }
  }
}

/* USER CODE END 4 */

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x24000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_512KB;
  MPU_InitStruct.SubRegionDisable = 0x0;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Number = MPU_REGION_NUMBER1;
  MPU_InitStruct.BaseAddress = 0x90000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_256MB;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Number = MPU_REGION_NUMBER2;
  MPU_InitStruct.Size = MPU_REGION_SIZE_128MB;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Number = MPU_REGION_NUMBER3;
  MPU_InitStruct.BaseAddress = 0xD0000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_256MB;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Number = MPU_REGION_NUMBER4;
  MPU_InitStruct.Size = MPU_REGION_SIZE_32MB;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_CACHEABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM6 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM6)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
