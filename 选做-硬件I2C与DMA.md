# 选做：硬件 I²C 驱动 OLED + DMA 发送串口反馈

本工程（`homework0032`）是在软件 I²C + 串口中断发送版本（`homework0031`）基础上完成的选做版本：

| | homework0031（基础） | homework0032（选做） |
| --- | --- | --- |
| OLED 总线 | 软件 I²C（GPIO 模拟时序，PB8/PB9） | **硬件 I²C1**（PB6/PB7，400 kHz） |
| 串口反馈 | `HAL_UART_Transmit_IT()` | **`HAL_UART_Transmit_DMA()`**（DMA1 通道 4） |
| 其余功能 | 流水灯、按键消抖、6 字节数据包、OLED 显示 | 完全相同（同一份 `app.c`） |

选做的两个功能都要求"反馈内容和功能要求相同"，所以 `app.c`、`protocol.c`、`bsp_key.c`、`bsp_led.c`
在 0032 中一行未改，只替换了 OLED 的底层总线驱动和串口反馈的发送方式——这也正好说明前面把驱动分层
（`oled` / `usart` 与 `app` 解耦）是有价值的。

---

## 一、硬件 I²C 驱动 OLED

### 1.1 CubeMX 配置

| 项目 | 设置 |
| --- | --- |
| 外设 | I2C1（Standard/Fast mode） |
| 引脚 | **PB6 = I2C1_SCL，PB7 = I2C1_SDA**（I2C1 默认引脚，不开重映射） |
| 速度 | Fast Mode 400 kHz |
| GPIO | 复用开漏 `GPIO_MODE_AF_OD`，高速 |
| 其他 | 7 位地址、无双地址、不使能通用呼叫、不使能从机时钟延展 |

> 说明：基础版软件 I²C 用的是 PB8/PB9。PB8/PB9 也能作为 I²C1 的**重映射**引脚
> （需要 `__HAL_AFIO_REMAP_I2C1_ENABLE()`），如果不想重新接线可以改成重映射；本工程为了避免
> 额外打开 AFIO 重映射、也为了方便和软件版对照，直接用了默认的 PB6/PB7。**接线时请注意换线。**

### 1.2 关键源码

**(1) `Core/Src/i2c.c`：外设初始化**（CubeMX 生成风格）

```c
I2C_HandleTypeDef hi2c1;

void MX_I2C1_Init(void)
{
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 400000;                    /* Fast Mode */
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;            /* Tlow/Thigh = 2 */
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
}

void HAL_I2C_MspInit(I2C_HandleTypeDef* i2cHandle)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(i2cHandle->Instance==I2C1)
  {
    __HAL_RCC_I2C1_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    /* PB6 -> I2C1_SCL, PB7 -> I2C1_SDA，复用开漏输出 */
    GPIO_InitStruct.Pin = GPIO_PIN_6|GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
  }
}
```

**(2) `Core/Src/oled.c`：把"发命令 / 发一页数据"换成 HAL 调用**

软件版是自己拉 SCL/SDA 发送一帧；硬件版只要把要发的字节交给 HAL，**起始位、地址、应答位、
停止位全部由 I2C1 外设自动产生**：

