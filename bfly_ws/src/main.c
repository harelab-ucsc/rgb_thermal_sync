/* ==========================================================
 * Boson 60Hz → STM32 F411RE → Blackfly S trigger
 * VPC3 EXT_SYNC (3.3V) → PA0 (TIM2_ETR)
 * Divided trigger output → PA6 (TIM3_CH1 PWM)
 *
 * Change DIVIDE_BY to select Blackfly frame rate:
 *   1=60Hz  2=30Hz  3=20Hz  4=15Hz  6=10Hz  12=5Hz
 * ========================================================== */

#include "main.h"
#include <string.h>
#include <stdio.h>

/* ── User config ─────────────────────────────────────────── */
#define DIVIDE_BY       10u      /* 60/2 = 30 Hz to Blackfly  */
#define PULSE_WIDTH_US  500u    /* Trigger pulse width in µs  */

/* ── Peripheral handles ──────────────────────────────────── */
TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim3;
UART_HandleTypeDef huart2;

/* ── Prototypes ──────────────────────────────────────────── */
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM2_Init(void);
static void MX_TIM3_Init(void);
static void MX_USART2_UART_Init(void);

/* ============================================================ */
int main(void)
{
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_USART2_UART_Init();
    MX_TIM3_Init();   /* Init TIM3 before TIM2 so slave is ready */
    MX_TIM2_Init();   /* TIM2 starts counting Boson pulses last   */

    /* Start TIM3 PWM — idles until triggered by TIM2 TRGO */
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);

    /* Start TIM2 counting edges on PA0 from VPC3 EXT_SYNC */
    HAL_TIM_Base_Start(&htim2);

    char msg[64];
    snprintf(msg, sizeof(msg),
        "Running: Boson 60Hz / %u = %u Hz to Blackfly\r\n",
        DIVIDE_BY, 60u / DIVIDE_BY);
    HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), 200);

    while (1)
    {
        /* Nothing needed in main loop.
         * All timing is handled in hardware by TIM2 + TIM3.
         * Add application logic here if needed (e.g. frame counter). */
        HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5); /* LD2 heartbeat */
        HAL_Delay(500);
    }
}

/* ============================================================
 * TIM2 — External clock via ETR (PA0)
 * Each rising edge on PA0 = one Boson frame pulse.
 * When CNT reaches ARR (= DIVIDE_BY - 1), it resets
 * and fires a TRGO Update event → triggers TIM3.
 * ============================================================ */
static void MX_TIM2_Init(void)
{
    TIM_ClockConfigTypeDef  sClk    = {0};
    TIM_MasterConfigTypeDef sMaster = {0};

    htim2.Instance               = TIM2;
    htim2.Init.Prescaler         = 0;
    htim2.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim2.Init.Period            = DIVIDE_BY - 1;  /* ARR */
    htim2.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_Base_Init(&htim2);

    /* Clock source: ETR mode 2 on PA0 */
    sClk.ClockSource    = TIM_CLOCKSOURCE_ETRMODE2;
    sClk.ClockPolarity  = TIM_CLOCKPOLARITY_NONINVERTED; /* rising edge */
    sClk.ClockPrescaler = TIM_CLOCKPRESCALER_DIV1;
    sClk.ClockFilter    = 4;  /* filter: 4 APB cycles, removes glitches */
    HAL_TIM_ConfigClockSource(&htim2, &sClk);

    /* TRGO = Update → feeds TIM3 slave input ITR1 */
    sMaster.MasterOutputTrigger = TIM_TRGO_UPDATE;
    sMaster.MasterSlaveMode     = TIM_MASTERSLAVEMODE_ENABLE;
    HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMaster);
}

/* ============================================================
 * TIM3 — One-shot PWM output on PA6
 * Slave mode: triggered by TIM2 via ITR1.
 * Each trigger fires a single pulse of PULSE_WIDTH_US on PA6.
 * TIM3 runs at 84 MHz / (PSC+1) = 1 MHz → 1 µs per tick.
 * ============================================================ */
