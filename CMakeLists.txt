cmake_minimum_required(VERSION 3.5.0)
project(sapnwrfc)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(NWRFCSDK_ROOT "${PROJECT_SOURCE_DIR}/nwrfcsdk" CACHE PATH "Path the the root of SAP NW RFC SDK")

add_library(sapnwrfc::sapnwrfc SHARED IMPORTED)
set_target_properties(sapnwrfc::sapnwrfc PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${NWRFCSDK_ROOT}/include"
        IMPORTED_IMPLIB "${NWRFCSDK_ROOT}/lib/${CMAKE_IMPORT_LIBRARY_PREFIX}sapnwrfc${CMAKE_IMPORT_LIBRARY_SUFFIX}"
        IMPORTED_LOCATION "${NWRFCSDK_ROOT}/lib/${CMAKE_SHARED_LIBRARY_PREFIX}sapnwrfc${CMAKE_SHARED_LIBRARY_SUFFIX}")

add_library(sapnwrfc::libsapucum SHARED IMPORTED)
set_target_properties(sapnwrfc::libsapucum PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${NWRFCSDK_ROOT}/include"
        IMPORTED_IMPLIB "${NWRFCSDK_ROOT}/lib/libsapucum${CMAKE_IMPORT_LIBRARY_SUFFIX}"
        IMPORTED_LOCATION "${NWRFCSDK_ROOT}/lib/libsapucum${CMAKE_SHARED_LIBRARY_SUFFIX}")

set(Sources
  src/binding.cc
  src/current_function.hpp
  src/Common.h
  src/Connection.cc
  src/Connection.h
  src/ConnectionOpen.cc
  src/ConnectionOpen.h
  src/Function.cc
  src/Function.h
  src/FunctionInvoke.cc
  src/FunctionInvoke.h
  src/Loggable.cc
  src/Loggable.h
  examples/example1.js
)

add_library(node-sapnwrfc SHARED ${Sources} ${CMAKE_JS_SRC})

target_compile_definitions(node-sapnwrfc PRIVATE SAPwithUNICODE SAPwithTHREADS)
if(WIN32)
  target_compile_definitions(node-sapnwrfc PRIVATE SAPonNT UNICODE _UNICODE NOMINMAX)
  target_link_libraries(node-sapnwrfc PRIVATE delayimp)
else()
  target_compile_definitions(node-sapnwrfc PRIVATE SAPonUNIX SAPonLIN __NO_MATH_INLINES)
  set_target_properties(node-sapnwrfc PROPERTIES LINK_FLAGS -rdynamic)
endif()

# Include N-API wrappers
execute_process(COMMAND node -p "require('node-addon-api').include"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE NODE_ADDON_API_DIR )
string(REPLACE "\n" "" NODE_ADDON_API_DIR ${NODE_ADDON_API_DIR})
string(REPLACE "\"" "" NODE_ADDON_API_DIR ${NODE_ADDON_API_DIR})

target_include_directories(node-sapnwrfc PRIVATE
   "${CMAKE_CURRENT_SOURCE_DIR}"
   "${CMAKE_JS_INC}"
   "${NODE_ADDON_API_DIR}")

target_link_libraries(node-sapnwrfc PRIVATE sapnwrfc::sapnwrfc sapnwrfc::libsapucum ${CMAKE_JS_LIB})

set_target_properties(node-sapnwrfc PROPERTIES
        OUTPUT_NAME sapnwrfc
        PREFIX ""
        SUFFIX .node)
