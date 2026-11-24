/**
 * @file    script_game.cpp
 * @brief   gach SDK v8 – engine 2D gach.game: scene native (GameScene + GameWorld), Lua chỉ làm logic.
 *
 * Tọa độ / kích thước trong gach.game là pixel của scene (không co giãn dp): ảnh sprite vẽ 1:1, không tốn CPU
 * phóng ảnh. game.scene trả kích thước thật (px) để app tự bố trí.
 */
#include <cstring>

#include "lua.hpp"
#include "lvgl.h"
#include "script_host.hpp"

namespace runtime {

namespace {

constexpr lua_Integer PERCENT = 100;
constexpr uint32_t MS_PER_S = 1000U;
constexpr size_t EVENT_BATCH = 32U;
constexpr uint16_t DEFAULT_SIZE = 16U; /**< Sprite không ảnh, không khai kích thước. */

GameEvent s_events[EVENT_BATCH];

uint8_t opa_from_percent(lua_Integer percent)
{
    const lua_Integer clamped = (percent < 0) ? 0 : ((percent > PERCENT) ? PERCENT : percent);
    return static_cast<uint8_t>((clamped * LV_OPA_COVER) / PERCENT);
}

size_t slot_of(int32_t id)
{
    constexpr uint32_t SLOT_MASK = 0x7FFU;
    return (static_cast<uint32_t>(id) & SLOT_MASK) - 1U;
}

} // namespace

struct GameApi {
    static ScriptHost& host(lua_State* p_lua) { return *ScriptHost::from_lua(p_lua); }

    static GameScene& scene(lua_State* p_lua)
    {
        GameScene& scene = host(p_lua).m_scene;
        if (!scene.is_active()) {
            luaL_error(p_lua, "no game scene (call gach.game.scene first)");
        }
        return scene;
    }

    static Sprite* sprite_arg(lua_State* p_lua, int arg)
    {
        return scene(p_lua).world().get(static_cast<int32_t>(luaL_checkinteger(p_lua, arg)));
    }

    /** @brief Hàm Lua tùy chọn ở arg -> ref (LUA_NOREF nếu nil). */
    static int ref_arg(lua_State* p_lua, int arg)
    {
        if (lua_isnoneornil(p_lua, arg)) {
            return LUA_NOREF;
        }
        luaL_checktype(p_lua, arg, LUA_TFUNCTION);
        lua_pushvalue(p_lua, arg);
        return luaL_ref(p_lua, LUA_REGISTRYINDEX);
    }

    static void unref(lua_State* p_lua, int& ref)
    {
        if (ref != LUA_NOREF) {
            luaL_unref(p_lua, LUA_REGISTRYINDEX, ref);
            ref = LUA_NOREF;
        }
    }

    /** @brief Áp các trường của bảng opts (arg) lên sprite. */
    static void apply(lua_State* p_lua, int arg, Sprite& s)
    {
        GameScene& sc = scene(p_lua);
        if (lua_getfield(p_lua, arg, "sheet") == LUA_TNUMBER) {
            s.sheet = static_cast<int16_t>(lua_tointeger(p_lua, -1));
            uint16_t w = 0U;
            uint16_t h = 0U;
            if (!sc.sheet_frame(s.sheet, w, h)) {
                luaL_error(p_lua, "bad sheet id %d", static_cast<int>(s.sheet));
            }
            s.w = w;
            s.h = h;
        }
        lua_pop(p_lua, 1);
        static const char* const NUMS[] = {"x", "y", "vx", "vy"};
        float* const p_nums[] = {&s.x, &s.y, &s.vx, &s.vy};
        for (size_t index = 0U; index < (sizeof(NUMS) / sizeof(NUMS[0])); ++index) {
            if (lua_getfield(p_lua, arg, NUMS[index]) == LUA_TNUMBER) {
                *p_nums[index] = static_cast<float>(lua_tonumber(p_lua, -1));
            }
            lua_pop(p_lua, 1);
        }
        if (lua_getfield(p_lua, arg, "w") == LUA_TNUMBER) {
            s.w = static_cast<uint16_t>(lua_tointeger(p_lua, -1));
        }
        if (lua_getfield(p_lua, arg, "h") == LUA_TNUMBER) {
            s.h = static_cast<uint16_t>(lua_tointeger(p_lua, -1));
        }
        if (lua_getfield(p_lua, arg, "frame") == LUA_TNUMBER) {
            s.frame = static_cast<uint16_t>(lua_tointeger(p_lua, -1));
        }
        if (lua_getfield(p_lua, arg, "color") == LUA_TNUMBER) {
            s.color = static_cast<uint32_t>(lua_tointeger(p_lua, -1));
        }
        if (lua_getfield(p_lua, arg, "layer") == LUA_TNUMBER) {
            s.layer = static_cast<uint8_t>(lua_tointeger(p_lua, -1) % static_cast<lua_Integer>(GameWorld::MAX_LAYERS));
        }
        if (lua_getfield(p_lua, arg, "group") == LUA_TNUMBER) {
            s.group = static_cast<uint8_t>(lua_tointeger(p_lua, -1) % static_cast<lua_Integer>(GameWorld::MAX_GROUPS));
        }
        if (lua_getfield(p_lua, arg, "opa") == LUA_TNUMBER) {
            s.opa = opa_from_percent(lua_tointeger(p_lua, -1));
        }
        if (lua_getfield(p_lua, arg, "visible") == LUA_TBOOLEAN) {
            s.visible = lua_toboolean(p_lua, -1) != 0;
        }
        lua_pop(p_lua, 8);
        s.changed = true;
    }

