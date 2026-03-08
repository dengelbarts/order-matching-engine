#include "fix_gateway.hpp"
#include "order_command.hpp"

#include <arpa/inet.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>

// Build a framed FIX message from a body string (computes BodyLength + CheckSum).
static std::string frame_fix(const std::string& body) {
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

static std::string build_logon_ack(const std::string& sender,
                                   const std::string& target,
                                   uint32_t seq_num,
                                   uint32_t heartbeat_int) {
    std::string body;
    body += "35=A\x01";
    body += "49=" + sender + "\x01";
    body += "56=" + target + "\x01";
    body += "34=" + std::to_string(seq_num) + "\x01";
    body += "108=" + std::to_string(heartbeat_int) + "\x01";
    return frame_fix(body);
}

static std::string build_logout(const std::string& sender,
                                const std::string& target,
                                uint32_t seq_num) {
    std::string body;
    body += "35=5\x01";
    body += "49=" + sender + "\x01";
    body += "56=" + target + "\x01";
    body += "34=" + std::to_string(seq_num) + "\x01";
    return frame_fix(body);
}

static OrderId parse_order_id(std::string_view sv) {
    OrderId id = 0;
    for (char c : sv) {
        if (c < '0' || c > '9') return 0;
        id = id * 10 + static_cast<OrderId>(c - '0');
    }
    return id;
}

FIXGateway::FIXGateway() {
    int fds[2];
    if (::pipe(fds) != 0)
        throw std::runtime_error("pipe failed");
    notify_pipe_r_ = fds[0];
    notify_pipe_w_ = fds[1];

    // Register notify pipe with TcpServer poll loop
    server_.add_watcher(notify_pipe_r_, [this] { drain_outbound(); });

    // Register TcpServer callbacks
    server_.on_accept([this](int fd)   { on_accept(fd);   });
    server_.on_readable([this](int fd) { on_readable(fd); });
    server_.on_closed([this](int fd)   { on_closed(fd);   });

    // Order event callback (fires on matching thread)
    pipeline_.set_order_callback([this](const OrderEvent& ev) {
        auto it = order_meta_.find(ev.order_id);
        if (it == order_meta_.end()) return;
        const OrderMeta& meta = it->second;

        std::string report = serializer_.execution_report(
            ev,
            meta.cl_ord_id,
            meta.symbol,
            "ENGINE",
            "CLIENT",
            1);

        push_outbound(meta.fd, report);

        if (ev.type == OrderEventType::Filled ||
            ev.type == OrderEventType::Cancelled) {
            order_meta_.erase(it);
        }
    });

    // Trade event callback (fires on matching thread)
    pipeline_.set_trade_callback([this](const TradeEvent& te) {
        auto send_fill = [&](OrderId oid, Side side) {
            auto it = order_meta_.find(oid);
            if (it == order_meta_.end()) return;
            const OrderMeta& meta = it->second;

            Quantity leaves = (meta.original_qty > te.quantity)
                                  ? meta.original_qty - te.quantity
                                  : 0;
            bool fully_filled = (leaves == 0);

            std::string report = serializer_.fill_report(
                oid,
                meta.cl_ord_id,
                meta.symbol,
                side,
                meta.original_qty,
                te.price,
                te.quantity,
                te.quantity,
                leaves,
                fully_filled,
                "ENGINE",
                "CLIENT",
                1);

            push_outbound(meta.fd, report);
        };

        send_fill(te.buy_order_id,  Side::Buy);
        send_fill(te.sell_order_id, Side::Sell);
    });
}

FIXGateway::~FIXGateway() {
    if (notify_pipe_r_ >= 0) ::close(notify_pipe_r_);
    if (notify_pipe_w_ >= 0) ::close(notify_pipe_w_);
}

void FIXGateway::start(uint16_t port) {
    uint16_t bound = server_.listen(port);
    std::cout << "FIX 4.2 gateway listening on :" << bound << "\n";
    std::cout.flush();
    pipeline_.start();
    server_.run();        // blocks until stop()
    pipeline_.shutdown();
}

void FIXGateway::stop() {
    server_.stop();
}

SymbolId FIXGateway::get_or_assign_symbol(const std::string& sym) {
    auto it = symbol_ids_.find(sym);
    if (it != symbol_ids_.end()) return it->second;
    SymbolId id = next_symbol_id_++;
    symbol_ids_.emplace(sym, id);
    return id;
}

void FIXGateway::push_outbound(int fd, const std::string& report) {
    OutboundMsg msg;
    msg.fd  = fd;
    msg.len = static_cast<int>(
        std::min(report.size(), sizeof(msg.data) - 1));
    std::memcpy(msg.data, report.data(), msg.len);

    while (!outbound_queue_.try_push(msg))
        ;

    char b = 1;
    ::write(notify_pipe_w_, &b, 1);
}

void FIXGateway::drain_outbound() {
    // Drain all bytes from notify pipe
    char buf[64];
    while (::read(notify_pipe_r_, buf, sizeof(buf)) > 0)
        ;

    OutboundMsg msg;
    while (outbound_queue_.try_pop(msg)) {
        if (msg.fd >= 0 && msg.len > 0)
            server_.send_to(msg.fd, msg.data, msg.len);
    }
}

void FIXGateway::on_accept(int fd) {
    clients_.emplace(fd, ClientState{
        std::string{},
        std::make_unique<Fix42Session>("ENGINE", "CLIENT"),
        {}
    });
}

void FIXGateway::on_readable(int fd) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) return;
    ClientState& state = it->second;

    char buf[4096];
    ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
    if (n <= 0) return;

    state.recv_buf.append(buf, n);
    process_recv_buf(fd, state);
}

