#ifndef STC_ENDPOINT_HPP
#define STC_ENDPOINT_HPP

#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>

#include <vector>
#include <unordered_map>
#include <unordered_set>

#include "Connection.hpp"

namespace SafetyTcpConn {

typedef std::shared_ptr<Connection> ConnectionPtr;

class Endpoint {
private:
    int m_sock_fd_;
    int m_epoll_fd_;
    const int m_port_;

    sockaddr_in                             m_sockaddr_;

    std::thread                             m_recv_thread_;
    std::thread                             m_send_thread_;
    const std::function<void(Endpoint*, ConnectionPtr)>  m_process_func_;
    const std::function<void(Endpoint*, ConnectionPtr)>  m_cleanup_func_;

    std::mutex                              m_mtx_connptrs_;
    std::unordered_map<int, ConnectionPtr>  m_fd_2_connptrs_;

    std::mutex                              m_mtx_trysend_connptrs_;
    std::condition_variable                 m_cond_trysend_connptrs_;
    std::unordered_set<ConnectionPtr>       m_trysend_connptrs_;
public:
    Endpoint(int port, std::function<void(Endpoint*, ConnectionPtr)> process_func, std::function<void(Endpoint*, ConnectionPtr)> cleanup_func)
        : m_port_(port), m_process_func_(process_func), m_cleanup_func_(cleanup_func)
    {
        if (m_port_ < 1 || m_port_ > 65535){
            std::cerr << "SafetyTcpConn >> Endpoint >> Error >> Port: " << m_port_ << " is not Avaliable." << std::endl;
            exit(EXIT_FAILURE);
        }

        m_sockaddr_.sin_port = htons(m_port_);
        m_sockaddr_.sin_family = AF_INET;
        m_sockaddr_.sin_addr.s_addr = htons(INADDR_ANY);

        // create socket
        m_sock_fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (bind(m_sock_fd_, (sockaddr *)&m_sockaddr_, sizeof(m_sockaddr_)) < 0) {
            std::cerr << "SafetyTcpConn >> Endpoint >> Error >> Socket Bind Failure." << std::endl;
            exit(EXIT_FAILURE);
        }

        // listen socket
        if (listen(m_sock_fd_, 16) == -1){
            std::cerr << "SafetyTcpConn >> Endpoint >> Error >> Socket Listen Failure." << std::endl;
            exit(EXIT_FAILURE);
        }

        // init epoll
        m_epoll_fd_ = epoll_create(1);
        epoll_event event{};
        event.events = EPOLLIN;
        event.data.fd = m_sock_fd_;
        epoll_ctl(m_epoll_fd_, EPOLL_CTL_ADD, m_sock_fd_, &event);

        m_recv_thread_ = std::thread(RecvLoop, this);
        m_send_thread_ = std::thread(SendLoop, this);
    }
    ~Endpoint() {
        m_recv_thread_.join();
        m_send_thread_.join();
    }

    void StartTrySend(ConnectionPtr& conn) {
        std::unique_lock<std::mutex> lck(m_mtx_trysend_connptrs_);

        // add connection ptr into try-send set
        m_trysend_connptrs_.emplace(conn);

        // notify the send thread to try send
        m_cond_trysend_connptrs_.notify_one();
    }

private:
    inline void Accept() {
        // accept connection
        sockaddr_in client_sockaddr{};
        socklen_t length = sizeof(client_sockaddr);
        int client_fd = accept(m_sock_fd_, (sockaddr *) &client_sockaddr, &length);
        std::cout << "SafetyTcpConn >> Endpoint >> Client Connected | FD:" << client_fd << std::endl;

        // add into connection ptr map
        ConnectionPtr conn = std::make_shared<Connection>(client_fd);
        m_fd_2_connptrs_[conn->m_fd] = conn;

        // epoll subscribe to client
        epoll_event client_event{};
        client_event.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP | EPOLLET; // Add "| EPOLLET" to activate ET mode.
        client_event.data.fd = conn->m_fd;
        epoll_ctl(m_epoll_fd_, EPOLL_CTL_ADD, conn->m_fd, &client_event);
    }

    inline void Remove(int fd) {
        // try get connection ptr
        auto it = m_fd_2_connptrs_.find(fd);
        if (it == m_fd_2_connptrs_.end())
            return;
        ConnectionPtr& conn = it->second;
        std::cout << "SafetyTcpConn >> Endpoint >> Client Disconnected | FD:" << conn->m_fd << std::endl;

        // remove from connection ptr map
        m_fd_2_connptrs_.erase(it);
        
        // close connection and run cleanup function
        conn->CloseConn();
        m_cleanup_func_(this, conn);
    }

    inline void Process(int fd) {
        // try get connection ptr
        auto it = m_fd_2_connptrs_.find(fd);
        if (it == m_fd_2_connptrs_.end())
            return;
        ConnectionPtr& conn = it->second;
        std::cout << "SafetyTcpConn >> Endpoint >> Message Come | FD:" << conn->m_fd << std::endl;

        // run process function
        m_process_func_(this, conn);
    }

private:
    static void RecvLoop(Endpoint* endpoint) {
        std::cout << "Start Recv Loop" << std::endl;
        constexpr int kMaxEventSize = 32;
        epoll_event epoll_events[kMaxEventSize];

        int event_count = 0;
        while (true) {
            event_count = epoll_wait(endpoint->m_epoll_fd_, epoll_events, kMaxEventSize, -1);
            if (event_count == -1) {
                std::cerr << "SafetyTcpConn >> Endpoint >> Error >> Epoll Error!" << std::endl;
                exit(EXIT_FAILURE);
            }

            if (event_count == 0) {
                usleep(100);
                continue;
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

        std::cout << "End Recv Loop" << std::endl;
    }

    static void SendLoop(Endpoint* endpoint) {
        std::cout << "Start Send Loop" << std::endl;
        std::vector<ConnectionPtr> need_to_send;

        while (true) {
            if (need_to_send.size() == 0 || need_to_send.size() != endpoint->m_trysend_connptrs_.size()) {
                need_to_send.clear();

                std::unique_lock<std::mutex> lck(endpoint->m_mtx_trysend_connptrs_);
                while (endpoint->m_trysend_connptrs_.size() == 0)
                    endpoint->m_cond_trysend_connptrs_.wait(lck);

                for (auto it = endpoint->m_trysend_connptrs_.begin(); it != endpoint->m_trysend_connptrs_.end(); it++)
                    need_to_send.push_back(*it);
            }

            // call TrySend for connection in need_to_send vector
            for (auto it = need_to_send.begin(); it != need_to_send.end(); it++) {
                const ConnectionPtr& conn = *it;
                
                conn->TrySend();

                if (!conn->NeedSend()) {
                    // remove from the set if connection no need to send
                    std::unique_lock<std::mutex> lck(endpoint->m_mtx_trysend_connptrs_);
                    endpoint->m_trysend_connptrs_.erase(conn);
                }
            }
        }
    }
};

}

#endif