#include "stm32f4xx_hal.h"

TIM_HandleTypeDef htim1;   // 60 Hz source
TIM_HandleTypeDef htim2;   // step pulses to DM542T (phase-locked to TIM3)
TIM_HandleTypeDef htim3;   // divider

// DM542T set to 800 steps/rev (1/4 microstep)
// Step frequency = 800 * 6 Hz = 4800 Hz
// TIM2 clock = 84 MHz, PSC=69 -> tick = 1.2 MHz
// ARR = 1,200,000 / 4800 - 1 = 249
#define TIM2_PSC  69
#define TIM2_ARR  249

// motor spin direction: 0/1
#define DIR_LEVEL  1

static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    osc.PLL.PLLM = 16;
    osc.PLL.PLLN = 336;
    osc.PLL.PLLP = RCC_PLLP_DIV4;
    osc.PLL.PLLQ = 7;
    HAL_RCC_OscConfig(&osc);

    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2);
}

static void GPIO_Init(void)
{
    GPIO_InitTypeDef g = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    // PD2 = TIM3_ETR input
    g.Pin = GPIO_PIN_2;
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_PULLDOWN;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    g.Alternate = GPIO_AF2_TIM3;
    HAL_GPIO_Init(GPIOD, &g);

    // PB4 = TIM3_CH1 divided output
    g.Pin = GPIO_PIN_4;
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF2_TIM3;
    HAL_GPIO_Init(GPIOB, &g);

    // PA8 = TIM1_CH1 60 Hz output
    g.Pin = GPIO_PIN_8;
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    g.Alternate = GPIO_AF1_TIM1;
    HAL_GPIO_Init(GPIOA, &g);

    // PA1 = TIM2_CH2 step pulses to DM542T PUL+
    g.Pin = GPIO_PIN_1;
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF1_TIM2;
    HAL_GPIO_Init(GPIOA, &g);

    // PA3 = DIR output to DM542T DIR+
    g.Pin = GPIO_PIN_3;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &g);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_3,
        DIR_LEVEL ? GPIO_PIN_SET : GPIO_PIN_RESET);

    // PA5 = LD2 pin, leave as input/high-Z
    g.Pin = GPIO_PIN_5;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &g);
}

static void TIM1_Init(void)
{
    TIM_OC_InitTypeDef oc = {0};

    __HAL_RCC_TIM1_CLK_ENABLE();

    htim1.Instance = TIM1;
    htim1.Init.Prescaler = 14000 - 1;
    htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim1.Init.Period = 101 - 1;  // 59.4 Hz 
    htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim1.Init.RepetitionCounter = 0;
    htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    HAL_TIM_Base_Init(&htim1);
    HAL_TIM_PWM_Init(&htim1);

    oc.OCMode = TIM_OCMODE_PWM1;
    oc.Pulse = 6;   // narrow pulse (6/6000 Hz tick)
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    oc.OCIdleState = TIM_OCIDLESTATE_RESET;
    oc.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    HAL_TIM_PWM_ConfigChannel(&htim1, &oc, TIM_CHANNEL_1);
}

static void TIM3_Init(void)
{
    TIM_ClockConfigTypeDef clk = {0};
    TIM_OC_InitTypeDef oc = {0};

    __HAL_RCC_TIM3_CLK_ENABLE();

    htim3.Instance = TIM3;
    htim3.Init.Prescaler = 0;
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim3.Init.Period = 9;   // divide by 10
    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_Base_Init(&htim3);

    clk.ClockSource = TIM_CLOCKSOURCE_ETRMODE2;
    clk.ClockPolarity = TIM_CLOCKPOLARITY_NONINVERTED;
    clk.ClockPrescaler = TIM_CLOCKPRESCALER_DIV1;
    clk.ClockFilter = 0;
    HAL_TIM_ConfigClockSource(&htim3, &clk);

    HAL_TIM_PWM_Init(&htim3);

    oc.OCMode = TIM_OCMODE_PWM1;
    oc.Pulse = 1;   // 30 for ~50% duty cycle (half of htim3.Init.Period)
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    HAL_TIM_PWM_ConfigChannel(&htim3, &oc, TIM_CHANNEL_1);

    htim3.Instance->EGR = TIM_EGR_UG;

    // Output TIM3 update event as TRGO -> resets TIM2 every 6 Hz period
    TIM_MasterConfigTypeDef master = {0};
    master.MasterOutputTrigger = TIM_TRGO_UPDATE;
    master.MasterSlaveMode     = TIM_MASTERSLAVEMODE_ENABLE;
    HAL_TIMEx_MasterConfigSynchronization(&htim3, &master);
}

static void TIM2_Init(void)
{
    TIM_OC_InitTypeDef     oc    = {0};
    TIM_SlaveConfigTypeDef slave = {0};

    __HAL_RCC_TIM2_CLK_ENABLE();

    htim2.Instance               = TIM2;
    htim2.Init.Prescaler         = TIM2_PSC;
    htim2.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim2.Init.Period            = TIM2_ARR;
    htim2.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_PWM_Init(&htim2);

    // Receiver: reset TIM2 counter on TIM3 TRGO (every 6 Hz) — phase lock
    // ITR2 = TIM3 for TIM2 on STM32F4
    slave.SlaveMode       = TIM_SLAVEMODE_RESET;
    slave.InputTrigger    = TIM_TS_ITR2;
    slave.TriggerPolarity = TIM_TRIGGERPOLARITY_NONINVERTED;
    slave.TriggerFilter   = 0;
    HAL_TIM_SlaveConfigSynchro(&htim2, &slave);

    // PWM on CH2 (PA1), 50% duty cycle
    oc.OCMode     = TIM_OCMODE_PWM1;
    oc.Pulse      = TIM2_ARR / 2;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    HAL_TIM_PWM_ConfigChannel(&htim2, &oc, TIM_CHANNEL_2);
}

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    GPIO_Init();
    TIM1_Init();
    TIM3_Init();
    TIM2_Init();

    __HAL_TIM_SET_COUNTER(&htim3, 0);
    __HAL_TIM_SET_COUNTER(&htim2, 0);

    HAL_TIM_Base_Start(&htim3);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);  // PB4 — BFS trigger
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2);  // PA1 — motor steps

    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    __HAL_TIM_MOE_ENABLE(&htim1);

    while (1)
    {
    }
}
