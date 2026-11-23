/**
 * @file    jd9365_init_cmds.h
 * @brief   Chuỗi init panel JD9365 của hãng (định nghĩa trong jd9365_init_cmds.c).
 */
#ifndef BOARD_JD9365_INIT_CMDS_H
#define BOARD_JD9365_INIT_CMDS_H

#include <stdint.h>

#include "esp_lcd_jd9365.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Bảng lệnh init (truyền vào jd9365_vendor_config_t::init_cmds). */
extern const jd9365_lcd_init_cmd_t g_jd9365_vendor_init_cmds[];

/** @brief Số phần tử của g_jd9365_vendor_init_cmds. */
extern const uint16_t g_jd9365_vendor_init_cmds_count;

#ifdef __cplusplus
}
#endif

#endif /* BOARD_JD9365_INIT_CMDS_H */