    // ---- Callback từ GameScene ----
    static void call(ScriptHost& h, int nargs_pushed) { (void)h.protected_call(nargs_pushed, 0); }

    /** @brief Đẩy hàm ref; false (không đẩy gì) nếu không gọi được. */
    static bool push_ref(ScriptHost& h, int ref)
    {
        if ((h.m_p_lua == nullptr) || h.m_faulted || h.m_in_call || (ref == LUA_NOREF)) {
            return false;
        }
        lua_rawgeti(h.m_p_lua, LUA_REGISTRYINDEX, ref);
        return true;
    }

    static void dispatch_events(ScriptHost& h)
    {
        GameWorld& world = h.m_scene.world();
        size_t got = 0U;
        do {
            got = world.take_events(s_events, EVENT_BATCH);
            for (size_t index = 0U; (index < got) && h.m_scene.is_active(); ++index) {
                const GameEvent& ev = s_events[index];
                const size_t slot = slot_of(ev.a);
                if (ev.kind == GameEvent::OVERLAP) {
                    const Sprite* const p_a = world.get(ev.a);
                    const Sprite* const p_b = world.get(ev.b);
                    if ((p_a == nullptr) || (p_b == nullptr)) {
                        continue; // Một bên đã bị xóa bởi callback trước.
                    }
                    if (push_ref(h, h.m_game_overlap_refs[p_a->group][p_b->group])) {
                        lua_pushinteger(h.m_p_lua, ev.a);
                        lua_pushinteger(h.m_p_lua, ev.b);
                        call(h, 2);
                    }
                } else if (ev.kind == GameEvent::EXPIRED) {
                    unref(h.m_p_lua, h.m_game_anim_refs[slot]);
                    unref(h.m_p_lua, h.m_game_move_refs[slot]);
                } else {
                    int& ref = (ev.kind == GameEvent::ANIM_DONE) ? h.m_game_anim_refs[slot] : h.m_game_move_refs[slot];
                    const int to_call = ref;
                    ref = LUA_NOREF;
                    if ((world.get(ev.a) != nullptr) && push_ref(h, to_call)) {
                        lua_pushinteger(h.m_p_lua, ev.a);
                        call(h, 1);
                    }
                    if ((h.m_p_lua != nullptr) && (to_call != LUA_NOREF)) {
                        luaL_unref(h.m_p_lua, LUA_REGISTRYINDEX, to_call);
                    }
                }
            }
        } while ((got == EVENT_BATCH) && h.m_scene.is_active());
    }

    static void on_logic(void* p_ctx, uint32_t dt_ms)
    {
        ScriptHost& h = *static_cast<ScriptHost*>(p_ctx);
        dispatch_events(h);
        if (h.m_scene.is_active() && push_ref(h, h.m_game_update_ref)) {
            lua_pushinteger(h.m_p_lua, static_cast<lua_Integer>(dt_ms));
            call(h, 1);
        }
    }

    static void on_touch(void* p_ctx, GameScene::TouchPhase phase, int32_t x, int32_t y)
    {
        static const char* const PHASES[] = {"down", "move", "up"};
        ScriptHost& h = *static_cast<ScriptHost*>(p_ctx);
        if (push_ref(h, h.m_game_touch_ref)) {
            lua_pushstring(h.m_p_lua, PHASES[phase]);
            lua_pushinteger(h.m_p_lua, x);
            lua_pushinteger(h.m_p_lua, y);
            call(h, 3);
        }
    }

