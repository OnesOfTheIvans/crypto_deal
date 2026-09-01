#ifndef OPERATION_CHAIN_RUN_SNAPSHOT_H
#define OPERATION_CHAIN_RUN_SNAPSHOT_H

#include "OperationChainSnapshot.hpp"
#include "type_aliasing.hpp"

#include <cstdint>

struct OperationChainRunSnapshot
{
    OperationChainRunId runId = 0;
    OperationChainSnapshot chainSnapshot;
    std::uint64_t updateSequence = 0;
};

#endif
