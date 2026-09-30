cmake_minimum_required(VERSION 3.20)
project(emerald_launcher C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Shared source — same code on both platforms (platform bits behind #ifdef).
set(EL_SOURCES
    src/main.cpp
    src/db.cpp
    src/launcher.cpp
    src/shelf.cpp
    src/boxart.cpp
    src/uikit.cpp
    src/prefs.cpp
    src/branding.cpp
    src/overlay.c
    src/core.c
    src/renderer.c
    src/scene3d.c
    src/boxshape.c
    src/fsutil.cpp
    src/ui.c
    src/gl.c
    src/sfx.c
)

# ======================================================================
#  Nintendo Switch (devkitPro)
#  Build:  cmake .. -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Switch.cmake
# ======================================================================
if(CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
    list(APPEND CMAKE_PREFIX_PATH
        "$ENV{DEVKITPRO}/portlibs/switch"
        "$ENV{DEVKITPRO}/libnx")

    find_package(SDL2 REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2_IMAGE REQUIRED SDL2_image)
    pkg_check_modules(SDL2_TTF   REQUIRED SDL2_ttf)

    add_executable(emerald_launcher ${EL_SOURCES})
    target_compile_definitions(emerald_launcher PRIVATE NINTENDO_SWITCH=1)
    target_include_directories(emerald_launcher PRIVATE
        src include
        "$ENV{DEVKITPRO}/portlibs/switch/include"
        "$ENV{DEVKITPRO}/portlibs/switch/include/SDL2"
        "$ENV{DEVKITPRO}/libnx/include")

    # libretro-common: shared utility code the cores expect the frontend to
    # provide (file/path/string/stream helpers). Built with STATIC_LINKING the
    # cores omit it, so we compile it here once for all statically-linked cores.
    set(LRC "${CMAKE_SOURCE_DIR}/cores-src/gambatte-libretro/libgambatte/libretro-common")
    target_include_directories(emerald_launcher PRIVATE "${LRC}/include")
    target_sources(emerald_launcher PRIVATE
        "${LRC}/compat/compat_posix_string.c"
        "${LRC}/compat/compat_snprintf.c"
        "${LRC}/compat/compat_strcasestr.c"
        "${LRC}/compat/compat_strl.c"
        "${LRC}/compat/fopen_utf8.c"
        "${LRC}/encodings/encoding_utf.c"
        "${LRC}/file/file_path.c"
        "${LRC}/file/file_path_io.c"
        "${LRC}/streams/file_stream.c"
        "${LRC}/streams/file_stream_transforms.c"
        "${LRC}/string/stdstring.c"
        "${LRC}/time/rtime.c"
        "${LRC}/vfs/vfs_implementation.c")

    # ── Built-in libretro cores ──────────────────────────────────────────
    # Every static core exports the same libretro API (retro_init, ...), so
    # linking more than one collides. We rename each core's API symbols to a
    # per-core prefix (objcopy --redefine-syms) so they coexist; core.c then
    # dispatches to the right set (gambatte_retro_init / snes9x_retro_init/...).
    set(LIBRETRO_API_SYMS
        retro_set_environment retro_set_video_refresh retro_set_audio_sample
        retro_set_audio_sample_batch retro_set_input_poll retro_set_input_state
        retro_init retro_deinit retro_api_version retro_get_system_info
        retro_get_system_av_info retro_set_controller_port_device retro_reset
        retro_run retro_serialize_size retro_serialize retro_unserialize
        retro_cheat_reset retro_cheat_set retro_load_game retro_load_game_special
        retro_unload_game retro_get_region retro_get_memory_data retro_get_memory_size
        # Internal globals that collide across cores and are cross-referenced
        # within a core (so must be renamed, not localized). Add new clashes here
        # (find them with: nm <core>.a | grep ' [TDBR] ' across cores | uniq -d).
        PPU)

    if(NOT CMAKE_OBJCOPY)
        set(CMAKE_OBJCOPY "$ENV{DEVKITPRO}/devkitA64/bin/aarch64-none-elf-objcopy")
    endif()

    set(EL_CORE_LIBS "")
    function(el_add_builtin_core prefix archive)
        set(_syms "")
        foreach(_s ${LIBRETRO_API_SYMS})
            string(APPEND _syms "${_s} ${prefix}_${_s}\n")
        endforeach()
        set(_symfile "${CMAKE_BINARY_DIR}/${prefix}.syms")
        file(WRITE "${_symfile}" "${_syms}")
        set(_out "${CMAKE_BINARY_DIR}/${prefix}_prefixed.a")
        # --redefine-syms: prefix the libretro API (+ PPU) so cores don't collide.
        # -L option_defs_*/options_*/option_cats_*: localize each core's core-options
        #   tables (same names across cores, used only inside libretro.o). NOTE: must
        #   be these exact prefixes, not a blanket "option*" — fceumm's option_ramstate
        #   is a cross-.o global that breaks if localized.
        # -L strtof_l: mgba redefines this libc symbol; localize it to avoid a
        #   multiple-definition clash with newlib.
        add_custom_command(
            OUTPUT  "${_out}"
            COMMAND "${CMAKE_OBJCOPY}" --redefine-syms=${_symfile} -w
                    -L "option_defs_*" -L "options_*" -L "option_cats_*" -L "strtof_l"
                    "${archive}" "${_out}"
            DEPENDS "${archive}" "${_symfile}"
            COMMENT "Prefixing libretro symbols (${prefix}_)")
        list(APPEND EL_CORE_LIBS "${_out}")
        set(EL_CORE_LIBS "${EL_CORE_LIBS}" PARENT_SCOPE)
    endfunction()

    el_add_builtin_core(gambatte "${CMAKE_SOURCE_DIR}/switch/cores/gambatte_libretro_libnx.a")
    el_add_builtin_core(snes9x   "${CMAKE_SOURCE_DIR}/switch/cores/snes9x_libretro_libnx.a")
    el_add_builtin_core(fceumm   "${CMAKE_SOURCE_DIR}/switch/cores/fceumm_libretro_libnx.a")
    el_add_builtin_core(mgba     "${CMAKE_SOURCE_DIR}/switch/cores/mgba_libretro_libnx.a")

    add_custom_target(el_prefix_cores DEPENDS ${EL_CORE_LIBS})
    add_dependencies(emerald_launcher el_prefix_cores)

    # Static link order matters: prefixed cores, satellite libs + deps, SDL2, GLES, nx.
    target_link_libraries(emerald_launcher PRIVATE
        ${EL_CORE_LIBS}
        ${SDL2_IMAGE_STATIC_LIBRARIES}
        ${SDL2_TTF_STATIC_LIBRARIES}
        SDL2::SDL2main SDL2::SDL2
        EGL GLESv2 glapi drm_nouveau
        nx)

    # Assemble the NRO romfs at CONFIGURE time — nx_create_nro checks it exists.
    set(EL_ROMFS "${CMAKE_BINARY_DIR}/romfs")
    file(MAKE_DIRECTORY "${EL_ROMFS}")
    file(COPY "${CMAKE_SOURCE_DIR}/imgs"        DESTINATION "${EL_ROMFS}")
    file(COPY "${CMAKE_SOURCE_DIR}/sounds"      DESTINATION "${EL_ROMFS}")
    file(COPY "${CMAKE_SOURCE_DIR}/alagard.ttf" DESTINATION "${EL_ROMFS}")

    nx_generate_nacp(emerald_launcher.nacp
        NAME    "Emerald Launcher"
        AUTHOR  "IgnacioLeivaP"
        VERSION "2.0.0")

    nx_create_nro(emerald_launcher
        NACP  emerald_launcher.nacp
        ICON  "${CMAKE_SOURCE_DIR}/switch/icon.jpg"
        ROMFS "${EL_ROMFS}")

# ======================================================================
#  PC (Windows / Linux desktop OpenGL 3.3)
# ======================================================================
else()
    # EL_GLES: render with OpenGL ES 3.0 exactly like the Switch build does
    # (needs a GLES driver, e.g. Mesa). Handy to check the Switch render path
    # — shaders included — on a PC.
    option(EL_GLES "Desktop build using OpenGL ES 3.0 (mirrors the Switch renderer)" OFF)

    find_package(SDL2 REQUIRED)
    find_package(SDL2_ttf REQUIRED)
    find_package(SDL2_image REQUIRED)

    add_executable(emerald_launcher ${EL_SOURCES})
    target_include_directories(emerald_launcher PRIVATE src include)
    target_link_libraries(emerald_launcher PRIVATE
        SDL2::SDL2main
        SDL2::SDL2
        SDL2_ttf::SDL2_ttf
        SDL2_image::SDL2_image)
    if(EL_GLES)
        find_library(GLESV2_LIBRARY NAMES GLESv2 REQUIRED)
        target_compile_definitions(emerald_launcher PRIVATE EL_GLES=1)
        target_link_libraries(emerald_launcher PRIVATE ${GLESV2_LIBRARY})
    else()
        find_package(OpenGL REQUIRED)
        target_link_libraries(emerald_launcher PRIVATE OpenGL::GL)
    endif()

    if(WIN32)
        target_sources(emerald_launcher PRIVATE resources.rc)
        target_link_libraries(emerald_launcher PRIVATE comdlg32)
        add_custom_command(TARGET emerald_launcher POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL2::SDL2>
                $<TARGET_FILE_DIR:emerald_launcher>)
    endif()
endif()
