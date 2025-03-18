#include <benchmark/benchmark.h>
#include <strata.h>

using namespace strata;

__declspec(noinline) int add(int a, int b) {
    return a + b;
}
struct AddOp : LayerOp<int, int, int> {};

struct HeavyLayer {
    template<typename Op>
    struct Impl;

    template<>
    struct Impl<AddOp> {
        static void Before(int a, int b) {
            volatile int sum = 0;
            for(int i = 0; i < 1000; i++) {
                sum += (a + b) * i;
            }
        }

        static void After(int& result, int a, int b) {
            volatile int product = 1;
            for(int i = 0; i < 1000; i++) {
                product *= result + i;
            }
        }
    };
};
template<> struct LayerTraits<HeavyLayer> { static constexpr bool compiletimeEnabled = true; };

static void BM_Direct(benchmark::State& state) {
    int a = 42, b = 24;
    for (auto _ : state) {
        benchmark::DoNotOptimize(add(a, b));
    }
}
BENCHMARK(BM_Direct);

using NoLayers = util::LayerFilter<>;
static void BM_LayerBypass(benchmark::State& state) {
    int a = 42, b = 24;
    for (auto _ : state) {
        benchmark::DoNotOptimize(NoLayers::Exec<AddOp>(add, a, b));
    }
}
BENCHMARK(BM_LayerBypass);

using BenchmarkEnabledLayers = util::LayerFilter<HeavyLayer>;
static void BM_LayerEnabled(benchmark::State& state) {
    int a = 42, b = 24;
    for (auto _ : state) {
        benchmark::DoNotOptimize(BenchmarkEnabledLayers::Exec<AddOp>(add, a, b));
    }
}
BENCHMARK(BM_LayerEnabled);
