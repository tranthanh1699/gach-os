/**
 * @file    script_image.cpp
 * @brief   gach SDK v8 – ảnh và canvas: ui.image, ui.set_image, ui.image_info, ui.canvas, ui.draw.
 *
 * Ảnh là file .gimg trong thư mục app (gapp_pack.py đổi từ PNG lúc đóng gói), nạp vào AssetCache (asset arena của
 * board, PSRAM) và vẽ thẳng bằng LVGL (RGB565 / RGB565A8), không giải mã trên board.
 */
#include <cstdio>
#include <cstring>

#include "lua.hpp"
#include "lvgl.h"
#include "script_host.hpp"

namespace runtime {

namespace {

constexpr const char* APPS_DIR = "apps";
constexpr size_t APP_DIR_LEN = 48U;
constexpr int32_t SCALE_ONE = 256; /**< lv_image_set_scale: 256 = 100 %. */
constexpr size_t RGB565_BYTES = 2U;
constexpr int32_t CANVAS_MAX_SIDE = 2048; /**< dp. */

// Chỉ số trường trong một lệnh vẽ {"tên", a, b, ...} (Lua 1-based; [1] là tên).
constexpr int ARG1 = 2;
constexpr int ARG2 = 3;
constexpr int ARG3 = 4;
constexpr int ARG4 = 5;
constexpr int ARG5 = 6;
constexpr int ARG6 = 7;

/** @brief Mô tả LVGL của từng ảnh trong kho (lv_image giữ con trỏ tới mô tả, nên phải sống cùng kho). */
lv_image_dsc_t s_dsc[AssetCache::MAX_IMAGES];

} // namespace

struct UiV8 {
    /** @brief Nạp ảnh theo đường dẫn của app; lỗi -> lỗi Lua có thông điệp. */
    static const lv_image_dsc_t* image_arg(lua_State* p_lua, int arg)
    {
        ScriptHost* const p_host = ScriptHost::from_lua(p_lua);
        const char* const p_path = luaL_checkstring(p_lua, arg);
        char dir[APP_DIR_LEN];
        (void)std::snprintf(dir, sizeof(dir), "%s/%s", APPS_DIR, p_host->m_manifest.id);
        const AssetImage* p_image = nullptr;
        const common::Status status = p_host->m_assets.load(*p_host->m_p_fs, dir, p_path, p_image);
        if (status == common::Status::ERR_NOT_FOUND) {
            luaL_error(p_lua, "image not found: %s", p_path);
        } else if (status == common::Status::ERR_NO_MEM) {
            luaL_error(p_lua, "asset memory full (loading %s)", p_path);
        } else if (status == common::Status::ERR_NOT_SUPPORTED) {
            luaL_error(p_lua, "images are not supported on this device");
        } else if (!common::is_ok(status)) {
            luaL_error(p_lua, "bad image: %s", p_path);
        } else {
            // Đã nạp.
        }
        // Dựng lại mỗi lần (rẻ): sau khi kho reset, cùng index có thể là ảnh khác của app khác.
        lv_image_dsc_t& dsc = s_dsc[p_image->index];
        {
            dsc = lv_image_dsc_t{};
            dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
            dsc.header.cf = p_image->has_alpha ? LV_COLOR_FORMAT_RGB565A8 : LV_COLOR_FORMAT_RGB565;
            dsc.header.w = p_image->width;
            dsc.header.h = p_image->height;
            dsc.header.stride = p_image->stride;
            dsc.data_size = p_image->data_size;
            dsc.data = p_image->p_data;
        }
        return &dsc;
    }

    /** @brief Hệ số dp -> px (x256). */
    static int32_t scale_256(lua_State* p_lua) { return ScriptHost::dp(p_lua, SCALE_ONE); }

    static void apply_image(lua_State* p_lua, lv_obj_t* p_img, const lv_image_dsc_t* p_dsc)
    {
        lv_image_set_src(p_img, p_dsc);
        const int32_t scale = scale_256(p_lua);
        if (scale != SCALE_ONE) {
            lv_image_set_scale(p_img, static_cast<uint32_t>(scale));
        }
    }

