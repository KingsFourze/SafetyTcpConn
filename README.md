# SafetyTcpConn - A Epoll TCP Server Library with Connection Safety

## What is "Connection Safety" means?
1. **Thread-Safety** Design
    - using `mutex` protect the connection `recv buffer` and `send buffer`
    - using `mutex` protect the `fd to connection map`
    - using `atomic_bool` prevent `close(fd)` multiple times
1. **Fair Usage Policy**
    - set quota for send counters per connection
        - quota : 10
    - set the maximum number of bytes sent by each sending process
        - max sending bytes : 1500
1. **Detect Undetectable Disconnections** (e.g.: power outage / vpn disconnection)
    1. detect unsendable connection with non-blocking mode when sending
    1. leave it for 5 seconds, if it go back to sendable state, then keep send
    1. if connection still unsendable state after 5 seconds, then close it

## Installation
This is a header-only library.

Clone this repo and copy `include/*` to `/usr/local/include/`.

```
# clone this repo
git clone https://github.com/KingsFourze/SafetyTcpConn.git

# copy library to include folder
cd SafetyTcpConn
sudo cp include/* /usr/local/include/ -r
```

Or you can include by using CMakeLists.txt by:
```
include_directories([path_to_library])
```

## Usage
See `demo/main.cpp`

## Test Enviroment
- Ubuntu 22.04 LTS (WSL)
- GCC Version 11.4.0 (Ubuntu 11.4.0-1ubuntu1~22.04)
- GDB Version 12.1 (Ubuntu 12.1-0ubuntu1~22.04)
- CPython 3.12.2