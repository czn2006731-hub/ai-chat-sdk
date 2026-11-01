# 设置Cmake的最小版本
cmake_minimum_required(VERSION 3.10)

# 项目名称
project(testLLM)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置构建类型Debug
set(CMAKE_BUILD_TYPE Debug)

# 添加可执行文件
add_executable(testLLM testLLM.cpp
../sdk/src/util/myLog.cpp
../sdk/src/DeepSeekProvider.cpp
# ../sdk/src/ChatGPTProvider.cpp
# ../sdk/src/GeminiProvider.cpp
../sdk/src/OllamaLLMProvider.cpp
../sdk/src/DataManager.cpp
../sdk/src/SessionManager.cpp
../sdk/src/LLMManager.cpp
../sdk/src/ChatSDK.cpp)

# 设置输出目录
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR})

# 添加头文件搜索路劲
include_directories(${CMAKE_PROJECT_INCLUDE_DIR}/../sdk/include)

find_package(OpenSSL REQUIRED)
include_directories(${OPENSSL_INCLUDE_DIR})

# 添加CPPHTTPLIB_OPENSSL_SUPPORT定义
target_compile_definitions(testLLM PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)

# 链接库
target_link_libraries(testLLM jsoncpp fmt  spdlog gtest OpenSSL::SSL OpenSSL::Crypto sqlite3)
