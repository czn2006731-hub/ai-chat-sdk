
#include "../include/DataManager.h"
#include "../include/util/myLog.h"
#include <memory>
#include <mutex>
#include <vector>



namespace ai_chat_sdk {

namespace {

// sqlite3_column_text 在字段为 NULL 时返回空指针，直接构造 std::string 是未定义行为，
// 这里统一做一次空指针保护。
std::string columnText(sqlite3_stmt* stmt, int index){
    const unsigned char* text = sqlite3_column_text(stmt, index);
    if(!text){
        return std::string();
    }
    return std::string(reinterpret_cast<const char*>(text));
}

} // end anonymous namespace

DataManager::DataManager(const std::string& dbName) 
    : _db(nullptr)
    , _dbName(dbName)
{
    // 创建并打开数据库
    int rc = sqlite3_open(dbName.c_str(), &_db);
    if(rc != SQLITE_OK){
        ERR("打开数据库失败：{}", _db ? sqlite3_errmsg(_db) : "sqlite3_open 返回空句柄");
        // 打开失败时 sqlite3 仍可能分配了句柄，这里统一释放并置空，后续操作会因 _db == nullptr 直接失败
        if(_db){
            sqlite3_close(_db);
            _db = nullptr;
        }
        return;
    }
    INFO("打开数据库成功：{}", dbName);

    // 初始化数据库表 - 创建会话表和消息表
    if(!initDataBase()){
        sqlite3_close(_db);
        _db = nullptr;
        ERR("初始化数据库表失败");
    }
}

DataManager::~DataManager(){
    if(_db){
        sqlite3_close(_db);
    }
}

// 初始化数据库
bool DataManager::initDataBase(){
    // 打开外键约束，否则 messages 表上的 ON DELETE CASCADE 不会生效（该开关是连接级的，必须每次连接后设置）
    if(!executeSQL("PRAGMA foreign_keys = ON;")){
        return false;
    }

    // 创建会话表
    std::string createSessionTable = R"(
        CREATE TABLE IF NOT EXISTS sessions (
            session_id TEXT PRIMARY KEY,
            model_name TEXT NOT NULL,
            create_time INTEGER NOT NULL,
            update_time INTEGER NOT NULL
        );
    )";

    // 执行创建Sessions表的SQL语句
    if(!executeSQL(createSessionTable)){
        return false;
    }

    // 创建消息表
    std::string createMessageTable = R"(
        CREATE TABLE IF NOT EXISTS messages (
            message_id TEXT PRIMARY KEY,
            session_id TEXT NOT NULL,
            role TEXT NOT NULL,
            content TEXT NOT NULL,
            timestamp INTEGER NOT NULL,
            FOREIGN KEY (session_id) REFERENCES sessions(session_id) ON DELETE CASCADE
        );
    )";

    // 执行创建Messages表的SQL语句
    if(!executeSQL(createMessageTable)){
        return false;
    }

    // 消息按会话查询，建个索引避免全表扫描
    if(!executeSQL("CREATE INDEX IF NOT EXISTS idx_messages_session_id ON messages(session_id);")){
        return false;
    }

    return true;
}

bool DataManager::executeSQL(const std::string& sql){
    if(!_db){
        ERR("数据库未初始化");
        return false;
    }

    char* errMsg = nullptr;
    int rc = sqlite3_exec(_db, sql.c_str(), nullptr, nullptr, &errMsg);
    if(rc != SQLITE_OK){
        ERR("执行SQL语句失败: {}", errMsg ? errMsg : "unknown error");
        sqlite3_free(errMsg);
        return false;
    }

    return true;
}

