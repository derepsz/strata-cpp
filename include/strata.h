#pragma once
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>

namespace strata
{
    namespace detail
    {
        template <typename, typename, typename, typename = void>
        struct is_invocable_impl : std::false_type
        {};

        template <typename F, typename Ret, typename... Args>
        struct is_invocable_impl<F, Ret, std::tuple<Args...>, std::void_t<decltype(std::declval<F>()(std::declval<Args>()...))>>
        {
            using actual_ret            = decltype(std::declval<F>()(std::declval<Args>()...));
            static constexpr bool value = std::is_convertible_v<actual_ret, Ret>;
        };
    }

    template <typename Ret, typename F, typename... Args>
    struct is_invocable_r : detail::is_invocable_impl<F, Ret, std::tuple<Args...>>
    {};

    template <typename Ret, typename F, typename... Args>
    constexpr bool is_invocable_r_v = is_invocable_r<Ret, F, Args...>::value;

    template <typename Ret, typename... Args>
    struct LayerOp
    {
        using Function   = Ret (*)(Args...);
        using ReturnType = Ret;
        using Arguments  = std::tuple<Args...>;

        template <typename F>
        static constexpr bool validates_function = is_invocable_r_v<ReturnType, F, Args...>;
    };

    /// @brief Main layer composition template
    ///
    /// Combines multiple layers into a processing pipeline where each layer can
    /// intercept operations before and after execution.
    template <typename... Layers>
    struct Strata
    {
        /// @brief Executes an operation through all enabled layers
        ///
        /// @tparam Op Operation type that defines return type and arguments
        /// @tparam Func Signature of the function to execute
        /// @tparam Args Types of the arguments
        ///
        /// @param func Core function to wrap in layers
        /// @param args Arguments to pass through layers to the function
        /// @return ReturnT Result of the operation
        template <typename Op, typename Func, typename... Args>
        static typename Op::ReturnType Exec(Func&& func, Args&&... args)
        {
            static_assert(Op::template validates_function<Func>,
                "Function signature does not match operation definition");

            #define CORE_FUNC() std::forward<Func>(func)(std::forward<Args>(args)...)
            {
                if constexpr (sizeof...(Layers) == 0)
                {
                    if constexpr (std::is_void_v<typename Op::ReturnType>)
                        CORE_FUNC();
                    else
                        return CORE_FUNC();
                }

                #ifdef DEBUG
                    auto currentOperation = { typeid(Op).name(), typeid(func).name() };
                    if constexpr (sizeof...(Layers) > 0)
                        auto enabledLayers = { typeid(Layers).name()... };
                #endif

                (ApplyBeforeIfExists<Op, Layers>(std::forward<Args>(args)...), ...);

                if constexpr (std::is_void_v<typename Op::ReturnType>)
                {
                    CORE_FUNC();
                    ApplyAfterIfExists<Op, Layers...>(std::forward<Args>(args)...);
                }
                else
                {
                    typename Op::ReturnType result = CORE_FUNC();
                    ApplyAfterIfExists<Op, typename Op::ReturnType, Layers...>(result, std::forward<Args>(args)...);
                    return result;
                }
            }
            #undef CORE_FUNC
        }

        // Helper to add new layers to the front
        template <typename NewLayer>
        using PrependLayer = Strata<NewLayer, Layers...>;

    private:
        template <typename Op, typename Layer, typename... Args>
        static void ApplyBeforeIfExists(Args&&... args)
        {
            if constexpr (has_specific_before_impl<Layer, Op>::value)
                Layer::template Impl<Op>::Before(std::forward<Args>(args)...);
            else if constexpr (has_generic_before_impl<Layer, Args...>::value)
                Layer::template Impl<Op>::Before(std::forward<Args>(args)...);
        }

        // After is called recursively to wrap function symmetrically
        // Helper for value-returning operations
        template <typename Op, typename ReturnT, typename First, typename... Rest, typename... Args>
        static void ApplyAfterIfExists(ReturnT& result, Args&&... args)
        {
            if constexpr (sizeof...(Rest) > 0)
                ApplyAfterIfExists<Op, ReturnT, Rest...>(result, std::forward<Args>(args)...);

            if constexpr (has_specific_after_impl<First, Op>::value)
                First::template Impl<Op>::After(result, std::forward<Args>(args)...);
            else if constexpr (has_generic_after_impl<First, ReturnT, Args...>::value)
                First::template Impl<Op>::After(result, std::forward<Args>(args)...);
        }

        // Helper for void-returning operations
        template <typename Op, typename First, typename... Rest, typename... Args>
        static void ApplyAfterIfExists(Args&&... args)
        {
            if constexpr (sizeof...(Rest) > 0)
                ApplyAfterIfExists<Op, Rest...>(std::forward<Args>(args)...);

            if constexpr (has_specific_after_impl<First, Op>::value)
                First::template Impl<Op>::After(std::forward<Args>(args)...);
            else if constexpr (has_generic_after_impl<First, void, Args...>::value)
                First::template Impl<Op>::After(std::forward<Args>(args)...);
        }

