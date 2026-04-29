cmake_minimum_required(VERSION 3.0.0)

project(ssne_ai_demo)

# 引入路径配置文件
include(cmake_config/Paths.cmake)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2")
add_compile_options(-Wno-psabi)

# 目录定义
set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)
set(INC_DIR ${CMAKE_SOURCE_DIR}/include)

# 你当前放置的 zbar 静态库路径
set(ZBAR_STATIC_LIB ${CMAKE_SOURCE_DIR}/zbar/.libs/libzbar.a)

# 可执行文件
add_executable(ssne_ai_demo
    ${PROJECT_SOURCE_DIR}/demo_face.cpp
)

# 添加 src 下所有 cpp（包含 qr_decoder.cpp / jq8400_uart.cpp）
file(GLOB SRC_FILES "${SRC_DIR}/*.cpp")
target_sources(ssne_ai_demo PRIVATE ${SRC_FILES})

# 头文件路径
# 说明：INC_DIR 里既有 zbar.h，也有 zbar/ 子目录（C++ 包装头）
target_include_directories(ssne_ai_demo PRIVATE
    ${INC_DIR}
    ${M1_SDK_INC_DIR}
    ${M1_SDK_INC_DIR}/smartsoc
)

# 检查 zbar 静态库存在
if(NOT EXISTS ${ZBAR_STATIC_LIB})
    message(FATAL_ERROR "zbar static library not found: ${ZBAR_STATIC_LIB}")
endif()

# 链接
# 直接链接绝对路径，避免 -L/-l 路径问题
target_link_libraries(ssne_ai_demo
    ${M1_SSNE_LIB}
    ${M1_CMABUFFER_LIB}
    ${M1_SSZLOG_LIB}
    ${M1_ZLOG_LIB}
    ${M1_OSD_LIB}
    ${M1_EMB_LIB}
    ${M1_UART_LIB}
    ${ZBAR_STATIC_LIB}
    pthread
    m
    # iconv   # 若后续提示 iconv 未定义，再打开
)

install(TARGETS ssne_ai_demo DESTINATION bin)
