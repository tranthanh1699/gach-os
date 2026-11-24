/**
 * @file    store.hpp
 * @brief   [Private] Khởi tạo kho ứng dụng trực tuyến (API công khai: runtime/runtime.hpp, mục "Kho ứng dụng").
 */
#ifndef RUNTIME_STORE_HPP
#define RUNTIME_STORE_HPP

#include "hal_api/board.hpp"

namespace runtime {

/** @brief Gắn HTTP / thẻ SD / NVS và nạp địa chỉ kho đã lưu. Gọi trong runtime::init. */
void store_init(const hal::Devices& devices);

} // namespace runtime

#endif /* RUNTIME_STORE_HPP */
