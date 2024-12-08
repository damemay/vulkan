cmake_minimum_required(VERSION 3.30)
get_filename_component(name ${CMAKE_CURRENT_SOURCE_DIR} NAME)
project(${name})
set(CMAKE_C_STANDARD 23)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_BUILD_TYPE MATCHES "Debug")
  set(CMAKE_C_FLAGS
      "-Wall -Werror -Wno-unused-function -Wno-unused-variable -g -fsanitize=undefined -fsanitize=address -fno-omit-frame-pointer"
  )
  set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -fno-rtti")
else()
  set(CMAKE_C_FLAGS
      "-Wall -Werror -Wno-unused-function -Wno-unused-variable -O2 -march=native -flto -fno-delete-null-pointer-checks -fno-strict-overflow -fno-strict-aliasing -ftrivial-auto-var-init=zero -D_FORTIFY_SOURCE=2 -finline-functions"
  )
  set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -fno-rtti")
endif()

find_package(slang REQUIRED)
include(FetchContent)

FetchContent_Declare(
  vki
  GIT_REPOSITORY git@github.com:damemay/vki.git
  GIT_TAG 236e88e4bba74354aaea1a95892d1fceb3976d39)
FetchContent_MakeAvailable(vki)

option(SPIRV_REFLECT_EXECUTABLE "" OFF)
option(SPIRV_REFLECT_STATIC_LIB "" ON)
FetchContent_Declare(
  spirv_reflect
  GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Reflect.git
  GIT_TAG 4c6d3f3b43ec90dc359d414c3039706ebaad92df)
FetchContent_MakeAvailable(spirv_reflect)

add_executable(${name} main.cc)
include_directories(${VKI_INCLUDES} ${spirv_reflect_SOURCE_DIR}/)
target_link_libraries(${name} ${VKI_LIBS} slang::slang slang-glslang
                      spirv-reflect-static)

if(CMAKE_BUILD_TYPE MATCHES "Debug")
  target_link_libraries(${name} -fsanitize=address -fsanitize=undefined)
endif()
