# Vendored Lua 5.4.7 (stock tree from https://www.lua.org/ftp/lua-5.4.7.tar.gz).
# Upstream sha256: 9fbf5e28ef86c69858f6d3d34eccc32e911c1a28b4120ff3e84aaa70cfbf1e30
# License: MIT (see third_party/lua/README and the copyright in src/lua.h).
#
# Static link only. The standalone interpreter (lua.c), the compiler
# (luac.c), and the libraries the ash_ext sandbox refuses to expose
# (io, os, package/require via dlopen, debug) are deliberately NOT
# compiled, so their symbols never enter the binary. ash_ext opens the
# safe libraries by hand (see src/ext/ext.c).

set(LUA_VERSION "5.4.7" CACHE STRING "vendored Lua version" FORCE)

add_library(lua54 STATIC
  src/lapi.c src/lcode.c src/lctype.c src/ldebug.c src/ldo.c src/ldump.c
  src/lfunc.c src/lgc.c src/llex.c src/lmem.c src/lobject.c src/lopcodes.c
  src/lparser.c src/lstate.c src/lstring.c src/ltable.c src/ltm.c
  src/lundump.c src/lvm.c src/lzio.c
  src/lauxlib.c
  src/lbaselib.c src/lcorolib.c src/lmathlib.c src/lstrlib.c src/ltablib.c
  src/lutf8lib.c)

target_include_directories(lua54 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(lua54 PRIVATE LUA_USE_LINUX)
target_link_libraries(lua54 PUBLIC m)

# Third-party code does not answer to our warning wall. Quiet it at the
# target only; project flags stay -Werror -Wall -Wextra everywhere else.
# UBSan is dropped for the vendored objects to avoid benign upstream
# reports; ASan stays on so the ash_ext allocator boundary is still
# checked.
target_compile_options(lua54 PRIVATE -w -Wno-error)
if(ASH_SANITIZE)
  target_compile_options(lua54 PRIVATE -fno-sanitize=undefined)
endif()
