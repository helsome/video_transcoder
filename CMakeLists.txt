cmake_minimum_required(VERSION 3.10)
project(TranscoderFinalWork CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 使用系统当前的FFmpeg版本（6.x）
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED
    libavformat
    libavcodec
    libavutil
    libavfilter
    libswscale
    libswresample
)

# 查找SoundTouch库（用于音频变速处理）
pkg_check_modules(SOUNDTOUCH REQUIRED soundtouch)

# 查找OpenGL库（用于视频处理中的图像旋转）
find_package(OpenGL REQUIRED)

# 查找GLEW库用于OpenGL扩展
pkg_check_modules(GLEW REQUIRED glew)

# 查找GLFW库用于OpenGL上下文管理
pkg_check_modules(GLFW REQUIRED glfw3)

# 查找GLM库用于数学计算
find_package(glm REQUIRED)

# 检查是否有必要的OpenGL组件
if(NOT OPENGL_FOUND)
    message(FATAL_ERROR "OpenGL not found. Video rotation requires OpenGL support.")
endif()

message(STATUS "OpenGL found: ${OPENGL_LIBRARIES}")
message(STATUS "GLEW found: ${GLEW_LIBRARIES}")
message(STATUS "GLFW found: ${GLFW_LIBRARIES}")
message(STATUS "SoundTouch found: ${SOUNDTOUCH_LIBRARIES}")
message(STATUS "GLM found")
add_definitions(-DWITH_OPENGL)

# 打印FFmpeg相关的所有变量，帮助调试
message(STATUS "FFMPEG_FOUND: ${FFMPEG_FOUND}")
message(STATUS "FFMPEG_LIBRARIES: ${FFMPEG_LIBRARIES}")
message(STATUS "FFMPEG_LIBRARY_DIRS: ${FFMPEG_LIBRARY_DIRS}")
message(STATUS "FFMPEG_LDFLAGS: ${FFMPEG_LDFLAGS}")
message(STATUS "FFMPEG_INCLUDE_DIRS: ${FFMPEG_INCLUDE_DIRS}")
message(STATUS "FFMPEG_CFLAGS: ${FFMPEG_CFLAGS}")
message(STATUS "FFMPEG_CFLAGS_OTHER: ${FFMPEG_CFLAGS_OTHER}")
message(STATUS "PKG_CONFIG_PATH: $ENV{PKG_CONFIG_PATH}")

# 包含头文件目录 - 只使用项目的include目录，FFmpeg路径通过target_include_directories设置
include_directories(${CMAKE_SOURCE_DIR}/include)

# 获取所有源文件，但排除主程序文件
set(CORE_SRC_FILES
    src/demuxer.cpp
    src/video_decoder.cpp
    src/audio_decoder.cpp
    src/queue.cpp
)

set(ENHANCED_SRC_FILES
    ${CORE_SRC_FILES}
    src/video_encoder.cpp
    src/audio_encoder.cpp
    src/audio_processor.cpp
    src/muxer.cpp
    src/video_processor.cpp
)

# 查找线程库
find_package(Threads REQUIRED)


# 创建转码器（包含OpenGL处理和新功能）
add_executable(EnhancedTranscoder ${ENHANCED_SRC_FILES} "${CMAKE_SOURCE_DIR}/src/Transcoder.cpp")
target_include_directories(EnhancedTranscoder PRIVATE 
    ${CMAKE_SOURCE_DIR}/include
    ${FFMPEG_INCLUDE_DIRS}
    ${SOUNDTOUCH_INCLUDE_DIRS}
    ${GLEW_INCLUDE_DIRS}
    ${GLFW_INCLUDE_DIRS}
)
target_compile_options(EnhancedTranscoder PRIVATE 
    ${FFMPEG_CFLAGS_OTHER}
    ${SOUNDTOUCH_CFLAGS_OTHER}
    ${GLEW_CFLAGS_OTHER}
    ${GLFW_CFLAGS_OTHER}
    -I/usr/local/include
)
target_link_libraries(EnhancedTranscoder
    PRIVATE
    ${FFMPEG_LDFLAGS}
    ${SOUNDTOUCH_LDFLAGS}
    ${OPENGL_LIBRARIES}
    ${GLEW_LDFLAGS}
    ${GLFW_LDFLAGS}
    glm::glm
    Threads::Threads
)

message(STATUS "Core source files: ${CORE_SRC_FILES}")
message(STATUS "Enhanced source files: ${ENHANCED_SRC_FILES}")
message(STATUS "FFmpeg libraries: ${FFMPEG_LIBRARIES}")
message(STATUS "FFmpeg include dirs: ${FFMPEG_INCLUDE_DIRS}")