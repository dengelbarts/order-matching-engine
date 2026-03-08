#include "tcp_server.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <stdexcept>

static void set_nonblocking(int fd) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

TcpServer::TcpServer() {
    int fds[2];
    if (::pipe(fds) != 0)
        throw std::runtime_error("pipe failed");
    stop_pipe_r_ = fds[0];
    stop_pipe_w_ = fds[1];
    set_nonblocking(stop_pipe_r_);
    set_nonblocking(stop_pipe_w_);
}

TcpServer::~TcpServer() {
    if (stop_pipe_r_ >= 0) ::close(stop_pipe_r_);
    if (stop_pipe_w_ >= 0) ::close(stop_pipe_w_);
    if (listen_fd_ >= 0)   ::close(listen_fd_);
    for (int fd : client_fds_) ::close(fd);
}

uint16_t TcpServer::listen(uint16_t port) {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0)
        throw std::runtime_error("socket failed");

    int yes = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    set_nonblocking(listen_fd_);

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
        throw std::runtime_error(std::string("bind failed: ") + std::strerror(errno));

    if (::listen(listen_fd_, 128) != 0)
        throw std::runtime_error("listen failed");

    sockaddr_in bound{};
    socklen_t len = sizeof(bound);
    ::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&bound), &len);
    port_ = ntohs(bound.sin_port);
    return port_;
}

void TcpServer::add_watcher(int fd, WatcherCb cb) {
    watchers_.push_back({fd, std::move(cb)});
}

void TcpServer::run() {
    while (true) {
        std::vector<pollfd> pfds;

        // Index 0: stop pipe
        pfds.push_back({stop_pipe_r_, POLLIN, 0});

        // Index 1: listen socket
        if (listen_fd_ >= 0)
            pfds.push_back({listen_fd_, POLLIN, 0});

        // Watchers (extra fds, e.g. notify pipe)
        std::size_t watcher_start = pfds.size();
        for (auto& w : watchers_)
            pfds.push_back({w.fd, POLLIN, 0});

        // Client fds
        std::size_t client_start = pfds.size();
        for (int fd : client_fds_)
            pfds.push_back({fd, POLLIN, 0});

        int n = ::poll(pfds.data(), static_cast<nfds_t>(pfds.size()), -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }

        // Stop pipe
        if (pfds[0].revents & POLLIN)
            break;

        // Listen socket
        if (listen_fd_ >= 0 && pfds[1].revents & POLLIN)
            handle_accept();

        // Watchers
        for (std::size_t i = 0; i < watchers_.size(); ++i) {
            if (pfds[watcher_start + i].revents & POLLIN)
                watchers_[i].cb();
        }

        // Client fds — snapshot first so callbacks can modify client_fds_
        std::vector<int> to_process;
        for (std::size_t i = 0; i < client_fds_.size(); ++i) {
            if (pfds[client_start + i].revents & (POLLIN | POLLHUP | POLLERR))
                to_process.push_back(client_fds_[i]);
        }

        for (int fd : to_process) {
            // Check fd still exists (may have been closed by prior iteration)
            if (std::find(client_fds_.begin(), client_fds_.end(), fd) == client_fds_.end())
                continue;

            char buf[1];
            ssize_t r = ::recv(fd, buf, 1, MSG_PEEK);
            if (r == 0) {
                do_close_client(fd);
            } else if (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                do_close_client(fd);
            } else if (r == 1) {
                if (on_readable_) on_readable_(fd);
            }
        }
    }
}

void TcpServer::stop() {
    char b = 1;
    ::write(stop_pipe_w_, &b, 1);
}

bool TcpServer::send_to(int fd, const char* data, int len) {
    int sent = 0;
    while (sent < len) {
        ssize_t n = ::send(fd, data + sent, len - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        sent += static_cast<int>(n);
    }
    return true;
}

bool TcpServer::send_to(int fd, const std::string& data) {
    return send_to(fd, data.data(), static_cast<int>(data.size()));
}

void TcpServer::close_client(int fd) {
    do_close_client(fd);
}

void TcpServer::handle_accept() {
    while (true) {
        sockaddr_in addr{};
        socklen_t len = sizeof(addr);
        int client_fd = ::accept(listen_fd_,
                                 reinterpret_cast<sockaddr*>(&addr), &len);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            break;
        }
        set_nonblocking(client_fd);
        client_fds_.push_back(client_fd);
        if (on_accept_) on_accept_(client_fd);
    }
}

void TcpServer::do_close_client(int fd) {
    auto it = std::find(client_fds_.begin(), client_fds_.end(), fd);
    if (it != client_fds_.end()) {
        client_fds_.erase(it);
        ::close(fd);
        if (on_closed_) on_closed_(fd);
    }
}
