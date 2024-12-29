#include <iostream>
#include <sstream>

#include <SafetyTcpConn/SafetyTcpConn.hpp>

using namespace SafetyTcpConn;

int main(int, char**) {
    Core core;

    

    Endpoint endpoint(&core, 8080,
        [](ConnectionPtr conn) {
            std::cout << "SafetyTcpConn >> Endpoint >> Client Connected | FD:" << conn->m_fd_ << std::endl;

        },
        [](ConnectionPtr conn) {
            std::cout << "SafetyTcpConn >> Endpoint >> Message Come | FD:" << conn->m_fd_ << std::endl;

            bool keep_read = true;
            while (keep_read) {
                // recv full msg, msg is end with \r\n
                // the keep_read will set to false when no ended message found
                std::string fullMsg = conn->ReadString("\r\n", keep_read);
                if (fullMsg.size() == 0)
                    continue;

                std::cout << "recved msg: " << fullMsg << std::endl;
                conn->MsgEnqueue(fullMsg.c_str(), fullMsg.size());
            }
        },
        [](ConnectionPtr conn) {
            std::cout << "SafetyTcpConn >> Endpoint >> Client Disconnected | FD:" << conn->m_fd_ << std::endl;
        }
    );

    // while loop to keep endpoint running
    while(true) usleep(1000);

    return 0;
}