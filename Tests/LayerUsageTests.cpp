#include <gtest/gtest.h>
#include <strata.h>
#include <iostream>
#include <string>
#include <sstream>
#include <vector>

using namespace strata;

namespace layertest
{
    std::stringstream Log {};

    // Example functions to be wrapped by layers
    int         Add(int a, int b) { return a + b; }
    void        Print(const std::string& msg) { std::cout << msg << "\n"; }
    std::string Concatenate(const std::string& a, const std::string& b) { return a + b; }
}

// 1. Define layer operations to match function signatures
struct LayerOpAdd       :  LayerOp<int, int, int> {};
struct LayerOpPrint     :  LayerOp<void, const std::string&> {};
struct LayerOpConcat    :  LayerOp<std::string, const std::string&, const std::string&> {};


// 2. Define layers
//    - Optional data struct to access state using LayerStateManager
//    - Any combination of Before/After functions for select operations
//    - Generic implemenation (MetricsLayer) can be used to wrap all operations

struct MetricsLayer
{
    struct Data
    {
        int operationCount = 0;
        std::vector<std::string> operationHistory;
    };
    template<typename Op>
    struct Impl {
        template<typename... Args>
        static void Before(Args&&... args) {
            auto state = LayerStateManager<MetricsLayer::Data>::global();
            state->operationCount++;
            state->operationHistory.push_back(typeid(Op).name());
        }
    };
};

struct LoggingLayer
{
    struct Data
    {
        enum LogLevel { kLogLevelNone, kLogLevelInfo, kLogLevelError, kNumLogLevels };
        LogLevel logLevel = kLogLevelNone;
    };

    template<typename Op>
    struct Impl;

    template<>
    struct Impl<LayerOpAdd> {
        static void After(int& result, int a, int b) {
            auto state = LayerStateManager<LoggingLayer::Data>::global();
            if (state->logLevel == LoggingLayer::Data::kLogLevelInfo)
                layertest::Log << a << " + " << b << " = " << result << "\n";
        }
    };

    template<>
    struct Impl<LayerOpPrint> {
        static void Before(const std::string& msg) {
            auto state = LayerStateManager<LoggingLayer::Data>::current();
            auto currentContext = LayerStateManager<LoggingLayer::Data>::getCurrentContext();
            if (state->logLevel == LoggingLayer::Data::kLogLevelError)
                layertest::Log << "Error logging (" << currentContext << "): " << msg << "\n";
            else if (state->logLevel == LoggingLayer::Data::kLogLevelInfo)
                layertest::Log << "Info logging (" << currentContext << "): " << msg << "\n";
        }
    };

    template<>
    struct Impl<LayerOpConcat> {
        static void After(std::string& result, const std::string& a, const std::string& b) {
            auto state = LayerStateManager<LoggingLayer::Data>::global();
            if (state->logLevel == LoggingLayer::Data::kLogLevelInfo)
                layertest::Log << "Concatenated: '" << a << "' and '" << b << "' to get '" << result << "'\n";
        }
    };
};

struct ValidationLayer
{
    template<typename Op>
    struct Impl;

    template<>
    struct Impl<LayerOpAdd> {
        static void Before(int a, int b) {
            if (a < 0 || b < 0)
                throw std::invalid_argument("Negative numbers not allowed");
        }
    };

    template<>
    struct Impl<LayerOpConcat> {
        static void Before(const std::string& a, const std::string& b) {
            if (a.empty() || b.empty())
                throw std::invalid_argument("Empty strings not allowed");
        }
    };
};

// 3. Enable layers
template<> struct LayerTraits<LoggingLayer> { static constexpr bool compiletimeEnabled = true; };
template<> struct LayerTraits<MetricsLayer> { static constexpr bool compiletimeEnabled = true; };
template<> struct LayerTraits<ValidationLayer> { static constexpr bool compiletimeEnabled = true; };


class LayerUsageTests : public ::testing::Test {
protected:
    void SetUp() override {
        layertest::Log.str("");
        LayerStateRegistry::Clear();
    }
};

TEST_F(LayerUsageTests, BasicLogging)
{
    using Stratum = util::LayerFilter<LoggingLayer>;
    auto state = LayerStateManager<LoggingLayer::Data>::global();
    state->logLevel = LoggingLayer::Data::kLogLevelInfo;

    int sum = Stratum::Exec<LayerOpAdd>(layertest::Add, 5, 3);

    EXPECT_EQ(sum, 8);
    EXPECT_EQ(layertest::Log.str(), "5 + 3 = 8\n");
}

TEST_F(LayerUsageTests, MetricsTracking)
{
    using Stratum = util::LayerFilter<MetricsLayer>;
    auto state = LayerStateManager<MetricsLayer::Data>::global();

    Stratum::Exec<LayerOpAdd>(layertest::Add, 1, 2);
    Stratum::Exec<LayerOpPrint>(layertest::Print, "Hello");
    Stratum::Exec<LayerOpConcat>(layertest::Concatenate, "Hello", "World");

    EXPECT_EQ(state->operationCount, 3);
    EXPECT_EQ(state->operationHistory.size(), 3);
    EXPECT_EQ(state->operationHistory[0], typeid(LayerOpAdd).name());
    EXPECT_EQ(state->operationHistory[1], typeid(LayerOpPrint).name());
    EXPECT_EQ(state->operationHistory[2], typeid(LayerOpConcat).name());
}

