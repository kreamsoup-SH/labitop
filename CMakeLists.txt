cmake_minimum_required(VERSION 3.15)
project(labitop CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(BACKEND_SOURCES
  src/term.cpp
  src/parse.cpp
  src/nvml.cpp
  src/sys_win.cpp
  src/sys_linux.cpp)

add_executable(labitop src/main.cpp ${BACKEND_SOURCES})
# The test includes src/main.cpp itself to reach its internal helpers.
add_executable(labitop_test tests/test.cpp ${BACKEND_SOURCES})

foreach(target labitop labitop_test)
  if(WIN32)
    # Only DLLs that ship with Windows; the C++ runtime is linked statically.
    target_link_libraries(${target} PRIVATE ntdll psapi iphlpapi pdh dxgi)
    if(MINGW)
      target_link_options(${target} PRIVATE -static)
    elseif(MSVC)
      set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
  else()
    target_link_libraries(${target} PRIVATE ${CMAKE_DL_LIBS})
  endif()
endforeach()

enable_testing()
add_test(NAME labitop_test COMMAND labitop_test)
