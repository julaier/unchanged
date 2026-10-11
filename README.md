# LDC1614 微缝隙测量固件 (STM32F103C8T6)

电梯曳引机制动器松闸间隙测量，LDC1614 双通道差分方案。
本工程是 **验证板 Board1** 的固件，一份代码同时给 Keil 和 VS Code 用。

---

## 一、目录结构

```
LDC1614_FW/
  App/                   应用层
    main.c                 入口, 只负责调用 app
    app.c / app.h          初始化顺序、采样、命令、周期上报
  Driver/                与芯片/板级硬件打交道的驱动
    board.c / board.h        时钟 72MHz、1ms 时基、INTB(PB0/EXTI0)
    hard_i2c.c / hard_i2c.h  STM32 硬件 I2C1 (PB6=SCL, PB7=SDA)
    uart.c / uart.h          USART1 (PA9=TX, PA10=RX), 接外部 CH340 模块
    adc_temp.c / adc_temp.h  内部温度 (ADC1 通道 16 + VREFINT 校准)
    flash_param.c / flash_param.h  内部 Flash 参数区 (0x0800FC00, 1KB)
  Device/                器件驱动
    ldc1614.c / ldc1614.h    LDC1614 寄存器读写、初始化、读双通道
  Service/               与硬件无关的服务
    measure.c / measure.h    滤波、标定插值、温补、迟滞+去抖
    protocol.c / protocol.h  串口帧组装与命令解析
  CMSIS/                 STM32 CMSIS 内核与器件寄存器定义
  Library/
    inc/                    STM32F10x SPL V3.5.0 头文件
    src/                    STM32F10x SPL V3.5.0 源文件
  Startup/               厂商文件 (启动文件)
  MDK-ARM/               Keil uVision5 工程
    LDC1614.uvprojx         工程文件, 双击即可用 Keil 打开
    LDC1614.uvoptx          调试/下载设置 (已选 ST-Link, SWD)
  tools/keil_build.cmd   VS Code 任务用的命令行构建脚本
  .vscode/               VS Code 工程配置
```

依赖方向是单向的：`App -> Service -> Device -> Driver`。
`Service` 层不访问寄存器，`Driver` 层不含业务逻辑。

---

## 二、硬件约定

| 项 | 接法 |
| --- | --- |
| 主控 | STM32F103C8T6, HSE 8MHz, 系统 72MHz |
| LDC1614 I2C | 硬件 I2C1，PB6 = SCL，PB7 = SDA，400kHz，板上 4.7K 上拉 |
| LDC1614 INTB | PB0 / EXTI0，下降沿触发（INTB 为推挽，无需上拉） |
| LDC1614 ADDR | GND -> 7 位地址 0x2A |
| LDC1614 SD | GND -> 正常工作 |
| LDC1614 CLKIN | 外部 40MHz 有源晶振（单端） |
| 传感线圈 | 板载两路 PCB 平面线圈，接 IN0A/IN0B 与 IN1A/IN1B |
| 未用通道 | IN2A/IN2B/IN3A/IN3B 并接 GND，扫描里不使用 |
| 上位机串口 | USART1，PA9 = TX，PA10 = RX，115200 8N1，接外部 CH340 模块 |
| USB | 仅取电（D+/D- 未接），调试板由 Micro-USB 5V 供电 |
| 温度 | STM32 内部温度通道，仅作临时温补 |

---

## 三、用 Keil 编译 / 下载

1. 双击 `MDK-ARM\LDC1614.uvprojx`，用 Keil uVision5 打开。
2. 器件已设为 `STMicroelectronics -> STM32F103C8`，编译器 ARMCC5。
   工程已定义 `USE_STDPERIPH_DRIVER`，并在 `Library` 分组中编译标准外设库；
   应用层和驱动层统一调用 SPL API。
3. 按 `F7` 编译，生成的固件在 `MDK-ARM\Objects\LDC1614.hex`。
4. 按 `F8` 下载（Debug/Utilities 已设为 ST-Link Debugger，接口 SWD）。

命令行等价（在工程根目录）：

```bat
tools\keil_build.cmd build      rem 增量编译
tools\keil_build.cmd rebuild    rem 全部重编
tools\keil_build.cmd flash      rem 编译 + ST-Link 下载
```

> 如果 Keil 装在别的位置，改 `tools\keil_build.cmd` 第一行的 `UV4=` 即可。

## 四、用 VS Code 编辑

用 VS Code 打开 `LDC1614_FW` 文件夹（或工作区文件）。`Ctrl+Shift+B` 触发
`Keil: 构建 (Build)`，任务会调用 `tools\keil_build.cmd` 调 UV4 编译，
出错信息会解析到 `问题` 面板。

`.vscode/c_cpp_properties.json` 已配好头文件路径与 `STM32F10X_MD` 宏，
IntelliSense 可直接跳转。

---

## 五、串口协议

### 5.1 周期上报（默认 100ms 一条，115200 8N1）

```
$D,<间隙mm>,<温度C>,<状态>\r\n
```

