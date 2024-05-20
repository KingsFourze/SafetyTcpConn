#include <iostream>
#include <sstream>

#include <SafetyTcpConn/SafetyTcpConn.hpp>

using namespace SafetyTcpConn;

int main(int, char**) {

    Endpoint endpoint(8080,
        [](Endpoint* endpoint, ConnectionPtr conn) {
            std::stringstream ss("");
            char buff[1024];

            // recv full msg, msg is end with \r\n
            std::string fullMsg = ss.str();
            while (fullMsg.size() < 2 || fullMsg.substr(fullMsg.size() - 2, 2) != "\r\n") {
                size_t readSize = conn->ReadString(buff, 1024);
                if (readSize == 0) {
                    if (conn->IsConn())
                        continue;
                    return;
                }

                // push string into stringstream
                ss << std::string(buff, buff + readSize);
                fullMsg = ss.str();
            }

            std::cout << ss.str();
            conn->MsgEnqueue(ss.str().c_str(), ss.str().size());
        },
        [](Endpoint* endpoint, ConnectionPtr conn) {

        }
    );

    return 0;
}