    // ---- API Lua ----
    /** game.scene(parent, w, h, {bg=color|path, fps=30}) -> handle, w_px, h_px */
    static int scene_new(lua_State* p_lua)
    {
        ScriptHost& h = host(p_lua);
        if (h.m_scene.is_active()) {
            return luaL_error(p_lua, "game scene already exists (one per app)");
        }
        lv_obj_t* const p_parent = ScriptHost::object_arg(p_lua, 1);
        const int32_t w = ScriptHost::dp(p_lua, luaL_checkinteger(p_lua, 2));
        const int32_t ht = ScriptHost::dp(p_lua, luaL_checkinteger(p_lua, 3));
        uint32_t hz = GameScene::LOGIC_HZ_DEFAULT;
        uint32_t bg = 0U;
        const lv_image_dsc_t* p_bg = nullptr;
        if (lua_istable(p_lua, 4)) {
            if (lua_getfield(p_lua, 4, "fps") == LUA_TNUMBER) {
                hz = static_cast<uint32_t>(lua_tointeger(p_lua, -1));
            }
            const int bg_type = lua_getfield(p_lua, 4, "bg");
            if (bg_type == LUA_TNUMBER) {
                bg = static_cast<uint32_t>(lua_tointeger(p_lua, -1));
            } else if (bg_type == LUA_TSTRING) {
                p_bg = ScriptHost::image_dsc_arg(p_lua, lua_gettop(p_lua));
            } else {
                // Không có nền: đen.
            }
            lua_pop(p_lua, 2);
        }
        if (!common::is_ok(h.m_scene.create(p_parent, w, ht, hz))) {
            return luaL_error(p_lua, "cannot create game scene");
        }
        h.m_scene.set_background(bg, p_bg);
        h.m_scene.set_callbacks(&GameApi::on_logic, &GameApi::on_touch, &h);
        (void)ScriptHost::push_object(p_lua, h.m_scene.object(), 0, LV_EVENT_ALL);
        lua_pushinteger(p_lua, w);
        lua_pushinteger(p_lua, ht);
        return 3;
    }

    /** game.sheet(path, frame_w, frame_h) -> sheet id (khung tính bằng px của ảnh) */
    static int sheet(lua_State* p_lua)
    {
        GameScene& sc = scene(p_lua);
        const lv_image_dsc_t* const p_img = ScriptHost::image_dsc_arg(p_lua, 1);
        const lua_Integer fw = luaL_optinteger(p_lua, 2, p_img->header.w);
        const lua_Integer fh = luaL_optinteger(p_lua, 3, p_img->header.h);
        const int32_t id = sc.add_sheet(p_img, static_cast<uint16_t>(fw), static_cast<uint16_t>(fh));
        if (id < 0) {
            return luaL_error(p_lua, "bad sheet (frame larger than image, or more than %d sheets)",
                              static_cast<int>(GameScene::MAX_SHEETS));
        }
        lua_pushinteger(p_lua, id);
        return 1;
    }

    /** game.sprite({sheet=, frame=, x=, y=, w=, h=, color=, layer=, group=, vx=, vy=, opa=, visible=}) -> id */
    static int sprite(lua_State* p_lua)
    {
        GameScene& sc = scene(p_lua);
        luaL_checktype(p_lua, 1, LUA_TTABLE);
        Sprite s{};
        s.sheet = -1;
        s.w = DEFAULT_SIZE;
        s.h = DEFAULT_SIZE;
        s.color = 0xFFFFFFU;
        s.opa = LV_OPA_COVER;
        s.visible = true;
        apply(p_lua, 1, s);
        const int32_t id = sc.world().add(s);
        if (id == GameWorld::INVALID) {
            return luaL_error(p_lua, "too many sprites (max %d)", static_cast<int>(GameWorld::MAX_SPRITES));
        }
        lua_pushinteger(p_lua, id);
        return 1;
    }

    /** game.set(id, {...}) -> true nếu sprite còn tồn tại */
    static int set(lua_State* p_lua)
    {
        Sprite* const p_s = sprite_arg(p_lua, 1);
        luaL_checktype(p_lua, 2, LUA_TTABLE);
        if (p_s != nullptr) {
            apply(p_lua, 2, *p_s);
        }
        lua_pushboolean(p_lua, p_s != nullptr);
        return 1;
    }

    /** game.get(id) -> x, y, frame, vx, vy (nil nếu không còn) */
    static int get(lua_State* p_lua)
    {
        const Sprite* const p_s = sprite_arg(p_lua, 1);
        if (p_s == nullptr) {
            lua_pushnil(p_lua);
            return 1;
        }
        lua_pushnumber(p_lua, p_s->x);
        lua_pushnumber(p_lua, p_s->y);
        lua_pushinteger(p_lua, p_s->frame);
        lua_pushnumber(p_lua, p_s->vx);
        lua_pushnumber(p_lua, p_s->vy);
        return 5;
    }

