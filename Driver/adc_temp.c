/*
 * adc_temp.c - 内部温度传感器实现
 *
 * 使用 ADC1 的内部通道 16 和 VREFINT 通道 17。
 * Vref 用 STM32 出厂校准值 (VREFINT_CAL @ 0x1FFFF7BA) 反算，
 * 温度公式按数据手册: V25 = 1.43V, 斜率 4.3mV/°C。
 *
 * 本文件使用 STM32F10x Standard Peripheral Library ADC/RCC API。
 */

#include "stm32f10x.h"
#include "adc_temp.h"
// VREFINT 校准值地址
#define VREFINT_CAL_ADDR   0x1FFFF7BAu

static uint16_t adc_convert(uint8_t channel)
{
    /* 单通道转换，每次切换通道都重新配置采样时间。 */
    ADC_RegularChannelConfig(ADC1, channel, 1u, ADC_SampleTime_239Cycles5);
    // 启动转换，等待结束
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
    while (ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET) {
    }
    //返回 读取转换结果
    return ADC_GetConversionValue(ADC1);
}

void adc_temp_init(void)
{
    ADC_InitTypeDef adc;
    volatile uint32_t i;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);
    RCC_ADCCLKConfig(RCC_PCLK2_Div6);

    ADC_StructInit(&adc);
    adc.ADC_Mode                = ADC_Mode_Independent;
    adc.ADC_ScanConvMode        = DISABLE;
    adc.ADC_ContinuousConvMode  = DISABLE;
    adc.ADC_ExternalTrigConv    = ADC_ExternalTrigConv_None;
    adc.ADC_DataAlign           = ADC_DataAlign_Right;
    adc.ADC_NbrOfChannel        = 1u;
    ADC_Init(ADC1, &adc);

    ADC_Cmd(ADC1, ENABLE);

    /* 复位并重新校准 ADC。 */
    ADC_ResetCalibration(ADC1);
    while (ADC_GetResetCalibrationStatus(ADC1) != RESET) {
    }
    ADC_StartCalibration(ADC1);
    while (ADC_GetCalibrationStatus(ADC1) != RESET) {
    }

    /* 打开温度传感器与 VREFINT。 */
    ADC_TempSensorVrefintCmd(ENABLE);

    for (i = 0u; i < 20000u; i++) {   /* 等温度传感器稳定 */
    }

    (void)adc_convert(ADC_Channel_16);   /* 丢弃上电后第一组值 */
    (void)adc_convert(ADC_Channel_17);
}
// 读取温度值，返回值为摄氏度 * 10
int32_t adc_temp_read_x10(void)
{
    uint16_t raw_temp = adc_convert(ADC_Channel_16);
    uint16_t raw_vref = adc_convert(ADC_Channel_17);
    uint16_t cal = *(const uint16_t *)(uintptr_t)VREFINT_CAL_ADDR;
    float vref;
    float voltage;
    float temp;

    /* VREFINT 在 25°C 时约 1.20V，出厂校准值对应 3.3V 供电。 */
    if (raw_vref == 0u) {
        vref = 3.3f;
    } else {
        vref = (1.20f * (float)cal) / (float)raw_vref;
    }
    // 温度传感器输出电压 = VREFINT * (ADC_TEMP / ADC_VREFINT)
    voltage = (vref * (float)raw_temp) / 4096.0f;
    // 温度公式: T = (V25 - Vsense) / Avg_Slope + 25
    temp = (1.43f - voltage) / 0.0043f + 25.0f;

    return (int32_t)(temp * 10.0f);
}
