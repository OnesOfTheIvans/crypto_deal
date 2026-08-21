#ifndef BINANCE_EXECUTION_REPORT_EVENT_DTO_H
#define BINANCE_EXECUTION_REPORT_EVENT_DTO_H

#include <boost/describe.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace binance {
    struct ExecutionReportEventDto
    {
        std::optional<std::string> s;
        std::optional<std::int64_t> i;
        std::optional<std::string> c;
        std::optional<std::string> S;
        std::optional<std::string> o;
        std::optional<std::string> f;
        std::optional<std::string> X;
        std::optional<std::string> r;
        std::optional<std::string> p;
        std::optional<std::string> q;
        std::optional<std::string> z;
        std::optional<std::string> Z;
        std::optional<std::int64_t> O;
        std::optional<std::int64_t> T;
    };

    BOOST_DESCRIBE_STRUCT(ExecutionReportEventDto, (), (s, i, c, S, o, f, X, r, p, q, z, Z, O, T))
}

#endif
