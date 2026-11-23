/**
 * @file    rotation.hpp
 * @brief   Ánh xạ tọa độ logic (sau xoay, OS nhìn thấy) <-> vật lý (hướng gốc của panel).
 *
 * Quy ước: góc xoay NGƯỢC chiều kim đồng hồ, cùng quy ước với PPA SRM của ESP32-P4.
 * Ảnh logic Wl x Hl xoay `rotation` -> ảnh vật lý Wp x Hp (90/270: Wp = Hl, Hp = Wl).
 *
 *   0  : (px, py) = (lx,          ly)
 *   90 : (px, py) = (ly,          Wl - 1 - lx)
 *   180: (px, py) = (Wl - 1 - lx, Hl - 1 - ly)
 *   270: (px, py) = (Hl - 1 - ly, lx)
 */
#ifndef BOARD_ROTATION_HPP
#define BOARD_ROTATION_HPP

#include <cstdint>

#include "hal_api/display.hpp"

namespace board {

/** @brief Góc xoay màn hình (ngược chiều kim đồng hồ). */
enum class Rotation : uint8_t {
    DEG_0,
    DEG_90,
    DEG_180,
    DEG_270
};

/** @brief Điểm nguyên có dấu. */
struct Point {
    int32_t x;
    int32_t y;
};

namespace rotation {

/** @return true nếu góc xoay hoán đổi chiều rộng/cao. */
constexpr bool swaps_axes(Rotation rot)
{
    return (rot == Rotation::DEG_90) || (rot == Rotation::DEG_270);
}

/**
 * @brief Góc trên-trái (vật lý) của khối ảnh sau khi xoay một vùng logic.
 *
 * @param[in] rot      Góc xoay.
 * @param[in] area     Vùng logic (inclusive).
 * @param[in] log_w    Chiều rộng logic toàn màn hình.
 * @param[in] log_h    Chiều cao logic toàn màn hình.
 */
constexpr Point area_origin_to_physical(Rotation rot, const hal::Area& area, int32_t log_w, int32_t log_h)
{
    switch (rot) {
    case Rotation::DEG_90:
        return Point{area.y1, log_w - 1 - area.x2};
    case Rotation::DEG_180:
        return Point{log_w - 1 - area.x2, log_h - 1 - area.y2};
    case Rotation::DEG_270:
        return Point{log_h - 1 - area.y2, area.x1};
    case Rotation::DEG_0:
    default:
        return Point{area.x1, area.y1};
    }
}

/**
 * @brief Đổi một điểm vật lý (VD tọa độ touch theo hướng gốc panel) sang tọa độ logic.
 *
 * @param[in] rot   Góc xoay.
 * @param[in] phys  Điểm vật lý.
 * @param[in] log_w Chiều rộng logic toàn màn hình.
 * @param[in] log_h Chiều cao logic toàn màn hình.
 */
constexpr Point physical_to_logical(Rotation rot, Point phys, int32_t log_w, int32_t log_h)
{
    switch (rot) {
    case Rotation::DEG_90:
        return Point{log_w - 1 - phys.y, phys.x};
    case Rotation::DEG_180:
        return Point{log_w - 1 - phys.x, log_h - 1 - phys.y};
    case Rotation::DEG_270:
        return Point{phys.y, log_h - 1 - phys.x};
    case Rotation::DEG_0:
    default:
        return phys;
    }
}

} // namespace rotation
} // namespace board

#endif /* BOARD_ROTATION_HPP */
