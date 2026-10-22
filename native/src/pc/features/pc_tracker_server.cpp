// COS_TRACKER_SERVER (pc_tracker.h, docs/TRACKER.md): the tracker's state over HTTP, for the tracker's
// autotracking. One thread accepts and answers one request per connection:
//   GET /state   the state's JSON (pc_tracker.cpp)
//   GET /        a line saying what this is
//   OPTIONS      the CORS preflight
// Every reply allows any origin (the tracker page is served from elsewhere) and private-network access
// (Chrome asks a page on a public origin before it reads 127.0.0.1). No password: the state is
// read-only, and it listens on 127.0.0.1 unless COS_TRACKER_BIND says otherwise (the Switch: 0.0.0.0).
#include "pc_internal.h"

#include "pc/pc_tracker.h"

#include "JSystem/JKernel/JKRHeap.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#if defined(__SWITCH__)
#include <switch.h>
#endif

namespace pc::tracker {

namespace {

int sListen = -1;
int sPort = 0;
in_addr sBind{};

int openListen() {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr = sBind;
    addr.sin_port = htons(uint16_t(sPort));
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || listen(fd, 4) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL; // a tracker that closed the connection early must not end the game
#else
constexpr int kSendFlags = 0; // SO_NOSIGPIPE on the socket instead (macOS)
#endif

void sendAll(int fd, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = send(fd, data.data() + sent, data.size() - sent, kSendFlags);
        if (n <= 0) {
            return;
        }
        sent += size_t(n);
    }
}

void reply(int fd, const char* status, const char* type, const std::string& body) {
    std::string head = std::string("HTTP/1.1 ") + status +
                       "\r\nAccess-Control-Allow-Origin: *"
                       "\r\nAccess-Control-Allow-Methods: GET, OPTIONS"
                       "\r\nAccess-Control-Allow-Headers: *"
                       "\r\nAccess-Control-Allow-Private-Network: true"
                       "\r\nCache-Control: no-store"
                       "\r\nConnection: close";
    if (type != nullptr) {
        head += std::string("\r\nContent-Type: ") + type;
    }
    head += "\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n";
    sendAll(fd, head + body);
}

void answer(int fd) {
    // the request line and headers; the requests here have no body
    std::string req;
    char buf[1024];
    while (req.find("\r\n\r\n") == std::string::npos && req.size() < 8192) {
        const ssize_t n = recv(fd, buf, sizeof buf, 0);
        if (n <= 0) {
            return;
        }
        req.append(buf, size_t(n));
    }
    const size_t sp1 = req.find(' ');
    const size_t sp2 = sp1 == std::string::npos ? std::string::npos : req.find(' ', sp1 + 1);
    if (sp2 == std::string::npos) {
        reply(fd, "400 Bad Request", "text/plain", "bad request\n");
        return;
    }
    const std::string method = req.substr(0, sp1);
    std::string path = req.substr(sp1 + 1, sp2 - sp1 - 1);
    if (const size_t q = path.find('?'); q != std::string::npos) {
        path.resize(q);
    }
    if (method == "OPTIONS") {
        reply(fd, "204 No Content", nullptr, "");
    } else if (method != "GET") {
        reply(fd, "405 Method Not Allowed", "text/plain", "GET only\n");
    } else if (path == "/state") {
        reply(fd, "200 OK", "application/json", stateJson());
    } else if (path == "/") {
        reply(fd, "200 OK", "text/plain", "switchwaker tracker state: GET /state (docs/TRACKER.md)\n");
    } else {
        reply(fd, "404 Not Found", "text/plain", "not found; the state is at /state\n");
    }
}

void serve() {
    // host memory, never the game's current JKRHeap (cos_debug.cpp's server thread does the same)
    JKRPcBeginHostAlloc();
    int failures = 0;
    for (;;) {
        sockaddr_in from{};
        socklen_t len = sizeof from;
        const int fd = accept(sListen, reinterpret_cast<sockaddr*>(&from), &len);
        if (fd < 0) {
            // the Switch's sleep mode closes every socket, the listening one too (debug_server.cpp does the
            // same): after a second of failures it is opened again
            if (++failures >= 10) {
                close(sListen);
                sListen = openListen();
                writef(STDERR_FILENO, "[cos] tracker: %s on port %d (errno %d)\n",
                       sListen >= 0 ? "listening again" : "cannot listen yet", sPort, errno);
                failures = 0;
                if (sListen < 0) {
                    usleep(1000 * 1000);
                }
            }
            usleep(100 * 1000);
            continue;
        }
        failures = 0;
#ifdef SO_NOSIGPIPE
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
        timeval timeout{2, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
        answer(fd);
        close(fd);
    }
}

} // namespace

bool startServer(const char* bindAddress, int port) {
    if (inet_pton(AF_INET, bindAddress, &sBind) != 1) {
        writef(STDERR_FILENO, "[cos] tracker: server off: COS_TRACKER_BIND=%s is not an IPv4 address\n", bindAddress);
        return false;
    }
    sPort = port;
#if defined(__SWITCH__)
    // the debug server may have started the socket service already; a second start then fails harmlessly
    socketInitializeDefault();
#endif
    sListen = openListen();
    if (sListen < 0) {
        writef(STDERR_FILENO, "[cos] tracker: server off: cannot listen on %s:%d (errno %d)\n", bindAddress, port, errno);
        return false;
    }
    std::thread(serve).detach();
    writef(STDERR_FILENO, "[cos] tracker: state at http://%s:%d/state\n", bindAddress, port);
    return true;
}

} // namespace pc::tracker