// 插入会话
bool DataManager::insertSession(const Session& session){
    std::lock_guard<std::mutex> lock(_mutex);

    if(!_db){
        ERR("insertSession - 数据库未初始化");
        return false;
    }

    // 构建SQL语句
    std::string insertSQL = R"(
        INSERT INTO sessions (session_id, model_name, create_time, update_time)
        VALUES (?, ?, ?, ?);
    )";

    // 准备SQL语句
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(_db, insertSQL.c_str(), -1, &stmt, nullptr);
    if(rc != SQLITE_OK){
        ERR("insertSession - 准备语句失败：{}", sqlite3_errmsg(_db));
        return false;
    }

    // 绑定参数
    sqlite3_bind_text(stmt, 1, session._sessionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, session._modelName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, static_cast<int64_t>(session._createdAt));
    sqlite3_bind_int64(stmt, 4, static_cast<int64_t>(session._updatedAt));

    // 执行SQL语句
    rc = sqlite3_step(stmt);
    if(rc != SQLITE_DONE){
        ERR("insertSession - 执行语句失败：{}", sqlite3_errmsg(_db));
        sqlite3_finalize(stmt);
        return false;
    }
    // 释放语句
    sqlite3_finalize(stmt);
    INFO("insertSession - 插入会话成功：{}", session._sessionId);
    return true;
}

// 获取指定sessionId的会话信息
std::shared_ptr<Session> DataManager::getSession(const std::string& sessionId)const{

    std::lock_guard<std::mutex> lock(_mutex);

    if(!_db){
        ERR("getSession - 数据库未初始化");
        return nullptr;
    }

    // 构建SQL语句
    std::string selectSQL = R"(
        SELECT model_name, create_time, update_time FROM sessions WHERE session_id = ?;
    )";

    // 准备SQL语句
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(_db, selectSQL.c_str(), -1, &stmt, nullptr);
    if(rc != SQLITE_OK){
        ERR("getSession - 准备语句失败：{}", sqlite3_errmsg(_db));
        return nullptr;
    }

    // 绑定参数
    sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);

    // 执行SQL语句。SELECT 命中一行时 sqlite3_step 返回 SQLITE_ROW，
    // 只有遍历结束才返回 SQLITE_DONE，因此这里必须判断 SQLITE_ROW。
    std::shared_ptr<Session> session;
    rc = sqlite3_step(stmt);
    if(rc == SQLITE_ROW){
        // 从结果集中提取数据
        std::string modelName = columnText(stmt, 0);
        int64_t createTime = sqlite3_column_int64(stmt, 1);
        int64_t updateTime = sqlite3_column_int64(stmt, 2);

        // 创建会话对象
        session = std::make_shared<Session>(modelName);
        session->_sessionId = sessionId;
        session->_createdAt = static_cast<std::time_t>(createTime);
        session->_updatedAt = static_cast<std::time_t>(updateTime);
    }else if(rc == SQLITE_DONE){
        WARN("getSession - 会话不存在：{}", sessionId);
    }else{
        ERR("getSession - 执行语句失败：{}", sqlite3_errmsg(_db));
    }

    // 释放语句
    sqlite3_finalize(stmt);

    if(!session){
        return nullptr;
    }

    // 获取该会话的所有消息。
    // 注意：这里必须调用不加锁的内部实现，getSession 已经持有 _mutex，
    // 直接调 getSessionMessages() 会对同一个非递归互斥量二次加锁 → 自死锁。
    session->_messages = getSessionMessagesUnlocked(sessionId);

    INFO("getSession - 获取会话成功：{}", sessionId);
    return session;
}

// 更新指定会话的时间戳
bool DataManager::updateSessionTimestamp(const std::string& sessionId, std::time_t timestamp){
    std::lock_guard<std::mutex> lock(_mutex);

    if(!_db){
        ERR("updateSessionTimestamp - 数据库未初始化");
        return false;
    }

    // 构建SQL语句
    std::string updateSQL = R"(
        UPDATE sessions SET update_time = ? WHERE session_id = ?;
    )";

    // 准备SQL语句
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(_db, updateSQL.c_str(), -1, &stmt, nullptr);
    if(rc != SQLITE_OK){
        ERR("updateSessionTimestamp - 准备语句失败：{}", sqlite3_errmsg(_db));
        return false;
    }

    // 绑定参数
    sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(timestamp));
    sqlite3_bind_text(stmt, 2, sessionId.c_str(), -1, SQLITE_TRANSIENT);

    // 执行SQL语句
    rc = sqlite3_step(stmt);
    if(rc != SQLITE_DONE){
        ERR("updateSessionTimestamp - 执行语句失败：{}", sqlite3_errmsg(_db));
        sqlite3_finalize(stmt);
        return false;
    }

    // 释放语句
    sqlite3_finalize(stmt);
    INFO("updateSessionTimestamp - 更新会话时间戳成功：{}", sessionId);
    return true;
}