    /** ui.image(parent, path, {w=, h=, fit="contain"|"cover"|"fill", opa=}) -> h */
    static int image(lua_State* p_lua)
    {
        lv_obj_t* const p_parent = ScriptHost::object_arg(p_lua, 1);
        const lv_image_dsc_t* const p_dsc = image_arg(p_lua, 2);
        lv_obj_t* const p_img = lv_image_create(p_parent);
        apply_image(p_lua, p_img, p_dsc);
        if (lua_istable(p_lua, 3)) {
            const bool has_w = lua_getfield(p_lua, 3, "w") == LUA_TNUMBER;
            const int32_t w = has_w ? ScriptHost::dp(p_lua, lua_tointeger(p_lua, -1)) : 0;
            const bool has_h = lua_getfield(p_lua, 3, "h") == LUA_TNUMBER;
            const int32_t h = has_h ? ScriptHost::dp(p_lua, lua_tointeger(p_lua, -1)) : 0;
            lua_pop(p_lua, 2);
            if (has_w || has_h) {
                static const char* const FITS[] = {"contain", "cover", "fill", nullptr};
                static const lv_image_align_t ALIGNS[] = {LV_IMAGE_ALIGN_CONTAIN, LV_IMAGE_ALIGN_COVER,
                                                          LV_IMAGE_ALIGN_STRETCH};
                (void)lua_getfield(p_lua, 3, "fit");
                const int fit = luaL_checkoption(p_lua, -1, "contain", FITS);
                lua_pop(p_lua, 1);
                lv_image_set_scale(p_img, SCALE_ONE); // Kích thước do khung quyết định.
                lv_obj_set_size(p_img, has_w ? w : LV_SIZE_CONTENT, has_h ? h : LV_SIZE_CONTENT);
                lv_image_set_inner_align(p_img, ALIGNS[fit]);
            }
            if (lua_getfield(p_lua, 3, "opa") == LUA_TNUMBER) {
                constexpr lua_Integer PERCENT = 100;
                const lua_Integer opa = lua_tointeger(p_lua, -1);
                lv_obj_set_style_image_opa(
                    p_img, static_cast<lv_opa_t>((((opa < 0) ? 0 : ((opa > PERCENT) ? PERCENT : opa)) * LV_OPA_COVER) /
                                                 PERCENT),
                    0);
            }
            lua_pop(p_lua, 1);
        }
        return ScriptHost::push_object(p_lua, p_img, 0, LV_EVENT_ALL);
    }

    /** ui.set_image(h, path) */
    static int set_image(lua_State* p_lua)
    {
        lv_obj_t* const p_img = ScriptHost::object_arg(p_lua, 1);
        if (!lv_obj_check_type(p_img, &lv_image_class)) {
            return luaL_argerror(p_lua, 1, "not an image");
        }
        lv_image_set_src(p_img, image_arg(p_lua, 2));
        return 0;
    }

    /** ui.canvas(parent, w, h) -> h : canvas RGB565, bộ đệm trong kho ảnh. */
    static int canvas(lua_State* p_lua)
    {
        lv_obj_t* const p_parent = ScriptHost::object_arg(p_lua, 1);
        const lua_Integer w_dp = luaL_checkinteger(p_lua, 2);
        const lua_Integer h_dp = luaL_checkinteger(p_lua, 3);
        luaL_argcheck(p_lua, (w_dp > 0) && (w_dp <= CANVAS_MAX_SIDE), 2, "width out of range");
        luaL_argcheck(p_lua, (h_dp > 0) && (h_dp <= CANVAS_MAX_SIDE), 3, "height out of range");
        const int32_t w = ScriptHost::dp(p_lua, w_dp);
        const int32_t h = ScriptHost::dp(p_lua, h_dp);
        ScriptHost* const p_host = ScriptHost::from_lua(p_lua);
        void* const p_buf = p_host->m_assets.alloc(static_cast<size_t>(w) * static_cast<size_t>(h) * RGB565_BYTES,
                                                   LV_DRAW_BUF_ALIGN);
        if (p_buf == nullptr) {
            return luaL_error(p_lua, "asset memory full (canvas %dx%d)", static_cast<int>(w_dp), static_cast<int>(h_dp));
        }
        lv_obj_t* const p_canvas = lv_canvas_create(p_parent);
        lv_canvas_set_buffer(p_canvas, p_buf, w, h, LV_COLOR_FORMAT_RGB565);
        lv_canvas_fill_bg(p_canvas, lv_color_black(), LV_OPA_COVER);
        return ScriptHost::push_object(p_lua, p_canvas, 0, LV_EVENT_ALL);
    }

