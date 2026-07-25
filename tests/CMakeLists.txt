function(ash_unit_test name)
  add_executable(${name} base/${name}.c $<TARGET_OBJECTS:ash_base>)
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME ${name} COMMAND ${name})
endfunction()

ash_unit_test(status_test)
ash_unit_test(arena_test)
ash_unit_test(arena_death_test)
ash_unit_test(buf_test)
ash_unit_test(slice_test)
ash_unit_test(json_test)
ash_unit_test(sha256_test)
ash_unit_test(base64_test)

set(LAYER ${CMAKE_CURRENT_SOURCE_DIR}/layering)
set(POLICY ${CMAKE_CURRENT_SOURCE_DIR}/policy)

set(ASH_LINK_EXTRA "-lcurl")
if(ASH_SANITIZE)
  set(ASH_LINK_EXTRA "${ASH_LINK_EXTRA} -fsanitize=address,undefined")
endif()
add_test(NAME layering_link COMMAND ${LAYER}/check_link.sh ${CMAKE_BINARY_DIR})
set_tests_properties(layering_link PROPERTIES
  ENVIRONMENT "CC=${CMAKE_C_COMPILER};EXTRA_LIBS=${ASH_LINK_EXTRA}")
add_test(NAME layering_symbols COMMAND ${LAYER}/check_symbols.sh ${CMAKE_BINARY_DIR})
add_test(NAME layering_includes COMMAND ${LAYER}/check_includes.sh ${CMAKE_SOURCE_DIR})

add_test(NAME policy_escapes COMMAND ${POLICY}/check_escapes.sh ${CMAKE_SOURCE_DIR})
add_test(NAME policy_malloc COMMAND ${POLICY}/check_malloc.sh ${CMAKE_SOURCE_DIR})
add_test(NAME policy_dlopen COMMAND ${POLICY}/check_dlopen.sh ${CMAKE_SOURCE_DIR})
add_test(NAME policy_handlers COMMAND ${POLICY}/check_handlers.sh ${CMAKE_SOURCE_DIR})

add_executable(screen_test term/screen_test.c
  ${CMAKE_SOURCE_DIR}/src/term/screen.c $<TARGET_OBJECTS:ash_base>)