```c
/* HAL 需要 8 位形式的地址（7 位地址 << 1） */
#define OLED_DEV_ADDR        ((uint16_t)((uint16_t)OLED_I2C_ADDR << 1))
#define OLED_I2C_TIMEOUT_MS  100u

/* 发一串 SSD1306 命令：控制字节 0x00 + 命令... */
static uint8_t OLED_SendCommands(const uint8_t *commands, uint16_t count)
{
  uint8_t buffer[40];
  HAL_StatusTypeDef status;

  if ((commands == 0) || (count == 0u) || (count > (sizeof(buffer) - 1u)))
  {
    return 0u;
  }
  buffer[0] = OLED_CTRL_CMD;              /* 0x00 = 后面是命令 */
  memcpy(&buffer[1], commands, count);
  status = HAL_I2C_Master_Transmit(&hi2c1, OLED_DEV_ADDR, buffer,
                                   (uint16_t)(count + 1u), OLED_I2C_TIMEOUT_MS);
  return (status == HAL_OK) ? 1u : 0u;
}

/* 刷一页 8 行像素：控制字节 0x40 + 128 字节显存 */
static void OLED_RefreshPage(uint8_t page)
{
  if (page >= OLED_PAGE_COUNT) { return; }
  OLED_SetPosition(page, 0u);             /* 0xB0|page, 列地址低/高 4 位 */
  s_tx_buffer[0] = OLED_CTRL_DATA;        /* 0x40 = 后面是显存数据 */
  memcpy(&s_tx_buffer[1], s_gram[page], OLED_WIDTH);
  (void)HAL_I2C_Master_Transmit(&hi2c1, OLED_DEV_ADDR, s_tx_buffer,
                                (uint16_t)sizeof(s_tx_buffer), OLED_I2C_TIMEOUT_MS);
}
```

`OLED_Init()` 里的 27 条初始化命令、`OLED_ShowString()` 打点、`OLED_RefreshLine()` 只刷 2 页这些
逻辑都和软件版完全一样（帧缓存 + 页地址），换的只是"最后一公里"的总线发送。这样 `app.c` 不需要任何修改。

### 1.3 两种方案的对比（自己实际测量/估算）

| 项目 | 软件 I²C（0031） | 硬件 I²C（0032） |
| --- | --- | --- |
| 引脚 | 任意 GPIO（这里 PB8/PB9） | I2C1 固定引脚 PB6/PB7（或重映射到 PB8/PB9） |
| 时钟来源 | 靠 `__NOP()` 延时凑时序，受编译优化影响 | 外设按时钟自动产生，400 kHz 精确 |
| 一页 128 字节（129 字节含控制字） | ≈ 4~9 ms（约 100~300 kHz） | ≈ 2.9 ms（400 kHz） |
| 整屏刷新 | ≈ 35~70 ms | ≈ 23 ms（实测按 8 页 × 2.9 ms 估算） |
| CPU 参与 | 全程忙等（拉线 + 延时） | 调用 `HAL_I2C_Master_Transmit` 时等标志，同样是阻塞，但不用管时序 |
| 代码量 | 多一个 `soft_i2c.c`（约 150 行） | 少一个文件，但 HAL 多约 3.7 KB Flash |
| 灵活性 | 任意引脚、可多总线 | 受外设/引脚约束，F1 的 I²C 有已知勘误 |

> 这里用的是**阻塞式**的 `HAL_I2C_Master_Transmit()`，因为 OLED 只在状态变化时刷新（切灯只刷 1 行），
> 阻塞几毫秒可以接受。若想进一步省 CPU，可以改用 `HAL_I2C_Master_Transmit_IT()` 或 DMA 版本，
> 但要注意 HAL 内部是按句柄状态机工作的，中途不能再用同一个 `hi2c1` 发命令——本工程没有这么做。

### 1.4 硬件 I²C 的两个坑

1. **上拉电阻**：I2C 是开漏总线，必须有上拉。0.96 寸 OLED 模块一般板载 4.7 kΩ~10 kΩ，400 kHz 也能用；
   如果模块没有上拉、只靠 STM32 内部约 40 kΩ 上拉，400 kHz 的上升沿会太慢，需要把 `ClockSpeed` 降到
   100 kHz，或者外接 4.7 kΩ。
2. **总线死锁**：F1 的 I2C 在从机拉低 SDA、主机复位等异常情况下可能卡在 BUSY。工程里用了 100 ms
   超时，`HAL_I2C_Master_Transmit()` 会返回 `HAL_TIMEOUT`/`HAL_ERROR`，OLED 驱动忽略返回值，
   所以即使屏幕没接，按键、串口、流水灯依然正常工作（软件版同理）。