    /** @brief Trường số thứ index của lệnh vẽ (bảng ở vị trí stack tuyệt đối cmd; dp -> px nếu is_length). */
    static int32_t field(lua_State* p_lua, int cmd, int index, bool is_length, lua_Integer fallback)
    {
        (void)lua_rawgeti(p_lua, cmd, index);
        const lua_Integer value = lua_isnoneornil(p_lua, -1) ? fallback : luaL_checkinteger(p_lua, -1);
        lua_pop(p_lua, 1);
        return is_length ? ScriptHost::dp(p_lua, value) : static_cast<int32_t>(value);
    }

    static lv_color_t color_field(lua_State* p_lua, int cmd, int index)
    {
        return lv_color_hex(static_cast<uint32_t>(field(p_lua, cmd, index, false, 0xFFFFFF)));
    }

    /** @brief Vẽ một lệnh (bảng ở đỉnh stack) lên layer. */
    static void draw_one(lua_State* p_lua, lv_layer_t* p_layer)
    {
        const int cmd = lua_gettop(p_lua);
        (void)lua_rawgeti(p_lua, cmd, 1);
        const char* const p_name = luaL_checkstring(p_lua, -1);
        lua_pop(p_lua, 1); // Chuỗi vẫn sống: bảng lệnh còn giữ nó.
        if (std::strcmp(p_name, "fill") == 0) {
            lv_draw_rect_dsc_t dsc;
            lv_draw_rect_dsc_init(&dsc);
            dsc.bg_color = color_field(p_lua, cmd, ARG1);
            const lv_area_t area{0, 0, lv_area_get_width(&p_layer->buf_area) - 1,
                                 lv_area_get_height(&p_layer->buf_area) - 1};
            lv_draw_rect(p_layer, &dsc, &area);
        } else if (std::strcmp(p_name, "rect") == 0) {
            lv_draw_rect_dsc_t dsc;
            lv_draw_rect_dsc_init(&dsc);
            const int32_t x = field(p_lua, cmd, ARG1, true, 0);
            const int32_t y = field(p_lua, cmd, ARG2, true, 0);
            const lv_area_t area{x, y, x + field(p_lua, cmd, ARG3, true, 1) - 1, y + field(p_lua, cmd, ARG4, true, 1) - 1};
            dsc.bg_color = color_field(p_lua, cmd, ARG5);
            dsc.radius = field(p_lua, cmd, ARG6, true, 0);
            lv_draw_rect(p_layer, &dsc, &area);
        } else if (std::strcmp(p_name, "line") == 0) {
            lv_draw_line_dsc_t dsc;
            lv_draw_line_dsc_init(&dsc);
            dsc.p1.x = field(p_lua, cmd, ARG1, true, 0);
            dsc.p1.y = field(p_lua, cmd, ARG2, true, 0);
            dsc.p2.x = field(p_lua, cmd, ARG3, true, 0);
            dsc.p2.y = field(p_lua, cmd, ARG4, true, 0);
            dsc.color = color_field(p_lua, cmd, ARG5);
            dsc.width = field(p_lua, cmd, ARG6, true, 1);
            dsc.round_start = 1;
            dsc.round_end = 1;
            lv_draw_line(p_layer, &dsc);
        } else if (std::strcmp(p_name, "circle") == 0) {
            lv_draw_rect_dsc_t dsc;
            lv_draw_rect_dsc_init(&dsc);
            const int32_t cx = field(p_lua, cmd, ARG1, true, 0);
            const int32_t cy = field(p_lua, cmd, ARG2, true, 0);
            const int32_t r = field(p_lua, cmd, ARG3, true, 1);
            dsc.bg_color = color_field(p_lua, cmd, ARG4);
            dsc.radius = LV_RADIUS_CIRCLE;
            const lv_area_t area{cx - r, cy - r, cx + r, cy + r};
            lv_draw_rect(p_layer, &dsc, &area);
        } else if (std::strcmp(p_name, "image") == 0) {
            (void)lua_rawgeti(p_lua, cmd, ARG1);
            const lv_image_dsc_t* const p_img = image_arg(p_lua, lua_gettop(p_lua));
            lua_pop(p_lua, 1);
            lv_draw_image_dsc_t dsc;
            lv_draw_image_dsc_init(&dsc);
            dsc.src = p_img;
            const int32_t x = field(p_lua, cmd, ARG2, true, 0);
            const int32_t y = field(p_lua, cmd, ARG3, true, 0);
            const lv_area_t area{x, y, x + static_cast<int32_t>(p_img->header.w) - 1,
                                 y + static_cast<int32_t>(p_img->header.h) - 1};
            lv_draw_image(p_layer, &dsc, &area);
        } else if (std::strcmp(p_name, "text") == 0) {
            lv_draw_label_dsc_t dsc;
            lv_draw_label_dsc_init(&dsc);
            const int32_t x = field(p_lua, cmd, ARG1, true, 0);
            const int32_t y = field(p_lua, cmd, ARG2, true, 0);
            dsc.color = color_field(p_lua, cmd, ARG4);
            (void)lua_rawgeti(p_lua, cmd, ARG3);
            dsc.text = luaL_checkstring(p_lua, -1);
            dsc.text_local = 1; // LVGL chép chuỗi: chuỗi Lua có thể bị thu gom trước khi vẽ xong.
            (void)lua_rawgeti(p_lua, cmd, ARG5);
            dsc.font = ScriptHost::font_named(p_lua, lua_isstring(p_lua, -1) ? lua_tostring(p_lua, -1) : "medium");
            const lv_area_t area{x, y, lv_area_get_width(&p_layer->buf_area) - 1,
                                 lv_area_get_height(&p_layer->buf_area) - 1};
            lv_draw_label(p_layer, &dsc, &area);
            lua_pop(p_lua, 2);
        } else {
            luaL_error(p_lua, "unknown draw command: %s", p_name);
        }
    }

