#ifndef STC_ENDPOINT_FUNC_HPP
#define STC_ENDPOINT_FUNC_HPP

#include "Endpoint.hpp"

namespace SafetyTcpConn {

//==============================
// Connection Control Area
//==============================

inline void Endpoint::StartTrySend() {
    std::unique_lock<std::mutex> lck(m_mtx_connptrs_);
    m_cond_connptrs_.notify_one();
}

//==============================
// Endpoint Control Area
//==============================

inline void Endpoint::Accept() {
    // accept connection
    sockaddr_in client_sockaddr{};
    socklen_t length = sizeof(client_sockaddr);
    int client_fd = accept(m_sock_fd_, (sockaddr *) &client_sockaddr, &length);

    // create connection instance
    ConnectionPtr conn = std::make_shared<Connection>(client_fd, this);

    // add into connection ptr map
    {
        std::unique_lock<std::mutex> lck(m_mtx_connptrs_);
        m_fd_2_connptrs_[conn->m_fd_] = conn;
    }

    // epoll subscribe to client
    epoll_event client_event{};
    client_event.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP; // Add "| EPOLLET" to activate ET mode.
    client_event.data.fd = conn->m_fd_;
    epoll_ctl(m_epoll_fd_, EPOLL_CTL_ADD, conn->m_fd_, &client_event);

    // run connection init function
    m_coninit_func_(conn);
}

inline void Endpoint::Remove(int fd) {
    ConnectionPtr conn;

    {
        std::unique_lock<std::mutex> lck(m_mtx_connptrs_);
        // try get connection ptr
        auto it = m_fd_2_connptrs_.find(fd);
        if (it == m_fd_2_connptrs_.end())
            return;
        conn = it->second;

        // remove from connection ptr map
        m_fd_2_connptrs_.erase(it);
    }

    // unsubscribe from epoll
    epoll_ctl(m_epoll_fd_, EPOLL_CTL_DEL, conn->m_fd_, nullptr);
    
    // close connection and run cleanup function
    conn->CloseConn();
    m_cleanup_func_(conn);
}

inline void Endpoint::Process(int fd) {
    ConnectionPtr conn;

    // try get connection ptr
    {
        std::unique_lock<std::mutex> lck(m_mtx_connptrs_);
        
        auto it = m_fd_2_connptrs_.find(fd);
        if (it == m_fd_2_connptrs_.end())
            return;
        conn = it->second;
    }

    // run process function
    m_process_func_(conn);
}

//==============================
// Endpoint Main Process Area
//==============================

inline void Endpoint::RecvLoop(Endpoint* endpoint) {
    constexpr int kMaxEventSize = 32;
    epoll_event epoll_events[kMaxEventSize];

    int event_count = 0;
    while (endpoint->m_running_.load()) {
        event_count = epoll_wait(endpoint->m_epoll_fd_, epoll_events, kMaxEventSize, 1000);
        if (event_count == -1) {
            std::cerr << "SafetyTcpConn >> Endpoint >> Error >> Epoll Error!" << std::endl;
            exit(EXIT_FAILURE);
        }

        for (int i = 0; i < event_count; i++) {
            // Accept Client
            if (epoll_events[i].data.fd == endpoint->m_sock_fd_)
                endpoint->Accept();
            // Error or Disconnect
            else if (epoll_events[i].events & EPOLLERR || epoll_events[i].events & EPOLLHUP || epoll_events[i].events & EPOLLRDHUP)
                endpoint->Remove(epoll_events[i].data.fd);
            // Data coming
            else if (epoll_events[i].events & EPOLLIN)
                endpoint->Process(epoll_events[i].data.fd);
        }
    }
}

inline void Endpoint::SendLoop(Endpoint* endpoint) {
    std::unordered_set<ConnectionPtr> need_to_send;

    while (true) {

        // update need_to_send set
        {
            std::unique_lock<std::mutex> lck(endpoint->m_mtx_connptrs_);

            start_update_set:
            // check running state before update need_to_send set
            if (!endpoint->m_running_.load())
                break;

            for (auto it = endpoint->m_fd_2_connptrs_.begin(); it != endpoint->m_fd_2_connptrs_.end(); it++) {
                const ConnectionPtr& conn = it->second;

                bool need_send = conn->NeedSend();
                auto it_need_send = need_to_send.find(conn);

                if (need_send && it_need_send == need_to_send.end())
                    need_to_send.emplace(conn);
                else if (!need_send && it_need_send != need_to_send.end())
                    need_to_send.erase(it_need_send);
            }

            // remove connection from `need_to_send` which is not in connection map
            auto it_need_send = need_to_send.begin();
            while (it_need_send != need_to_send.end()) {
                auto conn = *it_need_send;

                if (!conn->NeedSend()) {
                    need_to_send.erase(it_need_send);

                    it_need_send = need_to_send.begin();
                    continue;
                }

                it_need_send++;
            }

            // nothing need to send, wait
            if (need_to_send.size() == 0) {
                endpoint->m_cond_connptrs_.wait(lck);
                goto start_update_set; // this can save time on unlocking and relocking
            }
        }

        // call TrySend for connection in need_to_send set
        for (auto it = need_to_send.begin(); it != need_to_send.end(); it++) {
            const ConnectionPtr& conn = *it;
            
            // sent messages until can't send
            int quota = 10; // fair usage policy
            while (quota-- > 0 && conn->TrySend() > 0);
        }
    }
}

}

#endif