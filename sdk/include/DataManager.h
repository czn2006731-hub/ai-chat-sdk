#pragma once
#include <memory>
#include <sqlite3.h>
#include <string>
#include <mutex>
#include <vector>
#include "common.h"

namespace ai_chat_sdk {

class DataManager{
public:
    DataManager(const std::string& dbName);
    ~DataManager();

    // Session相关操作
    // 插入新会话
    bool insertSession(const Session& session);
    // 获取指定会话信息
    std::shared_ptr<Session> getSession(const std::string& sessionId)const;
    // 更新指定会话的时间戳
    bool updateSessionTimestamp(const std::string& sessionId, std::time_t timestamp);
    // 删除指定会话--注意：删除会话时，也需要删除该会话中管理的所有的消息
    bool deleteSession(const std::string& sessionId);
    // 获取所有会话id
    std::vector<std::string> getAllSessionIds()const;
    // 获取所有会话信息
    std::vector<std::shared_ptr<Session>> getAllSessions()const;
    // 删除所有会话
    bool clearAllSessions();
    // 获取会话总数
    size_t getSessionCount()const;
    ////////////////////////////////////////////////////////////////////
    // Message相关操作
    // 插入新消息--注意：插入消息时，需要更新会话的时间戳
    bool insertMessage(const std::string& sessionId, const Message& message);
    // 获取指定会话的历史消息
    std::vector<Message> getSessionMessages(const std::string& sessionId)const;
    // 删除指定会话的所有消息
    bool deleteSessionMessages(const std::string& sessionId);

private:
    // 初始化数据库 -- 创建数据库表
    bool initDataBase();
    // 执行SQL语句的工具函数
    bool executeSQL(const std::string& sql);
    // 以下两个是"不加锁"的内部实现：_mutex 是非递归的，已经持有锁的公有方法
    // （如 getSession、deleteSession）必须调用它们，否则会对同一互斥量二次加锁而死锁
    std::vector<Message> getSessionMessagesUnlocked(const std::string& sessionId)const;
    bool deleteSessionMessagesUnlocked(const std::string& sessionId);

private:
    sqlite3* _db = nullptr;
    std::string _dbName;
    mutable std::mutex _mutex;
};

}// end ai_chat_sdk
