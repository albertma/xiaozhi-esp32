/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

// 包含电机控制头文件
extern "C" {
#include "motion_control.h"
}

#include "tank_control.h"

#define TAG "tank_control"

// 运动控制命令类型
enum class MotionCmdType {
    STOP,       // 停止
    MOVE,       // 持续运动
    MOVE_TIMED  // 定时运动
};

// 运动控制命令结构
struct MotionCommand {
    MotionCmdType type;
    float x;
    float y;
    int duration_ms;  // 用于MOVE_TIMED
};

// 静态成员变量
static TaskHandle_t g_motion_task_handle = nullptr;
static QueueHandle_t g_motion_queue = nullptr;
static bool g_initialized = false;

// 运动控制常驻任务 - 只创建一次，通过队列接收命令
static void MotionControlTask(void* arg)
{
    (void)arg;
    MotionCommand cmd;
    int remaining_ticks = 0;
    bool is_moving = false;

    ESP_LOGI(TAG, "运动控制任务启动");

    while (true) {
        // 非阻塞检查队列，超时100ms
        if (xQueueReceive(g_motion_queue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE) {
            switch (cmd.type) {
                case MotionCmdType::STOP:
                    spark_bot_motion_control(0, 0);
                    is_moving = false;
                    remaining_ticks = 0;
                    ESP_LOGI(TAG, "收到停止命令");
                    break;

                case MotionCmdType::MOVE:
                    spark_bot_motion_control(cmd.x, cmd.y);
                    is_moving = true;
                    remaining_ticks = 0;  // 持续运动
                    ESP_LOGI(TAG, "收到运动命令: x=%.2f, y=%.2f", cmd.x, cmd.y);
                    break;

                case MotionCmdType::MOVE_TIMED:
                    spark_bot_motion_control(cmd.x, cmd.y);
                    is_moving = true;
                    remaining_ticks = pdMS_TO_TICKS(cmd.duration_ms);
                    ESP_LOGI(TAG, "收到定时运动命令: x=%.2f, y=%.2f, %d ms", cmd.x, cmd.y, cmd.duration_ms);
                    break;
            }
        }

        // 处理定时运动
        if (is_moving && remaining_ticks > 0) {
            remaining_ticks -= pdMS_TO_TICKS(100);
            if (remaining_ticks <= 0) {
                spark_bot_motion_control(0, 0);
                is_moving = false;
                ESP_LOGI(TAG, "定时运动结束，自动停止");
            }
        }
    }
}

// 发送运动命令
static bool SendMotionCommand(const MotionCommand& cmd)
{
    if (g_motion_queue == nullptr) {
        ESP_LOGE(TAG, "运动控制系统未初始化");
        return false;
    }

    return xQueueSend(g_motion_queue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE;
}

// 初始化坦克运动控制系统
extern "C" void TankControlInit(void)
{
    if (g_initialized) return;

    // 创建队列（深度为5，足够缓存命令）
    g_motion_queue = xQueueCreate(5, sizeof(MotionCommand));
    if (g_motion_queue == nullptr) {
        ESP_LOGE(TAG, "创建运动命令队列失败");
        return;
    }

    // 创建常驻任务
    BaseType_t result = xTaskCreate(
        MotionControlTask,
        "motion_control",
        3072,
        NULL,
        5,
        &g_motion_task_handle
    );

    if (result != pdPASS) {
        ESP_LOGE(TAG, "创建运动控制任务失败");
        vQueueDelete(g_motion_queue);
        g_motion_queue = nullptr;
        return;
    }

    g_initialized = true;
    ESP_LOGI(TAG, "坦克运动控制系统初始化完成");
}

// 发送前进命令（5秒后自动停止）
extern "C" bool TankMoveForward(void)
{
    ESP_LOGI(TAG, "小车向前移动5秒");
    MotionCommand cmd;
    cmd.type = MotionCmdType::MOVE_TIMED;
    cmd.x = 0;
    cmd.y = 1;
    cmd.duration_ms = 3000;
    return SendMotionCommand(cmd);
}

// 发送后退命令（5秒后自动停止）
extern "C" bool TankMoveBackward(void)
{
    ESP_LOGI(TAG, "小车向后移动5秒");
    MotionCommand cmd;
    cmd.type = MotionCmdType::MOVE_TIMED;
    cmd.x = 0;
    cmd.y = -1;
    cmd.duration_ms = 3000;
    return SendMotionCommand(cmd);
}

// 发送左转命令（5秒后自动停止）
extern "C" bool TankTurnLeft(void)
{
    ESP_LOGI(TAG, "小车向左转5秒");
    MotionCommand cmd;
    cmd.type = MotionCmdType::MOVE_TIMED;
    cmd.x = -1;
    cmd.y = 0;
    cmd.duration_ms = 3000;
    return SendMotionCommand(cmd);
}

// 发送右转命令（5秒后自动停止）
extern "C" bool TankTurnRight(void)
{
    ESP_LOGI(TAG, "小车向右转5秒");
    MotionCommand cmd;
    cmd.type = MotionCmdType::MOVE_TIMED;
    cmd.x = 1;
    cmd.y = 0;
    cmd.duration_ms = 3000;
    return SendMotionCommand(cmd);
}

// 发送停止命令
extern "C" bool TankStop(void)
{
    ESP_LOGI(TAG, "小车停止");
    MotionCommand cmd;
    cmd.type = MotionCmdType::STOP;
    cmd.x = 0;
    cmd.y = 0;
    cmd.duration_ms = 0;
    return SendMotionCommand(cmd);
}

// 发送舞蹈命令
extern "C" void TankDance(void)
{
    ESP_LOGI(TAG, "小车跳个舞");
    // 先停止当前运动
    TankStop();
    // 执行舞蹈
    spark_bot_dance();
}
