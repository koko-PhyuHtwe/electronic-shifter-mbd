/**
 * @file    sbw_wdog.c
 * @brief   S32K144 看门狗（WDOG）寄存器级实现 + 复位原因捕获
 *
 * @details 为什么不用 SDK 的 WDOG_DRV_Init()：
 *          wdog_driver.c 没有被加入 S32DS 工程的编译列表，调用它会链接失败。
 *          这里直接操作寄存器，只依赖 S32K144.h，不需要改构建系统。
 *
 *          寄存器操作序列（S32K1xx RM 第 13 章）：
 *            1. 写 CNT = 0xD928C520        解锁
 *            2. 写 TOVAL / WIN / CS        配置超时与时钟源（CS 里同时置 EN=1）
 *            3. 喂狗：写 CNT = 0xB480A602
 *
 *          注意：32 位写序列的前提是 CS[CMD32EN] = 1。该位复位值为 1
 *          （见 S32K144_features.h 的 FEATURE_WDOG_CS_RESET_VALUE = 0x2520），
 *          所以这里的 32 位解锁/喂狗写法是合法的。
 */

#include "sbw_wdog.h"

#include "S32K144.h"

/* ---------- 1. 常量 ---------- */

#define WDOG_UNLOCK_KEY     ( 0xD928C520UL )
#define WDOG_FEED_KEY       ( 0xB480A602UL )

#define WDOG_LPO_HZ         ( 128000UL )    /* LPO 频率：clock_config.c 中配为 128 kHz */
#define WDOG_PRES_DIV       ( 256UL )       /* 256 分频，把计数时钟压到 500 Hz
                                             * （不压频的话 16 位 TOVAL 撑不到 1 s） */
#define WDOG_TICK_HZ        ( WDOG_LPO_HZ / WDOG_PRES_DIV )

/* ---------- 2. 对外信号 ---------- */

volatile uint32_t g_reset_cause = 0U;

/* ---------- 3. 私有变量 ---------- */

static uint8_t s_resetCaptured = 0U;

/* ---------- 4. 对外接口 ---------- */

/**
 * @brief  武装看门狗，配置完成后立即开始计数
 */
void Wdog_Init(void)
{
    uint32_t toval = (WDOG_TICK_HZ * WDOG_TIMEOUT_MS) / 1000UL;

    if (toval > 65535UL)
    {
        toval = 65535UL;
    }
    if (toval == 0UL)
    {
        toval = 1UL;
    }

    WDOG->CNT   = WDOG_UNLOCK_KEY;          /* 解锁 */
    WDOG->TOVAL = toval;                    /* 500 counts @500 Hz = 1.0 s */
    WDOG->WIN   = 0UL;                      /* 不启用窗口模式，避免喂早也复位 */

    /* CLK = 01 (LPO) | PRES = 256 分频 | UPDATE 允许改配置 | EN 立刻生效 */
    WDOG->CS = WDOG_CS_EN(1UL) | WDOG_CS_UPDATE(1UL)
             | WDOG_CS_CLK(1UL) | WDOG_CS_PRES_MASK;
}

/**
 * @brief  喂狗
 */
void Wdog_Feed(void)
{
    WDOG->CNT = WDOG_FEED_KEY;
}

/**
 * @brief  读走并清空复位原因寄存器 RCM_SRS
 *
 * @details RCM_SRS 是「写 1 清零」的状态寄存器，记录本次复位由谁触发：
 *            WDOG   (0x020) 看门狗超时   → 心跳没喂上 / 某任务卡死
 *            LOCKUP (0x200) CPU 锁死     → HardFault / 除零 / 非法访问
 *            PIN    (0x040) 外部复位脚   → 复位键 / 下载器
 *            SW     (0x400) 软件复位     → NVIC_SystemReset()
 *            POR    (0x080) 正常上电     ← 只有这个是「干净」的
 *
 *          必须在任何外设初始化之前调用：别的模块可能顺手把标志清掉，
 *          那样就再也分不清是故障复位还是上电复位了。
 */
void Wdog_CaptureResetCause(void)
{
    uint32_t srs;

    if (s_resetCaptured != 0U)
    {
        return;
    }
    s_resetCaptured = 1U;

    srs = RCM->SRS;
    g_reset_cause = srs;

    /* 设备头文件把 SRS 声明成只读，这里绕过 const 写回去做 w1c 清零。
     * 不清的话标志会一直粘着，下一次复位就分不清是新故障还是旧故障。 */
    *(volatile uint32_t *)&RCM->SRS = srs;
}