        // SFINAE helpers to detect if a layer has an implementation for an operation
        template <typename Layer, typename Op, typename = void>
        struct has_specific_before_impl : std::false_type
        {};

        template <typename Layer, typename Op, typename = void>
        struct has_specific_after_impl : std::false_type
        {};

        template <typename Layer, typename Op>
        struct has_specific_before_impl<Layer, Op, std::void_t<decltype(Layer::template Impl<Op>::Before)>> : std::true_type
        {};

        template <typename Layer, typename Op>
        struct has_specific_after_impl<Layer, Op, std::void_t<decltype(Layer::template Impl<Op>::After)>> : std::true_type
        {};

        template <typename Layer, typename... Args>
        struct has_generic_before_impl
        {
            template <typename T>
            static auto test(int) -> decltype(T::template Impl<void>::Before(std::declval<Args>()...), std::true_type {});

            template <typename>
            static auto test(...) -> std::false_type;

            static constexpr bool value = decltype(test<Layer>(0))::value;
        };

        template <typename Layer, typename ReturnT, typename... Args>
        struct has_generic_after_impl
        {
            template <typename T>
            static auto test(int) -> decltype(T::template Impl<void>::After(std::declval<ReturnT&>(), std::declval<Args>()...), std::true_type {});

            template <typename>
            static auto test(...) -> std::false_type;

            static constexpr bool value = decltype(test<Layer>(0))::value;
        };
    };

    /// @brief Base traits for controlling layer enablement at compile time
    template <typename T>
    struct LayerTraits
    {
        static constexpr bool compiletimeEnabled = false;
    };


    ////////////////////////////////////////
    // Utilities
    ////////////////////////////////////////

    namespace util
    {
        /// @brief Define a template alias for all available layers
        template<typename... Layers>
        using LayerPack = std::tuple<Layers...>;

        /// @brief Helper to inspect enabled layers
        template <typename LayerPackType>
        struct EnabledLayerInfo
        {
            static constexpr auto enabled = detail::LayerPackHelper<LayerPackType>::template Apply<detail::EnabledLayerInfoImpl>::enabled;
        };

        /// @brief Check if a single layer is enabled
        template<typename Layer>
        inline constexpr bool IsLayerEnabled = LayerTraits<Layer>::compiletimeEnabled;

        /// @brief Count enabled layers
        template<typename LayerPackType>
        constexpr size_t CountEnabledLayers = detail::LayerPackHelper<LayerPackType>::template ApplyValue<detail::CountEnabledLayersImpl>;

        /// @brief Check if any layers are enabled
        template<typename LayerPackType>
        inline constexpr bool AnyLayersEnabled = detail::LayerPackHelper<LayerPackType>::template ApplyValue<detail::AnyLayersEnabledImpl>;

        // Forward declaration
        namespace detail { template <typename... Layers> struct LayerFilterImpl; }

        // LayerFilter with SFINAE to handle both LayerPack and individual layers
        /// @brief Provides Strata<Layers...> type with only enabled layers
        /// Can be used to call Exec<Op>(func, args...)
        /// Any <Layers...> with compiletimeEnabled = false are filtered out
        template <typename... Layers>
        using LayerFilter = typename detail::LayerFilterImpl<Layers...>::type;

        namespace detail
        {
            // Helper to work with LayerPack
            template<typename T>
            struct LayerPackHelper;


            template<typename... Layers>
            struct LayerPackHelper<LayerPack<Layers...>>
            {
                template<template<typename...> class F>
                using Apply = F<Layers...>;

                template<template<typename...> class F>
                static constexpr auto ApplyValue = F<Layers...>::value;
            };


            // Type trait to check if a type is a LayerPack
            template<typename T>
            struct is_layer_pack : std::false_type {};

            template<typename... Layers>
            struct is_layer_pack<LayerPack<Layers...>> : std::true_type {};

            /// @brief Helper to filter enabled layers at compile time
            template <typename... AllLayers>
            struct EnabledLayers
            {
                using type = Strata<>;
            };

            template <typename FirstLayer, typename... RestLayers>
            struct EnabledLayers<FirstLayer, RestLayers...>
            {
                using type = std::conditional_t<
                    LayerTraits<FirstLayer>::compiletimeEnabled,
                    typename EnabledLayers<RestLayers...>::type::template PrependLayer<FirstLayer>,
                    typename EnabledLayers<RestLayers...>::type>;
            };

                // LayerFilter implementation
            template <typename... Layers>
            struct LayerFilterImpl
            {
                using type = typename EnabledLayers<Layers...>::type;
            };

            // Specialization for LayerPack
            template <typename... Layers>
            struct LayerFilterImpl<LayerPack<Layers...>>
            {
                using type = typename EnabledLayers<Layers...>::type;
            };

            template<typename... Layers>
            struct EnabledLayerInfoImpl
            {
                static constexpr std::array<bool, sizeof...(Layers)> enabled = { LayerTraits<Layers>::compiletimeEnabled... };
            };

            // Count enabled layers
            template<typename... Layers>
            struct CountEnabledLayersImpl
            {
                static constexpr size_t value = (0 + ... + IsLayerEnabled<Layers>);
            };