// 删除指定会话
bool DataManager::deleteSession(const std::string& sessionId){
    std::lock_guard<std::mutex> lock(_mutex);

    if(!_db){
        ERR("deleteSession - 数据库未初始化");
        return false;
    }

    // 先删消息，再删会话。外键级联虽然已经开启，但老版本数据库（建表时未开外键）
    // 里可能残留孤儿消息，这里显式删除一次更稳妥。
    if(!deleteSessionMessagesUnlocked(sessionId)){
        ERR("deleteSession - 删除会话消息失败：{}", sessionId);
        return false;
    }

    // 构建SQL语句
    std::string deleteSQL = R"(
        DELETE FROM sessions WHERE session_id = ?;
    )";

    // 准备SQL语句
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(_db, deleteSQL.c_str(), -1, &stmt, nullptr);
    if(rc != SQLITE_OK){
        ERR("deleteSession - 准备语句失败：{}", sqlite3_errmsg(_db));
        return false;
    }

    // 绑定参数
    sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);

    // 执行SQL语句
    rc = sqlite3_step(stmt);
    if(rc != SQLITE_DONE){
        ERR("deleteSession - 执行语句失败：{}", sqlite3_errmsg(_db));
        sqlite3_finalize(stmt);
        return false;
    }

    // 释放语句
    sqlite3_finalize(stmt);
    INFO("deleteSession - 删除会话成功：{}", sessionId);
    return true;
}

// 获取所有会话id
std::vector<std::string> DataManager::getAllSessionIds()const{
    std::lock_guard<std::mutex> lock(_mutex);

    if(!_db){
        ERR("getAllSessionIds - 数据库未初始化");
        return {};
    }

    // 构建SQL语句
    std::string selectSQL = R"(
        SELECT session_id FROM sessions ORDER BY update_time DESC;
    )";

    // 准备SQL语句
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(_db, selectSQL.c_str(), -1, &stmt, nullptr);
    if(rc != SQLITE_OK){
        ERR("getAllSessionIds - 准备语句失败：{}", sqlite3_errmsg(_db));
        return {};
    }

    std::vector<std::string> sessionIds;
    while(sqlite3_step(stmt) == SQLITE_ROW){
        sessionIds.push_back(columnText(stmt, 0));
    }

    // 释放语句
    sqlite3_finalize(stmt);
    INFO("getAllSessionIds - 获取所有会话id成功, 会话总数：{}", sessionIds.size());
    return sessionIds;
}

// 获取所有session信息，并按照更新时间降序排列
std::vector<std::shared_ptr<Session>> DataManager::getAllSessions()const{
    std::lock_guard<std::mutex> lock(_mutex);

    if(!_db){
        ERR("getAllSessions - 数据库未初始化");
        return {};
    }

    // 构建SQL语句
    std::string selectSQL = R"(
        SELECT session_id, model_name, create_time, update_time FROM sessions ORDER BY update_time DESC;
    )";

    // 准备SQL语句
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(_db, selectSQL.c_str(), -1, &stmt, nullptr);
    if(rc != SQLITE_OK){
        ERR("getAllSessions - 准备语句失败：{}", sqlite3_errmsg(_db));
        return {};
    }

    std::vector<std::shared_ptr<Session>> sessions;
    while(sqlite3_step(stmt) == SQLITE_ROW){
        std::string sessionId = columnText(stmt, 0);
        std::string modelName = columnText(stmt, 1);
        int64_t createTime = sqlite3_column_int64(stmt, 2);
        int64_t updateTime = sqlite3_column_int64(stmt, 3);

        auto session = std::make_shared<Session>(modelName);
        session->_sessionId = sessionId;
        session->_createdAt = static_cast<std::time_t>(createTime);
        session->_updatedAt = static_cast<std::time_t>(updateTime);
        sessions.push_back(session);

        // 历史消息暂时不获取，需要时再通过会话id来进行获取
    }

    // 释放语句
    sqlite3_finalize(stmt);
    INFO("getAllSessions - 获取所有会话信息成功, 会话总数：{}", sessions.size());
    return sessions;
}