TEST_F(LayerUsageTests, ValidationLayer)
{
    using Stratum = util::LayerFilter<ValidationLayer>;

    EXPECT_NO_THROW(Stratum::Exec<LayerOpAdd>(layertest::Add, 5, 3));
    EXPECT_THROW(Stratum::Exec<LayerOpAdd>(layertest::Add, -1, 3), std::invalid_argument);

    EXPECT_NO_THROW(Stratum::Exec<LayerOpConcat>(layertest::Concatenate, "Hello", "World"));
    EXPECT_THROW(Stratum::Exec<LayerOpConcat>(layertest::Concatenate, "", "World"), std::invalid_argument);
}

TEST_F(LayerUsageTests, MultipleLayers)
{
    using Stratum = util::LayerFilter<LoggingLayer, MetricsLayer, ValidationLayer>;

    auto loggingState = LayerStateManager<LoggingLayer::Data>::global();
    loggingState->logLevel = LoggingLayer::Data::kLogLevelInfo;

    auto metricsState = LayerStateManager<MetricsLayer::Data>::global();

    Stratum::Exec<LayerOpAdd>(layertest::Add, 5, 3);
    Stratum::Exec<LayerOpConcat>(layertest::Concatenate, "Hello", "World");

    EXPECT_EQ(metricsState->operationCount, 2);
    EXPECT_TRUE(layertest::Log.str().find("5 + 3 = 8") != std::string::npos);
    EXPECT_TRUE(layertest::Log.str().find("Concatenated: 'Hello' and 'World'") != std::string::npos);
}

TEST_F(LayerUsageTests, ContextSpecificState)
{
    using Stratum = util::LayerFilter<LoggingLayer>;

    auto globalState = LayerStateManager<LoggingLayer::Data>::global();
    globalState->logLevel = LoggingLayer::Data::kLogLevelNone;

    auto context1State = LayerStateManager<LoggingLayer::Data>::forContext("Context1");
    context1State->logLevel = LoggingLayer::Data::kLogLevelInfo;

    auto context2State = LayerStateManager<LoggingLayer::Data>::forContext("Context2");
    context2State->logLevel = LoggingLayer::Data::kLogLevelError;

    // Set global context and execute
    LayerStateManager<LoggingLayer::Data>::setCurrentContext("global");
    Stratum::Exec<LayerOpPrint>(layertest::Print, "Global message");
    EXPECT_TRUE(layertest::Log.str().empty());

    // Set Context1 and execute
    layertest::Log.str("");
    LayerStateManager<LoggingLayer::Data>::setCurrentContext("Context1");
    Stratum::Exec<LayerOpPrint>(layertest::Print, "Context1 message");
    EXPECT_EQ(layertest::Log.str(), "Info logging (Context1): Context1 message\n");

    // Set Context2 and execute
    layertest::Log.str("");
    LayerStateManager<LoggingLayer::Data>::setCurrentContext("Context2");
    Stratum::Exec<LayerOpPrint>(layertest::Print, "Context2 message");
    EXPECT_EQ(layertest::Log.str(), "Error logging (Context2): Context2 message\n");
}

TEST_F(LayerUsageTests, StateObservers)
{
    auto state = LayerStateManager<LoggingLayer::Data>::global();
    std::vector<LoggingLayer::Data::LogLevel> observedLevels;

    state.addObserver([&observedLevels](const LoggingLayer::Data& newState) {
        observedLevels.push_back(newState.logLevel);
    });

    state->logLevel = LoggingLayer::Data::kLogLevelInfo;
    state.modify([](LoggingLayer::Data& s) { s.logLevel = LoggingLayer::Data::kLogLevelError; });
    state.write(LoggingLayer::Data{LoggingLayer::Data::kLogLevelNone});

    EXPECT_EQ(observedLevels.size(), 3);
    EXPECT_EQ(observedLevels[0], LoggingLayer::Data::kLogLevelInfo);
    EXPECT_EQ(observedLevels[1], LoggingLayer::Data::kLogLevelError);
    EXPECT_EQ(observedLevels[2], LoggingLayer::Data::kLogLevelNone);
}

TEST_F(LayerUsageTests, StateIteration)
{
    LayerStateManager<LoggingLayer::Data>::forContext("Context1")->logLevel = LoggingLayer::Data::kLogLevelInfo;
    LayerStateManager<LoggingLayer::Data>::forContext("Context2")->logLevel = LoggingLayer::Data::kLogLevelError;
    LayerStateManager<LoggingLayer::Data>::forContext("Context3")->logLevel = LoggingLayer::Data::kLogLevelNone;

    std::vector<std::string> contexts;
    std::vector<LoggingLayer::Data::LogLevel> levels;

    LayerStateManager<LoggingLayer::Data>::iterateStates([&](const std::string& context, const LayerState<LoggingLayer::Data>& state) {
        contexts.push_back(context);
        levels.push_back(state.read().logLevel);
    });

    EXPECT_EQ(contexts.size(), 3);
    EXPECT_EQ(levels.size(), 3);
    EXPECT_TRUE(std::find(contexts.begin(), contexts.end(), "Context1") != contexts.end());
    EXPECT_TRUE(std::find(contexts.begin(), contexts.end(), "Context2") != contexts.end());
    EXPECT_TRUE(std::find(contexts.begin(), contexts.end(), "Context3") != contexts.end());
    EXPECT_TRUE(std::find(levels.begin(), levels.end(), LoggingLayer::Data::kLogLevelInfo) != levels.end());
    EXPECT_TRUE(std::find(levels.begin(), levels.end(), LoggingLayer::Data::kLogLevelError) != levels.end());
    EXPECT_TRUE(std::find(levels.begin(), levels.end(), LoggingLayer::Data::kLogLevelNone) != levels.end());
}
