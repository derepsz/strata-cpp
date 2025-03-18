#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <chrono>
#include <strata.h>

using namespace strata;

struct ValidationState {
    bool strictMode = false;
    std::vector<std::string> errors;
};

struct CounterState {
    int value = 0;
    std::vector<std::string> history;
};

struct CounterLayer {
    template<typename Op>
    struct Impl {
        static void Before(int value) {
            auto state = LayerStateManager<CounterState>::global();
            state->value++;
            state->history.push_back("Before called");
        }

        static void After(int value) {
            auto state = LayerStateManager<CounterState>::global();
            state->history.push_back("After called");
        }
    };
};

class LayerStateTests : public ::testing::Test
{
protected:
    void SetUp() override {
        LayerStateRegistry::Clear();
    }
};

struct TestState {
    int counter = 0;
    std::string message;
};

TEST(LayerStateTests, BasicUsage)
{
    auto state = LayerStateManager<TestState>::global();

    state->counter = 42;
    state->message = "Hello, World!";

    auto result = state.read();
    EXPECT_EQ(result.counter, 42);
    EXPECT_EQ(result.message, "Hello, World!");
}

TEST(LayerStateTests, ConcurrentAccess)
{
    auto state = LayerStateManager<TestState>::global();

    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&state, i] {
            for (int j = 0; j < 1000; ++j) {
                state->counter++;
                state->message += std::to_string(i);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    auto result = state.read();
    EXPECT_EQ(result.counter, 10000);
    EXPECT_EQ(result.message.length(), 10000);
}

TEST(LayerStateTests, AtomicReadWrite)
{
    auto state = LayerStateManager<TestState>::global();

    TestState newState{100, "New State"};
    state.write(newState);

    auto result = state.read();
    EXPECT_EQ(result.counter, 100);
    EXPECT_EQ(result.message, "New State");
}

TEST(LayerStateTests, ConcurrentReadWrite)
{
    std::atomic<int> writesCompleted{0};
    std::atomic<int> readsCompleted{0};
    std::atomic<int> readErrors{0};

    auto state = LayerStateManager<TestState>::global();

    std::thread writer([&state, &writesCompleted] {
        for (int i = 0; i < 1000; ++i) {
            state.modify([i](TestState& s) {
                s.counter = i;
                s.message = "State " + std::to_string(i);
            });
            writesCompleted++;
            std::this_thread::yield();
        }
    });

    std::thread reader([&state, &readsCompleted, &readErrors] {
        int lastValue = -1;
        for (int i = 0; i < 1000; ++i) {
            auto result = state.read();
            if (result.counter < lastValue ||
                result.message != "State " + std::to_string(result.counter)) {
                readErrors++;
            }
            lastValue = result.counter;
            readsCompleted++;
            std::this_thread::yield();
        }
    });

    writer.join();
    reader.join();

    std::cout << "Writes completed: " << writesCompleted << std::endl;
    std::cout << "Reads completed: " << readsCompleted << std::endl;
    std::cout << "Read errors: " << readErrors << std::endl;

    EXPECT_EQ(writesCompleted, 1000);
    EXPECT_EQ(readsCompleted, 1000);
    EXPECT_EQ(readErrors, 0);
}


TEST(LayerStateTests, StateAccess_ThreadSafety)
{
    struct ThreadTestState {
        int counter = 0;
        std::vector<int> values;
    };


    std::vector<std::thread> threads;
    const int numThreads = 10;
    const int iterationsPerThread = 100;

    for (int i = 0; i < numThreads; i++) {
        threads.emplace_back([i, iterationsPerThread] {
            auto state = LayerStateManager<ThreadTestState>::global();
            for (int j = 0; j < iterationsPerThread; j++) {
                state->counter++;
                state->values.push_back(i);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    auto state = LayerStateManager<ThreadTestState>::global();
    auto finalState = state.read();
    EXPECT_EQ(finalState.counter, numThreads * iterationsPerThread);
    EXPECT_EQ(finalState.values.size(), numThreads * iterationsPerThread);
}

TEST(LayerStateTests, StateAccess_ExceptionSafety)
{
    struct ExceptionState {
        int counter = 0;
        bool modified = false;
    };

    auto state = LayerStateManager<ExceptionState>::global();

    try {
        state->counter = 42;
        state->modified = true;
        throw std::runtime_error("Simulated error");
    }
    catch (const std::runtime_error&) {
        // Expected exception
    }

    auto result = state.read();
    EXPECT_EQ(result.counter, 42);
    EXPECT_TRUE(result.modified);
}



TEST(LayerStateTests, BasicStateOperations)
{
    // Test initial state
    auto state = LayerStateManager<CounterState>::global();
    EXPECT_EQ(state->value, 0);
    EXPECT_TRUE(state->history.empty());

    // Test state modification
    state.write(CounterState{42, {"initial"}});
    EXPECT_EQ(state->value, 42);
    EXPECT_EQ(state->history.size(), 1);

    state->value++;
    state->history.push_back("updated");
    EXPECT_EQ(state->value, 43);
    EXPECT_EQ(state->history.size(), 2);
}

TEST(LayerStateTests, ContextSpecificState)
{
    auto state1 = LayerStateManager<ValidationState>::forContext("context1");
    auto state2 = LayerStateManager<ValidationState>::forContext("context2");

    state1.write(ValidationState{true, {"error1"}});

    // State2 should be independent
    EXPECT_FALSE(state2->strictMode);
    EXPECT_TRUE(state2->errors.empty());

    // Verify state1 wasn't affected by reading state2
    EXPECT_TRUE(state1->strictMode);
    EXPECT_EQ(state1->errors.size(), 1);
}

TEST(LayerStateTests, ThreadSafety)
{
    const int numThreads = 10;
    const int iterationsPerThread = 1000;
    std::vector<std::thread> threads;

    auto& state = LayerStateManager<CounterState>::global();

    // Create multiple threads that increment the counter
    for (int i = 0; i < numThreads; i++) {
        threads.emplace_back([iterationsPerThread]() {
            auto threadState = LayerStateManager<CounterState>::global();
            for (int j = 0; j < iterationsPerThread; j++) {
                threadState->value++;
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // Verify the final count using the getter method
    EXPECT_EQ(state->value, numThreads * iterationsPerThread);
}

TEST(LayerStateTests, StatePersistence)
{
    struct DummyOp : LayerOp<void, int> {};

    // Configure initial state
    auto state = LayerStateManager<CounterState>::global();
    state.write(CounterState{0, {}});

    // Create our layer system
    using TestLayers = Strata<CounterLayer>;

    // Execute multiple operations
    TestLayers::Exec<DummyOp>([](int) {}, 42);
    TestLayers::Exec<DummyOp>([](int) {}, 43);
    TestLayers::Exec<DummyOp>([](int) {}, 44);

    // Verify state was maintained
    EXPECT_EQ(state->value, 3);  // Before called 3 times
    EXPECT_EQ(state->history.size(), 6);  // Before + After each time
}

TEST(LayerStateTests, StateCleanup)
{
    auto state1 = LayerStateManager<CounterState>::forContext("test1");
    auto state2 = LayerStateManager<ValidationState>::forContext("test2");

    auto kCounterValue = 42;
    auto kStrictMode = true;

    state1.write(CounterState{kCounterValue, {"test"}});
    state2.write(ValidationState{kStrictMode, {"error"}});
    EXPECT_EQ(state1->value, kCounterValue);
    EXPECT_FALSE(state1->history.empty());
    EXPECT_TRUE(state2->strictMode);
    EXPECT_FALSE(state2->errors.empty());

    LayerStateRegistry::Clear();

    state1 = LayerStateManager<CounterState>::forContext("test1");
    state2 = LayerStateManager<ValidationState>::forContext("test2");
    EXPECT_NE(state1->value, kCounterValue);
    EXPECT_TRUE(state1->history.empty());
    EXPECT_FALSE(state2->strictMode);
    EXPECT_TRUE(state2->errors.empty());
}

TEST(LayerStateTests, MultipleStateTypes)
{
    auto counterState = LayerStateManager<CounterState>::global();
    auto validationState = LayerStateManager<ValidationState>::global();

    counterState->value = 42;
    counterState->history.push_back("counter updated");

    validationState->strictMode = true;
    validationState->errors.push_back("validation error");

    // Verify states remain independent
    EXPECT_EQ(counterState->value, 42);
    EXPECT_EQ(counterState->history.size(), 1);
    EXPECT_TRUE(validationState->strictMode);
    EXPECT_EQ(validationState->errors.size(), 1);
}

TEST(LayerStateTests, StateInitialization)
{
    // Test lazy initialization
    {
        auto state = LayerStateManager<CounterState>::forContext("lazy");
        EXPECT_EQ(state->value, 0);  // Should initialize with default values
    }

    // Test explicit initialization
    {
        auto state = LayerStateManager<CounterState>::forContext("explicit");
        state.write(CounterState{100, {"initialized"}});
        EXPECT_EQ(state->value, 100);
        EXPECT_EQ(state->history.size(), 1);
    }

    // Test copy initialization
    {
        auto state1 = LayerStateManager<CounterState>::forContext("source");
        state1.write(CounterState{42, {"source"}});

        auto state2 = LayerStateManager<CounterState>::forContext("destination");
        state2.write(state1.read());

        EXPECT_EQ(state2->value, 42);
        EXPECT_EQ(state2->history.size(), 1);
    }
}

TEST(LayerStateTests, StateAccess_BasicUsage)
{
    struct SimpleState {
        int counter = 0;
        std::string message;
        bool enabled = false;
    };

    // Initialize our state through the manager
    auto state = LayerStateManager<SimpleState>::global();

    // Basic modification of a single value
    state->counter = 42;

    // Verify the change was applied
    EXPECT_EQ(state->counter, 42);
}

TEST(LayerStateTests, StateAccess_MultipleModifications)
{
    struct SimpleState {
        int counter = 0;
        std::string message;
        bool enabled = false;
    };

    auto state = LayerStateManager<SimpleState>::global();

    // Multiple modifications are atomic when grouped in a scope
    {
        auto accessor = state;
        accessor->counter = 42;
        accessor->message = "Hello";
        accessor->enabled = true;
    }

    // Verify all changes were applied
    auto result = state;
    EXPECT_EQ(result->counter, 42);
    EXPECT_EQ(result->message, "Hello");
    EXPECT_TRUE(result->enabled);
}


TEST(LayerStateTests, StateRemoval)
{
    auto state1 = LayerStateManager<TestState>::forContext("context1");
    auto state2 = LayerStateManager<TestState>::forContext("context2");

    state1->counter = 42;
    state2->counter = 24;

    // Remove one state
    LayerStateManager<TestState>::removeState("context1");

    // Try to access the removed state (should create a new one)
    auto state1_new = LayerStateManager<TestState>::forContext("context1");
    auto state2_existing = LayerStateManager<TestState>::forContext("context2");

    EXPECT_EQ(state1_new->counter, 0);  // New state should have default value
    EXPECT_EQ(state2_existing->counter, 24);  // Existing state should retain its value
}

TEST(LayerStateTests, StateIteration)
{
    auto state1 = LayerStateManager<TestState>::forContext("context1");
    auto state2 = LayerStateManager<TestState>::forContext("context2");
    auto state3 = LayerStateManager<TestState>::forContext("context3");

    state1->counter = 10;
    state2->counter = 20;
    state3->counter = 30;

    std::vector<std::string> foundContexts;
    std::vector<int> foundValues;

    LayerStateManager<TestState>::iterateStates([&](const std::string& context, const LayerState<TestState>& state) {
        foundContexts.push_back(context);
        foundValues.push_back(state.read().counter);
    });

    EXPECT_EQ(foundContexts.size(), 3);
    EXPECT_EQ(foundValues.size(), 3);

    // Check if all contexts and values are present (order may vary)
    EXPECT_TRUE(std::find(foundContexts.begin(), foundContexts.end(), "context1") != foundContexts.end());
    EXPECT_TRUE(std::find(foundContexts.begin(), foundContexts.end(), "context2") != foundContexts.end());
    EXPECT_TRUE(std::find(foundContexts.begin(), foundContexts.end(), "context3") != foundContexts.end());

    EXPECT_TRUE(std::find(foundValues.begin(), foundValues.end(), 10) != foundValues.end());
    EXPECT_TRUE(std::find(foundValues.begin(), foundValues.end(), 20) != foundValues.end());
    EXPECT_TRUE(std::find(foundValues.begin(), foundValues.end(), 30) != foundValues.end());
}

TEST(LayerStateTests, StateObserver)
{
    LayerStateRegistry::Clear();  // Start with a clean slate

    auto state = LayerStateManager<TestState>::forContext("observed");

    std::vector<int> observedValues;

    state.addObserver([&observedValues](const TestState& newState) {
        observedValues.push_back(newState.counter);
    });

    state->counter = 10;  // This should now trigger the observer
    state.write(TestState{20, "Hello"});
    state.modify([](TestState& s) { s.counter = 30; });

    EXPECT_EQ(observedValues.size(), 3);
    EXPECT_EQ(observedValues[0], 10);
    EXPECT_EQ(observedValues[1], 20);
    EXPECT_EQ(observedValues[2], 30);
}

TEST(LayerStateTests, MultipleObservers)
{
    LayerStateRegistry::Clear();  // Start with a clean slate

    auto state = LayerStateManager<TestState>::forContext("multi_observed");

    std::vector<int> observedValues1;
    std::vector<int> observedValues2;

    state.addObserver([&observedValues1](const TestState& newState) {
        observedValues1.push_back(newState.counter);
    });

    state.addObserver([&observedValues2](const TestState& newState) {
        observedValues2.push_back(newState.counter);
    });

    state->counter = 10;
    state.write(TestState{20, "Hello"});
    state.modify([](TestState& s) { s.counter = 30; });

    auto expectedValues = std::vector<int>{10, 20, 30};

    EXPECT_EQ(observedValues1, expectedValues);
    EXPECT_EQ(observedValues2, expectedValues);
}

TEST(LayerStateTests, ConcurrentObservers)
{
    LayerStateRegistry::Clear();  // Start with a clean slate

    auto state = LayerStateManager<TestState>::forContext("concurrent");

    std::atomic<int> observerCount1{0};
    std::atomic<int> observerCount2{0};

    state.addObserver([&observerCount1](const TestState&) {
        observerCount1++;
    });

    state.addObserver([&observerCount2](const TestState&) {
        observerCount2++;
    });

    const int numThreads = 10;
    const int numModificationsPerThread = 100;

    std::vector<std::thread> threads;
    std::atomic<int> threadsCompleted{0};

    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([&state, &threadsCompleted, numModificationsPerThread]() {
            for (int j = 0; j < numModificationsPerThread; ++j) {
                state.modify([j](TestState& s) { s.counter = j; });
            }
            threadsCompleted++;
        });
    }

    // Wait for all threads to complete or timeout
    const int timeoutSeconds = 10;
    auto start = std::chrono::steady_clock::now();
    while (threadsCompleted < numThreads) {
        if (std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start).count() > timeoutSeconds) {
            FAIL() << "Test timed out";
            break;
        }
        std::this_thread::yield();
    }

    for (auto& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    EXPECT_EQ(threadsCompleted, numThreads);
    EXPECT_EQ(observerCount1, numThreads * numModificationsPerThread);
    EXPECT_EQ(observerCount2, numThreads * numModificationsPerThread);
}

TEST(LayerStateTests, DirectAccessVsModify)
{
    const int NUM_ITERATIONS = 100000;
    const int NUM_THREADS = 4;

    struct ComplexState {
        int value1 = 0;
        int value2 = 0;
        bool isConsistent() const { return value1 == value2; }
    };

    auto runTest = [NUM_ITERATIONS, NUM_THREADS](bool useModify) {
        LayerStateRegistry::Clear();
        auto state = LayerStateManager<ComplexState>::global();
        std::atomic<int> inconsistencies(0);

        auto updateFunc = [&state, &inconsistencies, useModify](int start, int end) {
            for (int i = start; i < end; ++i) {
                if (useModify) {
                    state.modify([i](ComplexState& s) {
                        s.value1 = i;
                        s.value2 = i;
                    });
                } else {
                    state->value1 = i;
                    // Small delay to increase chance of race condition
                    for (volatile int j = 0; j < 10; ++j) {}
                    state->value2 = i;
                }

                // Check consistency
                auto current = state.read();
                if (!current.isConsistent()) {
                    inconsistencies++;
                }
            }
        };

        auto startTime = std::chrono::high_resolution_clock::now();

        std::vector<std::thread> threads;
        for (int i = 0; i < NUM_THREADS; ++i) {
            int start = i * (NUM_ITERATIONS / NUM_THREADS);
            int end = (i + 1) * (NUM_ITERATIONS / NUM_THREADS);
            threads.emplace_back(updateFunc, start, end);
        }

        for (auto& thread : threads) {
            thread.join();
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

        std::cout << (useModify ? "Modify method: " : "Direct access: ")
                  << "Inconsistencies: " << inconsistencies
                  << ", Duration: " << duration.count() << "ms" << std::endl;

        return inconsistencies.load();
    };

    int directAccessInconsistencies = runTest(false);
    int modifyInconsistencies = runTest(true);

    EXPECT_GT(directAccessInconsistencies, 0) << "Expected to find inconsistencies with direct access";
    EXPECT_EQ(modifyInconsistencies, 0) << "Expected no inconsistencies with modify method";
}

struct ContextTestState {
    int value = 0;
    std::string contextName;
};

TEST(LayerStateTests, MultipleContexts)
{
    LayerStateRegistry::Clear();

    auto globalState = LayerStateManager<ContextTestState>::global();
    auto contextA = LayerStateManager<ContextTestState>::forContext("ContextA");
    auto contextB = LayerStateManager<ContextTestState>::forContext("ContextB");

    globalState->value = 100;
    globalState->contextName = "Global";
    contextA->value = 200;
    contextA->contextName = "ContextA";
    contextB->value = 300;
    contextB->contextName = "ContextB";

    EXPECT_EQ(globalState.read().value, 100);
    EXPECT_EQ(globalState.read().contextName, "Global");
    EXPECT_EQ(contextA.read().value, 200);
    EXPECT_EQ(contextA.read().contextName, "ContextA");
    EXPECT_EQ(contextB.read().value, 300);
    EXPECT_EQ(contextB.read().contextName, "ContextB");
}

TEST(LayerStateTests, ContextIsolation)
{
    LayerStateRegistry::Clear();

    auto contextA = LayerStateManager<ContextTestState>::forContext("ContextA");
    auto contextB = LayerStateManager<ContextTestState>::forContext("ContextB");

    contextA->value = 100;
    contextB->value = 200;

    EXPECT_EQ(contextA.read().value, 100);
    EXPECT_EQ(contextB.read().value, 200);

    // Modify contextA
    contextA.modify([](ContextTestState& state) {
        state.value += 50;
        state.contextName = "Modified A";
    });

    // Ensure contextB is unaffected
    EXPECT_EQ(contextA.read().value, 150);
    EXPECT_EQ(contextA.read().contextName, "Modified A");
    EXPECT_EQ(contextB.read().value, 200);
    EXPECT_TRUE(contextB.read().contextName.empty());
}

TEST(LayerStateTests, ContextThreadSafety)
{
    LayerStateRegistry::Clear();

    const int NUM_THREADS = 4;
    const int NUM_ITERATIONS = 10000;

    std::vector<std::string> contexts = {"ContextA", "ContextB", "ContextC", "ContextD"};
    std::vector<std::thread> threads;

    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([&contexts, i, NUM_ITERATIONS]() {
            auto context = LayerStateManager<ContextTestState>::forContext(contexts[i]);
            for (int j = 0; j < NUM_ITERATIONS; ++j) {
                context.modify([i, j, &contexts](ContextTestState& state) {
                    state.value = j;
                    state.contextName = contexts[i] + "_" + std::to_string(j);
                });
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    for (int i = 0; i < NUM_THREADS; ++i) {
        auto context = LayerStateManager<ContextTestState>::forContext(contexts[i]);
        auto finalState = context.read();
        EXPECT_EQ(finalState.value, NUM_ITERATIONS - 1);
        EXPECT_EQ(finalState.contextName, contexts[i] + "_" + std::to_string(NUM_ITERATIONS - 1));
    }
}


TEST(LayerStateTests, ContextIteration)
{
    LayerStateRegistry::Clear();

    std::vector<std::string> contexts = {"ContextA", "ContextB", "ContextC"};

    for (const auto& context : contexts) {
        auto state = LayerStateManager<ContextTestState>::forContext(context);
        state->value = contexts.size();
        state->contextName = context;
    }

    std::vector<std::string> foundContexts;
    std::vector<int> foundValues;

    LayerStateManager<ContextTestState>::iterateStates([&](const std::string& context, const LayerState<ContextTestState>& state) {
        foundContexts.push_back(context);
        foundValues.push_back(state.read().value);
    });

    EXPECT_EQ(foundContexts.size(), contexts.size());
    EXPECT_EQ(foundValues.size(), contexts.size());

    for (const auto& context : contexts) {
        EXPECT_NE(std::find(foundContexts.begin(), foundContexts.end(), context), foundContexts.end());
    }

    for (int value : foundValues) {
        EXPECT_EQ(value, contexts.size());
    }
}
