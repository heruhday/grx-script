cmake_minimum_required(VERSION 3.20)
project(arx01 LANGUAGES C CXX)

if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "arx01 requires 64-bit Microsoft Visual C++.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Supported CAD Platforms: AUTOCAD, GSTARCAD
set(CAD_PLATFORM "AUTOCAD" CACHE STRING "Target CAD platform (AUTOCAD or GSTARCAD)")
set_property(CACHE CAD_PLATFORM PROPERTY STRINGS "AUTOCAD" "GSTARCAD")

# If AUTOCAD_VERSION is 2026 or GSTARCAD is passed, switch CAD_PLATFORM to GSTARCAD
if(GSTARCAD OR AUTOCAD_VERSION STREQUAL "2026" OR AUTOCAD_VERSION MATCHES "GSTAR|GCAD")
  set(CAD_PLATFORM "GSTARCAD" CACHE STRING "Target CAD platform" FORCE)
endif()

if(CAD_PLATFORM STREQUAL "GSTARCAD")
  set(GSTARCAD_VERSION "2026" CACHE STRING "Target GstarCAD version")
  set(GSTARCAD_SDK_ROOT "C:/GstarCAD/grxsdk" CACHE PATH "GstarCAD GRX SDK root")
  if(NOT EXISTS "${GSTARCAD_SDK_ROOT}" AND EXISTS "C:/GstarCAD")
    if(EXISTS "C:/GstarCAD/grxsdk")
      set(GSTARCAD_SDK_ROOT "C:/GstarCAD/grxsdk" CACHE PATH "GstarCAD GRX SDK root" FORCE)
    endif()
  endif()

  message(STATUS ">>> Targeting GstarCAD ${GSTARCAD_VERSION} (SDK: ${GSTARCAD_SDK_ROOT})")

  set(CAD_INC_DIRS
    "${GSTARCAD_SDK_ROOT}/arx/inc"
    "${GSTARCAD_SDK_ROOT}/inc"
    "${GSTARCAD_SDK_ROOT}/inc-x64"
  )
  set(CAD_LIB_DIR "${GSTARCAD_SDK_ROOT}/lib-x64")
  set(CAD_MODULE_DEF "${GSTARCAD_SDK_ROOT}/arx/inc/AcRxDefault.def")

  set(CAD_LIBS
    "${CAD_LIB_DIR}/gcad.lib"
    "${CAD_LIB_DIR}/gccore.lib"
    "${CAD_LIB_DIR}/gcdb.lib"
    "${CAD_LIB_DIR}/gcbase.lib"
    "${CAD_LIB_DIR}/gcgs.lib"
    "${CAD_LIB_DIR}/gcax.lib"
    "${CAD_LIB_DIR}/gplot.lib"
    "${CAD_LIB_DIR}/GcModelDocObj.lib"
    "${CAD_LIB_DIR}/gcdyn.lib"
  )
  set(CAD_TARGET_EXT ".grx")
  set(CAD_OUTPUT_NAME "qjs_${GSTARCAD_VERSION}")
  set(CAD_VERSION_LABEL "GstarCAD_${GSTARCAD_VERSION}")
  set(CAD_COMPILE_DEFS
    _GCRXAPP _ACRXAPP _WINDOWS _USRDLL _WINDLL UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS
    GSTARCAD_VERSION=${GSTARCAD_VERSION}
  )

  foreach(REQUIRED_PATH
      "${GSTARCAD_SDK_ROOT}/arx/inc"
      "${CAD_LIB_DIR}"
      "${CAD_MODULE_DEF}")
    if(NOT EXISTS "${REQUIRED_PATH}")
      message(FATAL_ERROR "Required GstarCAD SDK path not found: ${REQUIRED_PATH}")
    endif()
  endforeach()

