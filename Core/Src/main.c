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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "oled.h"
#include <stdio.h>
#include <string.h>
#include "protocol.h"
#include "feedback.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef struct {
  uint8_t run;   /* 1=运行, 0=暂停 */
  uint8_t dir;   /* 0=正向(PA0→PA1→PA2), 1=反向 */
  uint8_t led;   /* 当前点亮的 LED: 0/1/2 */
  uint16_t step_ms;
} sys_t;

typedef struct {
  GPIO_TypeDef *port;
  uint16_t      pin;
  uint8_t       stable;   /* 消抖后的状态: 1=按下 */
  uint8_t       raw;      /* 上一次原始采样 */
  uint8_t       cnt;      /* 连续相同次数 */
  void        (*on_press)(void);  /* 按下瞬间执行一次 */
} btn_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
#define LED_NUM          3
#define KEY_SCAN_MS      5    /* 每 5ms 扫一次 */
#define KEY_DEBOUNCE_CNT 4    /* 连续 4 次 = 20ms 才算稳定 */
#define LED_STEP_MS      300  /* 先写死 300ms，串口那步再改成变量 */
#define LED_ON   GPIO_PIN_RESET   /* 低电平点亮 */
#define LED_OFF  GPIO_PIN_SET
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
static sys_t sys = {1, 0, 0,300};   /* 上电：运行、正向、第 1 颗 */
static uint32_t last_step = 0;
static btn_t key_dir;
static btn_t key_run;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */
static void ui_poll(void);
static void led_show(uint8_t idx);
static void led_advance(void);
static void key_scan(btn_t *k);
static void on_key_dir(void);
static void on_key_run(void);
static void app_tick(uint32_t now);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void) {
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  key_dir.port = KEY_DIR_GPIO_Port;
  key_dir.pin  = KEY_DIR_Pin;
  key_dir.on_press = on_key_dir;

  key_run.port = KEY_RUN_GPIO_Port;
  key_run.pin  = KEY_RUN_Pin;
  key_run.on_press = on_key_run;

  OLED_Init();
  protocol_start();
  feedback_mark();
  ui_poll();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    uint32_t now = HAL_GetTick();

    /* 5ms 扫一次按键（不用 HAL_Delay，主循环不被卡住） */
    static uint32_t key_tick = 0;
    if ((uint32_t)(now - key_tick) >= KEY_SCAN_MS) {
      key_tick = now;
      key_scan(&key_dir);
      key_scan(&key_run);
    }

    /* 流水灯推进 */
    app_tick(now);
    uint8_t  p_run, p_dir;
    uint16_t p_step;
    if (protocol_poll(&p_run, &p_dir, &p_step)) {
      sys.run     = p_run;
      sys.dir     = p_dir;
      sys.step_ms = p_step;
      last_step   = now;
      feedback_mark();
    }
    ui_poll();
    feedback_poll(sys.run, sys.dir, sys.step_ms, (uint8_t)(sys.led + 1u));
    /* USER CODE END 3 */
  }
}

  /**
    * @brief System Clock Configuration
    * @retval None
    */
  void SystemClock_Config(void)
  {
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /** Initializes the RCC Oscillators according to the specified parameters
    * in the RCC_OscInitTypeDef structure.
    */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

    /** Initializes the CPU, AHB and APB buses clocks
    */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                                |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
    {
      Error_Handler();
    }
  }

  /**
    * @brief USART1 Initialization Function
    * @param None
    * @retval None
    */
  static void MX_USART1_UART_Init(void)
  {

    /* USER CODE BEGIN USART1_Init 0 */

    /* USER CODE END USART1_Init 0 */

    /* USER CODE BEGIN USART1_Init 1 */

    /* USER CODE END USART1_Init 1 */
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart1) != HAL_OK)
    {
      Error_Handler();
    }
    /* USER CODE BEGIN USART1_Init 2 */

    /* USER CODE END USART1_Init 2 */

  }

  /**
    * @brief GPIO Initialization Function
    * @param None
    * @retval None
    */
  static void MX_GPIO_Init(void)
  {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    /* USER CODE BEGIN MX_GPIO_Init_1 */

    /* USER CODE END MX_GPIO_Init_1 */

    /* GPIO Ports Clock Enable */
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /*Configure GPIO pin Output Level */
    HAL_GPIO_WritePin(GPIOA, LED0_Pin|LED1_Pin|LED2_Pin, GPIO_PIN_RESET);

    /*Configure GPIO pin Output Level */
    HAL_GPIO_WritePin(GPIOB, OLED_SCK_Pin|OLED_SDA_Pin, GPIO_PIN_SET);

    /*Configure GPIO pins : LED0_Pin LED1_Pin LED2_Pin */
    GPIO_InitStruct.Pin = LED0_Pin|LED1_Pin|LED2_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /*Configure GPIO pins : KEY_DIR_Pin KEY_RUN_Pin */
    GPIO_InitStruct.Pin = KEY_DIR_Pin|KEY_RUN_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /*Configure GPIO pins : OLED_SCK_Pin OLED_SDA_Pin */
    GPIO_InitStruct.Pin = OLED_SCK_Pin|OLED_SDA_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* USER CODE BEGIN MX_GPIO_Init_2 */

    /* USER CODE END MX_GPIO_Init_2 */
  }

  /* USER CODE BEGIN 4 */