// 获取会话总数
size_t DataManager::getSessionCount()const
{
    std::lock_guard<std::mutex> lock(_mutex);

    if(!_db){
        ERR("getSessionCount - 数据库未初始化");
        return 0;
    }

    // 准备SQL语句
    std::string selectSQL = R"(
        SELECT COUNT(*) FROM sessions;
    )";

    // 准备SQL语句
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(_db, selectSQL.c_str(), -1, &stmt, nullptr);
    if(rc != SQLITE_OK){
        ERR("getSessionCount - 准备语句失败：{}", sqlite3_errmsg(_db));
        return 0;
    }

    // 执行SQL语句
    rc = sqlite3_step(stmt);
    if(rc != SQLITE_ROW){
        ERR("getSessionCount - 执行语句失败：{}", sqlite3_errmsg(_db));
        sqlite3_finalize(stmt);
        return 0;
    }

    // 获取会话总数
    size_t count = static_cast<size_t>(sqlite3_column_int64(stmt, 0));

    // 释放语句
    sqlite3_finalize(stmt);
    INFO("getSessionCount - 获取会话总数成功：{}", count);
    return count;
}

// 删除所有会话
bool DataManager::clearAllSessions(){
    std::lock_guard<std::mutex> lock(_mutex);

    if(!_db){
        ERR("clearAllSessions - 数据库未初始化");
        return false;
    }

    // 先清消息，再清会话，避免留下孤儿消息
    if(!executeSQL("DELETE FROM messages;")){
        ERR("clearAllSessions - 删除所有消息失败");
        return false;
    }

    // 构建SQL语句
    std::string deleteSQL = R"(
        DELETE FROM sessions;
    )";

    // 准备SQL语句
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(_db, deleteSQL.c_str(), -1, &stmt, nullptr);
    if(rc != SQLITE_OK){
        ERR("clearAllSessions - 准备语句失败：{}", sqlite3_errmsg(_db));
        return false;
    }

    // 执行SQL语句
    rc = sqlite3_step(stmt);
    if(rc != SQLITE_DONE){
        ERR("clearAllSessions - 执行语句失败：{}", sqlite3_errmsg(_db));
        sqlite3_finalize(stmt);
        return false;
    }

    // 释放语句
    sqlite3_finalize(stmt);
    INFO("clearAllSessions - 删除所有会话成功");
    return true;
}

/////////////////////////////////////////////////////////////Messages///////////////////////////////////////
// 插入新消息--注意：插入消息时，需要更新会话的时间戳
bool DataManager::insertMessage(const std::string& sessionId, const Message& message){
    std::lock_guard<std::mutex> lock(_mutex);

    if(!_db){
        ERR("insertMessage - 数据库未初始化");
        return false;
    }

    // 构建SQL语句
    std::string insertSQL = R"(
        INSERT INTO messages (message_id, session_id, role, content, timestamp)
        VALUES (?, ?, ?, ?, ?);
    )";

    // 准备SQL语句
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(_db, insertSQL.c_str(), -1, &stmt, nullptr);
    if(rc != SQLITE_OK){
        ERR("insertMessage - 准备语句失败：{}", sqlite3_errmsg(_db));
        return false;
    }

    // 绑定参数
    sqlite3_bind_text(stmt, 1, message._messageId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, sessionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, message._role.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, message._content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, static_cast<int64_t>(message._timestamp));

    // 执行SQL语句
    rc = sqlite3_step(stmt);
    if(rc != SQLITE_DONE){
        ERR("insertMessage - 执行语句失败：{}", sqlite3_errmsg(_db));
        sqlite3_finalize(stmt);
        return false;
    }

    // 同时更新session的update_time
    std::string updateSQL = R"(
        UPDATE sessions SET update_time = ? WHERE session_id = ?;
    )";

    // 准备SQL语句
    sqlite3_stmt* updateStmt = nullptr;
    rc = sqlite3_prepare_v2(_db, updateSQL.c_str(), -1, &updateStmt, nullptr);
    if(rc != SQLITE_OK){
        ERR("insertMessage - 准备语句失败：{}", sqlite3_errmsg(_db));
        sqlite3_finalize(stmt);
        return false;
    }

    // 绑定参数
    sqlite3_bind_int64(updateStmt, 1, static_cast<int64_t>(message._timestamp));
    sqlite3_bind_text(updateStmt, 2, sessionId.c_str(), -1, SQLITE_TRANSIENT);

    // 执行SQL语句
    rc = sqlite3_step(updateStmt);
    if(rc != SQLITE_DONE){
        ERR("insertMessage - 执行语句失败：{}", sqlite3_errmsg(_db));
        sqlite3_finalize(stmt);
        sqlite3_finalize(updateStmt);
        return false;
    }

    // 释放语句
    sqlite3_finalize(stmt);
    sqlite3_finalize(updateStmt);
    INFO("insertMessage - 插入消息成功：{}", message._messageId);
    return true;
}