else()
  # Supported AutoCAD versions: 2017, 2018, 2019, 2020, 2021, 2022, 2023, 2024
  set(AUTOCAD_VERSION "2021" CACHE STRING "Target AutoCAD version (2017..2024)")
  set_property(CACHE AUTOCAD_VERSION PROPERTY STRINGS "2017" "2018" "2019" "2020" "2021" "2022" "2023" "2024")

  set(AUTOCAD_SDK_DIR_2017 "C:/Autodesk/Autodesk_ObjectARX_2017_Win_64_and_32_Bit")
  set(AUTOCAD_SDK_DIR_2018 "C:/Autodesk/Autodesk_ObjectARX_2018_Win_64_and_32_Bit")
  set(AUTOCAD_SDK_DIR_2019 "C:/Autodesk/Autodesk_ObjectARX_2019_Win_64_and_32_Bit")
  set(AUTOCAD_SDK_DIR_2020 "C:/Autodesk/ObjectARX_for_AutoCAD_2020_Win_64_bit")
  set(AUTOCAD_SDK_DIR_2021 "C:/Autodesk/ObjectARX_for_AutoCAD_2021_Win_64bit_dlm")
  set(AUTOCAD_SDK_DIR_2022 "C:/Autodesk/ObjectARX_for_AutoCAD_2022_Win_64bit_dlm")
  set(AUTOCAD_SDK_DIR_2023 "C:/Autodesk/ObjectARX_for_AutoCAD_2023_Win_64bit_dlm")
  set(AUTOCAD_SDK_DIR_2024 "C:/Autodesk/ObjectARX_for_AutoCAD_2024_Win_64bit_dlm")

  # Select SDK root based on version unless explicitly customized via -DOBJECTARX_SDK_ROOT
  if(NOT OBJECTARX_SDK_ROOT OR OBJECTARX_SDK_ROOT STREQUAL AUTOCAD_SDK_DIR_2021 OR OBJECTARX_SDK_ROOT_AUTO)
    if(DEFINED AUTOCAD_SDK_DIR_${AUTOCAD_VERSION} AND EXISTS "${AUTOCAD_SDK_DIR_${AUTOCAD_VERSION}}")
      set(OBJECTARX_SDK_ROOT "${AUTOCAD_SDK_DIR_${AUTOCAD_VERSION}}" CACHE PATH "AutoCAD ObjectARX SDK root" FORCE)
      set(OBJECTARX_SDK_ROOT_AUTO TRUE CACHE INTERNAL "SDK root was automatically selected")
    endif()
  endif()

  set(OBJECTARX_SDK_ROOT "${AUTOCAD_SDK_DIR_2021}" CACHE PATH "AutoCAD ObjectARX SDK root")

  # Infer AUTOCAD_VERSION if OBJECTARX_SDK_ROOT was manually set
  foreach(_ver 2017 2018 2019 2020 2021 2022 2023 2024)
    if(OBJECTARX_SDK_ROOT MATCHES "${_ver}")
      set(AUTOCAD_VERSION "${_ver}" CACHE STRING "Target AutoCAD version (2017..2024)" FORCE)
      break()
    endif()
  endforeach()

  message(STATUS ">>> Targeting AutoCAD ${AUTOCAD_VERSION} (SDK: ${OBJECTARX_SDK_ROOT})")

  set(OBJECTARX_INC_DIR "${OBJECTARX_SDK_ROOT}/inc")
  set(OBJECTARX_INC_X64_DIR "${OBJECTARX_SDK_ROOT}/inc-x64")
  set(OBJECTARX_LIB_DIR "${OBJECTARX_SDK_ROOT}/lib-x64")
  set(OBJECTARX_MODULE_DEF "${OBJECTARX_INC_DIR}/AcRxDefault.def")

  foreach(REQUIRED_PATH
      "${OBJECTARX_INC_DIR}"
      "${OBJECTARX_LIB_DIR}"
      "${OBJECTARX_MODULE_DEF}")
    if(NOT EXISTS "${REQUIRED_PATH}")
      message(FATAL_ERROR "Required ObjectARX SDK path not found: ${REQUIRED_PATH}")
    endif()
  endforeach()

  # Locate AutoCAD ObjectARX version-dependent libraries (e.g. acdb24.lib, acge24.lib)
  file(GLOB ARX_ACDB_LIB "${OBJECTARX_LIB_DIR}/acdb[0-9]*.lib")
  file(GLOB ARX_ACGE_LIB "${OBJECTARX_LIB_DIR}/acge[0-9]*.lib")
  file(GLOB ARX_AC1ST_LIB "${OBJECTARX_LIB_DIR}/ac1st[0-9]*.lib")

  if(NOT ARX_ACDB_LIB)
    set(ARX_ACDB_LIB "${OBJECTARX_LIB_DIR}/acdb.lib")
  endif()
  if(NOT ARX_ACGE_LIB)
    set(ARX_ACGE_LIB "${OBJECTARX_LIB_DIR}/acge.lib")
  endif()

  set(CAD_INC_DIRS
    "${OBJECTARX_INC_DIR}"
    $<$<BOOL:${OBJECTARX_INC_X64_DIR}>:${OBJECTARX_INC_X64_DIR}>
  )
  set(CAD_LIB_DIR "${OBJECTARX_LIB_DIR}")
  set(CAD_MODULE_DEF "${OBJECTARX_MODULE_DEF}")

  set(CAD_LIBS
    rxapi.lib
    accore.lib
    acad.lib
    AcPal.lib
    ${ARX_ACDB_LIB}
    ${ARX_ACGE_LIB}
    ${ARX_AC1ST_LIB}
  )
  if(EXISTS "${OBJECTARX_LIB_DIR}/acgeoment.lib")
    list(APPEND CAD_LIBS "${OBJECTARX_LIB_DIR}/acgeoment.lib")
  endif()
  set(CAD_TARGET_EXT ".arx")
  set(CAD_OUTPUT_NAME "qjs_${AUTOCAD_VERSION}")
  set(CAD_VERSION_LABEL "AutoCAD_${AUTOCAD_VERSION}")
  set(CAD_COMPILE_DEFS
    _ACRXAPP _WINDOWS _USRDLL _WINDLL UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS
    AUTOCAD_VERSION=${AUTOCAD_VERSION}
  )
