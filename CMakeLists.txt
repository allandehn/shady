cmake_minimum_required(VERSION 3.21)
project(shady VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Quick ShaderTools)
# QShader (rhi/qshader.h) is semi-public API; Debian splits it into the
# private headers package (qt6-base-private-dev). Used only by --self-test.
# Qt >= 6.9 needs GuiPrivate listed in find_package as well.
qt_policy(SET QTP0004 NEW)
qt_standard_project_setup(REQUIRES 6.6)

qt_add_executable(shady src/main.cpp)
target_compile_definitions(shady PRIVATE SHADY_VERSION="${PROJECT_VERSION}")

qt_add_qml_module(shady
  URI Shady
  VERSION 1.0
  QML_FILES qml/Main.qml qml/ShaderView.qml
  NO_RESOURCE_TARGET_PATH
)

# Shadertoy-style shader, wrapped GLSL 440 -> .qsb (SPIR-V, GLSL, HLSL, MSL)
qt_add_shaders(shady "shady_shaders"
  PREFIX "/"
  FILES shaders/default.frag
)

target_link_libraries(shady PRIVATE Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Quick)
install(TARGETS shady RUNTIME DESTINATION bin)
