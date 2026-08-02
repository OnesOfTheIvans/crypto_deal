#ifndef BINANCE_EXECUTION_REPORT_MESSAGE_DTO_H
#define BINANCE_EXECUTION_REPORT_MESSAGE_DTO_H

#include "ExecutionReportEventDto.hpp"

#include <boost/describe.hpp>

#include <optional>

namespace binance {
    struct ExecutionReportMessageDto
    {
        std::optional<ExecutionReportEventDto> event;
    };

    BOOST_DESCRIBE_STRUCT(ExecutionReportMessageDto, (), (event))
}

#endif
