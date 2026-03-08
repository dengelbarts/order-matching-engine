#include "fix_gateway.hpp"

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static int tcp_connect(uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
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

static const char SOH = '\x01';

static std::string make_fix(const std::string& body) {
    std::string framed = "8=FIX.4.2\x01";
    framed += "9=" + std::to_string(body.size()) + "\x01";
    framed += body;
    uint32_t ck = 0;
    for (unsigned char c : framed) ck += c;
    ck %= 256;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "10=%03u\x01", ck);
    framed += buf;
    return framed;
}

static std::string fix_logon() {
    std::string body;
    body += "35=A\x01""49=CLIENT\x01""56=ENGINE\x01""34=1\x01""108=30\x01";
    return make_fix(body);
}

static std::string fix_logout(uint32_t seq = 2) {
    std::string body;
    body += "35=5\x01""49=CLIENT\x01""56=ENGINE\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    return make_fix(body);
}

static std::string fix_new_order(const std::string& cl_ord_id,
                                  const std::string& symbol,
                                  char side, int qty, int price_ticks,
                                  uint32_t seq = 2) {
    std::string body;
    body += "35=D\x01""49=CLIENT\x01""56=ENGINE\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "11=" + cl_ord_id + "\x01";
    body += "55=" + symbol + "\x01";
    body += std::string("54=") + side + "\x01";
    body += "38=" + std::to_string(qty) + "\x01";
    body += "40=2\x01";
    body += "44=" + std::to_string(price_ticks) + "\x01";
    return make_fix(body);
}

static std::string fix_cancel(const std::string& cl_ord_id,
                               const std::string& orig_cl_ord_id,
                               const std::string& symbol,
                               char side, uint32_t seq) {
    std::string body;
    body += "35=F\x01""49=CLIENT\x01""56=ENGINE\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "11=" + cl_ord_id + "\x01";
    body += "41=" + orig_cl_ord_id + "\x01";
    body += "55=" + symbol + "\x01";
    body += std::string("54=") + side + "\x01";
    return make_fix(body);
}

// Read available bytes from fd, polling up to timeout_ms.
static std::string recv_available(int fd, int timeout_ms = 400) {
    std::string result;
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        fd_set rfds; FD_ZERO(&rfds); FD_SET(fd, &rfds);
        timeval tv{0, 20000}; // 20 ms
        if (::select(fd + 1, &rfds, nullptr, nullptr, &tv) > 0) {
            char buf[2048]{};
            ssize_t n = ::recv(fd, buf, sizeof(buf) - 1, 0);
            if (n > 0) {
                result.append(buf, n);
            } else if (n == 0) {
                break;  // EOF — connection closed
            }
        }
    }
    return result;
}

