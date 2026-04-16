cmake_minimum_required(VERSION 3.24)
project(LumePT)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

#
# Target: AGPBaseAPI
#
if(NOT TARGET AGPBase::AGPBaseAPI)
  add_subdirectory(LumeBase)
endif()

#
# Target: AGPEngineAPI
#
if(NOT TARGET AGPEngine::AGPEngineAPI)
  add_subdirectory(LumeEngine)
  set(ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/LumeEngine")
else()
  get_filename_component(ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../LumeEngine" ABSOLUTE)
endif()

#
# Target: AGPRenderAPI
#
if(NOT TARGET AGPRender::AGPRenderAPI)
  add_subdirectory(LumeRender)
  set(RENDER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/LumeRender")
else()
  get_filename_component(RENDER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../LumeRender" ABSOLUTE)
endif()

#
# Target: AGP3DAPI
#
if(NOT TARGET AGP3D::AGP3DAPI)
  add_subdirectory(Lume3D)
  set(3D_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Lume3D")
else()
  get_filename_component(3D_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Lume3D" ABSOLUTE)
endif()

#
# Extract LumeShaderCompiler.exe from LumeRender
#
if(NOT EXISTS "${CMAKE_BINARY_DIR}/LumeShaderCompiler.exe")
  if(NOT EXISTS "${RENDER_DIR}/tools/LumeShaderCompiler.zip")
    message(FATAL_ERROR "Please run:\n"
      "git submodule update --init\n"
      "and then re-run cmake.")
  else()
    # Cmake 3.20+
    file(ARCHIVE_EXTRACT
      INPUT "${RENDER_DIR}/tools/LumeShaderCompiler.zip"
      DESTINATION "${CMAKE_BINARY_DIR}")
  endif()
endif()

#
# Target:LumePTAPI
#
add_library(LumePTAPI INTERFACE)
add_library(LumePT::LumePTAPI ALIAS LumePTAPI)
target_include_directories(LumePTAPI INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/api)
target_link_libraries(LumePTAPI INTERFACE
  AGPBase::AGPBaseAPI
  AGPRender::AGPRenderAPI
  AGP3D::AGP3DAPI)

#
# Target: PluginLumePT
#
add_library(PluginLumePT SHARED
  ${CMAKE_CURRENT_SOURCE_DIR}/src/plugin/dynamic_plugin.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/render/node/render_node_default_material_deferred_shading.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/render/node/render_node_sr_training.cpp)
add_library(LumePT::PluginLumePT ALIAS PluginLumePT)
target_include_directories(PluginLumePT PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(PluginLumePT PRIVATE
  AGPEngine::AGPEngineAPI
  AGPEngine::ComponentHelperSDK
  AGPRender::AGPRender
  AGPRender::AGPRenderAPI
  AGP3D::AGP3D
  AGP3D::AGP3DAPI
  LumePT::LumePTAPI)
target_compile_definitions(PluginLumePT PRIVATE LUME3D_DEFAULT_CONSTANTS_H="${3D_DIR}/src/render/default_constants.h")
target_compile_definitions(PluginLumePT PRIVATE LUME3D_RENDER_NODE_SCENE_UTIL_H="${3D_DIR}/src/render/render_node_scene_util.h")
target_compile_definitions(PluginLumePT PRIVATE CORE_PLUGIN=1)
target_compile_options(PluginLumePT PRIVATE /GR-)

set(shader_compiler_executable "${CMAKE_BINARY_DIR}/LumeShaderCompiler.exe")
set(argument_list)
# Comment out next line for RenderDoc debugging
list(APPEND argument_list "--optimize")
list(APPEND argument_list "--source")
list(APPEND argument_list "${CMAKE_CURRENT_SOURCE_DIR}/assets/pt/shaders")
list(APPEND argument_list "--include")
list(APPEND argument_list "${RENDER_DIR}/api")
list(APPEND argument_list "--include")
list(APPEND argument_list "${3D_DIR}/api")
list(APPEND argument_list "--include")
list(APPEND argument_list "${CMAKE_CURRENT_SOURCE_DIR}/api")
list(APPEND argument_list "--vulkan")
list(APPEND argument_list "1.1")
list(APPEND argument_list "--check-if-changed")
add_custom_target(PTCompileShaders ALL
  COMMAND ${shader_compiler_executable} ${argument_list}
  COMMAND_EXPAND_LISTS)
add_dependencies(PluginLumePT PTCompileShaders)