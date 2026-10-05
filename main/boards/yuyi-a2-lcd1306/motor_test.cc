/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// 包含电机控制头文件
extern "C" {
#include "motion_control.h"
}

#define TAG "motor_test"

extern "C" void MotorTestTask(void *arg) {
    (void)arg; // 未使用参数
    
    ESP_LOGI(TAG, "开始电机测试");
    
    // 测试1: 向前移动
    ESP_LOGI(TAG, "向前移动");
    spark_bot_motion_control(0, 0.5);  // x=0, y=0.5 (50%速度向前)
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    
    // 测试2: 向后移动
    ESP_LOGI(TAG, "向后移动");
    spark_bot_motion_control(0, -0.5); // x=0, y=-0.5 (50%速度向后)
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    
    // 测试3: 左转
    ESP_LOGI(TAG, "左转");
    spark_bot_motion_control(-0.5, 0);  // x=-0.5, y=0 (左转)
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    
    // 测试4: 右转
    ESP_LOGI(TAG, "右转");
    spark_bot_motion_control(0.5, 0);   // x=0.5, y=0 (右转)
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    
    // 测试5: 原地旋转
    ESP_LOGI(TAG, "原地旋转");
    spark_bot_motion_control(0.5, 0);   // 原地右转
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    
    // 测试6: 停止
    ESP_LOGI(TAG, "停止");
    spark_bot_motion_control(0, 0);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    
    // 测试7: 舞蹈模式
    ESP_LOGI(TAG, "执行舞蹈模式");
    spark_bot_dance();
    
    ESP_LOGI(TAG, "电机测试完成");
    spark_bot_motion_control(0, 0);
    
    vTaskDelete(NULL);
}

extern "C" void StartMotorTest(void *board) {
    (void)board; // 未使用参数
    ESP_LOGI(TAG, "创建电机测试任务");
    xTaskCreate(MotorTestTask, "motor_test", 4096, NULL, 5, NULL);
}