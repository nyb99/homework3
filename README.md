# 电控组第三次培训作业

基于 STM32F103C8T6（LQFP48）的流水灯：按键 + 串口数据包控制，**硬件 I²C1** 驱动 OLED 显示状态，
**DMA** 回传串口状态。—`homework0032`工程是**选做版本**，基础版本（软件 I²C + 串口中断发送）见 `homework0031`。
选做的原理与关键源码见 [docs/选做-硬件I2C与DMA.md]。

- 姓名：宁彦博
- 学号：20253003139
- 开发环境：STM32CubeMX 6.15.0 + STM32CubeIDE（GNU Arm 工具链），HAL 库版本 FW_F1 V1.8.7
- 主控：STM32F103C8T6 最小系统板，8 MHz 外部晶振，系统时钟 72 MHz

---

## 一、功能实现

| 文档要求 | 实现方式 |
| --- | --- |
| 至少 3 颗 LED 流水灯，同一时刻只亮一颗 | `bsp_led.c`：`LED_Show()` 先全部熄灭再点亮目标 LED |
| 一个按键切换正向/反向，另一个切换暂停/继续 | KEY1（PB11）切换方向，KEY2（PB1）切换运行/暂停 |
| 暂停时保留当前 LED，恢复后按当前方向继续；暂停期间按键和串口仍有效 | 运行状态与 LED 编号分开保存，暂停只是停止走时；按键与串口处理不受暂停影响 |
| 按键消抖，按一次只触发一次 | EXTI 只置位"有下降沿"标志，主循环 `KEY_Scan()` 用 20 ms 状态机确认按下/释放，每按一次只产生一个事件 |
| 串口接收中断，连续接收多个完整数据包 | `usart.c` 每字节中断接收 → 128 字节环形缓冲 → 主循环逐字节喂给状态机解析器 |
| 自定义结构体解析数据包、计算并比较校验和 | `protocol.h` 中 `Packet_t`（结构体 + 字节数组联合体）、`Packet_Checksum()`、`Packet_Check()` |
| 帧头或校验和不对时不处理命令 | 校验失败直接丢弃并回传 `ERR=...`，运行状态、方向、间隔均不改变 |
| 软件 I²C 驱动 OLED，显示 4 项状态 | **硬件 I²C1**（PB6/PB7，400 kHz）+ `oled.c` 帧缓存、按行刷新（选做） |
| `HAL_UART_Transmit_IT()` 中断回传反馈，按键改变状态/方向也回传 | **`HAL_UART_Transmit_DMA()`**（DMA1 通道 4）+ 发送忙时保留最新一条（选做） |
| 按键和串口操作同一份状态，效果/显示/反馈一致 | `app.c` 中唯一的 `AppState_t s_app` 是唯一数据源 |
| 中断只做接收/记录，校验、状态更新、刷新放主循环 | 见下文"程序结构" |

### 引脚分配

| 功能 | 引脚 | 模式 |
| --- | --- | --- |
| LED0 / LED1 / LED2 | PA0 / PA1 / PA2 | 推挽输出（高电平点亮） |
| KEY1（切换方向） | PB11 | 上拉输入 + EXTI11 下降沿中断 |
| KEY2（运行/暂停） | PB1 | 上拉输入 + EXTI1 下降沿中断 |
| USART1_TX | PA9 | 复用推挽输出（DMA1 通道 4 发送） |
| USART1_RX | PA10 | 浮空输入 |
| OLED_SCL | PB6 | 复用开漏（I2C1_SCL，400 kHz） |
| OLED_SDA | PB7 | 复用开漏（I2C1_SDA，400 kHz） |

> LED 与按键引脚完全按题目给出的引脚图配置（PA0/PA1/PA2、PB1/PB11）。引脚图里没有串口和 OLED，
> 这里选 USART1 的默认引脚 PA9/PA10，OLED 用 I2C1 的默认引脚 PB6/PB7。
> **与基础版（0031）相比 OLED 换了引脚：软件 I²C 用的是 PB8/PB9，硬件 I²C 默认引脚是 PB6/PB7，接线时要换过来**；
> 如果不想换线，也可以把 I²C1 重映射到 PB8/PB9（`__HAL_AFIO_REMAP_I2C1_ENABLE()`）。
> 若开发板 LED 为低电平点亮，只需把 `bsp_led.h` 里的 `LED_LEVEL_ON` 改成 `GPIO_PIN_RESET`。

---

## 二、串口通信协议

### 数据包（6 字节）

