#ifndef SFC_CORE_FUNC_HPP
#define SFC_CORE_FUNC_HPP

#include <sys/epoll.h>

#include "Classes.hpp"
#include "Core.hpp"
#include "Endpoint.hpp"

namespace SafetyTcpConn {

Core::Core() : m_open_(true) {
    if ((m_epoll_fd_ = epoll_create(1)) == -1) {
        std::cout << "SafetyTcpConn >> Core >> Error >> Can't create Epoll" << std::endl;
        exit(EXIT_FAILURE);
    }

    std::cout << "SafetyTcpConn >> Core >> Epoll Create Success | Epoll FD: " << m_epoll_fd_ << std::endl;
    m_epoll_thread_ = std::thread(EpollLoop, this);
    m_send_thread_ = std::thread(SendLoop, this);
}

Core::~Core() {
    m_open_.store(false);

    // wake up send thread
    StartTrySend();

    m_epoll_thread_.join();
    m_send_thread_.join();

    std::cout << "SafetyTcpConn >> Core >> Safety Clean | Epoll FD: " << m_epoll_fd_ << std::endl;
}

inline void Core::RegisterEndpoint(EndpointPtr endpoint) {
    {
        std::unique_lock<std::mutex> lck(m_mtx_endpoints_);
        m_fd_2_endpoints_[endpoint->m_fd_] = endpoint;
    }

    epoll_event event{};
    event.events = EPOLLIN;
    event.data.fd = endpoint->m_fd_;
    epoll_ctl(m_epoll_fd_, EPOLL_CTL_ADD, endpoint->m_fd_, &event);
}

inline void Core::UnregisterEndpoint(const int endpoint_fd) {
    EndpointPtr endpoint = nullptr;
    {
        std::unique_lock<std::mutex> lck(m_mtx_endpoints_);
        // try get endpoint ptr
        auto it = m_fd_2_endpoints_.find(endpoint_fd);
        if (it == m_fd_2_endpoints_.end())
            return;

        endpoint = it->second;

        // remove from connection ptr map
        m_fd_2_endpoints_.erase(it);
    }

    // unsubscribe from epoll
    epoll_ctl(m_epoll_fd_, EPOLL_CTL_DEL, endpoint->m_fd_, nullptr);
}

inline void Core::RegisterConnection(ConnectionPtr& conn) {
    // add into connection ptr map
    {
        std::unique_lock<std::mutex> lck(m_mtx_connptrs_);
        m_fd_2_connptrs_[conn->m_fd_] = conn;
    }

    // epoll subscribe to client
    epoll_event client_event{};
    client_event.events = EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP | EPOLLRDHUP | EPOLLET;
    client_event.data.fd = conn->m_fd_;
    epoll_ctl(m_epoll_fd_, EPOLL_CTL_ADD, conn->m_fd_, &client_event);

    // run connection init function
    conn->m_coninit_func_(conn);
}

inline void Core::UnregisterConnection(const int conn_fd) {
    ConnectionPtr conn;
    {
        std::unique_lock<std::mutex> lck(m_mtx_connptrs_);

        // try get connection ptr
        auto it = m_fd_2_connptrs_.find(conn_fd);
        if (it == m_fd_2_connptrs_.end())
            return;

        conn = it->second;

        // remove from connection ptr map
        m_fd_2_connptrs_.erase(it);
    }

    // unsubscribe from epoll
    epoll_ctl(m_epoll_fd_, EPOLL_CTL_DEL, conn->m_fd_, nullptr);

    // remove from endpoint
    EndpointPtr endpoint = conn->m_endpoint_.lock();
    if (endpoint != nullptr)
        Endpoint::Remove(endpoint, conn->m_fd_);
    
    // close connection and run cleanup function
    conn->CloseConn();
    conn->m_cleanup_func_(conn);
}

inline void Core::StartTrySend() {
    std::unique_lock<std::mutex> lck(m_mtx_connptrs_);
    m_cond_connptrs_.notify_one();
}

inline void Core::EpollLoop(Core* core) {
    constexpr int kMaxEventSize = 32;
    epoll_event epoll_events[kMaxEventSize];

    int event_count = 0;
    while (core->m_open_.load()) {
        if ((event_count = epoll_wait(core->m_epoll_fd_, epoll_events, kMaxEventSize, 1000)) == -1) {
            std::cerr << "SafetyTcpConn >> Endpoint >> Error >> Epoll Error!" << std::endl;
            exit(EXIT_FAILURE);
        }

        // scan and remove locally closed connection
        {
            // find all locally closed connection
            std::vector<ConnectionPtr> locally_closed_connections;
            for (auto it = core->m_fd_2_connptrs_.begin(); it != core->m_fd_2_connptrs_.end(); it++) {
                ConnectionPtr& conn = it->second;
                if (conn->IsConn()) 
                    continue;
                locally_closed_connections.push_back(conn);
            }

            // run normal cleanup funtion
            for (int i = 0; i < locally_closed_connections.size(); i++) {
                ConnectionPtr& conn = locally_closed_connections.at(i);
                core->UnregisterConnection(conn->m_fd_);
            }
        }

        for (int i = 0; i < event_count; i++) {
            // find endpoint from endpoint maps with fd
            EndpointPtr endpoint = nullptr;
            {
                std::unique_lock<std::mutex> lck_endpoints(core->m_mtx_endpoints_);
                auto it_endpoints = core->m_fd_2_endpoints_.find(epoll_events[i].data.fd);

                // endpoint found
                if (it_endpoints != core->m_fd_2_endpoints_.end())
                    endpoint = it_endpoints->second;
            }

            // endpoint found, accept connection
            if (endpoint.get() != nullptr) {
                ConnectionPtr conn = Endpoint::Accept(endpoint);
                if (conn != nullptr)
                    core->RegisterConnection(conn);
                continue;
            }

            // Error or Disconnect
            if (epoll_events[i].events & EPOLLERR || epoll_events[i].events & EPOLLHUP || epoll_events[i].events & EPOLLRDHUP) {
                core->UnregisterConnection(epoll_events[i].data.fd);
            }
            // Data Coming
            else if (epoll_events[i].events & EPOLLIN) {
                ConnectionPtr conn = nullptr;
                {
                    std::unique_lock<std::mutex> lck_connptrs(core->m_mtx_connptrs_);

                    auto it_connptrs = core->m_fd_2_connptrs_.find(epoll_events[i].data.fd);
                    if (it_connptrs != core->m_fd_2_connptrs_.end())
                        conn = it_connptrs->second;
                }

                // connection receive message
                if (conn != nullptr && conn->TryRecv()) {
                    // run process function
                    conn->m_process_func_(conn);
                }
            }
            // Send Avaliable
            else if (epoll_events[i].events & EPOLLOUT) {
                ConnectionPtr conn = nullptr;
                {
                    std::unique_lock<std::mutex> lck_connptrs(core->m_mtx_connptrs_);

                    auto it_connptrs = core->m_fd_2_connptrs_.find(epoll_events[i].data.fd);
                    if (it_connptrs != core->m_fd_2_connptrs_.end())
                        conn = it_connptrs->second;
                }

                conn->SetSendFlag();
                core->StartTrySend();
            }
        }
    }

    std::cout << "SafetyTcpConn >> Core >> Epoll Thread Ended | Epoll FD: " << core->m_epoll_fd_ << std::endl;
}

inline void Core::SendLoop(Core* core) {
    std::unordered_set<ConnectionPtr> need_to_send;
    std::unordered_set<ConnectionPtr> no_need_to_send;

    while (core->m_open_.load()) {
        // update need_to_send set
        {
            std::unique_lock<std::mutex> lck(core->m_mtx_connptrs_);

            start_update_set:
            for (auto it = core->m_fd_2_connptrs_.begin(); it != core->m_fd_2_connptrs_.end(); it++) {
                const ConnectionPtr& conn = it->second;

                if (conn->NeedSend() && need_to_send.find(conn) == need_to_send.end())
                    need_to_send.emplace(conn);
            }

            // nothing need to send, wait
            if (need_to_send.size() == 0) {
                core->m_cond_connptrs_.wait_for(lck, std::chrono::milliseconds(1));
                if (!core->m_open_.load())
                    break;

                goto start_update_set; // this can save time on unlocking and relocking
            }
        }

        // call TrySend for connection in need_to_send set
        uint32_t send_success_count = 0;
        for (auto it = need_to_send.begin(); it != need_to_send.end(); it++) {
            const ConnectionPtr& conn = *it;
            
            // sent messages until can't send
            int quota = 10; // fair usage policy
            while (quota-- > 0) {
                if (conn->TrySend() <= 0) {
                    no_need_to_send.emplace(conn);
                    break;
                }
                send_success_count++;
            }
        }

        // remove no_need_to_send connection from need_to_send set
        for (auto it = no_need_to_send.begin(); it != no_need_to_send.end(); it++)
            need_to_send.erase(*it);
        no_need_to_send.clear();
    }

    std::cout << "SafetyTcpConn >> Core >> Send Thread Ended | Epoll FD: " << core->m_epoll_fd_ << std::endl;
}

}

#endif