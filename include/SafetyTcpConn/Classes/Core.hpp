#ifndef SFC_CORE_HPP
#define SFC_CORE_HPP

#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <unordered_map>

#include "Classes.hpp"

namespace SafetyTcpConn {

class Core {
private:
    friend class Endpoint;
    friend class Connection;

    std::atomic_bool m_open_;

    int m_epoll_fd_;
    std::thread m_epoll_thread_;
    std::thread m_send_thread_;

    std::mutex m_mtx_containers_;
    std::condition_variable m_cond_containers_;
    std::unordered_map<int, ContainerPtr> m_fd_2_containers_;
public:
    Core();
    ~Core();

private:
    void RegisterContainer(ContainerPtr& container);
    void UnregisterContainer(const int container_fd);

    void StartTrySend();

private:
    static void EpollLoop(Core* core);
    static void SendLoop(Core* core);
};

}

#endif