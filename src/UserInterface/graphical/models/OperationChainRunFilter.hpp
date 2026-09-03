#ifndef OPERATION_CHAIN_RUN_FILTER_H
#define OPERATION_CHAIN_RUN_FILTER_H

#include "OperationChainRunSnapshot.hpp"

#include <vector>

enum class OperationChainRunFilter
{
    ACTIVE,
    COMPLETED,
    FAILED,
    CANCELLED,
    NON_ACTIVE,
    ALL
};

std::vector<OperationChainRunSnapshot> filterAndSortOperationChainRuns(std::vector<OperationChainRunSnapshot> runs,
                                                                       OperationChainRunFilter filter);

#endif
