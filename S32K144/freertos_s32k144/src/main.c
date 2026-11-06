/*
 * Copyright 2020 NXP
 * All rights reserved.
 *
 * NXP Confidential. This software is owned or controlled by NXP and may only be
 * used strictly in accordance with the applicable license terms. By expressly
 * accepting such terms or by downloading, installing, activating and/or otherwise
 * using the software, you are agreeing that you have read, and that you agree to
 * comply with and are bound by, such license terms. If you do not agree to be
 * bound by the applicable license terms, then you may not retain, install,
 * activate or otherwise use the software. The production use license in
 * Section 2.3 is expressly granted for this software.
 */

/**
 * @file    main.c
 * @brief   程序入口
 *
 * @details 这里只做一件事：把控制权交给 FreeRTOS 启动函数。
 *          外设初始化、任务创建都在 rtos_start() 里按顺序完成，
 *          目的是让「上电做了什么」集中在 rtos.c 一个文件里，好读也好查。
 */

#include "FreeRTOS.h"

#include <stdbool.h>
#include <stdint.h>

#include "sdk_project_config.h"

void rtos_start(void);

int main(void)
{
    rtos_start();

    /* 正常情况下不会返回；真返回了说明调度器没起来，停在这里便于调试器接手 */
    for (;;)
    {
    }
}