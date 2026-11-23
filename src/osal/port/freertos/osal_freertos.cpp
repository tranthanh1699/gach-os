/**
 * @file    osal_freertos.cpp
 * @brief   Port OSAL cho FreeRTOS (bản ESP-IDF: stack tính bằng byte).
 */
#include "osal/osal.hpp"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace osal {

namespace {

TickType_t to_ticks(uint32_t ms)
{
    if (ms == WAIT_FOREVER) {
        return portMAX_DELAY;
    }
    const TickType_t ticks = pdMS_TO_TICKS(ms);
    // Làm tròn lên 1 tick để delay ngắn (< 1 tick) vẫn nhường CPU thật sự.
    return ((ticks == 0U) && (ms > 0U)) ? 1U : ticks;
}

} // namespace

uint32_t tick_ms()
{
    return static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

uint32_t time_us()
{
    return static_cast<uint32_t>(esp_timer_get_time());
}

void delay_ms(uint32_t ms)
{
    vTaskDelay(to_ticks(ms));
}

Mutex::Mutex() : m_handle(xSemaphoreCreateRecursiveMutex()) {}

Mutex::~Mutex()
{
    if (m_handle != nullptr) {
        vSemaphoreDelete(static_cast<SemaphoreHandle_t>(m_handle));
    }
}

bool Mutex::lock(uint32_t timeout_ms)
{
    if (m_handle == nullptr) {
        return false;
    }
    return xSemaphoreTakeRecursive(static_cast<SemaphoreHandle_t>(m_handle), to_ticks(timeout_ms)) == pdTRUE;
}

void Mutex::unlock()
{
    if (m_handle != nullptr) {
        (void)xSemaphoreGiveRecursive(static_cast<SemaphoreHandle_t>(m_handle));
    }
}

BinarySemaphore::BinarySemaphore() : m_handle(xSemaphoreCreateBinary()) {}

BinarySemaphore::~BinarySemaphore()
{
    if (m_handle != nullptr) {
        vSemaphoreDelete(static_cast<SemaphoreHandle_t>(m_handle));
    }
}

void BinarySemaphore::give()
{
    if (m_handle != nullptr) {
        (void)xSemaphoreGive(static_cast<SemaphoreHandle_t>(m_handle));
    }
}

bool BinarySemaphore::take(uint32_t timeout_ms)
{
    if (m_handle == nullptr) {
        return false;
    }
    return xSemaphoreTake(static_cast<SemaphoreHandle_t>(m_handle), to_ticks(timeout_ms)) == pdTRUE;
}

void task_exit()
{
    vTaskDelete(nullptr);
    for (;;) {
        // vTaskDelete(NULL) không return; vòng lặp chỉ để thỏa [[noreturn]].
    }
}

common::Status task_create(const TaskConfig& config, TaskEntry entry, void* p_arg)
{
    if ((entry == nullptr) || (config.stack_bytes == 0U)) {
        return common::Status::ERR_PARAM;
    }
    // Chip ít core hơn yêu cầu (VD single-core) -> không ghim, để cùng một cấu hình chạy được mọi chip.
    const bool is_pinnable = (config.core_id >= 0) && (config.core_id < static_cast<int8_t>(portNUM_PROCESSORS));
    const BaseType_t core = is_pinnable ? static_cast<BaseType_t>(config.core_id) : tskNO_AFFINITY;
    const BaseType_t ret = xTaskCreatePinnedToCore(entry, config.p_name, config.stack_bytes, p_arg,
                                                   static_cast<UBaseType_t>(config.priority), nullptr, core);
    return (ret == pdPASS) ? common::Status::OK : common::Status::ERR_NO_MEM;
}

} // namespace osal
