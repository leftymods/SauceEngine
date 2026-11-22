cmake_minimum_required(VERSION 3.20)
project(SauceEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ENGINE_INCLUDE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/app"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/render"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/ui"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/entities"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/gameplay"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/network"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/physics"
)

file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/*.cpp"
)

if(WIN32)
    if(NOT MINGW)
        message(FATAL_ERROR "SauceEngine requires MinGW-w64 because the bundled GLFW library uses the MinGW format.")
    endif()

    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "SauceEngine requires a 64-bit MinGW-w64 toolchain.")
    endif()

    enable_language(RC)
    set(SAUCE_ENGINE_MANIFEST_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src/app.manifest")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/src/app.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/SauceEngine.rc"
        @ONLY
    )

    add_executable(SauceEngine ${ENGINE_SOURCES} "${CMAKE_CURRENT_BINARY_DIR}/SauceEngine.rc")

    set_target_properties(SauceEngine PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    target_include_directories(SauceEngine PRIVATE
        ${ENGINE_INCLUDE_DIRS}
        "${CMAKE_CURRENT_SOURCE_DIR}/src/glfw/include"
    )

    target_compile_definitions(SauceEngine PRIVATE _WIN32_WINNT=0x0600)
    target_compile_definitions(SauceEngine PRIVATE JPH_PROFILE_ENABLED NDEBUG)

    target_link_libraries(SauceEngine PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Jolt/lib/libJolt.a"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/glfw/lib/libglfw3.a"
        opengl32
        gdi32
        user32
        shell32
        winmm
        ws2_32
        advapi32
        ole32
        uuid
        comdlg32
        imm32
    )

    target_link_options(SauceEngine PRIVATE
        -static
        -static-libgcc
        -static-libstdc++
    )
else()
    # ------------------------------------------------------------------
    # Linux / other POSIX builds
    #
    # The bundled GLFW/Jolt binaries are Windows-only, so on Linux the
    # engine is linked against the system GLFW and Jolt is compiled from
    # the sources that ship in src/Jolt.
    # ------------------------------------------------------------------
    enable_language(C)

    find_package(OpenGL REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GLFW REQUIRED glfw3)

    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)

    set(JPH_PROFILE_ENABLED OFF CACHE BOOL "Enable Jolt profiling")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/src/Jolt" "${CMAKE_CURRENT_BINARY_DIR}/Jolt")

    add_executable(SauceEngine ${ENGINE_SOURCES})

    set_target_properties(SauceEngine PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    target_include_directories(SauceEngine PRIVATE
        ${ENGINE_INCLUDE_DIRS}
        ${GLFW_INCLUDE_DIRS}
    )

    target_compile_definitions(SauceEngine PRIVATE JPH_PROFILE_ENABLED NDEBUG)

    target_link_libraries(SauceEngine PRIVATE
        Jolt
        ${GLFW_LIBRARIES}
        OpenGL::GL
        Threads::Threads
        ${CMAKE_DL_LIBS}
        m
    )

    pkg_check_modules(PULSE_SIMPLE libpulse-simple)
    if(PULSE_SIMPLE_FOUND)
        target_compile_definitions(SauceEngine PRIVATE SAUCE_HAVE_PULSE=1)
        target_include_directories(SauceEngine PRIVATE ${PULSE_SIMPLE_INCLUDE_DIRS})
        target_link_libraries(SauceEngine PRIVATE ${PULSE_SIMPLE_LIBRARIES})
    else()
        message(STATUS "libpulse-simple not found: building without audio output")
    endif()
endif()
