cmake_minimum_required(VERSION 3.20)

#
# macOS build of dribtard.
#
# This project is completely separate from the Windows one ("External Macro"):
# it has its own sources, its own engines and its own settings file, and none
# of the Windows code is compiled or modified. The only shared thing is the
# vendored Dear ImGui copy, which is platform independent.
#
project(dribtard_macos LANGUAGES C CXX OBJCXX)

if(NOT APPLE)
    message(FATAL_ERROR
        "This project builds the macOS version only. "
        "Configure ../External Macro for the Windows build.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# App.mm and Overlay.mm are Objective-C++; without this clang treats .mm as
# C++98 and rejects the lambdas the app shell uses.
set(CMAKE_OBJCXX_STANDARD 17)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_EXTENSIONS OFF)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../External Macro/external/imgui")

if(NOT EXISTS "${IMGUI_DIR}/imgui.cpp")
    message(FATAL_ERROR "Dear ImGui not found at ${IMGUI_DIR}")
endif()

set(DRIBTARD_MAC_SOURCES
    src/main.mm
    src/App.mm
    src/Keys.cpp
    src/Overlay/Overlay.mm
    src/Platform/MacInput.mm
    src/Platform/Hotkeys.mm
    src/Macro/MacroEngine.cpp
    src/Macro/ClickerMacro.cpp
    src/Macro/BladeBallMacro.cpp
    src/Configuration/Settings.cpp
    src/UI/Menu.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_osx.mm
    ${IMGUI_DIR}/backends/imgui_impl_metal.mm
)

add_executable(dribtard-mac MACOSX_BUNDLE ${DRIBTARD_MAC_SOURCES})

target_include_directories(dribtard-mac PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

# ARC only reaches the Objective-C++ files; the C++ engines stay plain C++.
set_source_files_properties(
    src/main.mm
    src/App.mm
    src/Overlay/Overlay.mm
    src/Platform/MacInput.mm
    src/Platform/Hotkeys.mm
    PROPERTIES COMPILE_OPTIONS "-fobjc-arc"
)

target_compile_options(dribtard-mac PRIVATE -Wall -Wextra)

find_library(COCOA_LIBRARY Cocoa REQUIRED)
find_library(METAL_LIBRARY Metal REQUIRED)
find_library(METALKIT_LIBRARY MetalKit REQUIRED)
find_library(QUARTZCORE_LIBRARY QuartzCore REQUIRED)
find_library(APPLICATION_SERVICES_LIBRARY ApplicationServices REQUIRED)
find_library(GAME_CONTROLLER_LIBRARY GameController REQUIRED)
find_library(FOUNDATION_LIBRARY Foundation REQUIRED)

target_link_libraries(dribtard-mac PRIVATE
    ${COCOA_LIBRARY}
    ${METAL_LIBRARY}
    ${METALKIT_LIBRARY}
    ${QUARTZCORE_LIBRARY}
    ${APPLICATION_SERVICES_LIBRARY}
    ${GAME_CONTROLLER_LIBRARY}
    ${FOUNDATION_LIBRARY}
)

set_target_properties(dribtard-mac PROPERTIES
    OUTPUT_NAME "dribtard-mac"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
    MACOSX_BUNDLE_BUNDLE_NAME "dribtard"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.dribtard.mac"
    MACOSX_BUNDLE_BUNDLE_VERSION "1.1.35"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "1.1.35"
    XCODE_ATTRIBUTE_MACOSX_DEPLOYMENT_TARGET "${CMAKE_OSX_DEPLOYMENT_TARGET}"
    XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME "NO"
)

include(CTest)
if(BUILD_TESTING)
    add_executable(dribtard-mac-tests
        tests/MacroTests.cpp
        src/Keys.cpp
        src/Macro/MacroEngine.cpp
        src/Macro/ClickerMacro.cpp
        src/Macro/BladeBallMacro.cpp
        src/Platform/MacInput.mm
    )

    target_include_directories(dribtard-mac-tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    set_source_files_properties(
        src/Platform/MacInput.mm
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc"
    )

    target_compile_options(dribtard-mac-tests PRIVATE -Wall -Wextra)

    target_link_libraries(dribtard-mac-tests PRIVATE
        ${COCOA_LIBRARY}
        ${APPLICATION_SERVICES_LIBRARY}
        ${FOUNDATION_LIBRARY}
    )

    add_test(NAME mac_macro_sequence COMMAND dribtard-mac-tests)
endif()
