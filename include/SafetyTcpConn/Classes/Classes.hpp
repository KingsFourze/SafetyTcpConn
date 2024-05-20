#ifndef STC_CLASSES_HPP
#define STC_CLASSES_HPP

#include <memory>

namespace SafetyTcpConn {

class Endpoint;
class Connection;

typedef std::shared_ptr<Connection> ConnectionPtr;

}

#endif