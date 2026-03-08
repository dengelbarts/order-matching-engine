#pragma once

#include "fix42_parser.hpp"
#include "fix42_serializer.hpp"
#include "fix42_session.hpp"
#include "matching_pipeline.hpp"
#include "spsc_queue.hpp"
#include "tcp_server.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// FIXGateway: ties together TcpServer, MatchingPipeline, and FIX 4.2 codec.
//
// Threading:
//   - Gateway thread  : runs TcpServer::run(), handles accept/read/send, drains outbound queue.
//   - Matching thread : runs inside MatchingPipeline, fires order/trade callbacks.
//
// Hot path (matching thread -> gateway thread):
//   Callbacks push OutboundMsg into outbound_queue_ (SPSC, lock-free).
//   They then write 1 byte to notify_pipe_w_ to wake the poll loop.
//   The poll loop drains the queue and calls TcpServer::send_to().
class FIXGateway {
public:
    FIXGateway();
    ~FIXGateway();

    FIXGateway(const FIXGateway&) = delete;
    FIXGateway& operator=(const FIXGateway&) = delete;

    // Start listening and run the event loop. Blocks until stop().
    void start(uint16_t port = 9000);

    // Thread-safe. Stops the event loop and shuts down the matching pipeline.
    void stop();

    // For tests: returns bound port (valid after start() begins binding).
    uint16_t port() const { return server_.port(); }

private:
    // ---- Outbound queue (matching thread -> gateway thread) ----
    struct OutboundMsg {
        int fd = -1;
        int len = 0;
        char data[2048] = {};
    };
    static constexpr std::size_t OUTBOUND_CAPACITY = 1 << 12; // 4096 slots
    SpscQueue<OutboundMsg, OUTBOUND_CAPACITY> outbound_queue_;

    int notify_pipe_r_ = -1;
    int notify_pipe_w_ = -1;

    void push_outbound(int fd, const std::string& report);
    void drain_outbound();

    // ---- Per-order metadata (gateway thread only) ----
    struct OrderMeta {
        int         fd;
        std::string cl_ord_id;
        std::string symbol;
        Side        side;
        Quantity    original_qty;
    };
    std::unordered_map<OrderId, OrderMeta> order_meta_;

    // ---- Per-client state (gateway thread only) ----
    struct ClientState {
        std::string                   recv_buf;
        std::unique_ptr<Fix42Session> session;
        std::vector<OrderId>          resting_orders;
    };
    std::unordered_map<int, ClientState> clients_;

    // ---- Symbol registry ----
    std::unordered_map<std::string, SymbolId> symbol_ids_;
    SymbolId next_symbol_id_ = 1;
    SymbolId get_or_assign_symbol(const std::string& sym);

    // ---- Core components ----
    TcpServer        server_;
    MatchingPipeline pipeline_;
    Fix42Parser      parser_;
    Fix42Serializer  serializer_;

    // ---- TcpServer callbacks ----
    void on_accept(int fd);
    void on_readable(int fd);
    void on_closed(int fd);

    // ---- FIX message handling ----
    void process_recv_buf(int fd, ClientState& state);
    void handle_message(int fd, ClientState& state, const Fix42Message& msg);
    void handle_logon(int fd, ClientState& state, const Fix42Message& msg);
    void handle_logout(int fd, ClientState& state);
    void handle_new_order(int fd, ClientState& state, const Fix42Message& msg);
    void handle_cancel(int fd, ClientState& state, const Fix42Message& msg);
    void handle_amend(int fd, ClientState& state, const Fix42Message& msg);
};
