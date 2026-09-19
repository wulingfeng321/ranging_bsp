/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "adc.h"
#include "crc.h"
#include "dcmi.h"
#include "dma2d.h"
#include "fatfs.h"
#include "i2c.h"
#include "ltdc.h"
#include "lwip.h"
#include "quadspi.h"
#include "rtc.h"
#include "sai.h"
#include "sdmmc.h"
#include "spdifrx.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "usb_otg.h"
#include "gpio.h"
#include "fmc.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "app_net.h"
#include "app_range.h"
#include "stm32746g_discovery_lcd.h"
#include "app_mic_scope.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* BSP owns LCD/audio startup. Skip unrelated generated peripherals (e.g. SD).
 * Ethernet is initialized independently below in either mode. */
#define LCD_GRID_DEMO_ONLY  1
#define LCD_GRID_STEP       20U
#define LCD_GRID_LAYER      0U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void PeriphCommonClock_Config(void);
/* USER CODE BEGIN PFP */
static void LCD_FramebufferMPU_Config(void);
static void LCD_Grid_Init(void);
static void LCD_DrawLowerHalfGrid(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* PeriphCommonClock_Config already supplies 384 / 5 / 8 = 9.6 MHz.
 * Keep that PLLSAI configuration, including its separate 48 MHz output. */
void BSP_LCD_ClockConfig(LTDC_HandleTypeDef *handle, void *params)
{
  (void)handle;
  (void)params;
}

static void LCD_FramebufferMPU_Config(void)
{
  MPU_Region_InitTypeDef region = {0};

  /* Normal, non-cacheable SDRAM: CPU, DMA2D and LTDC see the same pixels. */
  HAL_MPU_Disable();
  region.Enable = MPU_REGION_ENABLE;
  region.Number = MPU_REGION_NUMBER0;
  region.BaseAddress = LCD_FB_START_ADDRESS;
  region.Size = MPU_REGION_SIZE_8MB;
  region.SubRegionDisable = 0;
  region.TypeExtField = MPU_TEX_LEVEL1;
  region.AccessPermission = MPU_REGION_FULL_ACCESS;
  region.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  region.IsShareable = MPU_ACCESS_SHAREABLE;
  region.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  region.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
  HAL_MPU_ConfigRegion(&region);
  /* Reserved by stm32f746xx_ethernet.icf: RX pool, LwIP heap and descriptors. */
  region.Number = MPU_REGION_NUMBER1;
  region.BaseAddress = 0x20040000U;
  region.Size = MPU_REGION_SIZE_64KB;
  HAL_MPU_ConfigRegion(&region);
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

static void LCD_DrawLowerHalfGrid(void)
{
  uint32_t x;
  uint32_t y;
  const uint32_t width = BSP_LCD_GetXSize();
  const uint32_t height = BSP_LCD_GetYSize();
  const uint32_t top = height / 2U;

  BSP_LCD_SetTextColor(LCD_COLOR_WHITE);
  for (x = 0; x < width; x += LCD_GRID_STEP)
  {
    BSP_LCD_DrawVLine((uint16_t)x, (uint16_t)top,
                     (uint16_t)(height - top));
  }
  for (y = top; y < height; y += LCD_GRID_STEP)
  {
    BSP_LCD_DrawHLine(0, (uint16_t)y, (uint16_t)width);
  }
  /* Close the right/bottom edges without writing outside the framebuffer. */
  BSP_LCD_DrawVLine((uint16_t)(width - 1U), (uint16_t)top,
                   (uint16_t)(height - top));
  BSP_LCD_DrawHLine(0, (uint16_t)(height - 1U), (uint16_t)width);
}

static void LCD_Grid_Init(void)
{
  if (BSP_LCD_Init() != LCD_OK)
  {
    Error_Handler();
  }
  BSP_LCD_DisplayOff();
  BSP_LCD_LayerDefaultInit(LCD_GRID_LAYER, LCD_FB_START_ADDRESS);
  BSP_LCD_SelectLayer(LCD_GRID_LAYER);
  BSP_LCD_Clear(LCD_COLOR_BLACK);
  LCD_DrawLowerHalfGrid();
  __DSB();
  BSP_LCD_DisplayOn();
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  LCD_FramebufferMPU_Config();
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* Configure the peripherals common clocks */
  PeriphCommonClock_Config();

  /* USER CODE BEGIN SysInit */
  /* Keep the BSP display/audio and Ethernet test independent of an SD card.
   * The conditional spans unrelated CubeMX peripheral startup calls. */
#if !LCD_GRID_DEMO_ONLY
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_ADC3_Init();
  MX_CRC_Init();
  MX_DCMI_Init();
  MX_DMA2D_Init();
  MX_FMC_Init();
  MX_I2C1_Init();
  MX_I2C3_Init();
  MX_LTDC_Init();
  MX_QUADSPI_Init();
  MX_RTC_Init();
  MX_SAI2_Init();
  MX_SDMMC1_SD_Init();
  MX_SPDIFRX_Init();
  MX_SPI2_Init();
  MX_TIM1_Init();
  MX_TIM2_Init();
  MX_TIM3_Init();
  MX_TIM5_Init();
  MX_TIM8_Init();
  MX_TIM12_Init();
  MX_USART1_UART_Init();
  MX_USART6_UART_Init();
  MX_FATFS_Init();
  MX_USB_OTG_FS_HCD_Init();
  /* USER CODE BEGIN 2 */
#else
  /* Generated init functions remain available for later CubeMX work.
   * These references make the intentionally unused functions explicit;
   * no peripheral is started by a function-designator expression. */
  (void)MX_ADC3_Init;
  (void)MX_CRC_Init;
  (void)MX_DCMI_Init;
  (void)MX_DMA2D_Init;
  /* LwIP now initializes ETH through ethernetif.c; MX_ETH_Init no longer exists. */
  (void)MX_FMC_Init;
  (void)MX_I2C1_Init;
  (void)MX_I2C3_Init;
  (void)MX_LTDC_Init;
  (void)MX_QUADSPI_Init;
  (void)MX_RTC_Init;
  (void)MX_SAI2_Init;
  (void)MX_SDMMC1_SD_Init;
  (void)MX_SPDIFRX_Init;
  (void)MX_SPI2_Init;
  (void)MX_TIM1_Init;
  (void)MX_TIM2_Init;
  (void)MX_TIM3_Init;
  (void)MX_TIM5_Init;
  (void)MX_TIM8_Init;
  (void)MX_TIM12_Init;
  (void)MX_USART1_UART_Init;
  (void)MX_USART6_UART_Init;
  (void)MX_USB_OTG_FS_HCD_Init;
  MX_GPIO_Init();
#endif
  MX_LWIP_Init();
  AppNet_Init();
  AppRange_Init();
  LCD_Grid_Init();
  MicScope_Init();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    MX_LWIP_Process();
    AppNet_Process();
    AppRange_Process();
    MicScope_Process();
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

  /** Configure LSE Drive Capability
  */
  HAL_PWR_EnableBkUpAccess();

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 25;
  RCC_OscInitStruct.PLL.PLLN = 400;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Activate the Over-Drive mode
  */
  if (HAL_PWREx_EnableOverDrive() != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_6) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief Peripherals Common Clock Configuration
  * @retval None
  */
void PeriphCommonClock_Config(void)
{
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  /** Initializes the peripherals clock
  */
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_LTDC|RCC_PERIPHCLK_SAI2
                              |RCC_PERIPHCLK_SDMMC1|RCC_PERIPHCLK_CLK48;
  PeriphClkInitStruct.PLLSAI.PLLSAIN = 384;
  PeriphClkInitStruct.PLLSAI.PLLSAIR = 5;
  PeriphClkInitStruct.PLLSAI.PLLSAIQ = 2;
  PeriphClkInitStruct.PLLSAI.PLLSAIP = RCC_PLLSAIP_DIV8;
  PeriphClkInitStruct.PLLSAIDivQ = 1;
  PeriphClkInitStruct.PLLSAIDivR = RCC_PLLSAIDIVR_8;
  PeriphClkInitStruct.Sai2ClockSelection = RCC_SAI2CLKSOURCE_PLLSAI;
  PeriphClkInitStruct.Clk48ClockSelection = RCC_CLK48SOURCE_PLLSAIP;
  PeriphClkInitStruct.Sdmmc1ClockSelection = RCC_SDMMC1CLKSOURCE_CLK48;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

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

#ifdef  USE_FULL_ASSERT
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