            // Check if any layers are enabled
            template<typename... Layers>
            struct AnyLayersEnabledImpl
            {
                static constexpr bool value = (... || IsLayerEnabled<Layers>);
            };

        }
    }


    ////////////////////////////////////////
    // Layer State Manager
    ////////////////////////////////////////

    template <typename T>
    class LayerState
    {
    private:
        mutable std::mutex                         mutex_;
        T                                          data_;
        std::vector<std::function<void(const T&)>> observers_;

    public:
        class Proxy
        {
        private:
            LayerState<T>&               state_;
            std::unique_lock<std::mutex> lock_;

        public:
            Proxy(LayerState<T>& state)
                : state_(state), lock_(state_.mutex_) {}
            ~Proxy() { state_.notifyObservers(); }

            T*       operator->() { return &state_.data_; }
            const T* operator->() const { return &state_.data_; }
        };

        Proxy access() { return Proxy(*this); }

        T read() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return data_;
        }

        void write(const T& newData)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            data_ = newData;
            notifyObservers();
        }

        template <typename F>
        void modify(F&& func)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            func(data_);
            notifyObservers();
        }

        void addObserver(std::function<void(const T&)> observer)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            observers_.push_back(std::move(observer));
        }

    private:
        void notifyObservers()
        {
            for (const auto& observer : observers_)
            {
                observer(data_);
            }
        }
    };

    class LayerStateRegistry
    {
        template <typename T>
        friend class LayerStateManager;

    public:
        static void Clear() { GetInstance().clear(); }
        static void setCurrentContext(const std::string& context) { currentContext = context; }

        static const std::string& getCurrentContext() { return currentContext; }

    private:
        static inline thread_local std::string currentContext;
        static LayerStateRegistry&             GetInstance()
        {
            static LayerStateRegistry instance;
            return instance;
        }

    public:
        template <typename T>
        std::shared_ptr<LayerState<T>> getOrCreateState(const std::string& key)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto&                       typeMap = states_[std::type_index(typeid(T))];
            auto                        it      = typeMap.find(key);
            if (it == typeMap.end())
            {
                auto newState = std::make_shared<LayerState<T>>();
                typeMap[key]  = newState;
                return newState;
            }
            return std::static_pointer_cast<LayerState<T>>(it->second);
        }

        void clear()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            states_.clear();
        }

        void initialize()
        {
            states_.reserve(10);
        }

        template <typename T>
        void removeState(std::string_view key)
        {
            std::unique_lock lock(mutex_);
            auto             typeIt = states_.find(std::type_index(typeid(T)));
            if (typeIt != states_.end())
            {
                typeIt->second.erase(std::string(key));
            }
        }

        template <typename T>
        void iterateStates(const std::function<void(const std::string&, const LayerState<T>&)>& func)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto                        typeIt = states_.find(std::type_index(typeid(T)));
            if (typeIt != states_.end())
            {
                for (const auto& [key, state] : typeIt->second)
                {
                    func(key, *std::static_pointer_cast<LayerState<T>>(state));
                }
            }
        }

        std::mutex mutex_;
        std::unordered_map<std::type_index, std::unordered_map<std::string, std::shared_ptr<void>>>states_;
        std::once_flag init_flag_;
    };

    template <typename T>
    class LayerStateWrapper
    {
    private:
        std::shared_ptr<LayerState<T>> state_;

    public:
        explicit LayerStateWrapper(std::shared_ptr<LayerState<T>> state)
            : state_(std::move(state)) {}

        typename LayerState<T>::Proxy operator->() { return state_->access(); }
        typename LayerState<T>::Proxy operator->() const { return state_->access(); }

        T    read() const { return state_->read(); }
        void write(const T& newData) { state_->write(newData); }

        template <typename F>
        void modify(F&& func)
        {
            state_->modify(std::forward<F>(func));
        }

        void addObserver(std::function<void(const T&)> observer)
        {
            state_->addObserver(std::move(observer));
        }
    };

    template <typename T>
    class LayerStateManager
    {
    public:
        static LayerStateWrapper<T> global()
        {
            return LayerStateWrapper<T>(LayerStateRegistry::GetInstance().getOrCreateState<T>("global"));
        }
        static void setCurrentContext(const std::string& context)
        {
            LayerStateRegistry::setCurrentContext(context);
        }

        static const std::string& getCurrentContext()
        {
            return LayerStateRegistry::getCurrentContext();
        }

        static LayerStateWrapper<T> current()
        {
            return LayerStateWrapper<T>(LayerStateRegistry::GetInstance().getOrCreateState<T>(getCurrentContext()));
        }

        static LayerStateWrapper<T> forContext(const std::string& context)
        {
            return LayerStateWrapper<T>(LayerStateRegistry::GetInstance().getOrCreateState<T>(context));
        }

        static void removeState(std::string_view context)
        {
            LayerStateRegistry::GetInstance().removeState<T>(context);
        }

        static void iterateStates(const std::function<void(const std::string&, const LayerState<T>&)>& func)
        {
            LayerStateRegistry::GetInstance().iterateStates<T>(func);
        }
    };

}  // namespace strata
