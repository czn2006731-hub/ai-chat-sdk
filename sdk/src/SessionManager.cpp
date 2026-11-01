#include "../include/SessionManager.h"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <vector>
#include <utility>
#include "../include/util/myLog.h"


namespace ai_chat_sdk {
SessionManager::SessionManager(const std::string& dbName) 
    : _dataManager(dbName)
{
    // 获取所有会话（此时只加载会话基本信息，历史消息在第一次 getSession 时再按需加载）
    auto sessions = _dataManager.getAllSessions();
    for(auto& session : sessions){
        _sessions[session->_sessionId] = session;
    }
    INFO("SessionManager - 加载历史会话完成，会话总数：{}", _sessions.size());
}

// 生成会话id  会话id格式：session_时间戳_会话计数
std::string SessionManager::generateSessionId(){
    // 会话计数自增
    int64_t counter = _sessionCounter.fetch_add(1) + 1;
    std::time_t time = std::time(nullptr);

    // 生成会话id
    std::ostringstream os;
    //  session_1234567890_00000001
    os<<"session_"<<time<<"_"<<std::setw(8)<<std::setfill('0')<<counter;
    return os.str();
}

// 生成消息id  msg_1234567890_00000001
// 注意：计数必须是全局自增的。早期实现用"会话内消息条数"做序号，
// 同一秒内在两个不同会话里各发第一条消息就会生成相同的 message_id，
// 触发 messages 表主键冲突，导致消息静默写库失败。
std::string SessionManager::generateMessageId(){
    int64_t counter = _messageCounter.fetch_add(1) + 1;
    std::time_t time = std::time(nullptr);

    std::ostringstream os;
    os<<"msg_"<<time<<"_"<<std::setw(8)<<std::setfill('0')<<counter;
    return os.str();
}

// 创建会话，提供模型名称
std::string SessionManager::createSession(const std::string& modelName){
    std::string sessionId;
    std::shared_ptr<Session> session;
    {
        std::lock_guard<std::mutex> lock(_mutex);

        // 生成会话id
        sessionId = generateSessionId();

        // 创建会话，并设置会话id
        session = std::make_shared<Session>(modelName);
        session->_sessionId = sessionId;
        session->_createdAt = std::time(nullptr);
        session->_updatedAt = session->_createdAt;

        // 将会话入到会话列表
        _sessions[sessionId] = session;
    }

    // 将会话保存到数据库（放到锁外，避免持锁做磁盘 IO）
    if(!_dataManager.insertSession(*session)){
        ERR("createSession - 会话入库失败：{}", sessionId);
    }
    return sessionId;
}

// 通过会话ID获取会话信息
std::shared_ptr<Session> SessionManager::getSession(const std::string& sessionId){
    // 先在内存中查找。
    // 注意：解锁后不能再使用迭代器（其他线程可能已经 erase 掉该元素），
    // 所以这里在持锁期间就把 shared_ptr 拷贝出来。
    std::shared_ptr<Session> session;
    bool foundInMemory = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _sessions.find(sessionId);
        if(it != _sessions.end()){
            session = it->second;
            foundInMemory = true;
        }
    }

    if(foundInMemory){
        // 服务启动时只从库里加载了会话基本信息，历史消息是空的，
        // 这里按需补一次（已有消息的会话不再重复查库）
        bool needLoadMessages = false;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            needLoadMessages = session->_messages.empty();
        }

        if(needLoadMessages){
            auto messages = _dataManager.getSessionMessages(sessionId);
            std::lock_guard<std::mutex> lock(_mutex);
            if(session->_messages.empty()){
                session->_messages = std::move(messages);
            }
        }
        return session;
    }

    // 内存中没有找到，从数据库中查找（getSession 内部会一并加载历史消息）
    auto loaded = _dataManager.getSession(sessionId);
    if(!loaded){
        WARN("sessionId = {} not found", sessionId);
        return nullptr;
    }

    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _sessions.find(sessionId);
        if(it == _sessions.end()){
            // 内存中没有，将会话加入会话列表
            _sessions[sessionId] = loaded;
        }else{
            // 并发情况下别的线程已经放进去了，以内存中的对象为准
            return it->second;
        }
    }
    return loaded;
}

