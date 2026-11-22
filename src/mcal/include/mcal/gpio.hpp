/**
 * @file    gpio.hpp
 * @brief   MCAL contract: chân GPIO số (output / input).
 *
 * @note    Chỉ là interface thuần ảo. Hiện thực nằm ở `platform/<chip>/.../mcal`.
 */
#ifndef MCAL_GPIO_HPP
#define MCAL_GPIO_HPP

#include <cstdint>

namespace mcal {

/** @brief Mức logic của chân GPIO. */
enum class PinLevel : uint8_t {
    LOW = 0U,
    HIGH = 1U
};

/** @brief Chân GPIO output (đã được cấu hình bởi platform). */
class IGpioOutput {
public:
    virtual ~IGpioOutput() = default;

    /**
     * @brief Ghi mức logic ra chân.
     * @param[in] level Mức logic cần ghi.
     */
    virtual void write(PinLevel level) = 0;
};

/** @brief Chân GPIO input (đã được cấu hình bởi platform). */
class IGpioInput {
public:
    virtual ~IGpioInput() = default;

    /** @return Mức logic hiện tại của chân. */
    virtual PinLevel read() const = 0;
};

} // namespace mcal

#endif /* MCAL_GPIO_HPP */