    /** ui.draw(canvas, {cmd, cmd, ...}) : vẽ cả danh sách trong một lần (một layer). */
    static int draw(lua_State* p_lua)
    {
        lv_obj_t* const p_canvas = ScriptHost::object_arg(p_lua, 1);
        if (!lv_obj_check_type(p_canvas, &lv_canvas_class)) {
            return luaL_argerror(p_lua, 1, "not a canvas");
        }
        luaL_checktype(p_lua, 2, LUA_TTABLE);
        lv_layer_t layer;
        lv_canvas_init_layer(p_canvas, &layer);
        const lua_Integer count = luaL_len(p_lua, 2);
        for (lua_Integer index = 1; index <= count; ++index) {
            if (lua_rawgeti(p_lua, 2, index) != LUA_TTABLE) {
                lv_canvas_finish_layer(p_canvas, &layer);
                return luaL_error(p_lua, "draw command %d is not a table", static_cast<int>(index));
            }
            draw_one(p_lua, &layer);
            lua_pop(p_lua, 1);
        }
        lv_canvas_finish_layer(p_canvas, &layer);
        return 0;
    }

    /** ui.image_info(path) -> w, h (dp) */
    static int image_info(lua_State* p_lua)
    {
        const lv_image_dsc_t* const p_dsc = image_arg(p_lua, 1);
        const int32_t scale = scale_256(p_lua);
        lua_pushinteger(p_lua, (static_cast<int32_t>(p_dsc->header.w) * SCALE_ONE) / scale);
        lua_pushinteger(p_lua, (static_cast<int32_t>(p_dsc->header.h) * SCALE_ONE) / scale);
        return 2;
    }
};

const lv_image_dsc_t* ScriptHost::image_dsc_arg(lua_State* p_lua, int arg)
{
    return UiV8::image_arg(p_lua, arg);
}

void ScriptHost::register_ui_v8(lua_State* p_lua)
{
    static const luaL_Reg V8[] = {{"image", UiV8::image},
                                  {"set_image", UiV8::set_image},
                                  {"image_info", UiV8::image_info},
                                  {"canvas", UiV8::canvas},
                                  {"draw", UiV8::draw},
                                  {nullptr, nullptr}};
    luaL_setfuncs(p_lua, V8, 0);
}

} // namespace runtime