// 往某个会话中添加消息
bool SessionManager::addMessage(const std::string& sessionId, const Message& message){
    Message msg;
    {
        std::lock_guard<std::mutex> lock(_mutex);

        // 获取到sessionId对应的会话
        auto it = _sessions.find(sessionId);
        if(it == _sessions.end()){
            ERR("addMessage - 会话不存在：{}", sessionId);
            return false;
        }

        // 创建消息
        msg = Message(message._role, message._content);
        msg._messageId = generateMessageId();
        msg._timestamp = std::time(nullptr);  // 设置消息时间戳

        // 将消息添加到会话中
        it->second->_messages.push_back(msg);
        it->second->_updatedAt = msg._timestamp;
    }

    // 将会话保存到数据库（放到锁外，避免持锁做磁盘 IO）
    if(!_dataManager.insertMessage(sessionId, msg)){
        ERR("addMessage - 消息入库失败：sessionId = {}, messageId = {}", sessionId, msg._messageId);
        return false;
    }

    INFO("add message success, sessionId = {}, messageId = {}", sessionId, msg._messageId);
    return true;
}

// 获取某个会话的所有历史消息
std::vector<Message> SessionManager::getHistroyMessages(const std::string& sessionId)const{
    // 先从内存中获取会话消息，如果内存中获取不到，再到数据库中获取
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _sessions.find(sessionId);
        if(it != _sessions.end()){
            return it->second->_messages;
        }
    }

    // 从数据库中获取消息列表
    return _dataManager.getSessionMessages(sessionId);
}

// 更新会话时间戳
void SessionManager::updateSessionTimestamp(const std::string& sessionId){
    std::time_t timestamp = std::time(nullptr);

    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _sessions.find(sessionId);
        if(it == _sessions.end()){
            // 会话不存在，只更新数据库里可能存在的那条记录
            WARN("updateSessionTimestamp - 会话不在内存中：{}", sessionId);
        }else{
            it->second->_updatedAt = timestamp;
        }
    }

    // 更新数据库中的会话时间戳（不能再用上面那个迭代器，锁已经释放）
    _dataManager.updateSessionTimestamp(sessionId, timestamp);
}

// 获取所有会话列表
std::vector<std::string> SessionManager::getSessionLists()const{
    auto sessions = _dataManager.getAllSessions();

    std::lock_guard<std::mutex> lock(_mutex);
    // 构建一个临时对话列表，将其内容的会话按照会话更新的时间戳降序排列
    std::vector<std::pair<std::time_t, std::shared_ptr<Session>>> temp;
    temp.reserve(_sessions.size() + sessions.size());

    // 将会话添加到临时列表中
    for(const auto& pair : _sessions){
        temp.emplace_back(pair.second->_updatedAt, pair.second);
    }

    // 将数据库中的会话添加到临时列表中
    for(const auto& session : sessions){
        if(_sessions.find(session->_sessionId) == _sessions.end()){
            temp.emplace_back(session->_updatedAt, session);
        }
    }

    // 按照会话更新时间戳降序排序
    std::sort(temp.begin(), temp.end(), [](const auto& a, const auto& b){
        return a.first > b.first;
    });

    std::vector<std::string> sessionIds;
    sessionIds.reserve(temp.size());
    // 从临时列表中提取会话id
    for(const auto& pair : temp){
        sessionIds.push_back(pair.second->_sessionId);
    }
    return sessionIds;
}

// 删除某个会话
bool SessionManager::deleteSession(const std::string& sessionId){
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _sessions.find(sessionId);
        if(it == _sessions.end()){
            return false;
        }

        // 从内存中删除会话
        _sessions.erase(it);
    }

    // 从数据库中删除会话（DataManager 会同时删除该会话下的所有消息）
    if(!_dataManager.deleteSession(sessionId)){
        ERR("deleteSession - 数据库删除失败：{}", sessionId);
        return false;
    }
    return true;
}

// 清空所有会话
void SessionManager::clearAllSessions(){
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _sessions.clear();
    }

    // 清空数据库中的所有会话（连同消息）
    _dataManager.clearAllSessions();
}

// 获取会话总数
size_t SessionManager::getSessionCount()const{
    std::lock_guard<std::mutex> lock(_mutex);
    return _sessions.size();
}


} // end ai_chat_sdk
