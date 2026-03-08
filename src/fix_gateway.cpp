#include "fix_gateway.hpp"
#include "order_command.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
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

    // Make notify_pipe_r_ non-blocking so drain_outbound's read loop exits
    // when the pipe is empty rather than blocking forever.
    {
        int flags = ::fcntl(notify_pipe_r_, F_GETFL, 0);
        ::fcntl(notify_pipe_r_, F_SETFL, flags | O_NONBLOCK);
    }

    // Register notify pipe with TcpServer poll loop
    server_.add_watcher(notify_pipe_r_, [this] { drain_outbound(); });

    // Register TcpServer callbacks
    server_.on_accept([this](int fd)   { on_accept(fd);   });
    server_.on_readable([this](int fd) { on_readable(fd); });
    server_.on_closed([this](int fd)   { on_closed(fd);   });

    // Order event callback (fires on matching thread)
    pipeline_.set_order_callback([this](const OrderEvent& ev) {
        std::unique_lock<std::mutex> lk(order_meta_mutex_);
        auto it = order_meta_.find(ev.order_id);
        if (it == order_meta_.end()) return;
        // Copy metadata so we can release the lock before building the report
        const int         fd        = it->second.fd;
        const std::string cl_ord_id = it->second.cl_ord_id;
        const std::string symbol    = it->second.symbol;
        lk.unlock();

        std::string report = serializer_.execution_report(
            ev,
            cl_ord_id,
            symbol,
            "ENGINE",
            "CLIENT",
            1);

        push_outbound(fd, report);
    });

    // Trade event callback (fires on matching thread)
    pipeline_.set_trade_callback([this](const TradeEvent& te) {
        auto send_fill = [&](OrderId oid, Side side) {
            std::unique_lock<std::mutex> lk(order_meta_mutex_);
            auto it = order_meta_.find(oid);
            if (it == order_meta_.end()) return;
            // Copy metadata so we can release the lock before building the report
            const int         fd           = it->second.fd;
            const std::string cl_ord_id    = it->second.cl_ord_id;
            const std::string symbol       = it->second.symbol;
            const Quantity    original_qty = it->second.original_qty;
            lk.unlock();

            Quantity leaves = (original_qty > te.quantity)
                                  ? original_qty - te.quantity
                                  : 0;
            bool fully_filled = (leaves == 0);

            std::string report = serializer_.fill_report(
                oid,
                cl_ord_id,
                symbol,
                side,
                original_qty,
                te.price,
                te.quantity,
                te.quantity,
                leaves,
                fully_filled,
                "ENGINE",
                "CLIENT",
                1);

            push_outbound(fd, report);
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
    if (report.size() >= sizeof(msg.data)) {
        // Report too large for outbound buffer — truncated. Should not happen
        // in normal operation. Increase OutboundMsg::data size if this fires.
        std::cerr << "push_outbound: FIX report truncated ("
                  << report.size() << " >= " << sizeof(msg.data) << ")\n";
    }
    msg.len = static_cast<int>(
        std::min(report.size(), sizeof(msg.data) - 1));
    std::memcpy(msg.data, report.data(), msg.len);

    while (!outbound_queue_.try_push(msg))
        ;

    char b = 1;
    ::write(notify_pipe_w_, &b, 1);
}

void FIXGateway::drain_outbound() {
    // Drain the queue first to avoid missing wakeups
    OutboundMsg msg;
    while (outbound_queue_.try_pop(msg)) {
        if (msg.fd >= 0 && msg.len > 0)
            server_.send_to(msg.fd, msg.data, msg.len);
    }

    // Then drain all notification bytes from the pipe
    char buf[64];
    while (::read(notify_pipe_r_, buf, sizeof(buf)) > 0)
        ;
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

    {
        std::lock_guard<std::mutex> lk(order_meta_mutex_);
        for (auto mit = order_meta_.begin(); mit != order_meta_.end(); ) {
            if (mit->second.fd == fd)
                mit = order_meta_.erase(mit);
            else
                ++mit;
        }
    }

    clients_.erase(it);
}

void FIXGateway::process_recv_buf(int fd, ClientState& /*state*/) {
    while (true) {
        // Re-look up the client each iteration: handle_message (e.g. on
        // Logout) may have erased it from clients_, invalidating references.
        auto it = clients_.find(fd);
        if (it == clients_.end()) return;
        ClientState& cur = it->second;
        auto& buf = cur.recv_buf;

        auto pos = buf.find("10=");
        if (pos == std::string::npos) break;

        auto end = buf.find('\x01', pos + 3);
        if (end == std::string::npos) break;

        std::size_t consumed = end + 1;

        std::string_view msg_view(buf.data(), consumed);
        Fix42Message msg = parser_.parse(msg_view);

        if (msg.valid)
            handle_message(fd, cur, msg);
        // NOTE: after handle_message, 'cur' and 'buf' may be dangling
        //       (client erased by handle_logout).  Re-look up next iteration.

        // Erase consumed bytes only if the client is still alive.
        auto it2 = clients_.find(fd);
        if (it2 == clients_.end()) return;
        it2->second.recv_buf.erase(0, consumed);
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

    {
        std::lock_guard<std::mutex> lk(order_meta_mutex_);
        order_meta_[order_id] = OrderMeta{
            fd,
            std::string(msg.cl_ord_id),
            symbol,
            msg.side,
            msg.qty};
    }

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
