#include <gtest/gtest.h>
#include <strata.h>

using namespace strata;

// Define some test layers
struct Layer1 {};
struct Layer2 {};
struct Layer3 {};

// Define layer traits
template<> struct LayerTraits<Layer1> { static constexpr bool compiletimeEnabled = true; };
template<> struct LayerTraits<Layer2> { static constexpr bool compiletimeEnabled = false; };
template<> struct LayerTraits<Layer3> { static constexpr bool compiletimeEnabled = true; };

class utilTest : public ::testing::Test {};

TEST_F(utilTest, LayerPack)
{
    using TestPack = util::LayerPack<Layer1, Layer2, Layer3>;
    EXPECT_TRUE((std::is_same_v<TestPack, std::tuple<Layer1, Layer2, Layer3>>));
}

TEST_F(utilTest, LayerFilterWithLayerPack)
{
    using TestPack = util::LayerPack<Layer1, Layer2, Layer3>;
    using FilteredPack = util::LayerFilter<TestPack>;

    auto filteredType = typeid(FilteredPack).name();
    auto expectedType = typeid(Strata<Layer1, Layer3>).name();

    EXPECT_EQ(filteredType, expectedType);
    EXPECT_TRUE((std::is_same_v<FilteredPack, Strata<Layer1, Layer3>>));
}

TEST_F(utilTest, LayerFilterWithIndividualLayers)
{
    using FilteredLayers = util::LayerFilter<Layer1, Layer2, Layer3>;

    auto filteredType = typeid(FilteredLayers).name();
    auto expectedType = typeid(Strata<Layer1, Layer3>).name();
    EXPECT_EQ(filteredType, expectedType);

    EXPECT_TRUE((std::is_same_v<FilteredLayers, Strata<Layer1, Layer3>>));
}

TEST_F(utilTest, EnabledLayerInfo)
{
    using TestPack1 = util::LayerPack<Layer1, Layer2, Layer3>;
    constexpr auto info = util::EnabledLayerInfo<TestPack1>::enabled;
    EXPECT_EQ(info.size(), 3);
    EXPECT_TRUE(info[0]);
    EXPECT_FALSE(info[1]);
    EXPECT_TRUE(info[2]);

    using TestPack2 = util::LayerPack<Layer2, Layer1, Layer3>;
    constexpr auto info2 = util::EnabledLayerInfo<TestPack2>::enabled;
    EXPECT_EQ(info2.size(), 3);
    EXPECT_FALSE(info2[0]);
    EXPECT_TRUE(info2[1]);
    EXPECT_TRUE(info2[2]);

}

TEST_F(utilTest, IsLayerEnabled)
{
    EXPECT_TRUE(util::IsLayerEnabled<Layer1>);
    EXPECT_FALSE(util::IsLayerEnabled<Layer2>);
    EXPECT_TRUE(util::IsLayerEnabled<Layer3>);
}

TEST_F(utilTest, CountEnabledLayersWithLayerPack)
{
    using TestPack = util::LayerPack<Layer1, Layer2, Layer3>;
    constexpr auto count = util::CountEnabledLayers<TestPack>;
    EXPECT_EQ(count, 2);
}

TEST_F(utilTest, AnyLayersEnabledWithLayerPack)
{
    using TestPack = util::LayerPack<Layer1, Layer2, Layer3>;
    constexpr bool anyEnabled = util::AnyLayersEnabled<TestPack>;
    EXPECT_TRUE(anyEnabled);
}

TEST_F(utilTest, AnyLayersEnabledWithAllDisabled)
{
    using TestPack = util::LayerPack<Layer2>;
    constexpr bool anyEnabled = util::AnyLayersEnabled<TestPack>;
    EXPECT_FALSE(anyEnabled);
}

TEST_F(utilTest, LayerFilterExec)
{
    struct TestOp : LayerOp<int, int, int> {};
    auto testFunc = [](int a, int b) { return a + b; };

    //  Call Exec on LayerFilter
    using FilteredLayers = util::LayerFilter<Layer1, Layer2, Layer3>;
    int result = FilteredLayers::Exec<TestOp>(testFunc, 2, 3);

    EXPECT_EQ(result, 5);
}
