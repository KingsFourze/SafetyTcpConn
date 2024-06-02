#ifndef STC_CONNECTION_FUNC_HPP
#define STC_CONNECTION_FUNC_HPP

#include "Classes.hpp"
#include "Connection.hpp"
#include "Endpoint.hpp"

namespace SafetyTcpConn {

//==============================
// Public Area
//==============================
inline bool Connection::IsConn() {
    return m_connected.load();
}

inline void Connection::CloseConn() {
    bool conn_state = m_connected.load();
    // no need to close connection
    if (!conn_state) return;

    // atomic to set m_connected to false
    while (!m_connected.compare_exchange_weak(conn_state, false)) {
        // connection will be close by another thread
        if (conn_state == false)
            return;
    }

    // close connection
    close(m_fd_);
}

inline size_t Connection::ReadString(char* buff, size_t buff_len) {
    if (!m_connected.load())
        return 0;

    memset(buff, '\0', buff_len);

    int r = recv(m_fd_, buff, buff_len, MSG_DONTWAIT);

    switch (r) {
        case 0:
            CloseConn();
            return 0;
        case -1:
            if (errno != EAGAIN && errno != EINTR)
                CloseConn();
            return 0;
        default:
            return r;
    };
}

inline char* Connection::ReadBytes(const size_t size) {
    if (!m_connected.load())
        return nullptr;

    char* buff = new char[size];
    size_t recved = 0;

    while (recved < size) {
        int r = recv(m_fd_, buff + recved, size - recved, MSG_DONTWAIT);

        switch (r) {
            case -1:
            {
                if (errno == EAGAIN || errno == EINTR)
                    break;
            }
            case 0:
            {
                CloseConn();
                delete [] buff;
                return nullptr;
            }
            default:
            {
                recved += r;
                break;
            }
        };
    }
}

inline void Connection::MsgEnqueue(const char* msg, const size_t len) {
    if (!IsConn()) return;

    // append msg in to send buff
    {
        std::unique_lock<std::mutex> lck(m_send_buff_mtx_);

        // calculate the total size of data
        const size_t total_data_len = m_send_buff_size_ + len;

        // check if buff size is enough, if not then extend it
        if (!ExtendBuffer(m_send_buff_, total_data_len, m_send_buff_size_, m_send_buff_allcasize_))
            return;
            
        // copy msg's data into the end of buff
        memcpy(m_send_buff_ + m_send_buff_size_, msg, len);
        m_send_buff_size_ = total_data_len;
    }

    m_endpoint_->StartTrySend();
}

//==============================
// Endpoint Control Area
//==============================

inline bool Connection::ExtendBuffer(char*& buff_ptr, size_t future_size, size_t& curr_size, size_t& allocsize) {
    const size_t target_buff_allocsize = (future_size / kDefaultSize + (size_t)(future_size % kDefaultSize > 0)) * kDefaultSize;
    // no need to extend buff
    if (target_buff_allocsize <= allocsize) return true;

    // reach max allocation size
    if (target_buff_allocsize > kMaxSize) {
        CloseConn();
        return false;
    }

    // allocate buff
    char* old_buff = buff_ptr;
    char* new_buff = new char[target_buff_allocsize];

    // copy old buff's data to new buff
    memcpy(new_buff, old_buff, curr_size);

    // replace buff ptr and allocated size
    buff_ptr = new_buff;
    allocsize = target_buff_allocsize;

    return true;
}

inline bool Connection::NeedSend() {
    return IsConn() && m_send_buff_size_ > 0;
}

inline int Connection::TrySend() {
    if (!IsConn())
        return 0;

    std::unique_lock<std::mutex> lck(m_send_buff_mtx_);
    if (m_send_buff_size_ == 0)
        return -1;

    // get the len need to send and copy msg to tmp_buff
    size_t len = m_send_buff_size_ > 1500 ? 1500 : m_send_buff_size_;

    // send with non-blocking mode
    int sent = send(m_fd_, m_send_buff_, len, MSG_DONTWAIT);

    // send done
    time_t current_time = time(nullptr);
    if (sent > 0) {
        m_prev_sendtime_ = current_time;

        m_send_buff_size_ -= sent;
        memcpy(m_send_buff_, m_send_buff_ + sent, m_send_buff_size_);
        return sent;
    }
    // can't send currently
    else if (sent < 0 && (errno == EAGAIN || errno == EINTR)) {
        if (current_time - m_prev_sendtime_ < 5)
            return -1;
        CloseConn();
        return 0;
    }
    // disconnected
    else {
        CloseConn();
        return 0;
    }
}

}

#endif