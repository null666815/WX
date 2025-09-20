#include "src/network/tcp_socket.hpp"
#include <iostream>
#include<stdlib.h>
int main() {
    TcpSocket s;
    if (!s.init()) {
        std::cerr << "init failed: " << s.getLastError() << std::endl;
        return 1;
    }
    if (!s.create()) {
        std::cerr << "create failed: " << s.getLastError() << std::endl;
        return 1;
    }
    if (!s.bind(2345, "0.0.0.0")) {
        std::cerr << "bind failed: " << s.getLastError() << " code=" << s.getLastErrorCode() << std::endl;
        return 1;
    }
    if (!s.listen()) {
        std::cerr << "listen failed: " << s.getLastError() << " code=" << s.getLastErrorCode() << std::endl;
        return 1;
    }
    std::cout << "Listening on 2345 ..." << std::endl;
    while (true) system("pause");
}
