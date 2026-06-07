// CardCode local harness: runs the engine and speaks the JSON wire protocol
// (docs/integration/protocol.md) over newline-delimited messages.
//
//   stdio (default):  one message per line on stdin -> responses on stdout.
//                     Pair with `websocketd --port=8080 ./cardcode-harness` to
//                     expose it to a browser at ws://localhost:8080.
//   --port N:         listen on 127.0.0.1:N and speak the same line protocol
//                     directly over TCP (one client at a time).
//   --realtime:       sleep for drive/wait durations so highlight timing matches
//                     a real run (default: instant).

#include <cstring>
#include <iostream>
#include <mutex>
#include <string>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "protocol_bridge.hpp"

using cardcode::harness::ProtocolBridge;

namespace {

void run_stdio(bool realtime) {
    std::mutex out_mu;
    auto emit = [&out_mu](const std::string& line) {
        std::lock_guard<std::mutex> lk(out_mu);
        std::cout << line << '\n';
        std::cout.flush();
    };

    ProtocolBridge bridge(emit, cardcode::harness::BridgeOptions{/*async=*/true, realtime});
    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty()) bridge.handle(line);
    }
}

bool write_all(int fd, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = ::write(fd, data.data() + sent, data.size() - sent);
        if (n <= 0) return false;
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

void serve_client(int fd, bool realtime) {
    std::mutex out_mu;
    auto emit = [fd, &out_mu](const std::string& line) {
        std::lock_guard<std::mutex> lk(out_mu);
        write_all(fd, line + "\n");
    };

    ProtocolBridge bridge(emit, cardcode::harness::BridgeOptions{/*async=*/true, realtime});
    std::string buf;
    char chunk[4096];
    for (;;) {
        ssize_t n = ::read(fd, chunk, sizeof(chunk));
        if (n <= 0) break;
        buf.append(chunk, static_cast<std::size_t>(n));
        std::size_t pos;
        while ((pos = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0, pos);
            buf.erase(0, pos + 1);
            if (!line.empty()) bridge.handle(line);
        }
    }
}

int run_tcp(int port, bool realtime) {
    int srv = ::socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { std::cerr << "socket() failed\n"; return 1; }
    int opt = 1;
    ::setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (::bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "bind() failed on port " << port << "\n";
        return 1;
    }
    ::listen(srv, 1);
    std::cerr << "cardcode-harness listening on 127.0.0.1:" << port << "\n";

    for (;;) {
        int cli = ::accept(srv, nullptr, nullptr);
        if (cli < 0) continue;
        std::cerr << "client connected\n";
        serve_client(cli, realtime);
        ::close(cli);
        std::cerr << "client disconnected\n";
    }
}

} // namespace

int main(int argc, char** argv) {
    bool realtime = false;
    int port = 0;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--realtime") {
            realtime = true;
        } else if (a == "--port" && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        } else if (a == "--help" || a == "-h") {
            std::cerr << "usage: cardcode-harness [--port N] [--realtime]\n"
                         "  default: speak the line protocol over stdin/stdout\n";
            return 0;
        } else {
            std::cerr << "unknown argument: " << a << "\n";
            return 2;
        }
    }

    if (port > 0) return run_tcp(port, realtime);
    run_stdio(realtime);
    return 0;
}
