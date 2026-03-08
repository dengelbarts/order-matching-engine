#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Non-blocking TCP server backed by poll(2).
// All callbacks fire on the thread that calls run().
// stop() is thread-safe (writes to a self-pipe).
class TcpServer {
public:
    using AcceptCb   = std::function<void(int fd)>;
    using ReadableCb = std::function<void(int fd)>;
    using ClosedCb   = std::function<void(int fd)>;
    using WatcherCb  = std::function<void()>;

    TcpServer();
    ~TcpServer();

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    // Bind and listen. port=0 → OS assigns ephemeral port.
    // Returns the actual bound port. Throws std::runtime_error on failure.
    uint16_t listen(uint16_t port);

    // Register event callbacks (call before run()).
    void on_accept(AcceptCb cb)    { on_accept_   = std::move(cb); }
    void on_readable(ReadableCb cb){ on_readable_  = std::move(cb); }
    void on_closed(ClosedCb cb)    { on_closed_   = std::move(cb); }

    // Register an extra fd to watch in the poll loop.
    // cb fires whenever fd is readable. Used by FIXGateway for its notify pipe.
    void add_watcher(int fd, WatcherCb cb);

    // Blocking event loop. Returns after stop() is called.
    void run();

    // Thread-safe. Wakes the poll loop via self-pipe. Idempotent.
    void stop();

    // Send data to a connected client fd. Call from run() thread only.
    bool send_to(int fd, const char* data, int len);
    bool send_to(int fd, const std::string& data);

    // Close and remove a client fd. Call from run() thread only.
    void close_client(int fd);

    // For tests: returns bound port even before run().
    uint16_t port() const { return port_; }

private:
    int listen_fd_ = -1;
    int stop_pipe_r_ = -1;
    int stop_pipe_w_ = -1;
    uint16_t port_ = 0;

    AcceptCb   on_accept_;
    ReadableCb on_readable_;
    ClosedCb   on_closed_;

    struct Watcher { int fd; WatcherCb cb; };
    std::vector<Watcher> watchers_;
    std::vector<int> client_fds_;

    void handle_accept();
    void do_close_client(int fd);
};