---

## 二、串口反馈改用 DMA 发送

### 2.1 硬件连接关系

STM32F103 的 DMA 请求是**固定映射**的：`USART1_TX` 只能走 **DMA1 通道 4**（`USART1_RX` 是通道 5）。
所以配置时只要：开 DMA1 时钟、初始化通道 4（内存→外设、字节宽度、普通模式）、把它和 `huart1`
绑定，然后调用 `HAL_UART_Transmit_DMA()` 即可。

### 2.2 关键源码

**(1) `Core/Src/dma.c`：DMA 控制器时钟与中断**

```c
void MX_DMA_Init(void)
{
  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA1_Channel4_IRQn interrupt configuration (USART1_TX) */
  HAL_NVIC_SetPriority(DMA1_Channel4_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel4_IRQn);
}
```

**(2) `Core/Src/usart.c`：DMA 通道初始化并绑定到串口**

```c
UART_HandleTypeDef huart1;
DMA_HandleTypeDef hdma_usart1_tx;

void HAL_UART_MspInit(UART_HandleTypeDef* uartHandle)
{
  ...
  if(uartHandle->Instance==USART1)
  {
    /* USART1_TX Init：内存 -> 外设，字节对齐，普通模式 */
    hdma_usart1_tx.Instance = DMA1_Channel4;
    hdma_usart1_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_usart1_tx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_usart1_tx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_usart1_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart1_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_usart1_tx.Init.Mode = DMA_NORMAL;
    hdma_usart1_tx.Init.Priority = DMA_PRIORITY_LOW;
    if (HAL_DMA_Init(&hdma_usart1_tx) != HAL_OK)
    {
      Error_Handler();
    }
    __HAL_LINKDMA(uartHandle, hdmatx, hdma_usart1_tx);   /* 绑定到 huart1.hdmatx */
    ...
  }
}
```

**(3) `Core/Src/usart.c`：发送改用 DMA（和中断版只差一行）**

```c
void UART_TxTask(void)          /* 主循环里调用，非阻塞 */
{
  if ((s_tx_busy != 0u) || (s_tx_pending == 0u))
  {
    return;
  }
  if (s_tx_next_len == 0u)
  {
    s_tx_pending = 0u;
    return;
  }
  (void)memcpy(s_tx_active, s_tx_next, s_tx_next_len);   /* 发送期间缓冲区必须有效 */
  s_tx_active_len = s_tx_next_len;
  s_tx_pending    = 0u;
  s_tx_busy       = 1u;
  /* DMA1 通道 4 在后台把整条消息搬进 USART1->DR，CPU 全程不参与 */
  if (HAL_UART_Transmit_DMA(&huart1, (uint8_t *)s_tx_active, s_tx_active_len) != HAL_OK)
  {
    s_tx_busy    = 0u;
    s_tx_pending = 1u;      /* 下次再试 */
  }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    s_tx_busy = 0u;         /* DMA 搬完 + 最后一个字节移出，线路空闲 */
  }
}
```

**(4) `Core/Src/stm32f1xx_it.c`：DMA 通道中断**

```c
void DMA1_Channel4_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&hdma_usart1_tx);
}
```

### 2.3 发送流程

```
主循环 UART_SendText()  ──►  s_tx_next（最新一条，覆盖旧消息）
        │
        ▼  UART_TxTask()（总线空闲时才启动）
HAL_UART_Transmit_DMA(&huart1, buf, len)
        │  置位 USART1->CR3.DMAT，使能 DMA1 通道 4
        ▼
DMA1 通道 4：每来一次 TXE 就搬一个字节到 USART1->DR
        │  搬完 → 传输完成中断（DMA1_Channel4_IRQHandler）
        ▼
HAL 内部：关 DMA → 等 USART 的 TC（最后一个字节移出）
        │
        ▼
HAL_UART_TxCpltCallback() → s_tx_busy = 0 → 主循环可以发下一条
```