/* 按"当前下标"整体刷新，保证同一时刻只有一颗亮 */
static void led_show(uint8_t idx)
{
  HAL_GPIO_WritePin(GPIOA, LED0_Pin, (idx == 0) ? LED_ON : LED_OFF);
  HAL_GPIO_WritePin(GPIOA, LED1_Pin, (idx == 1) ? LED_ON : LED_OFF);
  HAL_GPIO_WritePin(GPIOA, LED2_Pin, (idx == 2) ? LED_ON : LED_OFF);
}

/* 按方向走一步：正向 +1，反向 -1，取模循环 */
static void led_advance(void)
{
  if (sys.dir == 0)
    sys.led = (uint8_t)((sys.led + 1u) % LED_NUM);
  else
    sys.led = (uint8_t)((sys.led + LED_NUM - 1u) % LED_NUM);
  led_show(sys.led);
}

/* 消抖状态机：只在"稳定变为按下"那一刻回调一次 */
static void key_scan(btn_t *k)
{
  uint8_t raw = (HAL_GPIO_ReadPin(k->port, k->pin) == GPIO_PIN_RESET) ? 1u : 0u;

  if (raw != k->raw) {        /* 电平变了，重新计时 */
    k->raw = raw;
    k->cnt = 0;
    return;
  }
  if (k->cnt < KEY_DEBOUNCE_CNT) {
    if (++k->cnt == KEY_DEBOUNCE_CNT && raw != k->stable) {
      k->stable = raw;
      if (raw && k->on_press) k->on_press();   /* 只在按下沿触发 */
    }
  }
}

/* 两个按键各自的动作：只改状态，不碰硬件 */
static void on_key_dir(void) { sys.dir ^= 1u; feedback_mark();}   /* 换向 */
static void on_key_run(void) { sys.run ^= 1u; feedback_mark();}   /* 暂停 / 继续 */

/* 非阻塞定时：到点走一步 */
static void app_tick(uint32_t now)
{
  if (!sys.run) return;                                  /* 暂停：不推进，但按键照常响应 */
  if ((uint32_t)(now - last_step) <  sys.step_ms) return;
  last_step = now;
  led_advance();
}
/* 把 sys 里的状态画到 OLED；只有文字真的变了才写屏 */
static void ui_poll(void)
{
  static char last[4][20];
  char buf[4][20];
  uint8_t changed = 0;

  snprintf(buf[0], sizeof buf[0], "STATE: %s",  sys.run ? "RUN" : "STOP");
  snprintf(buf[1], sizeof buf[1], "DIR: %s",    sys.dir ? "REV" : "FWD");
  snprintf(buf[2], sizeof buf[2], "STEP: %ums", (unsigned)sys.step_ms);
  snprintf(buf[3], sizeof buf[3], "LED: %u",    (unsigned)(sys.led + 1u));

  for (uint8_t i = 0; i < 4u; i++) {
    if (strcmp(buf[i], last[i]) != 0) {
      OLED_ShowLine(i, buf[i]);
      memcpy(last[i], buf[i], sizeof last[i]);
      changed = 1;
    }
  }
  if (changed) (void)OLED_Refresh();
}
  /* USER CODE END 4 */

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
