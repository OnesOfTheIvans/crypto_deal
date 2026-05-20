#ifndef PRIVATE_ACCESS_HPP
#define PRIVATE_ACCESS_HPP

#include "../src/DealService/binance/BinanceDealService.hpp"
#include "../src/DealService/bybit/BybitDealService.hpp"

#include <string>

namespace test_private_access {
    template <typename Tag, typename Tag::type Member> struct PrivateMemberAccessor
    {
        friend typename Tag::type get(Tag)
        {
            return Member;
        }
    };

    struct BinanceHandleUserStreamMessageTag
    {
        using type = void (BinanceDealService::*)(const std::string &);
        friend type get(BinanceHandleUserStreamMessageTag);
    };

    struct BybitHandleUserStreamMessageTag
    {
        using type = void (BybitDealService::*)(const std::string &);
        friend type get(BybitHandleUserStreamMessageTag);
    };

    template struct PrivateMemberAccessor<BinanceHandleUserStreamMessageTag,
                                          &BinanceDealService::handleUserStreamMessage>;
    template struct PrivateMemberAccessor<BybitHandleUserStreamMessageTag, &BybitDealService::handleUserStreamMessage>;

    inline void dispatchBinanceUserStreamMessage(BinanceDealService &service, const std::string &message)
    {
        (service.*get(BinanceHandleUserStreamMessageTag{}))(message);
    }

    inline void dispatchBybitUserStreamMessage(BybitDealService &service, const std::string &message)
    {
        (service.*get(BybitHandleUserStreamMessageTag{}))(message);
    }
} // namespace test_private_access

#endif
