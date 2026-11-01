# 设置Cmake的最小版本
cmake_minimum_required(VERSION 3.10)

# 项目名称
project(AIChatServer)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 未指定构建类型时默认使用 Release（命令行传入 -DCMAKE_BUILD_TYPE=Debug 可覆盖）
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# 添加可执行文件
add_executable(AIChatServer main.cpp ChatServer.cpp)

# 设置输出目录
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR})

# 查找依赖库
find_package(OpenSSL REQUIRED)

# 添加CPPHTTPLIB_OPENSSL_SUPPORT定义
target_compile_definitions(AIChatServer PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)

# 设置静态库的目录
link_directories(/usr/local/lib)

# 链接库
target_link_libraries(AIChatServer 
    ai_chat_sdk
    jsoncpp 
    fmt  
    spdlog 
    gflags
    OpenSSL::SSL 
    OpenSSL::Crypto 
    sqlite3
    gflags)

# 构建完成后把前端静态资源拷贝到可执行文件所在目录
# 服务端以相对路径 "./www" 挂载静态目录，因此可执行文件旁边必须有 www/
add_custom_command(TARGET AIChatServer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/www
            $<TARGET_FILE_DIR:AIChatServer>/www
    COMMENT "Copying web assets to $<TARGET_FILE_DIR:AIChatServer>/www")