// 获取会话中的所有消息（不加锁版本，调用方必须已持有 _mutex）
std::vector<Message> DataManager::getSessionMessagesUnlocked(const std::string& sessionId)const
{
    if(!_db){
        ERR("getSessionMessages - 数据库未初始化");
        return {};
    }

    // 按时间排序，保证前端渲染出来的顺序与对话顺序一致
    std::string selectSQL = R"(
        SELECT message_id, role, content, timestamp FROM messages WHERE session_id = ? ORDER BY timestamp ASC, rowid ASC;
    )";

    // 准备SQL语句
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(_db, selectSQL.c_str(), -1, &stmt, nullptr);
    if(rc != SQLITE_OK){
        ERR("getSessionMessages - 准备语句失败：{}", sqlite3_errmsg(_db));
        return {};
    }

    // 绑定参数
    sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);

    // 执行SQL语句
    std::vector<Message> messages;
    while((rc = sqlite3_step(stmt)) == SQLITE_ROW){
        Message message;
        message._messageId = columnText(stmt, 0);
        message._role = columnText(stmt, 1);
        message._content = columnText(stmt, 2);
        message._timestamp = static_cast<std::time_t>(sqlite3_column_int64(stmt, 3));
        messages.push_back(message);
    }

    if(rc != SQLITE_DONE){
        ERR("getSessionMessages - 执行语句失败：{}", sqlite3_errmsg(_db));
        sqlite3_finalize(stmt);
        return {};
    }

    // 释放语句
    sqlite3_finalize(stmt);
    return messages;
}

// 获取会话中的所有消息
std::vector<Message> DataManager::getSessionMessages(const std::string& sessionId)const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return getSessionMessagesUnlocked(sessionId);
}

// 删除指定会话的历史消息（不加锁版本，调用方必须已持有 _mutex）
bool DataManager::deleteSessionMessagesUnlocked(const std::string& sessionId){
    if(!_db){
        ERR("deleteSessionMessages - 数据库未初始化");
        return false;
    }

    // 构建SQL语句
    std::string deleteSQL = R"(
        DELETE FROM messages WHERE session_id = ?;
    )";

    // 准备SQL语句
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(_db, deleteSQL.c_str(), -1, &stmt, nullptr);
    if(rc != SQLITE_OK){
        ERR("deleteSessionMessages - 准备语句失败：{}", sqlite3_errmsg(_db));
        return false;
    }

    // 绑定参数
    sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);

    // 执行SQL语句
    rc = sqlite3_step(stmt);
    if(rc != SQLITE_DONE){
        ERR("deleteSessionMessages - 执行语句失败：{}", sqlite3_errmsg(_db));
        sqlite3_finalize(stmt);
        return false;
    }

    // 释放语句
    sqlite3_finalize(stmt);
    INFO("deleteSessionMessages - 删除会话消息成功：{}", sessionId);
    return true;
}

// 删除指定会话的历史消息
bool DataManager::deleteSessionMessages(const std::string& sessionId){
    std::lock_guard<std::mutex> lock(_mutex);
    return deleteSessionMessagesUnlocked(sessionId);
}


} // end ai_chat_sdk
