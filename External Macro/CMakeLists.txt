cmake_minimum_required(VERSION 3.20)

project(dribtard LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(IMGUI_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui
)

set(MONOCYPHER_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/external/monocypher
)

add_executable(dribtard WIN32

    src/main.cpp
    src/app.rc
    src/resource.hpp
    src/Version.hpp

    src/Application.cpp
    src/Application.hpp

    src/Configuration/SettingsManager.cpp
    src/Configuration/SettingsManager.hpp

    src/Logging/Logger.cpp
    src/Logging/Logger.hpp

    src/Process/ProcessManager.cpp
    src/Process/ProcessManager.hpp

    src/Input/HotkeyManager.cpp
    src/Input/HotkeyManager.hpp

    src/Macro/MacroEngine.cpp
    src/Macro/MacroEngine.hpp

    src/Macro/MoonwalkMacro.cpp
    src/Macro/MoonwalkMacro.hpp

    src/Overlay/OverlayWindow.cpp
    src/Overlay/OverlayWindow.hpp

    src/Overlay/Renderer.cpp
    src/Overlay/Renderer.hpp

    src/Overlay/ImGuiUI.cpp
    src/Overlay/ImGuiUI.hpp

    src/Licensing/LicenseClient.cpp
    src/Licensing/LicenseClient.hpp
    src/Licensing/LicenseConfig.hpp

    src/Update/Updater.cpp
    src/Update/Updater.hpp

    ${MONOCYPHER_DIR}/monocypher.c
    ${MONOCYPHER_DIR}/monocypher-ed25519.c

    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp

    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)

target_include_directories(dribtard PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${MONOCYPHER_DIR}
)

target_compile_definitions(dribtard PRIVATE
    NOMINMAX
)

target_link_libraries(dribtard PRIVATE
    d3d11
    windowscodecs
    dxgi
    dwmapi
    winhttp
    bcrypt
)

if(MSVC)
    target_compile_options(dribtard PRIVATE
        /W4
        /EHsc
    )
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(curve_tests
        tests/CurveTests.cpp
        src/Macro/MacroEngine.cpp
    )
    target_include_directories(curve_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(curve_tests PRIVATE NOMINMAX)
    add_test(NAME curve_settings_and_camera_return COMMAND curve_tests)

    add_executable(macro_sequence_tests
        tests/MacroSequenceTests.cpp
        src/Macro/MacroEngine.cpp
    )
    target_include_directories(macro_sequence_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(macro_sequence_tests PRIVATE NOMINMAX)
    add_test(NAME macro_sequence COMMAND macro_sequence_tests)

    add_executable(moonwalk_tests
        tests/MoonwalkTests.cpp
        src/Macro/MoonwalkMacro.cpp
        src/Macro/MacroEngine.cpp
    )
    target_include_directories(moonwalk_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(moonwalk_tests PRIVATE NOMINMAX)
    add_test(NAME moonwalk_sequence COMMAND moonwalk_tests)

    add_executable(config_tests
        tests/ConfigTests.cpp
        src/Configuration/SettingsManager.cpp
        src/Macro/MacroEngine.cpp
    )
    target_include_directories(config_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(config_tests PRIVATE NOMINMAX)
    add_test(NAME config_save_load_autosave COMMAND config_tests)

    add_executable(license_tests
        tests/LicenseTests.cpp
        src/Licensing/LicenseClient.cpp
        src/Logging/Logger.cpp
        ${MONOCYPHER_DIR}/monocypher.c
        ${MONOCYPHER_DIR}/monocypher-ed25519.c
    )
    target_include_directories(license_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${MONOCYPHER_DIR}
    )
    target_compile_definitions(license_tests PRIVATE NOMINMAX)
    target_link_libraries(license_tests PRIVATE winhttp bcrypt)
    add_test(NAME license_response_verification COMMAND license_tests)

    add_executable(update_tests
        tests/UpdateTests.cpp
        src/Update/Updater.cpp
        src/Licensing/LicenseClient.cpp
        src/Logging/Logger.cpp
        ${MONOCYPHER_DIR}/monocypher.c
        ${MONOCYPHER_DIR}/monocypher-ed25519.c
    )
    target_include_directories(update_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${MONOCYPHER_DIR}
    )
    target_compile_definitions(update_tests PRIVATE NOMINMAX)
    target_link_libraries(update_tests PRIVATE winhttp bcrypt)
    add_test(NAME update_verification COMMAND update_tests)

    add_executable(imgui_ui_tests
        tests/ImGuiUITests.cpp
        src/Overlay/ImGuiUI.cpp
        src/Input/HotkeyManager.cpp
        src/Macro/MacroEngine.cpp
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
        ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
    )
    target_include_directories(imgui_ui_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
    )
    target_compile_definitions(imgui_ui_tests PRIVATE NOMINMAX IMGUI_ENABLE_TEST_ENGINE)
    target_link_libraries(imgui_ui_tests PRIVATE d3d11 windowscodecs dxgi)
    add_test(NAME imgui_curve_menu_interactions COMMAND imgui_ui_tests)

    if(MSVC)
        target_compile_options(curve_tests PRIVATE /W4 /EHsc)
        target_compile_options(macro_sequence_tests PRIVATE /W4 /EHsc)
        target_compile_options(moonwalk_tests PRIVATE /W4 /EHsc)
        target_compile_options(config_tests PRIVATE /W4 /EHsc)
        target_compile_options(imgui_ui_tests PRIVATE /W4 /EHsc)
        target_compile_options(license_tests PRIVATE /W4 /EHsc)
        target_compile_options(update_tests PRIVATE /W4 /EHsc)
    endif()
endif()

add_custom_command(TARGET dribtard POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:dribtard>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/assets/macro_icon.png" "$<TARGET_FILE_DIR:dribtard>/assets/macro_icon.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/assets/macro_icon.ico" "$<TARGET_FILE_DIR:dribtard>/assets/macro_icon.ico"
)
