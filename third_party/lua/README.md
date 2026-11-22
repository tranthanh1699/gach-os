# Lua 5.4.7 (vendored)

- Nguồn: https://www.lua.org/ftp/lua-5.4.7.tar.gz
- SHA-256: `9fbf5e28ef86c69858f6d3d34eccc32e911c1a28b4120ff3e84aaa70cfbf1e30`
- Giấy phép: MIT (xem `LICENSE`).
- Đã bỏ `lua.c`, `luac.c`, `Makefile`.
- **Một patch duy nhất** (`src/luaconf.h`): bọc `#define LUA_32BITS 0` bằng `#if !defined(LUA_32BITS)` để cấu hình 32-bit qua compile definition. Khi nâng cấp Lua phải áp lại patch này.
- Chỉ tầng `src/runtime` được include header Lua (cưỡng chế bởi `tools/layer_matrix.json`).