    /** game.alive(id) -> bool */
    static int alive(lua_State* p_lua)
    {
        lua_pushboolean(p_lua, sprite_arg(p_lua, 1) != nullptr);
        return 1;
    }

    /** game.play(id, {from=, to=, fps=10, loop=true, done=fn}) */
    static int play(lua_State* p_lua)
    {
        ScriptHost& h = host(p_lua);
        const auto id = static_cast<int32_t>(luaL_checkinteger(p_lua, 1));
        luaL_checktype(p_lua, 2, LUA_TTABLE);
        if (scene(p_lua).world().get(id) == nullptr) {
            return 0;
        }
        (void)lua_getfield(p_lua, 2, "from");
        (void)lua_getfield(p_lua, 2, "to");
        (void)lua_getfield(p_lua, 2, "fps");
        (void)lua_getfield(p_lua, 2, "loop");
        const auto from = static_cast<uint16_t>(luaL_optinteger(p_lua, -4, 0));
        const auto to = static_cast<uint16_t>(luaL_optinteger(p_lua, -3, from));
        const lua_Integer fps = luaL_optinteger(p_lua, -2, 10);
        const bool loop = lua_isnil(p_lua, -1) || (lua_toboolean(p_lua, -1) != 0);
        lua_pop(p_lua, 4);
        luaL_argcheck(p_lua, fps > 0, 2, "fps must be > 0");
        (void)lua_getfield(p_lua, 2, "done");
        int& ref = h.m_game_anim_refs[slot_of(id)];
        unref(p_lua, ref);
        ref = ref_arg(p_lua, lua_gettop(p_lua));
        lua_pop(p_lua, 1);
        scene(p_lua).world().play(id, from, to, static_cast<uint16_t>(MS_PER_S / static_cast<uint32_t>(fps)), loop);
        return 0;
    }

    /** game.move_to(id, x, y, ms, done) */
    static int move_to(lua_State* p_lua)
    {
        ScriptHost& h = host(p_lua);
        const auto id = static_cast<int32_t>(luaL_checkinteger(p_lua, 1));
        const auto x = static_cast<float>(luaL_checknumber(p_lua, 2));
        const auto y = static_cast<float>(luaL_checknumber(p_lua, 3));
        const lua_Integer ms = luaL_checkinteger(p_lua, 4);
        if (scene(p_lua).world().get(id) == nullptr) {
            return 0;
        }
        int& ref = h.m_game_move_refs[slot_of(id)];
        unref(p_lua, ref);
        ref = ref_arg(p_lua, 5);
        scene(p_lua).world().move_to(id, x, y, static_cast<uint32_t>((ms < 0) ? 0 : ms));
        return 0;
    }

    /** game.life(id, ms) */
    static int life(lua_State* p_lua)
    {
        const auto id = static_cast<int32_t>(luaL_checkinteger(p_lua, 1));
        const lua_Integer ms = luaL_checkinteger(p_lua, 2);
        scene(p_lua).world().set_life(id, static_cast<uint32_t>((ms < 0) ? 0 : ms));
        return 0;
    }

    static void release_slot_refs(lua_State* p_lua, int32_t id)
    {
        ScriptHost& h = host(p_lua);
        const size_t slot = slot_of(id);
        unref(p_lua, h.m_game_anim_refs[slot]);
        unref(p_lua, h.m_game_move_refs[slot]);
    }

    /** game.remove(id) */
    static int remove(lua_State* p_lua)
    {
        const auto id = static_cast<int32_t>(luaL_checkinteger(p_lua, 1));
        GameWorld& world = scene(p_lua).world();
        if (world.get(id) != nullptr) {
            release_slot_refs(p_lua, id);
            world.remove(id);
        }
        return 0;
    }

    /** game.clear(): xóa mọi sprite (giữ sheet, nền, callback) */
    static int clear(lua_State* p_lua)
    {
        GameWorld& world = scene(p_lua).world();
        for (size_t slot = 0U; slot < GameWorld::MAX_SPRITES; ++slot) {
            const int32_t id = world.id_at(slot);
            if (id != GameWorld::INVALID) {
                release_slot_refs(p_lua, id);
                world.remove(id);
            }
        }
        return 0;
    }

