cmake_minimum_required(VERSION 3.30)
project(vki)
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

include(FetchContent)

FetchContent_Declare(
  vma
  GIT_REPOSITORY
    https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
  GIT_TAG 009ecd192c1289c7529bff248a16cfe896254816)
FetchContent_MakeAvailable(vma)

set(VKI_INCLUDES
    ${vma_SOURCE_DIR}/include/ ${vki_SOURCE_DIR}/
    PARENT_SCOPE)

set(VKI_LIBS
    vki GPUOpen::VulkanMemoryAllocator glfw
    PARENT_SCOPE)

add_library(${PROJECT_NAME} vki.cc ext.cc vki_app.cc)
set_target_properties(
  ${PROJECT_NAME}
  PROPERTIES CXX_STANDARD_REQUIRED ON
             CXX_STANDARD 20
             INCLUDE_DIRECTORIES "${VKI_INCLUDES}"
             LINK_LIBRARIES "${VKI_LIBS}")

if(CMAKE_BUILD_TYPE MATCHES "Debug")
  add_executable(sample sample.cc)
  target_link_libraries(sample vki GPUOpen::VulkanMemoryAllocator glfw
                        -fsanitize=undefined -fsanitize=address)
endif()