```
AA  RUN  DIR  STEP_L  STEP_H  CHECK
```

| 字段 | 含义 |
| --- | --- |
| `AA` | 固定帧头 |
| `RUN` | `00` 暂停，`01` 运行 |
| `DIR` | `00` 正向（LED1→LED2→LED3），`01` 反向 |
| `STEP_L` / `STEP_H` | 切换间隔 100~2000 ms，低字节在前 |
| `CHECK` | `(0xAA + RUN + DIR + STEP_L + STEP_H) % 256` |

示例：运行、正向、300 ms → `AA 01 00 2C 01 D8`；暂停、反向、1000 ms → `AA 00 01 E8 03 96`。

串口参数：**115200, 8N1, 无校验, 无流控**；发送用 HEX 模式、不加换行；每次发一包，收到反馈再发下一包。

### 自定义串口反馈格式（英文文本 + CRLF）

校验通过并更新状态后，回传更新后的状态：

```
STATE=RUN,DIR=FWD,STEP=300,LED=1
```

| 字段 | 取值 |
| --- | --- |
| `STATE` | `RUN` / `PAUSE` |
| `DIR` | `FWD` / `REV` |
| `STEP` | 当前间隔（毫秒，纯数字） |
| `LED` | 当前点亮的 LED 编号 1~3 |

- 按键改变运行状态或方向时同样回传一行状态（不要求、也没有每次切灯都打印）。
- 校验失败的包不执行，并回传一行错误提示：`ERR=CHECKSUM` / `ERR=RANGE` / `ERR=HEADER`。
- 所有反馈以 `\r\n` 结尾，串口助手用文本模式（GBK/UTF-8 均可，内容全是 ASCII）即可查看。

### 状态校验规则（非法包的判定）

1. 第 1 字节不是 `AA`：丢弃，不报错（等待帧头）。
2. 校验和不等于前 5 字节之和的低 8 位 → `ERR=CHECKSUM`。
3. `RUN` 或 `DIR` 不是 0/1，或间隔不在 100~2000 ms → `ERR=RANGE`。

另外，解析器在坏帧里会重新寻找 `AA`，所以即使数据包中混入 `AA`、或者半包/错包后紧跟正确包，也能在下一包恢复同步。

---

## 三、程序结构

```
Core/Src/main.c            CubeMX 生成的初始化 + 主循环调用 App_Loop()
Core/Src/app.c             应用层：唯一的状态机、按键/串口事件处理、OLED 刷新、反馈
Core/Src/bsp_led.c         3 颗 LED 驱动
Core/Src/bsp_key.c         按键：EXTI 记录事件 + 主循环 20 ms 消抖状态机
Core/Src/protocol.c        数据包结构体、校验和、逐字节帧解析（含重同步）
Core/Src/usart.c           USART1 初始化、接收中断 + 环形缓冲、DMA 发送反馈
Core/Src/dma.c             DMA1 时钟与通道 4 中断（USART1_TX）
Core/Src/i2c.c             I2C1 初始化（PB6/PB7，400 kHz，复用开漏）
Core/Src/oled.c            SSD1306 128×64 驱动（帧缓存 + 按行/按页刷新，走硬件 I²C）
Core/Inc/oledfont.h        8×16 ASCII 字模
tools/gen_oled_font.py     由 Consolas 生成 8×16 字模的小工具
docs/选做-硬件I2C与DMA.md   选做说明：硬件 I²C 与 DMA 的关键源码、原理、对比
```

`App_Loop()` 每轮依次做：

1. `KEY_Scan()` 推进消抖状态机；
2. 处理按键事件（改方向 / 改运行状态）→ 置"显示脏"和"反馈脏"；
3. 从环形缓冲取字节，喂 `PacketParser_Feed()`；合法包才 `App_ApplyPacket()`；
4. 到点则切换 LED（暂停时不走时）→ 置"显示脏"；
5. 屏幕脏则只重画内容变化的那一行（2 页），然后推送到 OLED；
6. 需要反馈则格式化状态行交给 `UART_SendText()`，`UART_TxTask()` 启动 DMA 发送。

中断里只做两件事：`HAL_UART_RxCpltCallback` 把字节放进环形缓冲并重新开启接收，`HAL_GPIO_EXTI_Callback` 置按键边沿标志；`HAL_UART_TxCpltCallback` 只清发送忙标志（DMA 搬完 + USART 的 TC 中断之后由 HAL 调用）。

### 时序

