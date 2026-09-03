#include "graphical/models/OperationChainRunFilter.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <vector>

using namespace std;

namespace {
    OperationChainRunSnapshot createRunSnapshot(OperationChainRunId runId,
                                                OperationChainStatus status,
                                                int64_t updatedMilliseconds,
                                                uint64_t updateSequence)
    {
        OperationChainRunSnapshot run;
        run.runId = runId;
        run.chainSnapshot.status = status;
        run.chainSnapshot.updatedAt = OperationChainTimePoint{chrono::milliseconds(updatedMilliseconds)};
        run.updateSequence = updateSequence;
        return run;
    }

    vector<OperationChainRunId> getRunIds(const vector<OperationChainRunSnapshot> &runs)
    {
        vector<OperationChainRunId> runIds;
        runIds.reserve(runs.size());
        for (const OperationChainRunSnapshot &run : runs)
        {
            runIds.push_back(run.runId);
        }
        return runIds;
    }
}

TEST(OperationChainRunFilterTest, FiltersEveryStatusViewAndSortsByRecencySequenceAndRunId)
{
    const vector<OperationChainRunSnapshot> runs{
        createRunSnapshot(1, OperationChainStatus::COMPLETED, 100, 1),
        createRunSnapshot(2, OperationChainStatus::RUNNING, 300, 2),
        createRunSnapshot(3, OperationChainStatus::FAILED, 200, 3),
        createRunSnapshot(4, OperationChainStatus::CANCELLED, 400, 4),
        createRunSnapshot(5, OperationChainStatus::PENDING, 300, 5),
        createRunSnapshot(6, OperationChainStatus::COMPLETED, 200, 3),
    };

    EXPECT_EQ(getRunIds(filterAndSortOperationChainRuns(runs, OperationChainRunFilter::ACTIVE)),
              (vector<OperationChainRunId>{5, 2}));
    EXPECT_EQ(getRunIds(filterAndSortOperationChainRuns(runs, OperationChainRunFilter::COMPLETED)),
              (vector<OperationChainRunId>{6, 1}));
    EXPECT_EQ(getRunIds(filterAndSortOperationChainRuns(runs, OperationChainRunFilter::FAILED)),
              (vector<OperationChainRunId>{3}));
    EXPECT_EQ(getRunIds(filterAndSortOperationChainRuns(runs, OperationChainRunFilter::CANCELLED)),
              (vector<OperationChainRunId>{4}));
    EXPECT_EQ(getRunIds(filterAndSortOperationChainRuns(runs, OperationChainRunFilter::NON_ACTIVE)),
              (vector<OperationChainRunId>{4, 6, 3, 1}));
    EXPECT_EQ(getRunIds(filterAndSortOperationChainRuns(runs, OperationChainRunFilter::ALL)),
              (vector<OperationChainRunId>{4, 5, 2, 6, 3, 1}));
}
