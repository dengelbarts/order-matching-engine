#include "tcp_server.hpp"

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static int tcp_connect(uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = ::inet_addr("127.0.0.1");
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

static std::thread start_server(TcpServer& s) {
    return std::thread([&s]{ s.run(); });
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// 1. ListenAndAccept
TEST(TcpServer, ListenAndAccept) {
    TcpServer server;
    std::atomic<int> accepted_fd{-1};

    server.on_accept([&](int fd) {
        accepted_fd.store(fd);
    });

    uint16_t port = server.listen(0);
    ASSERT_GT(port, 0);

    auto t = start_server(server);

    int client = tcp_connect(port);
    ASSERT_GE(client, 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_GE(accepted_fd.load(), 0);

    ::close(client);
    server.stop();
    t.join();
}

// 2. SendAndReceive — server echoes bytes back in on_readable
TEST(TcpServer, SendAndReceive) {
    TcpServer server;

    server.on_readable([&](int fd) {
        char buf[256]{};
        ssize_t n = ::recv(fd, buf, sizeof(buf) - 1, 0);
        if (n > 0)
            server.send_to(fd, buf, static_cast<int>(n));
    });

    uint16_t port = server.listen(0);
    auto t = start_server(server);

    int client = tcp_connect(port);
    ASSERT_GE(client, 0);

    const char msg[] = "hello";
    ::send(client, msg, sizeof(msg) - 1, 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    char reply[64]{};
    ssize_t n = ::recv(client, reply, sizeof(reply) - 1, MSG_DONTWAIT);
    EXPECT_GT(n, 0);
    EXPECT_EQ(std::string(reply, n), "hello");

    ::close(client);
    server.stop();
    t.join();
}

// 3. MultipleClients — 3 clients connect, on_accept fires 3 times
TEST(TcpServer, MultipleClients) {
    TcpServer server;
    std::atomic<int> accept_count{0};

    server.on_accept([&](int /*fd*/) {
        accept_count.fetch_add(1);
    });

    uint16_t port = server.listen(0);
    auto t = start_server(server);

    int c1 = tcp_connect(port);
    int c2 = tcp_connect(port);
    int c3 = tcp_connect(port);

    ASSERT_GE(c1, 0);
    ASSERT_GE(c2, 0);
    ASSERT_GE(c3, 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(accept_count.load(), 3);

    ::close(c1);
    ::close(c2);
    ::close(c3);
    server.stop();
    t.join();
}

// 4. CleanShutdown — stop() + join() must not block
TEST(TcpServer, CleanShutdown) {
    TcpServer server;
    server.listen(0);
    auto t = start_server(server);

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    server.stop();
    t.join();  // test times out if this blocks
}

// 5. PartialRead — accumulate data sent in two chunks
TEST(TcpServer, PartialRead) {
    TcpServer server;
    std::mutex mu;
    std::string accumulated;

    server.on_readable([&](int fd) {
        char buf[256]{};
        ssize_t n = ::recv(fd, buf, sizeof(buf) - 1, 0);
        if (n > 0) {
            std::lock_guard<std::mutex> lk(mu);
            accumulated.append(buf, n);
        }
    });

    uint16_t port = server.listen(0);
    auto t = start_server(server);

    int client = tcp_connect(port);
    ASSERT_GE(client, 0);

    ::send(client, "hel", 3, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    ::send(client, "lo", 2, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    {
        std::lock_guard<std::mutex> lk(mu);
        EXPECT_EQ(accumulated, "hello");
    }

    ::close(client);
    server.stop();
    t.join();
}

// 6. OnClosedFired — client disconnects, on_closed fires
TEST(TcpServer, OnClosedFired) {
    TcpServer server;
    std::atomic<bool> closed_fired{false};

    // on_readable must drain EOF so the poll loop detects the disconnect
    server.on_readable([&](int fd) {
        char buf[64]{};
        ::recv(fd, buf, sizeof(buf), 0);
    });

    server.on_closed([&](int /*fd*/) {
        closed_fired.store(true);
    });

    uint16_t port = server.listen(0);
    auto t = start_server(server);

    int client = tcp_connect(port);
    ASSERT_GE(client, 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ::close(client);  // abrupt close

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    EXPECT_TRUE(closed_fired.load());

    server.stop();
    t.join();
}

// 7. WatcherCbFires — extra fd registered via add_watcher triggers callback
TEST(TcpServer, WatcherCbFires) {
    TcpServer server;
    std::atomic<bool> watcher_fired{false};

    int pipe_fds[2];
    ASSERT_EQ(::pipe(pipe_fds), 0);

    server.add_watcher(pipe_fds[0], [&]() {
        char buf[8]{};
        ::read(pipe_fds[0], buf, sizeof(buf));
        watcher_fired.store(true);
    });

    server.listen(0);
    auto t = start_server(server);

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    char b = 1;
    ::write(pipe_fds[1], &b, 1);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(watcher_fired.load());

    server.stop();
    t.join();

    ::close(pipe_fds[0]);
    ::close(pipe_fds[1]);
}

// 8. EphemeralPort — listen(0) returns a port > 0; port() returns the same value
TEST(TcpServer, EphemeralPort) {
    TcpServer server;
    uint16_t returned_port = server.listen(0);
    EXPECT_GT(returned_port, 0);
    EXPECT_EQ(server.port(), returned_port);
}