- 流水灯节拍用 `HAL_GetTick()`（SysTick 1 ms），不阻塞、不占用定时器；`step_tick += step_ms` 累加，落后超过一个周期才重新对齐，长时间运行不累计误差。
- 修改间隔 / 恢复运行时 `step_tick` 重新置为当前 tick，因此改完间隔从当前时刻重新计时。
- OLED 用帧缓存 + 页地址，切灯时只刷新 `LED:` 一行（2 页 256 字节）；硬件 I²C 400 kHz 下这一行约 6 ms，
  不会因为整屏刷新而拖慢灯的节拍。
- 工程里保留着第二次作业的 `MX_TIM3_Init()`（CubeMX 里仍配置了 TIM3），本次按 ms 计时改用了 SysTick 的
  `HAL_GetTick()`，TIM3 没有启动、不产生中断，不影响功能。

---

## 四、编译与使用

1. STM32CubeIDE：`File → Import → General → Existing Projects into Workspace`，选择本目录，编译下载。
2. **接线**：OLED 的 SCL → **PB6**、SDA → **PB7**（基础版是 PB8/PB9）；串口仍为 PA9/PA10，按键 LED 不变。
3. 上电后 LED1 点亮、300 ms 正向流水，OLED 显示 `STATE: RUN / DIR: FWD / STEP: 300ms / LED: 1`，串口先回传一行初始状态。
4. 按 KEY1 切换方向，按 KEY2 暂停/继续；每次操作 OLED 与串口同步更新。
5. 串口助手（115200 8N1，HEX 发送）发送 `AA 01 01 2C 01 D9` 可切到"运行、反向、300 ms"，应收到
   `STATE=RUN,DIR=REV,STEP=300,LED=1`。

> 说明：工程由 CubeMX 6.15 生成，`STM32F103C8TX_FLASH.ld` 中使用了 GCC 11 起才支持的 `(READONLY)`
> 段属性。若使用自带 GCC 10 的旧版 CubeIDE，请删除这几处 `(READONLY)` 或升级到 1.15 及以上版本。

---

## 五、参考资料与驱动来源

- **ST 官方 HAL 库**（`Drivers/STM32F1xx_HAL_Driver`，FW_F1 V1.8.7）与 **CubeMX 生成的底层初始化代码**
  （时钟、GPIO、USART、I²C、DMA、MSP、中断向量）：直接使用，未修改生成部分。
- **STM32F103xx 参考手册**（RM0008）：I²C 外设（CR2/FREQ、时钟控制）、DMA 请求映射表（USART1_TX →
  DMA1 通道 4）、USART 的 DMAT 位与 TC 标志。
- **SSD1306 数据手册**：OLED 初始化命令、页地址模式、显存格式。
- **江协科技（江科大）STM32 入门教程**：OLED 软件 I²C 的接线与"帧缓存 + 页刷新"思路、按键消抖与
  串口收发数据包的教学思路；本工程的代码按作业要求自行重写（分层为 `i2c` / `oled` / `protocol` /
  `usart` / `dma` / `bsp_*` / `app`），并加入了环形缓冲、坏帧重同步、发送排队和按行局部刷新。
- **8×16 字模**：不是抄来的字库，而是用 `tools/gen_oled_font.py` 从 Windows 字体 Consolas 渲染、
  二值化后打包成列优先的页格式（可重新运行脚本再生成 `Core/Inc/oledfont.h`）。

---

## 六、选做部分（已完成）

两项选做都在本工程中实现，详细讲解与关键源码见 **[docs/选做-硬件I2C与DMA.md]：

1. **硬件 I²C 驱动 OLED**：I2C1 + PB6/PB7 + 400 kHz Fast Mode，OLED 驱动只把"发命令/发一页"换成
   `HAL_I2C_Master_Transmit()`，帧缓存与按行刷新的逻辑不变；`soft_i2c.c` 在本工程中已移除
   （软件 I²C 版本在 `homework0031` 中完整保留，按规定提交两份代码）。
2. **串口反馈改用 DMA**：DMA1 通道 4（USART1_TX 固定映射），`UART_TxTask()` 调用
   `HAL_UART_Transmit_DMA()`，发送完成走 `HAL_UART_TxCpltCallback()`；与中断发送相比，一条 38 字节
   状态行从约 38 次中断降到 1 次 DMA 完成中断 + 1 次 TC 中断。按题目要求，本工程不再单独保留
   `HAL_UART_Transmit_IT()` 的发送路径（中断/DMA 二选一）。
