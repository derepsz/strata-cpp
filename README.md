# Strata: Composable Layer System for C++

Strata is a generic, compile-time composable layer system for C++ that allows you to wrap operations with pre- and post-processing layers capable of inspecting or modifying data and even carrying their own state and actions.

For detailed examples see
- [General Usage Tests](./Tests/LayerUsageTests.cpp)
- [State Management Tests](./Tests/LayerStateTests.cpp)
- [Layer Utility Tests](./Tests/LayerUtilTests.cpp)

## Features

- Compile-time composition of layers
- Conditional enabling/disabling of layers
- Support for both value-returning and void operations
- Thread-safe layer state management
- Flexible layer implementation with Before and After hooks
- Compile-time layer filtering for zero runtime overhead of disabled layers

## Use Cases

Strata can be used to:

- Validate inputs and outputs
- Log or trace execution
- Modify parameters or return values
- Collect metrics
- Add debug functionality
- Track operation state

## Getting Started

### Prerequisites

- C++17 compatible compiler
- CMake for building examples and tests

### Installation

Strata is a header-only library. Simply include the strata.h file in your project.

```cpp
#include "strata.h"
```


### Compile-time Enablement

Set enabled layers either explicitly or using compilation flags.
Subsets of enabled layers can be used via mechanisms like `util::LayerFilter`.

```cpp
// Set manually for debugging
template<>
struct strata::LayerTraits<SomeLayer> {
    static constexpr bool compiletimeEnabled = true;
};

// Enable via compile flags
template<>
struct strata::LayerTraits<AnotherLayer> {
    static constexpr bool compiletimeEnabled =
        #if defined(ENABLELAYERS) && defined(ENABLETHIS_LAYER)
            true
        #else
            false
        #endif
    ;
};
```

### Basic Usage

1. Define a Layer Operation:

```cpp
bool SomeFunction(int, float);
struct SomeOp : strata::LayerOp<bool, int, float> {};

// Operation must match function signature
// Convention for template parameters: <Result, Args...>
// Name can be any but recommend matching the function name
```

2. Create a Layer:

```cpp
struct SomeLayer {
    // Declare base to be specialized
    template<typename Op>
    struct Impl;

    struct Data {
        // Layer-specific data
        // Access via LayerStateManager
    };

    // LayerOp as template parameter
    template<>
    struct Impl<SomeOp> {
        static void Before(int i, float f) {
            // Pre-processing logic
        }
        static void After(bool result, int i, float f) {
            // Post-processing logic
        }
    };
};
```

3. Call functions wrapped in layers:

```cpp
bool result = strata::Strata<SomeLayer>::Exec<SomeOp>(SomeFunction, 42, 3.14f);

using ActiveLayers = strata::Strata<SomeLayer, AnotherLayer>;
bool result = ActiveLayers::Exec<SomeOp>(SomeFunction, 42, 3.14f);
```

### Layer Utilities

Strata provides several utilities to work with layers:

#### Define a pack of layers:
```cpp
using AvailableLayers = util::LayerPack<Layer1, Layer2, Layer3>;
```

#### Use filter to get callable Strata<Layers...> type from packs or explicit lists
```cpp
// Using packs
using Stratum = util::LayerFilter<AvailableLayers>;

// Or explicitly
using Stratum = util::LayerFilter<Layer1, Layer3>;

// Note if (LayerTraits<Layer1>::compiletimeEnabled == false),
// it will be filtered out and not included in the layer application

// Call becomes
auto result = Stratum::Exec<SomeOp>(SomeFunction, 42, 3.14f);
```

#### Check layer system status:
```cpp
constexpr auto count = util::CountStratum<AvailableLayers>;

constexpr bool anyEnabled = util::AnyLayersEnabled<AvailableLayers>;

constexpr bool isLayer1Enabled = util::IsLayerEnabled<Layer1>;

constexpr auto info = util::EnabledLayerInfo<AvailableLayers>::enabled;
bool isLayer1Enabled = info[0];
bool isLayer2Enabled = info[1];

// Use for inspection, control flow, etc
if constexpr (!util::IsLayerEnabled<Layer1>)
    GTEST_SKIP();
```

### Persistent State Management

Strata provides a thread-safe state management system for layers. Usage includes storage and access of layer-specific data, managing different contexts (layer tagging), sharing data between layers, observing value changes, etc.

Detailed use in .

```cpp
struct SomeLayer
{
    struct Data {
        int counter = 0;
    };

    // Operation definitions
};

auto state = strata::LayerStateManager<SomeLayer::Data>::global();
state->counter++;
```

### System Bypass

`// TODO add asm comparison`

Internal mechanisms will bypass the system when fed an empty set of layers ([benchmark](./Benchmarks/LayerBenchmarks.cpp)). To completely eliminate overhead, layers can be bypassed with relative ease using a pattern like so:

```cpp
#if defined(ENABLE_LAYERS) && !defined(RELEASE)
    using ActiveLayers = strata::Strata<SomeLayer, AnotherLayer>;
    bool result = ActiveLayers::Exec<SomeOp>(SomeFunction, 42, 3.14f);
#else
    bool result = SomeFunction(42, 3.14f);
#endif
```