endif()

# Resolve Microsoft WebView2 SDK
set(WEBVIEW2_SEARCH_PATHS
  "${WEBVIEW2_SDK_ROOT}"
  "$ENV{WEBVIEW2_SDK_ROOT}"
  "${CMAKE_CURRENT_SOURCE_DIR}/../script/packages/Microsoft.Web.WebView2.1.0.3650.58"
  "$ENV{USERPROFILE}/.nuget/packages/microsoft.web.webview2/1.0.3650.58"
  "C:/Users/Heruh/.nuget/packages/microsoft.web.webview2/1.0.3650.58"
)

if(NOT WEBVIEW2_SDK_ROOT OR NOT EXISTS "${WEBVIEW2_SDK_ROOT}/build/native/include/WebView2.h")
  foreach(_path IN LISTS WEBVIEW2_SEARCH_PATHS)
    if(_path AND EXISTS "${_path}/build/native/include/WebView2.h")
      set(WEBVIEW2_SDK_ROOT "${_path}" CACHE PATH "Microsoft WebView2 SDK root" FORCE)
      break()
    endif()
  endforeach()
endif()

set(WEBVIEW2_INCLUDE_DIR "${WEBVIEW2_SDK_ROOT}/build/native/include")
set(WEBVIEW2_LOADER_LIB "${WEBVIEW2_SDK_ROOT}/build/native/x64/WebView2Loader.dll.lib")
set(WEBVIEW2_LOADER_DLL "${WEBVIEW2_SDK_ROOT}/build/native/x64/WebView2Loader.dll")

