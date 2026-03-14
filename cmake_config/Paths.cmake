# Paths.cmake
# ===========

# SDK根目录
set(SDK_ROOT "$ENV{BASE_DIR}/$ENV{EXPORT_LIB_M1_SDK_ROOT_PATH}" CACHE PATH "SDK root directory")

# 第三方库路径
set(THIRD_PARTY_DIR "${SDK_ROOT}/third_party" CACHE PATH "Third-party libraries directory")
# m1-sdk include path
set(M1_SDK_INC_DIR "${SDK_ROOT}/include" CACHE PATH "M1-SDK include directory")
# m1-sdk lib path
set(M1_SDK_LIB_DIR "${SDK_ROOT}/lib" CACHE PATH "M1-SDK libraries directory")

# 自定义库搜索路径
set(CUSTOM_LIB_DIRS 
    "${SDK_ROOT}/lib"
        CACHE STRING "Custom library search paths"
)

# 自定义头文件搜索路径
set(CUSTOM_INCLUDE_DIRS 
    "${SDK_ROOT}/include"
    "${SDK_ROOT}/include/smartsoc"
    "${THIRD_PARTY_DIR}/include"
    CACHE STRING "Custom include search paths"
)


set(M1_SSNE_LIB        "${M1_SDK_LIB_DIR}/libssne.so"       CACHE STRING INTERNAL)
set(M1_CMABUFFER_LIB   "${M1_SDK_LIB_DIR}/libcmabuffer.so"  CACHE STRING INTERNAL)
set(M1_OSD_LIB         "${M1_SDK_LIB_DIR}/libosd.so"        CACHE STRING INTERNAL)
set(M1_SSZLOG_LIB      "${M1_SDK_LIB_DIR}/libsszlog.so"     CACHE STRING INTERNAL)
set(M1_ZLOG_LIB        "${M1_SDK_LIB_DIR}/libzlog.so"       CACHE STRING INTERNAL)
set(M1_EMB_LIB         "${M1_SDK_LIB_DIR}/libemb.so"        CACHE STRING INTERNAL)

# zbar: 优先使用项目内置静态库，其次再查SDK/target绝对路径，避免链接阶段出现 -lzbar 找不到
set(_M1_ZBAR_SEARCH_PATHS
    "${CMAKE_SOURCE_DIR}/zbar/.libs"
        "${M1_SDK_LIB_DIR}"
    "$ENV{BASE_DIR}/target/lib"
    "$ENV{BASE_DIR}/target/usr/lib"
)

find_library(M1_ZBAR_LIB
    NAMES zbar libzbar
    PATHS ${_M1_ZBAR_SEARCH_PATHS}
    NO_DEFAULT_PATH
)

if (NOT M1_ZBAR_LIB)
    message(FATAL_ERROR
        "zbar library not found. Expected one of: ${CMAKE_SOURCE_DIR}/zbar/.libs/libzbar.a, project zbar/.libs, SDK lib, or target lib. You can also pass -DM1_ZBAR_LIB=/abs/path/to/libzbar.a")
endif()
