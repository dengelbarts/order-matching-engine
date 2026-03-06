#pragma once

#include "order_book.hpp"
#include "order_command.hpp"

#include <functional>
#include <memory>
#include <unordered_map>

// MatchingEngine routes OrderCommands to the correct per-symbol OrderBook.
// Books are created lazily on first order for that symbol.
//
// Cancel/Amend routing: the engine maintains a reverse map (OrderId -> SymbolId)
// so that cancel/amend commands do not need to carry the symbol explicitly.
// OrderBook is non-movable (pmr pool_resource_), so books are heap-allocated
// via unique_ptr.
class MatchingEngine {
    std::unordered_map<SymbolId, std::unique_ptr<OrderBook>> books_;

    // Reverse lookup: resting order_id -> which symbol's book owns it.
    std::unordered_map<OrderId, SymbolId> order_symbol_;

    std::function<void(const TradeEvent&)> on_trade_;
    std::function<void(const OrderEvent&)> on_order_event_;

    OrderBook& create_book(SymbolId sym) {
        auto book = std::make_unique<OrderBook>();
        if (on_trade_)
            book->set_trade_callback(on_trade_);
        if (on_order_event_)
            book->set_order_callback(on_order_event_);
        return *books_.emplace(sym, std::move(book)).first->second;
    }

public:
    MatchingEngine() = default;

    // Non-copyable, non-movable (owns OrderBooks which are non-movable).
    MatchingEngine(const MatchingEngine&) = delete;
    MatchingEngine& operator=(const MatchingEngine&) = delete;

    void set_trade_callback(std::function<void(const TradeEvent&)> cb) {
        on_trade_ = cb;
        for (auto& [sym, book] : books_)
            book->set_trade_callback(cb);
    }

    void set_order_callback(std::function<void(const OrderEvent&)> cb) {
        on_order_event_ = cb;
        for (auto& [sym, book] : books_)
            book->set_order_callback(cb);
    }

    // Returns the book for sym, creating it if it does not yet exist.
    OrderBook& get_or_create_book(SymbolId sym) {
        auto it = books_.find(sym);
        if (it != books_.end())
            return *it->second;
        return create_book(sym);
    }

    // Returns nullptr if no order has ever been routed to sym.
    OrderBook* get_book(SymbolId sym) {
        auto it = books_.find(sym);
        return (it != books_.end()) ? it->second.get() : nullptr;
    }

    const OrderBook* get_book(SymbolId sym) const {
        auto it = books_.find(sym);
        return (it != books_.end()) ? it->second.get() : nullptr;
    }

    size_t symbol_count() const { return books_.size(); }

    // Dispatch a command to the appropriate book.
    void route(const OrderCommand& cmd) {
        switch (cmd.type) {
        case CommandType::NewOrder: {
            OrderBook& book = get_or_create_book(cmd.symbol_id);
            book.create_order(cmd.order_id,
                              cmd.symbol_id,
                              cmd.trader_id,
                              cmd.side(),
                              cmd.price,
                              cmd.quantity,
                              cmd.timestamp,
                              cmd.order_type());
            // Track resting orders for cancel/amend routing.
            if (book.has_order(cmd.order_id))
                order_symbol_[cmd.order_id] = cmd.symbol_id;
            break;
        }
        case CommandType::Cancel: {
            auto it = order_symbol_.find(cmd.order_id);
            if (it != order_symbol_.end()) {
                auto* book = get_book(it->second);
                if (book)
                    book->cancel_order(cmd.order_id);
                order_symbol_.erase(it);
            }
            break;
        }
        case CommandType::Amend: {
            auto it = order_symbol_.find(cmd.order_id);
            if (it != order_symbol_.end()) {
                auto* book = get_book(it->second);
                if (book) {
                    book->amend_order(cmd.order_id, cmd.new_qty, cmd.new_price);
                    // If the order left the book (fully filled / cancelled), clean up.
                    if (!book->has_order(cmd.order_id))
                        order_symbol_.erase(it);
                }
            }
            break;
        }
        default:
            break;
        }
    }
};
