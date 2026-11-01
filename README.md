# AISDKprogram · C++ AI Chat SDK & ChatServer

一个用现代 C++17 编写的、可复用的 AI 对话 SDK（`ai_chat_sdk`），以及一个基于 [cpp-httplib](https://github.com/yhirose/cpp-httplib) 的示例 Web 服务（`AIChatServer`）。

SDK 负责「和模型说话」这件事：多模型接入、会话管理、上下文拼装、流式回调、SQLite 持久化；服务端负责把它暴露成 HTTP + SSE 接口，并托管一个开箱即用的聊天前端。

> 前端界面：深色玻璃拟态聊天室，支持多会话切换、流式打字机效果、Markdown 渲染。

---

## ✨ 特性

- **多模型接入**：DeepSeek API（OpenAI 兼容协议）与 Ollama 本地模型，统一的 `LLMProvider` 抽象，新增厂商只需实现一个子类。
- **会话管理**：`SessionManager` 负责会话创建 / 查询 / 删除、历史消息持久化（SQLite）。
- **流式输出**：`sendMessageStream()` 通过回调逐块吐出增量文本，HTTP 层使用 SSE（`/api/message/async`）推送到浏览器。
- **开箱即用**：`AIChatServer` 内置静态资源托管，编译完成即可用浏览器聊天。
- **纯 C++17**：无第三方 RPC 框架，依赖清晰，可把 `ai_chat_sdk` 静态库直接嵌进你自己的程序。

## 🗂 目录结构

```
AISDKprogram/
├── sdk/                     # 核心 SDK，编译产物为静态库 libai_chat_sdk.a
│   ├── include/
│   │   ├── ChatSDK.h        # 对外主入口
│   │   ├── LLMManager.h     # 模型调度
│   │   ├── LLMProvider.h    # 模型提供者抽象基类
│   │   ├── DeepSeekProvider.h
│   │   ├── OllamaLLMProvider.h
│   │   ├── SessionManager.h
│   │   ├── DataManager.h    # SQLite 持久化
│   │   ├── common.h
│   │   └── util/myLog.h     # spdlog 封装
│   └── src/
├── ChatServer/              # HTTP 服务 + Web 前端
│   ├── main.cpp             # gflags 参数解析、启动入口
│   ├── ChatServer.cpp/.h    # 路由与业务处理
│   └── www/                 # 前端静态资源（index.html / app.js / styles.css）
├── test/                    # gtest 单元测试
└── .github/workflows/ci.yml # GitHub Actions 构建 & 测试
```

## 🔧 依赖

| 依赖 | 用途 | Ubuntu/Debian 包名 |
| --- | --- | --- |
| CMake ≥ 3.10 | 构建 | `cmake` |
| GCC ≥ 9 / Clang ≥ 10 | C++17 | `build-essential` |
| jsoncpp | JSON 解析 | `libjsoncpp-dev` |
| fmt | 格式化 | `libfmt-dev` |
| spdlog | 日志 | `libspdlog-dev` |
| gflags | 命令行参数 | `libgflags-dev` |
| SQLite3 | 会话持久化 | `libsqlite3-dev` |
| OpenSSL | HTTPS 客户端 | `libssl-dev` |
| cpp-httplib | HTTP 服务端 | 见下方说明 |
| GoogleTest | 单元测试 | `libgtest-dev` |

```bash
sudo apt update
sudo apt install -y build-essential cmake libjsoncpp-dev libfmt-dev libspdlog-dev \
                    libgflags-dev libsqlite3-dev libssl-dev libgtest-dev
```

**cpp-httplib** 是单头文件库，把 `httplib.h` 放到系统 include 目录即可：

```bash
sudo wget -O /usr/local/include/httplib.h \
  https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.15.3/httplib.h
```

## 🚀 构建

```bash
# 1. 编译并安装 SDK（默认安装到 /usr/local/lib 与 /usr/local/include）
cd sdk
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
sudo cmake --install build

# 2. 编译服务端（会自动把 www/ 拷到可执行文件旁边）
cd ../ChatServer
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# 3. 编译测试（可选）
cd ../test
cmake -B build && cmake --build build -j
```

## ▶️ 运行

API Key 通过**环境变量**读取，绝不写进代码：

```bash
export deepseek_apikey="sk-xxxxxxxxxxxxxxxx"     # Windows PowerShell: $env:deepseek_apikey="sk-..."
cd ChatServer && ./build/AIChatServer --port=8080
```

浏览器打开 <http://localhost:8080> 即可开始聊天。

只想用本地模型（Ollama）时无需 API Key：

```bash
./build/AIChatServer --ollama_model_name=qwen2.5 --ollama_endpoint=http://localhost:11434
```

### 命令行参数（gflags）

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `--host` | `0.0.0.0` | 监听地址 |
| `--port` | `8080` | 监听端口 |
| `--log_level` | `INFO` | 日志级别（TRACE/DEBUG/INFO/WARN/ERROR） |
| `--temperature` | `0.7` | 采样温度 |
| `--max_tokens` | `2048` | 单次回复最大 token 数 |
| `--config_file` | `./ChatServer.conf` | gflags 配置文件路径 |
| `--ollama_model_name` | 空 | 启用的 Ollama 模型名 |
| `--ollama_model_desc` | 空 | 模型描述（前端展示） |
| `--ollama_endpoint` | 空 | Ollama 服务地址 |

## 🌐 HTTP 接口

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| `POST` | `/api/session` | 创建会话（参数：模型名） |
| `GET` | `/api/sessions` | 会话列表 |
| `GET` | `/api/models` | 可用模型列表 |
| `DELETE` | `/api/session/{id}` | 删除会话 |
| `GET` | `/api/session/{id}/history` | 历史消息 |
| `POST` | `/api/message` | 发送消息，一次性返回完整回复 |
| `POST` | `/api/message/async` | 发送消息，SSE 流式返回 |

示例：

```bash
curl -X POST http://localhost:8080/api/session -H 'Content-Type: application/json' \
     -d '{"modelName":"deepseek-chat"}'

curl -N -X POST http://localhost:8080/api/message/async -H 'Content-Type: application/json' \
     -d '{"sessionId":"<ID>","message":"用三句话介绍 C++17"}'
```

## 🧪 测试

```bash
cd test && ./build/AIChatServerTest
```

> `testLLM.cpp` 需要真实 API Key，未设置 `deepseek_apikey` 时会自动跳过，因此可以在 CI 中安全运行。

## 🗺 Roadmap

- [ ] 支持 OpenAI / Claude / Gemini Provider
- [ ] 会话标题自动摘要
- [ ] Docker 镜像与 `docker-compose.yml`
- [ ] 用 `find_package` + `FetchContent` 彻底替代对 `/usr/local/lib` 的硬编码依赖
## 效果展示
<img width="1273" height="683" alt="屏幕截图 2026-11-01 155704" src="https://github.com/user-attachments/assets/9553c9b5-fd06-4773-a1af-ed3e5fd50d60" />

<img width="1276" height="686" alt="屏幕截图 2026-11-01 155710" src="https://github.com/user-attachments/assets/0f3453e5-f42c-4959-859e-c1afa67a3020" />
<img width="1268" height="683" alt="屏幕截图 2026-11-01 155720" src="https://github.com/user-attachments/assets/55c41471-d722-4905-9d4e-31b989c4b288" />


<img width="1265" height="673" alt="屏幕截图 2026-11-01 155844" src="https://github.com/user-attachments/assets/064e3e35-aebc-45e5-9d99-d63972382bbd" />

## 🤝 参与贡献

欢迎提 Issue 和 PR。提交前请确保 `cmake --build` 与单元测试通过，代码风格保持与现有文件一致。

## 📄 License

[MIT](LICENSE) © 2026 nan-yy
