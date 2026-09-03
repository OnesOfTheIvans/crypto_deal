#ifndef MOCK_HTTP_REQUEST_H
#define MOCK_HTTP_REQUEST_H

#include "../src/DealService/common/HttpRequestContext.hpp"
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

class MockNetwork
{
  public:
    struct RecordedRequest
    {
        std::string target;
        std::string method;
        std::string body;
        std::map<std::string, std::string> headers;
    };

    static MockNetwork &instance()
    {
        static MockNetwork inst;
        return inst;
    }

    void reset()
    {
        std::lock_guard<std::mutex> lock(mutex);
        responses.clear();
        requests.clear();
        defaultResponse = "{}";
    }

    void setResponse(const std::string &targetSubstring, const std::string &response)
    {
        std::lock_guard<std::mutex> lock(mutex);
        responses[targetSubstring].push(response);
    }

    void setDefaultResponse(const std::string &response)
    {
        std::lock_guard<std::mutex> lock(mutex);
        defaultResponse = response;
    }

    std::string getResponse(const std::string &target)
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto best = responses.end();
        for (auto it = responses.begin(); it != responses.end(); ++it)
        {
            const auto &key = it->first;
            if (target.find(key) != std::string::npos)
            {
                if (best == responses.end() || key.size() > best->first.size())
                {
                    best = it;
                }
            }
        }

        if (best != responses.end())
        {
            auto &queue = best->second;
            if (queue.empty())
            {
                return defaultResponse;
            }
            std::string resp = queue.front();
            if (queue.size() > 1)
            {
                queue.pop();
            }
            return resp;
        }

        return defaultResponse;
    }

    void recordRequest(const HttpRequestContext &context)
    {
        const auto &request = context.getRequest();
        RecordedRequest recorded;
        recorded.target = std::string(request.target());
        recorded.method = std::string(request.method_string());
        recorded.body = request.body();

        for (const auto &field : request)
        {
            recorded.headers.emplace(std::string(field.name_string()), std::string(field.value()));
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            requests.push_back(recorded);
        }
        requestCondition.notify_all();
    }

    std::vector<RecordedRequest> getRequests() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return requests;
    }

    RecordedRequest lastRequest() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return requests.back();
    }

    bool waitForRequestCount(const std::string &targetSubstring,
                             std::size_t expectedCount,
                             std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(mutex);
        return requestCondition.wait_for(lock,
                                         timeout,
                                         [this, &targetSubstring, expectedCount]()
                                         {
                                             std::size_t count = 0;
                                             for (const RecordedRequest &request : requests)
                                             {
                                                 if (request.target.find(targetSubstring) != std::string::npos)
                                                 {
                                                     ++count;
                                                 }
                                             }
                                             return count >= expectedCount;
                                         });
    }

  private:
    mutable std::mutex mutex;
    std::condition_variable requestCondition;
    std::map<std::string, std::queue<std::string>> responses;
    std::vector<RecordedRequest> requests;
    std::string defaultResponse = "{}";
};

std::string httpsGet(HttpRequestContext &context);

std::string httpsPost(HttpRequestContext &context);

#endif
