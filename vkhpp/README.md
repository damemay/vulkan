# vki
vulkan interface

- C++20 with `vulkan.hpp`. 
- Basic Vulkan context/app bootstrapper and helpers

## Building

Trivial CMake usage:
```cmake
add_subdirectory(../vki ${CMAKE_CURRENT_BINARY_DIR}/vki)
include_directories(${VKI_INCLUDES})
add_executable(${name} main.cc)
target_link_libraries(${name} ${VKI_LIBS})
```
