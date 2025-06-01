#ifndef STC_CONNECTION_FUNC_HPP
#define STC_CONNECTION_FUNC_HPP

#include "Classes.hpp"
#include "Connection.hpp"
#include "Endpoint.hpp"

namespace SafetyTcpConn {

Connection::Connection(int fd, EndpointPtr& endpoint) :
    Container(ContainerType::kConnection),
    m_fd_(fd), m_endpoint_(endpoint), m_core_(endpoint->m_core_), m_connected_(true), m_send_flag_(true),
    m_recv_buff_size_(0), m_recv_buff_allcasize_(kDefaultSize), m_recv_buff_(new char[kDefaultSize]),
    m_send_buff_size_(0), m_send_buff_allcasize_(kDefaultSize), m_send_buff_(new char[kDefaultSize]),
    m_coninit_func_(endpoint->m_coninit_func_), m_process_func_(endpoint->m_process_func_), m_cleanup_func_(endpoint->m_cleanup_func_)
{
    int send_buff_size = 8192;
    if (setsockopt(m_fd_, SOL_SOCKET, SO_SNDBUF, &send_buff_size, sizeof(send_buff_size)) < 0) {
        std::cerr << "SafetyTcpConn >> Connection >> Error >> Set Socket Send Buffer Size Failure." << std::endl;
        CloseConn();
        return;
    }

    int cork = 1;
    if (setsockopt(m_fd_, IPPROTO_TCP, TCP_CORK, &cork, sizeof(cork)) < 0) {
        std::cerr << "SafetyTcpConn >> Connection >> Error >> Set Socket Send Buffer Size Failure." << std::endl;
        CloseConn();
        return;
    }
}

//==============================
// Public Area
//==============================

Connection::~Connection() {
    // close connection if not close
    CloseConn();
    // release buffer
    delete [] m_recv_buff_;
    delete [] m_send_buff_;
}

inline bool Connection::IsConn() {
    return m_connected_.load();
}

inline void Connection::CloseConn() {
    bool conn_state = m_connected_.load();
    // no need to close connection
    if (!conn_state) return;

    // atomic to set m_connected to false
    while (!m_connected_.compare_exchange_weak(conn_state, false)) {
        // connection will be close by another thread
        if (conn_state == false)
            return;
    }

    // close connection
    close(m_fd_);
}

inline std::string Connection::ReadString(const std::string delimiter, bool& keep_read) {
    // set flag to false before a message readed
    keep_read = false;

    if (!m_connected_.load())
        return "";

    const size_t delimiter_size = delimiter.size();

    std::unique_lock<std::mutex> lck(m_recv_buff_mtx_);
    if (m_recv_buff_size_ < delimiter_size)
        return "";

    std::string msg = std::string();

    for (int start_index = 0; start_index <= m_recv_buff_size_ - delimiter_size; start_index++) {
        size_t end_index = start_index + delimiter_size;
        
        if (std::string(m_recv_buff_ + start_index, m_recv_buff_ + end_index) != delimiter)
            continue;
        
        // copy msg data into string container
        msg.append(m_recv_buff_, m_recv_buff_ + start_index);

        // move other data to the front
        std::memmove(m_recv_buff_, m_recv_buff_ + end_index, m_recv_buff_size_ - end_index);
        // reset buff size
        m_recv_buff_size_ -= end_index;

        // set keep read if still have message not readed
        keep_read = true;
        break;
    }

    return msg;
}

inline char* Connection::ReadBytes(const size_t size) {
    if (!m_connected_.load())
        return nullptr;

    std::unique_lock<std::mutex> lck(m_recv_buff_mtx_);
    if (m_recv_buff_size_ < size)
        return nullptr;

    const size_t size_after_read = m_recv_buff_size_ - size;

    // copy message from recv buff to read buff
    char* buff = new char[size];
    std::memcpy(buff, m_recv_buff_, size);

    // move other data to the front
    std::memmove(m_recv_buff_, m_recv_buff_ + size, size_after_read);

    // reset buff size
    m_recv_buff_size_ = size_after_read;

    return buff;
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

    if (m_send_flag_.load())
        m_core_->StartTrySend();
}

inline void Connection::MsgEnqueue(const std::string msg) {
    this->MsgEnqueue(msg.c_str(), msg.size());
}

//==============================
// Endpoint Control Area
//==============================

inline bool Connection::ExtendBuffer(char*& buff_ptr, size_t future_size, size_t& curr_size, size_t& allocsize) {
    // check if need to extend
    if (future_size > allocsize) {
        const size_t target_buff_allocsize = (future_size / kDefaultSize + (size_t)(future_size % kDefaultSize > 0)) * kDefaultSize;

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

        // release old buff memory
        delete [] old_buff;
    }

    return true;
}

inline bool Connection::TryRecv() {
    constexpr size_t recv_buff_size = 1500;
    char buff[recv_buff_size];

    int recved = 0;
    {
        std::unique_lock<std::mutex> lck(m_recv_buff_mtx_);
        while (IsConn()) {
            recved = recv(m_fd_, buff, recv_buff_size, MSG_DONTWAIT | MSG_NOSIGNAL);

            // nothing need to recevie
            if (recved <= 0) break;

            // calculate the total size of data
            const size_t total_data_len = m_recv_buff_size_ + recved;

            // check if buff size is enough, if not then extend it
            if (!ExtendBuffer(m_recv_buff_, total_data_len, m_recv_buff_size_, m_recv_buff_allcasize_))
                break; 

            // copy msg's data into the end of buff
            memcpy(m_recv_buff_ + m_recv_buff_size_, buff, recved);
            m_recv_buff_size_ = total_data_len;
        }
    }
    
    // connection closed / error
    if (recved == 0 || (recved < 0 && errno != EAGAIN && errno != EINTR)) {
        CloseConn();
        return false;
    }

    // nothing need to recevie
    return IsConn();
}

inline void Connection::SetSendFlag() {
    m_send_flag_.store(true);
}

inline bool Connection::NeedSend() {
    bool connect_state = m_connected_.load();
    bool send_flag = m_send_flag_.load();

    // when the connection's send is timeout, close connection
    if (!send_flag && time(nullptr) - m_prev_sendtime_ >= 5) {
        CloseConn();
    }

    return connect_state && send_flag && m_send_buff_size_ > 0;
}

inline int Connection::TrySend() {
    if (!IsConn())
        return 0;

    int sent = 0;
    {
        std::unique_lock<std::mutex> lck(m_send_buff_mtx_);
        if (m_send_buff_size_ == 0)
            return -1;

        // get the len need to send and copy msg to tmp_buff
        size_t len = m_send_buff_size_ > 1500 ? 1500 : m_send_buff_size_;

        // send with non-blocking mode
        sent = send(m_fd_, m_send_buff_, len, MSG_DONTWAIT | MSG_NOSIGNAL);

        // send done
        if (sent > 0) {
            m_prev_sendtime_ = time(nullptr);

            m_send_buff_size_ -= sent;
            memmove(m_send_buff_, m_send_buff_ + sent, m_send_buff_size_);
            return sent;
        }
    }
    
    // can't send currently
    if (sent < 0 && (errno == EAGAIN || errno == EINTR)) {
        m_send_flag_.store(false);
        return -1;
    }
    // disconnected
    else {
        CloseConn();
        return 0;
    }
}

}

#endif