target_include_directories(screen_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(screen_test PRIVATE util)
add_test(NAME screen_test COMMAND screen_test)

add_executable(signals_test term/signals_test.c
  ${CMAKE_SOURCE_DIR}/src/term/screen.c ${CMAKE_SOURCE_DIR}/src/term/signals.c
  $<TARGET_OBJECTS:ash_base>)
target_include_directories(signals_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(signals_test PRIVATE util)
add_test(NAME signals_test COMMAND signals_test)
set_tests_properties(signals_test PROPERTIES
  ENVIRONMENT "ASAN_OPTIONS=handle_segv=0:handle_abort=0:abort_on_error=0:detect_leaks=0")

add_executable(input_test term/input_test.c
  ${CMAKE_SOURCE_DIR}/src/term/input.c $<TARGET_OBJECTS:ash_base>)
target_include_directories(input_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME input_test COMMAND input_test)

add_executable(sse_test ai/sse_test.c)
target_include_directories(sse_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sse_test PRIVATE ash_ai)
add_test(NAME sse_test COMMAND sse_test)

add_executable(http_test ai/http_test.c)
target_include_directories(http_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(http_test PRIVATE ash_ai)
add_test(NAME http_test COMMAND http_test)

add_executable(provider_test ai/provider_test.c)
target_include_directories(provider_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(provider_test PRIVATE ash_ai)
add_test(NAME provider_test COMMAND provider_test)

add_executable(wire_test ai/wire_test.c)
target_include_directories(wire_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(wire_test PRIVATE ash_ai)
add_test(NAME wire_test COMMAND wire_test)

add_executable(registry_test ai/registry_test.c)
target_include_directories(registry_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(registry_test PRIVATE ash_ai m)
add_test(NAME registry_test COMMAND registry_test)

add_executable(smoke_deepseek ai/smoke_deepseek.c)
target_include_directories(smoke_deepseek PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(smoke_deepseek PRIVATE ash_ai)

add_executable(smoke_deepseek_tool ai/smoke_deepseek_tool.c)
target_include_directories(smoke_deepseek_tool PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(smoke_deepseek_tool PRIVATE ash_ai)

add_executable(sse_cap_test ai/sse_cap_test.c
  ${CMAKE_SOURCE_DIR}/src/ai/sse.c $<TARGET_OBJECTS:ash_base>)
target_include_directories(sse_cap_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(sse_cap_test PRIVATE
  ASH_SSE_MAX_LINE=256u ASH_SSE_MAX_DATA=4096u)
add_test(NAME sse_cap_test COMMAND sse_cap_test)

if(ASH_FUZZ)
  add_executable(fuzz_sse fuzz/fuzz_sse.c)
  target_include_directories(fuzz_sse PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(fuzz_sse PRIVATE ash_ai)
  target_link_options(fuzz_sse PRIVATE -fsanitize=fuzzer)

  add_executable(fuzz_input fuzz/fuzz_input.c
    ${CMAKE_SOURCE_DIR}/src/term/input.c $<TARGET_OBJECTS:ash_base>)
  target_include_directories(fuzz_input PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_options(fuzz_input PRIVATE -fsanitize=fuzzer)

  add_executable(fuzz_mouse fuzz/fuzz_mouse.c
    ${CMAKE_SOURCE_DIR}/src/term/input.c $<TARGET_OBJECTS:ash_base>)
  target_include_directories(fuzz_mouse PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_options(fuzz_mouse PRIVATE -fsanitize=fuzzer)

  add_executable(fuzz_session fuzz/fuzz_session.c
    ${CMAKE_SOURCE_DIR}/src/core/session/scan.c $<TARGET_OBJECTS:ash_base>)
  target_include_directories(fuzz_session PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_options(fuzz_session PRIVATE -fsanitize=fuzzer)

  add_executable(fuzz_json fuzz/fuzz_json.c $<TARGET_OBJECTS:ash_base>)
  target_include_directories(fuzz_json PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_options(fuzz_json PRIVATE -fsanitize=fuzzer)

  add_executable(fuzz_settings fuzz/fuzz_settings.c)
  target_include_directories(fuzz_settings PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(fuzz_settings PRIVATE ash_core)
  target_link_options(fuzz_settings PRIVATE -fsanitize=fuzzer)

  add_executable(fuzz_editmatch fuzz/fuzz_editmatch.c)
  target_include_directories(fuzz_editmatch PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(fuzz_editmatch PRIVATE ash_tools)
  target_link_options(fuzz_editmatch PRIVATE -fsanitize=fuzzer)

  add_executable(fuzz_rpc fuzz/fuzz_rpc.c ${CMAKE_SOURCE_DIR}/src/app/rpc.c)
  target_include_directories(fuzz_rpc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(fuzz_rpc PRIVATE ash_tools)
  target_link_options(fuzz_rpc PRIVATE -fsanitize=fuzzer)

  add_executable(fuzz_gapbuf fuzz/fuzz_gapbuf.c)
  target_include_directories(fuzz_gapbuf PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(fuzz_gapbuf PRIVATE ash_edit)
  target_link_options(fuzz_gapbuf PRIVATE -fsanitize=fuzzer)
endif()

add_executable(session_test core/session_test.c)
target_include_directories(session_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(session_test PRIVATE ash_core)
add_test(NAME session_test COMMAND session_test)

add_executable(config_test core/config_test.c)
target_include_directories(config_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(config_test PRIVATE ash_core)
add_test(NAME config_test COMMAND config_test)

add_executable(settings_test core/settings_test.c)
target_include_directories(settings_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(settings_test PRIVATE ash_core)
add_test(NAME settings_test COMMAND settings_test)

add_executable(auth_test core/auth_test.c)
target_include_directories(auth_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(auth_test PRIVATE ash_core)
add_test(NAME auth_test COMMAND auth_test)

add_executable(oauth_test core/oauth_test.c)
target_include_directories(oauth_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(oauth_test PRIVATE ash_core)
add_test(NAME oauth_test COMMAND oauth_test)

add_executable(oauth_header_test ai/oauth_header_test.c)
target_include_directories(oauth_header_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(oauth_header_test PRIVATE ash_ai)
add_test(NAME oauth_header_test COMMAND oauth_header_test)

add_executable(manifest_test core/manifest_test.c)
target_include_directories(manifest_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(manifest_test PRIVATE ash_core)
target_compile_definitions(manifest_test PRIVATE
  ASH_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
add_test(NAME manifest_test COMMAND manifest_test)

add_executable(coro_test core/coro_test.c)
target_include_directories(coro_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(coro_test PRIVATE ash_core)
add_test(NAME coro_test COMMAND coro_test)

add_executable(proc_test core/proc_test.c)
target_include_directories(proc_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(proc_test PRIVATE ash_core)
add_test(NAME proc_test COMMAND proc_test)

add_executable(loop_test app/loop_test.c
  ${CMAKE_SOURCE_DIR}/src/app/agent.c
  ${CMAKE_SOURCE_DIR}/src/app/loop.c
  ${CMAKE_SOURCE_DIR}/src/app/bang.c
  ${CMAKE_SOURCE_DIR}/src/app/transcript.c
  ${CMAKE_SOURCE_DIR}/src/app/queue.c
  ${CMAKE_SOURCE_DIR}/src/app/select.c
  ${CMAKE_SOURCE_DIR}/src/app/tokensrc.c
  ${CMAKE_SOURCE_DIR}/src/term/input.c
  ${CMAKE_SOURCE_DIR}/src/term/screen.c
  ${CMAKE_SOURCE_DIR}/src/term/signals.c)
target_include_directories(loop_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(loop_test PRIVATE ash_tools ash_ui ash_edit)
add_test(NAME loop_test COMMAND loop_test)

add_executable(bang_test app/bang_test.c
  ${CMAKE_SOURCE_DIR}/src/app/bang.c)
target_include_directories(bang_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME bang_test COMMAND bang_test)

add_executable(queue_test app/queue_test.c
  ${CMAKE_SOURCE_DIR}/src/app/queue.c)
target_include_directories(queue_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(queue_test PRIVATE ash_fb)
target_compile_definitions(queue_test PRIVATE
  ASH_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/app/golden")
add_test(NAME queue_test COMMAND queue_test)

add_executable(transcript_test app/transcript_test.c
  ${CMAKE_SOURCE_DIR}/src/app/transcript.c)
target_include_directories(transcript_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(transcript_test PRIVATE ash_ui)
target_compile_definitions(transcript_test PRIVATE
  ASH_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/app/golden")
add_test(NAME transcript_test COMMAND transcript_test)

add_executable(transcript_input_test app/transcript_input_test.c
  ${CMAKE_SOURCE_DIR}/src/term/input.c $<TARGET_OBJECTS:ash_base>)
target_include_directories(transcript_input_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME transcript_input_test COMMAND transcript_input_test)

add_executable(select_test app/select_test.c
  ${CMAKE_SOURCE_DIR}/src/app/select.c)
target_include_directories(select_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(select_test PRIVATE ash_fb)
target_compile_definitions(select_test PRIVATE
  ASH_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/app/golden")
add_test(NAME select_test COMMAND select_test)

add_executable(rpc_test app/rpc_test.c
  ${CMAKE_SOURCE_DIR}/src/app/agent.c
  ${CMAKE_SOURCE_DIR}/src/app/rpc.c)
target_include_directories(rpc_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rpc_test PRIVATE ash_tools)
add_test(NAME rpc_test COMMAND rpc_test)

function(ash_tools_test name)
  add_executable(${name} tools/${name}.c)
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(${name} PRIVATE ash_tools)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

ash_tools_test(tools_registry_test)
ash_tools_test(tools_test)

function(ash_ext_test name)
  add_executable(${name} ext/${name}.c)
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(${name} PRIVATE ash_ext)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

ash_ext_test(ext_sandbox_test)
ash_ext_test(ext_host_test)

add_test(NAME ash_smoke COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/app/smoke.sh $<TARGET_FILE:ash>)

add_executable(keys_test ui/keys_test.c
  ${CMAKE_SOURCE_DIR}/src/term/input.c $<TARGET_OBJECTS:ash_base>)
target_include_directories(keys_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(keys_test PRIVATE ash_ui)
add_test(NAME keys_test COMMAND keys_test)

add_executable(textarea_test ui/textarea_test.c)
target_include_directories(textarea_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(textarea_test PRIVATE ash_ui)
add_test(NAME textarea_test COMMAND textarea_test)

add_executable(ui_render_test ui/render_test.c)
target_include_directories(ui_render_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ui_render_test PRIVATE ash_ui)
target_compile_definitions(ui_render_test PRIVATE
  ASH_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/ui/golden")
add_test(NAME ui_render_test COMMAND ui_render_test)

add_executable(footer_test ui/footer_test.c)
target_include_directories(footer_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(footer_test PRIVATE ash_ui)
target_compile_definitions(footer_test PRIVATE
  ASH_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/footer")
add_test(NAME footer_test COMMAND footer_test)
add_executable(tui_test ui/tui_test.c)
target_include_directories(tui_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tui_test PRIVATE ash_ui)
target_compile_definitions(tui_test PRIVATE
  ASH_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/ui/golden")
add_test(NAME tui_test COMMAND tui_test)

add_executable(menubar_test ui/menubar_test.c)
target_include_directories(menubar_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(menubar_test PRIVATE ash_ui)
target_compile_definitions(menubar_test PRIVATE
  ASH_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/ui/golden")
add_test(NAME menubar_test COMMAND menubar_test)

add_executable(fuzzy_test ui/fuzzy_test.c)
target_include_directories(fuzzy_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fuzzy_test PRIVATE ash_ui)
add_test(NAME fuzzy_test COMMAND fuzzy_test)

add_executable(dialog_test ui/dialog_test.c)
target_include_directories(dialog_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dialog_test PRIVATE ash_ui)
target_compile_definitions(dialog_test PRIVATE
  ASH_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/ui/golden")
add_test(NAME dialog_test COMMAND dialog_test)
add_executable(gapbuf_test edit/gapbuf_test.c)
target_include_directories(gapbuf_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gapbuf_test PRIVATE ash_edit)
add_test(NAME gapbuf_test COMMAND gapbuf_test)

add_executable(measure_test edit/measure_test.c)
target_include_directories(measure_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(measure_test PRIVATE ash_edit)
add_test(NAME measure_test COMMAND measure_test)

add_executable(editor_test edit/editor_test.c)
target_include_directories(editor_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(editor_test PRIVATE ash_edit)
target_compile_definitions(editor_test PRIVATE
  ASH_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/edit/golden")
add_test(NAME editor_test COMMAND editor_test)

add_executable(diff_test edit/diff_test.c)
target_include_directories(diff_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(diff_test PRIVATE ash_edit)
target_compile_definitions(diff_test PRIVATE
  ASH_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/edit/golden")
add_test(NAME diff_test COMMAND diff_test)
add_executable(settings_modal_test ui/settings_modal_test.c)
target_include_directories(settings_modal_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(settings_modal_test PRIVATE ash_ui)
target_compile_definitions(settings_modal_test PRIVATE
  ASH_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/ui/golden")
add_test(NAME settings_modal_test COMMAND settings_modal_test)

add_executable(oklab_test fb/oklab_test.c)
target_include_directories(oklab_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(oklab_test PRIVATE ash_fb)
add_test(NAME oklab_test COMMAND oklab_test)

add_executable(fb_test fb/fb_test.c)
target_include_directories(fb_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fb_test PRIVATE ash_fb)
add_test(NAME fb_test COMMAND fb_test)

add_executable(scrollback_test fb/scrollback_test.c)
target_include_directories(scrollback_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(scrollback_test PRIVATE ash_fb)
add_test(NAME scrollback_test COMMAND scrollback_test)

add_executable(golden_test fb/golden_test.c)
target_include_directories(golden_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(golden_test PRIVATE ash_fb)
target_compile_definitions(golden_test PRIVATE
  ASH_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/fb/golden")
add_test(NAME golden_test COMMAND golden_test)

if(ASH_SANITIZE)
  add_executable(coro_suar_test core/coro_suar_test.c)
  target_include_directories(coro_suar_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(coro_suar_test PRIVATE ash_core)
  add_test(NAME coro_suar
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/core/expect_suar.sh
            $<TARGET_FILE:coro_suar_test>)
  set_tests_properties(coro_suar PROPERTIES
    ENVIRONMENT
      "ASAN_OPTIONS=detect_stack_use_after_return=1:abort_on_error=1:detect_leaks=1")
endif()
