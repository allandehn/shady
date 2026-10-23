cmake_minimum_required(VERSION 3.21)
project(shady VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# User shaders are compiled at runtime by Qt's `qsb` tool (qt6-shadertools),
# run as a separate process, so the app never links glslang.
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Quick LinguistTools)
# rhi/qshader.h and rhi/qrhi.h are semi-public API; Debian ships them in
# qt6-base-private-dev. Qt >= 6.9 also needs GuiPrivate in find_package.
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.9)
    find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
endif()
qt_standard_project_setup(REQUIRES 6.6)
qt_policy(SET QTP0004 NEW)

qt_add_executable(shady
  src/main.cpp
  src/toml.h src/toml.cpp
  src/compiler.h src/compiler.cpp
  src/library.h src/library.cpp
  src/theme.h src/theme.cpp
  src/selftest.h src/selftest.cpp
  src/textures.h src/textures.cpp
  src/glslhighlighter.h src/glslhighlighter.cpp
  src/framemeter.h
)
target_compile_definitions(shady PRIVATE SHADY_VERSION="${PROJECT_VERSION}")

qt_add_qml_module(shady
  URI Shady
  VERSION 1.0
  QML_FILES
    qml/Main.qml qml/ShaderList.qml qml/ShaderCard.qml qml/Viewer.qml
    qml/ShaderView.qml qml/Label.qml qml/Hint.qml qml/Hatch.qml qml/Tip.qml
    qml/Button.qml qml/Confirm.qml qml/Menu.qml qml/KeySheet.qml qml/SettingsSheet.qml
    qml/Editor.qml qml/Field.qml qml/Channel.qml qml/ChannelPicker.qml qml/Benchmark.qml qml/LivelyMaker.qml
)

qt_add_resources(shady "examples"
  PREFIX "/"
  FILES
    examples/northern-veil.glsl examples/plasma-drift.glsl
    examples/cell-garden.glsl examples/warp-tunnel.glsl
    examples/crouwel-new-alphabet.glsl
)
# The 0.2.0 Crouwel source, for the example-update self-test.
qt_add_resources(shady "selftest_fixtures"
  PREFIX "/selftest" BASE "tests/fixtures"
  FILES tests/fixtures/crouwel-0.2.0.glsl
)

# English is in the source; Danish ships from the start (i18n/update.sh).
qt_add_lrelease(shady TS_FILES i18n/shady_da.ts QM_FILES_OUTPUT_VARIABLE shady_qm)
qt_add_resources(shady "translations" PREFIX "/i18n" BASE "${CMAKE_CURRENT_BINARY_DIR}" FILES ${shady_qm})

target_link_libraries(shady PRIVATE Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Quick)
target_include_directories(shady PRIVATE src)
include(GNUInstallDirs)
install(TARGETS shady RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/linux/shady.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES packaging/linux/shady.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/shady)
install(FILES README.md DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/shady)
