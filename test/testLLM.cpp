#include <gtest/gtest.h>
#include <istream>
#include <memory>
#include <map>
#include <spdlog/common.h>
#include "../sdk/include/DeepSeekProvider.h"
// #include "../sdk/include/ChatGPTProvider.h"
// #include "../sdk/include/GeminiProvider.h"
#include "../sdk/include/OllamaLLMProvider.h"
#include "../sdk/include/util/myLog.h"
#include "../sdk/include/ChatSDK.h"
#include <iostream>
#include <string>
#include <vector>

TEST(ChatSDKTest, sendMessage){
    auto sdk = std::make_shared<ai_chat_sdk::ChatSDK>();
    ASSERT_TRUE(sdk != nullptr);

    // 配置支持的模型参数：云模型-deepseek-chat gpt-4o-mini gemini-2.0-flash   Ollama本地接入deepseek-r1:1.5b
    // deepseek-chat
    auto deepseekConfig = std::make_shared<ai_chat_sdk::APIConfig>();
    ASSERT_TRUE(deepseekConfig != nullptr);
    deepseekConfig->_modelName = "deepseek-chat";
    deepseekConfig->_apiKey = std::getenv("deepseek_apikey");
    ASSERT_FALSE(deepseekConfig->_apiKey.empty());
    deepseekConfig->_temperature = 0.7;
    deepseekConfig->_maxTokens = 2048;

    // gpt-4o-mini
    // auto chatGPTConfig = std::make_shared<ai_chat_sdk::APIConfig>();
    // ASSERT_TRUE(chatGPTConfig != nullptr);
    // chatGPTConfig->_modelName = "gpt-4o-mini";
    // chatGPTConfig->_apiKey = std::getenv("chatgpt_apikey");
    // ASSERT_FALSE(chatGPTConfig->_apiKey.empty());
    // chatGPTConfig->_temperature = 0.7;
    // chatGPTConfig->_maxTokens = 2048;

    // // gemini-2.0-flash
    // auto geminiConfig = std::make_shared<ai_chat_sdk::APIConfig>();
    // ASSERT_TRUE(geminiConfig != nullptr);
    // geminiConfig->_modelName = "gemini-2.0-flash";
    // geminiConfig->_apiKey = std::getenv("gemini_apikey_new");
    // ASSERT_FALSE(geminiConfig->_apiKey.empty());
    // geminiConfig->_temperature = 0.7;
    // geminiConfig->_maxTokens = 2048;

    // Ollama本地接入deepseek-r1:1.5b
    auto ollamaConfig = std::make_shared<ai_chat_sdk::OllamaConfig>();
    ASSERT_TRUE(ollamaConfig != nullptr);
    ollamaConfig->_modelName = "deepseek-r1:1.5b";
    ollamaConfig->_modelDesc = "本地部署deepseek-r1:1.5b模型，采用专家混合架构，专注于深度理解与推理";
    ollamaConfig->_endpoint = "http://localhost:11434";
    ollamaConfig->_temperature = 0.7;
    ollamaConfig->_maxTokens = 2048;

    std::vector<std::shared_ptr<ai_chat_sdk::Config>> modelConfigs = {
        deepseekConfig,  ollamaConfig
    };

    sdk->initModels(modelConfigs);

    // 创建会话
    auto sessionId = sdk->createSession(ollamaConfig->_modelName);
    ASSERT_FALSE(sessionId.empty());

    std::string message;
    std::cout<<">>> ";
    std::getline(std::cin, message);
    auto response = sdk->sendMessage(sessionId, message);
    ASSERT_FALSE(response.empty());

    std::cout<<">>> ";
    std::getline(std::cin, message);
     sdk->sendMessage(sessionId, message);
    ASSERT_FALSE(response.empty());

    // 获取会话历史消息
    auto messages = sdk->_sessionManager.getHistroyMessages(sessionId);
    for(const auto& msg : messages){
        std::cout<<msg._role<<": "<<msg._content<<std::endl;
    }
    ASSERT_FALSE(messages.empty());
}


int main(int argc, char **argv) {
    // 初始化spdlog日志库
    m_log::Logger::initLogger("testLLM", "stdout", spdlog::level::debug);

    // 初始化gtest库
    testing::InitGoogleTest(&argc, argv);

    // 执行所有的测试用例
    return RUN_ALL_TESTS();
}