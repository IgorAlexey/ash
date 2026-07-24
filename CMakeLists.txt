cmake_minimum_required(VERSION 3.20)
project(ash C)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR
    "ash is Linux-only by design (see DOCTRINE.md, Platform). ucontext, "
    "pidfd, eventfd, epoll, and the --whole-archive layering check all "
    "assume Linux and glibc.")
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(
  -Werror -Wall -Wextra
  -Wconversion -Wsign-conversion
  -Wmissing-prototypes -Wmissing-declarations
  -Wshadow -Wpointer-arith -Wstrict-prototypes -Wwrite-strings
  -Wvla -Werror=vla -Walloca -Werror=alloca
  -Wswitch-enum
  -fvisibility=hidden -fno-common)
add_compile_definitions(_GNU_SOURCE)

option(ASH_SANITIZE "build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
if(ASH_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
  add_link_options(-fsanitize=address,undefined)
endif()

option(ASH_FUZZ "build libFuzzer harnesses (clang only)" OFF)
if(ASH_FUZZ)
  add_compile_options(-fsanitize=fuzzer-no-link,address,undefined -g -O1)
  add_link_options(-fsanitize=address,undefined)
endif()

include_directories(include)

add_library(ash_base OBJECT
  src/base/status.c
  src/base/arena.c
  src/base/buf.c
  src/base/slice.c
  src/base/json.c
  src/base/sha256.c
  src/base/base64.c)

find_package(CURL REQUIRED)

add_library(ash_ai STATIC $<TARGET_OBJECTS:ash_base>
  src/ai/version.c
  src/ai/sse.c
  src/ai/http.c
  src/ai/registry.c
  src/ai/provider.c)
set_target_properties(ash_ai PROPERTIES OUTPUT_NAME ash-ai)
target_link_libraries(ash_ai PUBLIC CURL::libcurl)

add_library(ash_fb STATIC $<TARGET_OBJECTS:ash_base>
  src/fb/oklab.c
  src/term/vtout.c
  src/fb/fb.c
  src/fb/scrollback.c)
set_target_properties(ash_fb PROPERTIES OUTPUT_NAME ash-fb)
target_link_libraries(ash_fb PUBLIC m)

add_library(ash_ui STATIC
  src/ui/keys.c
  src/ui/textarea.c
  src/ui/footer.c
  src/ui/tui.c
  src/ui/menubar.c
  src/ui/fuzzy.c
  src/ui/dialog.c
  src/ui/settings_modal.c)
set_target_properties(ash_ui PROPERTIES OUTPUT_NAME ash-ui)
target_link_libraries(ash_ui PUBLIC ash_fb)

add_library(ash_core STATIC
  src/core/version.c
  src/core/coro.c
  src/core/proc.c
  src/core/manifest.c
  src/core/config.c
  src/core/settings.c
  src/core/auth.c
  src/core/oauth.c
  src/core/session/scan.c
  src/core/session/log.c)
set_target_properties(ash_core PROPERTIES OUTPUT_NAME ash)
target_link_libraries(ash_core PUBLIC ash_ai)

add_library(ash_tools STATIC
  src/tools/registry.c
  src/tools/util.c
  src/tools/bash.c
  src/tools/read.c
  src/tools/write.c
  src/tools/edit.c
  src/tools/grep.c
  src/tools/ls.c)
set_target_properties(ash_tools PROPERTIES OUTPUT_NAME ash-tools)
target_link_libraries(ash_tools PUBLIC ash_core)

add_library(ash_edit STATIC
  src/edit/gapbuf.c
  src/edit/measure.c
  src/edit/editor.c
  src/edit/diffview.c)
set_target_properties(ash_edit PROPERTIES OUTPUT_NAME ash-edit)
target_link_libraries(ash_edit PUBLIC ash_fb ash_tools)

add_subdirectory(third_party/lua)

add_library(ash_ext STATIC src/ext/ext.c)
set_target_properties(ash_ext PROPERTIES OUTPUT_NAME ash-ext)
target_link_libraries(ash_ext PUBLIC ash_tools lua54)

add_executable(ash
  src/app/main.c
  src/app/loop.c
  src/app/bang.c
  src/app/transcript.c
  src/app/queue.c
  src/app/select.c
  src/app/rpc.c
  src/app/tokensrc.c
  src/term/screen.c
  src/term/signals.c
  src/term/input.c)
target_link_libraries(ash PRIVATE ash_tools ash_ui)

enable_testing()
add_subdirectory(tests)