    /** game.on_overlap(group_a, group_b, fn|nil): fn(a_id, b_id) khi bắt đầu chạm */
    static int on_overlap(lua_State* p_lua)
    {
        ScriptHost& h = host(p_lua);
        const lua_Integer ga = luaL_checkinteger(p_lua, 1);
        const lua_Integer gb = luaL_checkinteger(p_lua, 2);
        const auto max = static_cast<lua_Integer>(GameWorld::MAX_GROUPS);
        luaL_argcheck(p_lua, (ga >= 0) && (ga < max), 1, "group must be 0..15");
        luaL_argcheck(p_lua, (gb >= 0) && (gb < max), 2, "group must be 0..15");
        int& ref = h.m_game_overlap_refs[ga][gb];
        unref(p_lua, ref);
        ref = ref_arg(p_lua, 3);
        scene(p_lua).world().watch_overlap(static_cast<uint8_t>(ga), static_cast<uint8_t>(gb), ref != LUA_NOREF);
        return 0;
    }

    static int set_callback(lua_State* p_lua, int& ref)
    {
        unref(p_lua, ref);
        ref = ref_arg(p_lua, 1);
        return 0;
    }

    /** game.on_update(fn(dt_ms)) */
    static int on_update(lua_State* p_lua) { return set_callback(p_lua, host(p_lua).m_game_update_ref); }

    /** game.on_touch(fn(phase, x, y)) */
    static int on_touch_set(lua_State* p_lua) { return set_callback(p_lua, host(p_lua).m_game_touch_ref); }

    /** game.pick(x, y [, group]) -> id | nil */
    static int pick(lua_State* p_lua)
    {
        const auto x = static_cast<int32_t>(luaL_checkinteger(p_lua, 1));
        const auto y = static_cast<int32_t>(luaL_checkinteger(p_lua, 2));
        const auto group = static_cast<int16_t>(luaL_optinteger(p_lua, 3, -1));
        const int32_t id = scene(p_lua).world().pick(x, y, group);
        if (id == GameWorld::INVALID) {
            lua_pushnil(p_lua);
        } else {
            lua_pushinteger(p_lua, id);
        }
        return 1;
    }

    /** game.pause(bool) */
    static int pause(lua_State* p_lua)
    {
        scene(p_lua).set_paused(lua_toboolean(p_lua, 1) != 0);
        return 0;
    }

    /** game.stats() -> {sprites=, fps=, tick_us=, update_us=, draw_us=} */
    static int stats(lua_State* p_lua)
    {
        GameScene::Stats st{};
        scene(p_lua).stats(st);
        lua_createtable(p_lua, 0, 5);
        lua_pushinteger(p_lua, st.sprites);
        lua_setfield(p_lua, -2, "sprites");
        lua_pushinteger(p_lua, st.fps);
        lua_setfield(p_lua, -2, "fps");
        lua_pushinteger(p_lua, st.tick_us);
        lua_setfield(p_lua, -2, "tick_us");
        lua_pushinteger(p_lua, st.logic_us);
        lua_setfield(p_lua, -2, "update_us");
        lua_pushinteger(p_lua, st.draw_us);
        lua_setfield(p_lua, -2, "draw_us");
        return 1;
    }
};

void ScriptHost::register_game(lua_State* p_lua)
{
    static const luaL_Reg GAME[] = {{"scene", GameApi::scene_new},
                                    {"sheet", GameApi::sheet},
                                    {"sprite", GameApi::sprite},
                                    {"set", GameApi::set},
                                    {"get", GameApi::get},
                                    {"alive", GameApi::alive},
                                    {"play", GameApi::play},
                                    {"move_to", GameApi::move_to},
                                    {"life", GameApi::life},
                                    {"remove", GameApi::remove},
                                    {"clear", GameApi::clear},
                                    {"on_overlap", GameApi::on_overlap},
                                    {"on_touch", GameApi::on_touch_set},
                                    {"on_update", GameApi::on_update},
                                    {"pick", GameApi::pick},
                                    {"pause", GameApi::pause},
                                    {"stats", GameApi::stats},
                                    {nullptr, nullptr}};
    luaL_newlib(p_lua, GAME);
}

void ScriptHost::reset_game()
{
    m_scene.destroy();
    m_game_update_ref = LUA_NOREF; // VM đóng ngay sau: không cần unref.
    m_game_touch_ref = LUA_NOREF;
    for (auto& row : m_game_overlap_refs) {
        for (int& ref : row) {
            ref = LUA_NOREF;
        }
    }
    for (size_t slot = 0U; slot < GameWorld::MAX_SPRITES; ++slot) {
        m_game_anim_refs[slot] = LUA_NOREF;
        m_game_move_refs[slot] = LUA_NOREF;
    }
}

} // namespace runtime
