#include "ApplicationConfigurationUtil.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace std;

namespace {
    string buildValidConfigurationText()
    {
        return R"([API]
BINANCE_HOST = binance-host.example
BINANCE_WEBSOCKET_HOST = binance-websocket.example
BINANCE_API_KEY = binance-public-test-value
BINANCE_SECRET_KEY = binance-private-test-value
BYBIT_HOST = bybit-host.example
BYBIT_WEBSOCKET_HOST = bybit-websocket.example
BYBIT_API_KEY = bybit-public-test-value
BYBIT_SECRET_KEY = bybit-private-test-value
)";
    }

    class ApplicationConfigurationTest : public ::testing::Test
    {
      protected:
        filesystem::path configurationPath;

        void SetUp() override
        {
            const auto *testInfo = ::testing::UnitTest::GetInstance()->current_test_info();
            configurationPath =
                filesystem::temp_directory_path() / ("crypto_deal_" + string(testInfo->name()) + ".ini");
        }

        void TearDown() override
        {
            error_code errorCode;
            filesystem::remove(configurationPath, errorCode);
        }

        void writeConfiguration(string_view configuration)
        {
            ofstream configurationFile(configurationPath);
            ASSERT_TRUE(configurationFile.is_open());
            configurationFile << configuration;
        }

        void expectConfigurationErrorContains(const string &expectedText)
        {
            try
            {
                loadApplicationConfiguration(configurationPath);
                FAIL() << "Expected configuration loading to fail";
            }
            catch (const runtime_error &error)
            {
                EXPECT_NE(string(error.what()).find(expectedText), string::npos);
            }
        }
    };
}

TEST_F(ApplicationConfigurationTest, LoadsEveryRequiredSetting)
{
    writeConfiguration(buildValidConfigurationText());

    const ApplicationConfiguration configuration = loadApplicationConfiguration(configurationPath);

    EXPECT_EQ(configuration.binance.host, "binance-host.example");
    EXPECT_EQ(configuration.binance.websocketHost, "binance-websocket.example");
    EXPECT_EQ(configuration.binance.apiKey, "binance-public-test-value");
    EXPECT_EQ(configuration.binance.secretKey, "binance-private-test-value");
    EXPECT_EQ(configuration.bybit.host, "bybit-host.example");
    EXPECT_EQ(configuration.bybit.websocketHost, "bybit-websocket.example");
    EXPECT_EQ(configuration.bybit.apiKey, "bybit-public-test-value");
    EXPECT_EQ(configuration.bybit.secretKey, "bybit-private-test-value");
}

TEST_F(ApplicationConfigurationTest, ReportsMissingRequiredSettingWithoutOtherValues)
{
    writeConfiguration(R"([API]
BINANCE_HOST = should-not-appear
)");

    try
    {
        loadApplicationConfiguration(configurationPath);
        FAIL() << "Expected configuration loading to fail";
    }
    catch (const runtime_error &error)
    {
        const string message = error.what();
        EXPECT_NE(message.find("API.BINANCE_WEBSOCKET_HOST"), string::npos);
        EXPECT_EQ(message.find("should-not-appear"), string::npos);
    }
}

TEST_F(ApplicationConfigurationTest, ReportsEmptyRequiredSetting)
{
    writeConfiguration(R"([API]
BINANCE_HOST =
)");

    expectConfigurationErrorContains("API.BINANCE_HOST");
}

TEST_F(ApplicationConfigurationTest, ReportsUnreadableConfigurationFile)
{
    expectConfigurationErrorContains(configurationPath.string());
}
