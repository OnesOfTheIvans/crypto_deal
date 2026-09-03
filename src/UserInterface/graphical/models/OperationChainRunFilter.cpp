#include "OperationChainRunFilter.hpp"

#include <algorithm>
#include <vector>

using namespace std;

namespace {
    bool isRunActive(OperationChainStatus status)
    {
        return status == OperationChainStatus::PENDING || status == OperationChainStatus::RUNNING;
    }

    bool isRunNonActive(OperationChainStatus status)
    {
        return status == OperationChainStatus::COMPLETED || status == OperationChainStatus::FAILED ||
               status == OperationChainStatus::CANCELLED;
    }

    bool matchesRunFilter(const OperationChainRunSnapshot &run, OperationChainRunFilter filter)
    {
        const OperationChainStatus status = run.chainSnapshot.status;
        switch (filter)
        {
        case OperationChainRunFilter::ACTIVE:
            return isRunActive(status);
        case OperationChainRunFilter::COMPLETED:
            return status == OperationChainStatus::COMPLETED;
        case OperationChainRunFilter::FAILED:
            return status == OperationChainStatus::FAILED;
        case OperationChainRunFilter::CANCELLED:
            return status == OperationChainStatus::CANCELLED;
        case OperationChainRunFilter::NON_ACTIVE:
            return isRunNonActive(status);
        case OperationChainRunFilter::ALL:
            return true;
        }
        return false;
    }

    bool isRunMoreRecent(const OperationChainRunSnapshot &left, const OperationChainRunSnapshot &right)
    {
        if (left.chainSnapshot.updatedAt != right.chainSnapshot.updatedAt)
        {
            return left.chainSnapshot.updatedAt > right.chainSnapshot.updatedAt;
        }
        if (left.updateSequence != right.updateSequence)
        {
            return left.updateSequence > right.updateSequence;
        }
        return left.runId > right.runId;
    }
}

vector<OperationChainRunSnapshot> filterAndSortOperationChainRuns(vector<OperationChainRunSnapshot> runs,
                                                                  OperationChainRunFilter filter)
{
    erase_if(runs, [filter](const OperationChainRunSnapshot &run) { return !matchesRunFilter(run, filter); });
    ranges::sort(runs, isRunMoreRecent);
    return runs;
}