void FIXGateway::on_closed(int fd) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) return;
    ClientState& state = it->second;

    for (OrderId oid : state.resting_orders)
        pipeline_.submit(OrderCommand::make_cancel(oid));

    for (auto mit = order_meta_.begin(); mit != order_meta_.end(); ) {
        if (mit->second.fd == fd)
            mit = order_meta_.erase(mit);
        else
            ++mit;
    }

    clients_.erase(it);
}

void FIXGateway::process_recv_buf(int fd, ClientState& state) {
    auto& buf = state.recv_buf;
    while (true) {
        auto pos = buf.find("10=");
        if (pos == std::string::npos) break;

        auto end = buf.find('\x01', pos + 3);
        if (end == std::string::npos) break;

        std::string_view msg_view(buf.data(), end + 1);
        Fix42Message msg = parser_.parse(msg_view);

        if (msg.valid)
            handle_message(fd, state, msg);

        buf.erase(0, end + 1);
    }
}

void FIXGateway::handle_message(int fd, ClientState& state, const Fix42Message& msg) {
    switch (msg.type) {
    case Fix42MsgType::Logon:
        handle_logon(fd, state, msg);
        break;
    case Fix42MsgType::Logout:
        handle_logout(fd, state);
        break;
    case Fix42MsgType::NewOrderSingle:
        if (state.session && state.session->logged_in)
            handle_new_order(fd, state, msg);
        break;
    case Fix42MsgType::OrderCancelRequest:
        if (state.session && state.session->logged_in)
            handle_cancel(fd, state, msg);
        break;
    case Fix42MsgType::OrderCancelReplaceRequest:
        if (state.session && state.session->logged_in)
            handle_amend(fd, state, msg);
        break;
    default:
        break;
    }
}

void FIXGateway::handle_logon(int fd, ClientState& state, const Fix42Message& msg) {
    if (!state.session) return;
    state.session->logged_in = true;
    auto ack = build_logon_ack(
        state.session->sender_comp_id,
        state.session->target_comp_id,
        state.session->next_seq_num(),
        msg.heartbeat_int ? msg.heartbeat_int : 30);
    server_.send_to(fd, ack);
}

void FIXGateway::handle_logout(int fd, ClientState& state) {
    if (!state.session) return;
    auto resp = build_logout(
        state.session->sender_comp_id,
        state.session->target_comp_id,
        state.session->next_seq_num());
    server_.send_to(fd, resp);
    server_.close_client(fd);
}

void FIXGateway::handle_new_order(int fd, ClientState& state, const Fix42Message& msg) {
    std::string symbol(msg.symbol);
    SymbolId sym_id = get_or_assign_symbol(symbol);

    OrderId order_id = generate_order_id();

    OrderCommand cmd = OrderCommand::make_new(
        order_id,
        sym_id,
        static_cast<TraderId>(fd),
        msg.side,
        msg.price,
        msg.qty,
        get_timestamp_ns(),
        msg.ord_type);

    order_meta_[order_id] = OrderMeta{
        fd,
        std::string(msg.cl_ord_id),
        symbol,
        msg.side,
        msg.qty};

    state.resting_orders.push_back(order_id);

    pipeline_.submit(cmd);
}

void FIXGateway::handle_cancel(int fd, ClientState& state, const Fix42Message& msg) {
    (void)fd;
    OrderId order_id = parse_order_id(msg.orig_cl_ord_id);
    if (order_id == 0) return;

    pipeline_.submit(OrderCommand::make_cancel(order_id));

    auto& ro = state.resting_orders;
    ro.erase(std::remove(ro.begin(), ro.end(), order_id), ro.end());
}

void FIXGateway::handle_amend(int fd, ClientState& state, const Fix42Message& msg) {
    (void)fd;
    (void)state;
    OrderId order_id = parse_order_id(msg.orig_cl_ord_id);
    if (order_id == 0) return;

    pipeline_.submit(OrderCommand::make_amend(order_id, msg.price, msg.qty));
}