foreach(REQUIRED_WEBVIEW_PATH
    "${WEBVIEW2_INCLUDE_DIR}/WebView2.h"
    "${WEBVIEW2_LOADER_LIB}"
    "${WEBVIEW2_LOADER_DLL}")
  if(NOT EXISTS "${REQUIRED_WEBVIEW_PATH}")
    message(FATAL_ERROR "Required WebView2 file not found: ${REQUIRED_WEBVIEW_PATH}")
  endif()
endforeach()

add_library(quickjs STATIC third_party/quickjs/quickjs-amalgam.c)
target_include_directories(quickjs PRIVATE third_party/quickjs)
target_compile_definitions(quickjs PRIVATE
  QJS_BUILD_LIBC WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)

add_library(msgpack_serde STATIC third_party/msgpack_serde/msgpack_serde.cpp)
target_include_directories(msgpack_serde PUBLIC third_party/msgpack_serde)

add_library(sqlite STATIC third_party/sqlite/sqlite-amalgamation-3530200/sqlite3.c)
target_include_directories(sqlite PUBLIC third_party/sqlite/sqlite-amalgamation-3530200)
target_compile_definitions(sqlite PRIVATE
  SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION SQLITE_OMIT_DEPRECATED
  _CRT_SECURE_NO_WARNINGS)
target_compile_options(sqlite PRIVATE /W0)

add_library(query_dsl STATIC src/query_dsl/query_dsl.cpp)
target_include_directories(query_dsl PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/src/query_dsl"
)
target_compile_definitions(query_dsl PRIVATE
  WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS
)

add_library(arx01 SHARED
  command/ipc_server.cpp
  command/js_repl.cpp
  command/set_color.cpp
  command/text_util.cpp
  src/js_modules/color_module.cpp
  src/js_modules/console_module.cpp
  src/js_modules/dialog_module.cpp
  src/js_modules/editor_module.cpp
  src/js_modules/entity_module.cpp
  src/js_modules/excel_module.cpp
  src/js_modules/fs_module.cpp
  src/js_modules/js_module_utils.cpp
  src/js_modules/layer_module.cpp
  src/js_modules/msgpack_module.cpp
  src/js_modules/net_module.cpp
  src/js_modules/path_module.cpp
  src/js_modules/process_module.cpp
  src/js_modules/sqlite_module.cpp
  src/js_modules/transaction_manager_module.cpp
  src/js_engine.cpp
  src/selection_event.cpp
  src/safe_commands.cpp
  src/arx_entry.cpp
  "${CAD_MODULE_DEF}"
)

target_include_directories(arx01 PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_SOURCE_DIR}/command"
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/quickjs"
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/msgpack_serde"
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite/sqlite-amalgamation-3530200"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/query_dsl"
  "${WEBVIEW2_INCLUDE_DIR}"
  ${CAD_INC_DIRS}
)

target_compile_definitions(arx01 PRIVATE ${CAD_COMPILE_DEFS})

target_link_directories(arx01 PRIVATE "${CAD_LIB_DIR}")
target_link_libraries(arx01 PRIVATE
  quickjs msgpack_serde sqlite query_dsl
  "${WEBVIEW2_LOADER_LIB}"
  ${CAD_LIBS}
  ole32.lib oleaut32.lib dwmapi.lib shell32.lib shlwapi.lib ws2_32.lib
)

set_target_properties(arx01 PROPERTIES
  PREFIX ""
  OUTPUT_NAME "${CAD_OUTPUT_NAME}"
  SUFFIX "${CAD_TARGET_EXT}"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_command(TARGET arx01 POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${WEBVIEW2_LOADER_DLL}" "$<TARGET_FILE_DIR:arx01>/WebView2Loader.dll"
)

add_executable(test_query_dsl test/test_query_dsl.cpp)
target_include_directories(test_query_dsl PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/quickjs"
)
target_link_libraries(test_query_dsl PRIVATE query_dsl quickjs)
