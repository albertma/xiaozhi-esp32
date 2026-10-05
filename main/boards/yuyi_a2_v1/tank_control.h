/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// 初始化坦克运动控制系统
void TankControlInit(void);

// 发送前进命令（5秒后自动停止）
bool TankMoveForward(void);

// 发送后退命令（5秒后自动停止）
bool TankMoveBackward(void);

// 发送左转命令（5秒后自动停止）
bool TankTurnLeft(void);

// 发送右转命令（5秒后自动停止）
bool TankTurnRight(void);

// 发送停止命令
bool TankStop(void);

// 发送舞蹈命令
void TankDance(void);

#ifdef __cplusplus
}
#endif
