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

void Core::RegisterContainer(ContainerPtr& container) {
    if (container.get() == nullptr)
        return;

    if (container->m_type_ == ContainerType::kEndpoint) {
        EndpointPtr endpoint = std::static_pointer_cast<Endpoint>(container);

        {
            std::unique_lock<std::mutex> lck(m_mtx_containers_);
            m_fd_2_containers_[endpoint->m_fd_] = container;
        }

        epoll_event event{};
        event.events = EPOLLIN;
        event.data.fd = endpoint->m_fd_;
        epoll_ctl(m_epoll_fd_, EPOLL_CTL_ADD, endpoint->m_fd_, &event);
    }
    else {
        ConnectionPtr conn = std::static_pointer_cast<Connection>(container);

        // add into connection ptr map
        {
            std::unique_lock<std::mutex> lck(m_mtx_containers_);
            m_fd_2_containers_[conn->m_fd_] = container;
        }

        // epoll subscribe to client
        epoll_event client_event{};
        client_event.events = EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP | EPOLLRDHUP | EPOLLET;
        client_event.data.fd = conn->m_fd_;
        epoll_ctl(m_epoll_fd_, EPOLL_CTL_ADD, conn->m_fd_, &client_event);

        // run connection init function
        conn->m_coninit_func_(conn);
    }
}

void Core::UnregisterContainer(const int container_fd) {
    ContainerPtr container = nullptr;
    {
        std::unique_lock<std::mutex> lck(m_mtx_containers_);
        // try get container ptr
        auto it = m_fd_2_containers_.find(container_fd);
        if (it == m_fd_2_containers_.end())
            return;

        container = it->second;

        // remove from connection ptr map
        m_fd_2_containers_.erase(it);
    }

    if (container->m_type_ == ContainerType::kEndpoint) {
        EndpointPtr endpoint = std::static_pointer_cast<Endpoint>(container);

        // unsubscribe from epoll
        epoll_ctl(m_epoll_fd_, EPOLL_CTL_DEL, endpoint->m_fd_, nullptr);
    }
    else {
        ConnectionPtr conn = std::static_pointer_cast<Connection>(container);
        
        // unsubscribe from epoll
        epoll_ctl(m_epoll_fd_, EPOLL_CTL_DEL, conn->m_fd_, nullptr);

        // remove from endpoint
        EndpointPtr endpoint = conn->m_endpoint_.lock();
        if (endpoint != nullptr) // ready for client mode
            Endpoint::Remove(endpoint, conn->m_fd_);
        
        // close connection and run cleanup function
        conn->CloseConn();
        conn->m_cleanup_func_(conn);
    }
}

inline void Core::StartTrySend() {
    std::unique_lock<std::mutex> lck(m_mtx_containers_);
    m_cond_containers_.notify_one();
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
            for (auto it = core->m_fd_2_containers_.begin(); it != core->m_fd_2_containers_.end(); it++) {
                ContainerPtr& container = it->second;
                if (container->m_type_!=ContainerType::kConnection)
                    continue;
                ConnectionPtr conn = std::static_pointer_cast<Connection>(container);
                if (conn->IsConn())
                    continue;
                locally_closed_connections.push_back(conn);
            }

            // run normal cleanup funtion
            for (int i = 0; i < locally_closed_connections.size(); i++) {
                ConnectionPtr& conn = locally_closed_connections.at(i);
                core->UnregisterContainer(conn->m_fd_);
            }
        }

        for (int i = 0; i < event_count; i++) {
            const int target_fd = epoll_events[i].data.fd;

            // get container from container map
            ContainerPtr container;
            {
                std::unique_lock<std::mutex> lck(core->m_mtx_containers_);
                auto it_containers = core->m_fd_2_containers_.find(target_fd);

                // container found
                if (it_containers == core->m_fd_2_containers_.end())
                    continue;
                container = it_containers->second;
            }

            // endpoint found, accept connection
            if (container->m_type_ == ContainerType::kEndpoint) {
                EndpointPtr endpoint = std::static_pointer_cast<Endpoint>(container);

                ContainerPtr conn = Endpoint::Accept(endpoint);
                core->RegisterContainer(conn);
            }
            // connection found
            else {
                ConnectionPtr conn = std::static_pointer_cast<Connection>(container);

                // error or connection closed
                if (epoll_events[i].events & EPOLLERR || epoll_events[i].events & EPOLLHUP || epoll_events[i].events & EPOLLRDHUP) {
                    core->UnregisterContainer(target_fd);
                }
                // data receive
                else if (epoll_events[i].events & EPOLLIN) {
                    // connection receive message
                    if (conn->TryRecv()) {
                        // run process function
                        conn->m_process_func_(conn);
                    }
                }
                // available to send
                else if (epoll_events[i].events & EPOLLOUT) {
                    conn->SetSendFlag();
                    core->StartTrySend();
                }
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
            std::unique_lock<std::mutex> lck(core->m_mtx_containers_);

            start_update_set:
            for (auto it = core->m_fd_2_containers_.begin(); it != core->m_fd_2_containers_.end(); it++) {
                const ContainerPtr& container = it->second;
                if (container->m_type_ != ContainerType::kConnection)
                    continue;
                ConnectionPtr conn = std::static_pointer_cast<Connection>(container);
                if (conn->NeedSend() && need_to_send.find(conn) == need_to_send.end())
                    need_to_send.emplace(conn);
            }

            // nothing need to send, wait
            if (need_to_send.size() == 0) {
                core->m_cond_containers_.wait_for(lck, std::chrono::milliseconds(1));
                if (!core->m_open_.load())
                    break;
                goto start_update_set; // this can save time on unlocking and relocking
            }
        }

        // call TrySend for connections in need_to_send set
        for (auto it = need_to_send.begin(); it != need_to_send.end(); it++) {
            const ConnectionPtr& conn = *it;
            
            // sent messages until can't send
            int quota = 10; // fair usage policy
            while (quota-- > 0) {
                // keep send data until can't send or reach the limit
                if (conn->TrySend() > 0) continue;

                // when connection is unable to send data, put it into the no_need_to_send set
                // it will removed from the need_to_send set after all connections have sent their data
                no_need_to_send.emplace(conn);
                break;
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