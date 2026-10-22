# QuickJS (quickjs-ng, MIT): the JavaScript engine the in-game tracker runs the tracker's own logic in
# (native/src/pc/features/pc_tracker_logic.cpp, docs/TRACKER.md "In the game"). Only the engine's core
# (no quickjs-libc: the script gets no files, no OS), as the static library cos_quickjs, which cos_pc
# links publicly.
include_guard(GLOBAL)
include(FetchContent)

set(COS_QUICKJS_REPOSITORY "https://github.com/quickjs-ng/quickjs.git" CACHE STRING "quickjs-ng repository")
set(COS_QUICKJS_COMMIT "3c9afc9943323ee9c7dbd123c0cd991448f4b6c2" CACHE STRING "quickjs-ng commit (v0.10.1)")

FetchContent_Declare(cos_quickjs_src
        GIT_REPOSITORY "${COS_QUICKJS_REPOSITORY}"
        GIT_TAG "${COS_QUICKJS_COMMIT}"
        GIT_SHALLOW FALSE
        # sources only: a subdirectory that does not exist, so its own CMakeLists.txt (which also
        # builds the qjs command line tool) is not added
        SOURCE_SUBDIR cos-sources-only)
FetchContent_MakeAvailable(cos_quickjs_src)

add_library(cos_quickjs STATIC
        "${cos_quickjs_src_SOURCE_DIR}/cutils.c"
        "${cos_quickjs_src_SOURCE_DIR}/libregexp.c"
        "${cos_quickjs_src_SOURCE_DIR}/libunicode.c"
        "${cos_quickjs_src_SOURCE_DIR}/quickjs.c"
        "${cos_quickjs_src_SOURCE_DIR}/xsum.c")
target_include_directories(cos_quickjs SYSTEM PUBLIC "${cos_quickjs_src_SOURCE_DIR}")
target_compile_definitions(cos_quickjs PRIVATE _GNU_SOURCE)
# its own warnings are not ours (the game's flags come from GameConfig.cmake's directory options)
target_compile_options(cos_quickjs PRIVATE -w)
set_target_properties(cos_quickjs PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
find_package(Threads)
target_link_libraries(cos_quickjs PUBLIC ${CMAKE_THREAD_LIBS_INIT})
if (NOT APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(cos_quickjs PUBLIC m)
endif ()
