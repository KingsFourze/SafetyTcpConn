#ifndef SFC_CORE_HPP
#define SFC_CORE_HPP

#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <unordered_map>

#include "Classes.hpp"

namespace SafetyTcpConn {

class Core {
private:
    friend class Endpoint;
    friend class Connection;

    int m_epoll_fd_;
    std::thread m_epoll_thread_;
    std::thread m_send_thread_;

    std::mutex m_mtx_endpoints_;
    std::unordered_map<int, Endpoint*> m_fd_2_endpoints_;

    std::mutex m_mtx_connptrs_;
    std::condition_variable m_cond_connptrs_;
    std::unordered_map<int, ConnectionPtr> m_fd_2_connptrs_;
public:
    Core();

private:
    void RegisterEndpoint(Endpoint* endpoint);
    void UnregisterEndpoint(const int endpoint_fd);
    
    void RegisterConnection(ConnectionPtr& conn);
    void UnregisterConnection(const int conn_fd);

    void StartTrySend();

private:
    static void EpollLoop(Core* core);
    static void SendLoop(Core* core);
};

}

#endif