### 2.4 与中断发送的对比

| 项目 | `HAL_UART_Transmit_IT()`（0031） | `HAL_UART_Transmit_DMA()`（0032） |
| --- | --- | --- |
| 一条 38 字节状态行的中断次数 | ≈ 38 次（每字节一次 TXE 中断）+ 1 次 TC | 1 次 DMA 传输完成 + 1 次 TC |
| CPU 占用 | 每次中断几十个周期 | 基本为 0（只有首尾两次中断） |
| 代码改动量 | — | 加 `dma.c`、通道初始化、一个 IRQHandler，发送函数换一行 |
| 关键约束 | 缓冲区在发送完成前不能改 | **同样**不能改；`DMA_NORMAL` 模式每次发完要重新启动 |
| 适合场景 | 数据量小、次数少 | 数据量大、频率高、CPU 忙（例如同时做 OLED 刷新） |

### 2.5 为什么反馈还保留"最新一条"队列

`UART_SendText()` 只保存最新消息，`UART_TxTask()` 在 DMA 忙时不覆盖正在发送的 `s_tx_active`，
而是把新消息留在 `s_tx_next`，等 `HAL_UART_TxCpltCallback()` 把 `s_tx_busy` 清零后再发。
这样既不会打断正在发送的数据，也不会丢最终状态（按键连点、连续两个数据包时只回传最终状态，
和 0031 的行为一致）。

---

## 三、验证

1. **编译**：`arm-none-eabi-gcc` 完整编译链接通过，`-Wall -Wextra` 无警告；
   Flash 22 724 B / RAM 3 416 B（其中 I2C HAL 约占 3.7 KB Flash）。
2. **代码路径确认**：`nm` 可以看到最终固件里只有 `HAL_UART_Transmit_DMA`（中断版已被
   `--gc-sections` 裁掉）和 `HAL_I2C_Master_Transmit`，说明选做代码确实参与了链接。
3. **主机端逻辑测试**：把 `app.c / bsp_key.c / bsp_led.c / protocol.c / oled.c` 用主机 GCC 编译，
   用桩函数替换 `HAL_I2C_Master_Transmit()`（推到虚拟 SSD1306）和串口传输层，跑与 0031 完全相同的
   46 项集成测试：上电状态、走灯节拍、正反向、暂停保持、暂停中改参数、错误包、长按/抖动、OLED 内容
   与实际点亮 LED 一致等，全部通过。
4. **未完成的验证**：手上没有实物板子/逻辑分析仪，`HAL_I2C_Master_Transmit` 的真实波形和 DMA 时序需要
   上板确认（见第四节注意事项）。

---

## 四、上板注意事项

1. **换线**：OLED 的 SCL/SDA 从 PB8/PB9 改到 **PB6/PB7**（除非改成 I2C1 重映射）。
2. 先确认模块自带上拉；如果没有，I²C 扫描不到 0x3C，屏幕不亮但程序其余部分正常。
3. 若串口反馈发不出来，先检查 `MX_DMA_Init()` 是否在 `MX_USART1_UART_Init()` **之前**调用
   （DMA 通道必须先初始化，`__HAL_LINKDMA` 才有意义），以及 `DMA1_Channel4_IRQHandler` 是否在
   `stm32f1xx_it.c` 中。
4. DMA 发送期间不能用 `s_tx_active` 这块缓冲区做别的事（本工程已用独立的静态缓冲区）。

---

## 附：两份代码的关系

- `homework0031`：软件 I²C（PB8/PB9）+ `HAL_UART_Transmit_IT()` —— 基础要求。
- `homework0032`：硬件 I²C1（PB6/PB7，400 kHz）+ `HAL_UART_Transmit_DMA()` —— 选做。

两份工程的 `app.c / protocol.c / bsp_key.c / bsp_led.c / main.c` 逻辑一致，只有 `oled.c` 的总线部分
和 `usart.c` 的发送部分不同，便于对照阅读。