例：`$D,0.523,28.5,0`

状态：`0` 正常 / `1` 提醒 / `2` 报警 / `3` 故障 / `4` 未标定。

上位机只要按 `$D` 拆行，取第 2 个字段就是距离，第 4 个字段决定显示颜色。

### 5.2 调试帧（带原始数据）

```
$R,<ch0>,<ch1>,<比值>,<间隙mm>,<温度C>,<状态>\r\n
```

### 5.3 上电提示

- 正常：`$B,LDC1614 ready`
- 初始化失败：`$E,<code>`，code 为负数取绝对值（见 `Device/ldc1614.c` 的 `LDC_ERR_xxx`）

### 5.4 上位机命令（每条以 `\r\n` 结尾）

| 命令 | 功能 |
| --- | --- |
| `READ` | 立刻回一帧 `$D` |
| `RAW` | 立刻回一帧 `$R` |
| `RATE,100` | 上报周期 ms，`0` = 停止主动上报 |
| `STX,0.800` | 设置报警阈值（mm） |
| `ST2,0.500` | 设置提醒阈值（mm） |
| `SWAP,1` | 交换 CH0/CH1 的比值方向（哪个是测量线圈不确定时用） |
| `CAL,1,0.300` | 用当前比值录入第 1 个标定点 = 0.300mm（必须从 1 依次录入） |
| `CALS` | 标定表与参数写入 Flash |
| `CLR` | 清空标定表 |
| `TC,0.00002,25.0` | 温补系数与参考温度 |
| `INFO` | 返回版本、周期、阈值、标定点、当前值 |
| `RD,16` | 读寄存器（十六进制），返回 `REG,<reg>,<val>` |
| `WR,17,820F` | 写寄存器 |
| `DUMP` | 一次打印 0x00~0x1E 全部寄存器的低字节 |

应答：`OK` / `ERR,<code>`。

---

## 六、采样与处理流程

1. LDC1614 按 CH0 + CH1 自动扫描，每完成一次转换 INTB 拉低，PB0 触发 EXTI0；
2. 主循环读 DATA_CH0 / DATA_CH1 的 28 位原始值（MSB 高 4 位是错误标志）；
3. 比值（默认 ch0/ch1，可用 `SWAP` 翻转）-> 5 点中值 -> 8 点滑动平均；
4. 温度二阶修正 -> 标定表分段线性插值得到间隙；
5. 阈值判断（0.05mm 迟滞）+ 1.5s 去抖 -> 状态码；
6. 每 100ms 发一帧 `$D`；超过 500ms 没有 INTB 或 I2C 读失败 -> 状态 3（故障）。

RCOUNT 当前取 `0xFFFF`（约 26.2ms/通道，双通道一轮约 52ms），
所以上报周期不要小于 60ms。台架上要提速时改 `Device/ldc1614.c` 里的
`LDC1614_RCOUNT_VALUE`。

---

## 七、上板前必须核对的寄存器常量

这几个值按数据手册填写，**投产前请对照手头的 LDC1612/1614 数据手册再确认一遍**，
都在 `Device/ldc1614.c` 顶部：

| 常量 | 当前值 | 含义 |
| --- | --- | --- |
| `LDC1614_RCOUNT_VALUE` | `0xFFFF` | 转换时间，约 26.2ms/通道 @40MHz |
| `LDC1614_SETTLECOUNT_VALUE` | `0x0180` | 建立时间，需 >= (fREF/fSENSOR) x Q |
| `LDC1614_CLOCK_DIV_DEFAULT` | `0x1001` | 时钟分频 |
| `LDC1614_DRIVE_CURRENT_VALUE` | `0x9000` | 驱动电流初值 |
| `LDC1614_MUX_AUTOSCAN_CH01` | `0x820F` | 自动扫描 CH0+CH1 |
| `LDC1614_CONFIG_REF_CLK_SRC` | `0x0200` | CONFIG 的 REF_CLK_SRC，用外部时钟 |
| `LDC1614_CONFIG_SLEEP_MODE` | `0x2000` | CONFIG 的 SLEEP_MODE_EN，初始化时清零 |

其中 `MUX_AUTOSCAN_CH01` 和 `CONFIG_REF_CLK_SRC` 最关键：配错的话上电
INTB 不来，串口只会发 `$D,0.000,xx,4` 或直接判故障。
配好后可以用 `DUMP` 命令把寄存器整表打出来核对，或用 `WR` 在线改，
不需要重新编译。

寄存器地址按 TI `SNOSCY9A`（LDC1612/LDC1614 Rev. A）实现：
`CONFIG=0x1A`、`MUX_CONFIG=0x1B`、`RESET_DEV=0x1C`、
`DRIVE_CURRENT0/1=0x1E/0x1F`、`MANUFACTURER_ID=0x7E`、`DEVICE_ID=0x7F`。
初始化会先校验 `0x5449/0x3055`，ID 不对会通过串口报 `E,11`，不会假报 ready。