static void MX_TIM3_Init(void)
{
    TIM_SlaveConfigTypeDef  sSlave  = {0};
    TIM_MasterConfigTypeDef sMaster = {0};
    TIM_OC_InitTypeDef      sOC     = {0};

    htim3.Instance               = TIM3;
    htim3.Init.Prescaler         = 83;           /* 84MHz / 84 = 1MHz  */
    htim3.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim3.Init.Period            = PULSE_WIDTH_US - 1; /* ticks = µs    */
    htim3.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_PWM_Init(&htim3);

    /* Slave: restart on trigger from ITR1 (= TIM2 on F411RE) */
    sSlave.SlaveMode       = TIM_SLAVEMODE_TRIGGER;  /* retrigger mode  */
    sSlave.InputTrigger    = TIM_TS_ITR1;            /* ITR1 = TIM2     */
    sSlave.TriggerPolarity = TIM_TRIGGERPOLARITY_RISING;
    HAL_TIM_SlaveConfigSynchro(&htim3, &sSlave);

    sMaster.MasterOutputTrigger = TIM_TRGO_RESET;
    sMaster.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;
    HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMaster);

    /* PWM CH1 on PA6: HIGH for entire period = pulse width */
    sOC.OCMode       = TIM_OCMODE_PWM1;
    sOC.Pulse        = PULSE_WIDTH_US - 1;
    sOC.OCPolarity   = TIM_OCPOLARITY_HIGH;
    sOC.OCFastMode   = TIM_OCFAST_DISABLE;
    HAL_TIM_PWM_ConfigChannel(&htim3, &sOC, TIM_CHANNEL_1);
}

/* ============================================================
 * GPIO
 * PA0 — TIM2_ETR  (input from VPC3 EXT_SYNC)
 * PA2 — USART2_TX (ST-Link virtual COM, debug)
 * PA5 — LD2 LED   (heartbeat)
 * PA6 — TIM3_CH1  (PWM trigger output to Blackfly)
 * ============================================================ */
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* PA0: TIM2_ETR input */
    g.Pin       = GPIO_PIN_0;
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF1_TIM2;
    HAL_GPIO_Init(GPIOA, &g);

    /* PA5: onboard LED LD2 (heartbeat) */
    g.Pin   = GPIO_PIN_5;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &g);

    /* PA6: TIM3_CH1 PWM output to Blackfly */
    g.Pin       = GPIO_PIN_6;
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF2_TIM3;
    HAL_GPIO_Init(GPIOA, &g);
}

/* ============================================================
 * USART2 — debug output via ST-Link USB virtual COM
 * Open a serial terminal at 115200 8N1 to see status messages
 * ============================================================ */
static void MX_USART2_UART_Init(void)
{
    huart2.Instance          = USART2;
    huart2.Init.BaudRate     = 115200;
    huart2.Init.WordLength   = UART_WORDLENGTH_8B;
    huart2.Init.StopBits     = UART_STOPBITS_1;
    huart2.Init.Parity       = UART_PARITY_NONE;
    huart2.Init.Mode         = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart2);
}

/* ============================================================
 * SystemClock_Config — 84 MHz using HSI + PLL
 * APB1 timers (TIM3) run at 84 MHz (2x APB1 = 42x2)
 * APB2 timers (TIM2) run at 84 MHz
 * ============================================================ */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState       = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState   = RCC_PLL_ON;
    osc.PLL.PLLSource  = RCC_PLLSOURCE_HSI;
    osc.PLL.PLLM       = 16;
    osc.PLL.PLLN       = 336;
    osc.PLL.PLLP       = RCC_PLLP_DIV4;
    osc.PLL.PLLQ       = 7;
    HAL_RCC_OscConfig(&osc);

    clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                       | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2);
}