// Extract value of a FIX tag from a message string.
static std::string extract_tag(const std::string& msg, const std::string& tag_eq) {
    auto pos = msg.find(tag_eq);
    if (pos == std::string::npos) return {};
    pos += tag_eq.size();
    auto end = msg.find(SOH, pos);
    if (end == std::string::npos) return {};
    return msg.substr(pos, end - pos);
}

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class GatewayTest : public ::testing::Test {
protected:
    FIXGateway gw;
    std::thread gw_thread;
    uint16_t port = 0;

    void SetUp() override {
        gw_thread = std::thread([this]{ gw.start(0); });
        // Wait for gateway to bind and print "listening on"
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        port = gw.port();
    }

    void TearDown() override {
        gw.stop();
        gw_thread.join();
    }

    // Connect and send logon. Returns connected fd. Caller must ::close(fd).
    int connect_and_logon() {
        int fd = tcp_connect(port);
        std::string logon = fix_logon();
        ::send(fd, logon.data(), static_cast<int>(logon.size()), 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return fd;
    }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// 1. LogonReceivesAck
TEST_F(GatewayTest, LogonReceivesAck) {
    int fd = tcp_connect(port);
    ASSERT_GE(fd, 0);

    std::string logon = fix_logon();
    ::send(fd, logon.data(), static_cast<int>(logon.size()), 0);

    std::string response = recv_available(fd, 400);
    EXPECT_NE(response.find("35=A"), std::string::npos)
        << "Expected Logon Ack (35=A), got: " << response;

    ::close(fd);
}

// 2. LogoutClosesSession
TEST_F(GatewayTest, LogoutClosesSession) {
    int fd = connect_and_logon();

    // Drain the logon ack
    recv_available(fd, 200);

    std::string logout = fix_logout(2);
    ::send(fd, logout.data(), static_cast<int>(logout.size()), 0);

    std::string response = recv_available(fd, 400);
    EXPECT_NE(response.find("35=5"), std::string::npos)
        << "Expected Logout response (35=5), got: " << response;

    // NOTE: the server calls close_client() inside handle_logout(), so it
    // already closed the underlying fd.  Do NOT call ::close(fd) here or
    // anywhere that sends a Logout — the server owns the fd lifetime.
}

// 3. NewOrderAcknowledged — resting order returns ExecutionReport
TEST_F(GatewayTest, NewOrderAcknowledged) {
    int fd = connect_and_logon();
    recv_available(fd, 200); // drain logon ack

    std::string order = fix_new_order("ORD001", "AAPL", '1', 100, 1000000, 2);
    ::send(fd, order.data(), static_cast<int>(order.size()), 0);

    std::string response = recv_available(fd, 500);
    EXPECT_NE(response.find("35=8"), std::string::npos)
        << "Expected ExecutionReport (35=8), got: " << response;

    ::close(fd);
}

// 4. CrossingOrdersGenerateFills — two clients, buy and sell at same price
TEST_F(GatewayTest, CrossingOrdersGenerateFills) {
    int fd_a = connect_and_logon();
    recv_available(fd_a, 200);

    int fd_b = connect_and_logon();
    recv_available(fd_b, 200);

    // Client A: sell 10 @ 1000000 (100.00)
    std::string sell = fix_new_order("SELL001", "CROSS", '2', 10, 1000000, 2);
    ::send(fd_a, sell.data(), static_cast<int>(sell.size()), 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Client B: buy 10 @ 1000000 — should cross
    std::string buy = fix_new_order("BUY001", "CROSS", '1', 10, 1000000, 2);
    ::send(fd_b, buy.data(), static_cast<int>(buy.size()), 0);

    std::string resp_a = recv_available(fd_a, 600);
    std::string resp_b = recv_available(fd_b, 600);

    EXPECT_NE(resp_a.find("150=F"), std::string::npos)
        << "Client A (sell) expected fill report (150=F), got: " << resp_a;
    EXPECT_NE(resp_b.find("150=F"), std::string::npos)
        << "Client B (buy) expected fill report (150=F), got: " << resp_b;

    ::close(fd_a);
    ::close(fd_b);
}

// 5. CancelOrderReceivesCancelledReport
TEST_F(GatewayTest, CancelOrderReceivesCancelledReport) {
    int fd = connect_and_logon();
    recv_available(fd, 200);

    // Submit a resting buy
    std::string order = fix_new_order("ORD_CXLTEST", "CXLSYM", '1', 50, 500000, 2);
    ::send(fd, order.data(), static_cast<int>(order.size()), 0);

    std::string ack = recv_available(fd, 500);
    ASSERT_NE(ack.find("35=8"), std::string::npos)
        << "Expected order ack (35=8), got: " << ack;

    // Extract OrderId from tag 37
    std::string order_id_str = extract_tag(ack, "37=");
    ASSERT_FALSE(order_id_str.empty()) << "Could not find tag 37 in ack: " << ack;

    // Send cancel — use OrderId as OrigClOrdId (tag 41)
    std::string cancel = fix_cancel("CXL001", order_id_str, "CXLSYM", '1', 3);
    ::send(fd, cancel.data(), static_cast<int>(cancel.size()), 0);

    std::string resp = recv_available(fd, 500);
    bool has_cancelled = (resp.find("150=4") != std::string::npos) ||
                         (resp.find("39=4")  != std::string::npos);
    EXPECT_TRUE(has_cancelled)
        << "Expected cancelled report (150=4 or 39=4), got: " << resp;

    ::close(fd);
}

// 6. DisconnectCancelsRestingOrders
TEST_F(GatewayTest, DisconnectCancelsRestingOrders) {
    // Client A submits a resting buy on "DISC"
    int fd_a = connect_and_logon();
    recv_available(fd_a, 200);

    std::string buy = fix_new_order("DISC_BUY", "DISC", '1', 100, 3000000, 2);
    ::send(fd_a, buy.data(), static_cast<int>(buy.size()), 0);
    recv_available(fd_a, 300); // drain ack

    // Abruptly disconnect — gateway should cancel the resting order
    ::close(fd_a);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Client B connects and sends a sell at the same price
    int fd_b = connect_and_logon();
    recv_available(fd_b, 200);

    std::string sell = fix_new_order("DISC_SELL", "DISC", '2', 100, 3000000, 2);
    ::send(fd_b, sell.data(), static_cast<int>(sell.size()), 0);

    std::string resp = recv_available(fd_b, 500);

    // The buy was cancelled on disconnect — sell should rest, NOT fill
    EXPECT_EQ(resp.find("150=F"), std::string::npos)
        << "Expected no fill (buy was cancelled on disconnect), but got: " << resp;

    ::close(fd_b);
}
