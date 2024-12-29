#ifndef STC_CLASSES_HPP
#define STC_CLASSES_HPP

#include <memory>

namespace SafetyTcpConn {

class Core;
class Endpoint;
class Connection;

typedef std::shared_ptr<Endpoint> EndpointPtr;
typedef std::shared_ptr<Connection> ConnectionPtr;

}

#endif