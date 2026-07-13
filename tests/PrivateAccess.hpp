#ifndef PRIVATE_ACCESS_HPP
#define PRIVATE_ACCESS_HPP

#include "../src/DealService/binance/BinanceDealService.hpp"
#include "../src/DealService/bybit/BybitDealService.hpp"

#include <atomic>
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

    struct BybitLastSyncMonoMsTag
    {
        using type = std::atomic<long long> BybitDealService::*;
        friend type get(BybitLastSyncMonoMsTag);
    };

    template struct PrivateMemberAccessor<BinanceHandleUserStreamMessageTag,
                                          &BinanceDealService::handleUserStreamMessage>;
    template struct PrivateMemberAccessor<BybitHandleUserStreamMessageTag, &BybitDealService::handleUserStreamMessage>;
    template struct PrivateMemberAccessor<BybitLastSyncMonoMsTag, &BybitDealService::lastSyncMonoMs>;

    inline void dispatchBinanceUserStreamMessage(BinanceDealService &service, const std::string &message)
    {
        (service.*get(BinanceHandleUserStreamMessageTag{}))(message);
    }

    inline void dispatchBybitUserStreamMessage(BybitDealService &service, const std::string &message)
    {
        (service.*get(BybitHandleUserStreamMessageTag{}))(message);
    }

    inline void setBybitLastSyncMonoMs(BybitDealService &service, long long lastSyncMonoMs)
    {
        (service.*get(BybitLastSyncMonoMsTag{})).store(lastSyncMonoMs);
    }
} // namespace test_private_access

#endif
