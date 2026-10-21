cmake_minimum_required(VERSION 3.21)
project(shady VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# User shaders are compiled at runtime by Qt's `qsb` tool (qt6-shadertools),
# run as a separate process, so the app never links glslang.
find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Quick)
# rhi/qshader.h and rhi/qrhi.h are semi-public API; Debian ships them in
# qt6-base-private-dev. Qt >= 6.9 also needs GuiPrivate in find_package.
qt_standard_project_setup(REQUIRES 6.6)
qt_policy(SET QTP0004 NEW)

qt_add_executable(shady
  src/main.cpp
  src/toml.h src/toml.cpp
  src/compiler.h src/compiler.cpp
  src/library.h src/library.cpp
  src/theme.h src/theme.cpp
  src/selftest.h src/selftest.cpp
)
target_compile_definitions(shady PRIVATE SHADY_VERSION="${PROJECT_VERSION}")

qt_add_qml_module(shady
  URI Shady
  VERSION 1.0
  QML_FILES
    qml/Main.qml qml/ShaderList.qml qml/ShaderCard.qml qml/Viewer.qml
    qml/ShaderView.qml qml/Label.qml qml/Hint.qml qml/Hatch.qml
  NO_RESOURCE_TARGET_PATH
)

qt_add_resources(shady "examples"
  PREFIX "/"
  FILES
    examples/northern-veil.glsl examples/plasma-drift.glsl
    examples/cell-garden.glsl examples/warp-tunnel.glsl
)

target_link_libraries(shady PRIVATE Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Quick)
install(TARGETS shady RUNTIME DESTINATION bin)
