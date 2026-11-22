# Lua 5.4.7 (MIT, https://www.lua.org) – máy ảo chạy app cài thêm của gach-os.
# Bản gốc + 1 patch ở luaconf.h (xem README.md); cấu hình qua compile definitions.
#   LUA_INT_DEFAULT=LUA_INT_INT: số nguyên 32 bit (phép nguyên nhanh trên CPU 32 bit);
#   số thực giữ double (mặc định Lua) để app tính toán chính xác ~15 chữ số (VD: máy tính).
# Bỏ các thư viện không dùng trong sandbox: io, os, package/loadlib, debug, linit (runtime tự mở thư viện).
set(LUA_SRCS lapi.c lauxlib.c lbaselib.c lcode.c lcorolib.c lctype.c ldebug.c ldo.c ldump.c lfunc.c lgc.c
             llex.c lmathlib.c lmem.c lobject.c lopcodes.c lparser.c lstate.c lstring.c lstrlib.c ltable.c
             ltablib.c ltm.c lundump.c lutf8lib.c lvm.c lzio.c)
list(TRANSFORM LUA_SRCS PREPEND "src/")

idf_component_register(SRCS ${LUA_SRCS}
                       INCLUDE_DIRS "src")

target_compile_definitions(${COMPONENT_LIB} PUBLIC LUA_INT_DEFAULT=LUA_INT_INT)
# Mã nguồn bên thứ ba: không áp OS_STRICT_FLAGS; tắt cảnh báo vô hại để không vướng -Werror=all của IDF.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-but-set-variable -Wno-implicit-fallthrough)
