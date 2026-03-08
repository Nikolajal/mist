#pragma once
#include <random>
#include <type_traits>
#include <stdexcept>

namespace mist
{
    // Reusable SFINAE alias: resolves to void if T is a floating-point type,
    // otherwise removes the template from the overload set entirely.
    // (C++20 equivalent: template <std::floating_point T>)
    template <typename T>
    using floating_type_check = std::enable_if_t<std::is_floating_point_v<T>>;

    /**
     * @brief Random number generator wrapper with convenient distributions.
     *
     * Provides:
     *  - Deterministic and non-deterministic seeding
     *  - Continuous distributions: uniform, normal
     *  - Discrete distributions: Poisson
     *  - Convenience functions: generate_phi
     *
     * Each sample call constructs its distribution locally, which:
     *  - Avoids implicit double-precision widening for float/long double types
     *  - Is safe for concurrent use across separate instances
     *  - Has negligible overhead (distribution objects are cheap to construct)
     *
     * @note Not thread-safe. Give each thread its own rnd instance.
     *
     * Example:
     * @code{.cpp}
     * mist::rnd rng;                    // non-deterministic seed
     * double x = rng.uniform();         // uniform [0, 1)
     * float  y = rng.uniform(0.f, 5.f);
     * int    z = rng.poisson(5);        // Poisson(λ=5)
     * @endcode
     */
    class [[nodiscard]] rnd
    {
    public:
        using engine_t = std::mt19937;

        // ------------------------------------------------------------------
        // Constructors
        // ------------------------------------------------------------------

        /// Non-deterministic seed via std::random_device.
        rnd() : gen_(std::random_device{}()) {}

        /// Deterministic seed for reproducible sequences.
        explicit rnd(uint32_t seed) : gen_(seed) {}

        /**
         * @brief Reseed the engine.
         * @warning Resets the random sequence; breaks reproducibility if
         *          called mid-run.
         */
        void reseed(uint32_t seed) { gen_.seed(seed); }

        // ------------------------------------------------------------------
        // Discrete distributions
        // ------------------------------------------------------------------

        /**
         * @brief Sample from Poisson(λ).
         * @param lambda Mean of the distribution. Must be > 0.
         * @throws std::invalid_argument if lambda <= 0.
         */
        [[nodiscard]] int poisson(int lambda)
        {
            if (lambda <= 0)
                throw std::invalid_argument("Poisson lambda must be > 0");
            std::poisson_distribution<int> dist(lambda);
            return dist(gen_);
        }

        // ------------------------------------------------------------------
        // Continuous distributions
        // ------------------------------------------------------------------

        /**
         * @brief Sample from Uniform[start, end).
         * @tparam T Floating-point type (float, double, long double).
         * @param start Inclusive lower bound (default 0).
         * @param end   Exclusive upper bound (default 1).
         * @note Arguments are swapped automatically if end < start.
         */
        template <typename T, typename = floating_type_check<T>>
        [[nodiscard]] T uniform(T start = T(0), T end = T(1))
        {
            if (end < start) std::swap(start, end);
            std::uniform_real_distribution<T> dist(start, end);
            return dist(gen_);
        }

        /**
         * @brief Sample from N(mean, stdv).
         * @tparam T Floating-point type.
         * @param mean Mean of the distribution (default 0).
         * @param stdv Standard deviation (default 1).
         */
        template <typename T, typename = floating_type_check<T>>
        [[nodiscard]] T normal(T mean = T(0), T stdv = T(1))
        {
            std::normal_distribution<T> dist(mean, stdv);
            return dist(gen_);
        }

        // ------------------------------------------------------------------
        // Convenience
        // ------------------------------------------------------------------

        /**
         * @brief Sample a random angle φ uniformly from [-π, π).
         * @return Angle in radians.
         * @note Uses a constexpr literal for π (std::numbers::pi is C++20).
         */
        [[nodiscard]] double generate_phi()
        {
            constexpr double pi = 3.14159265358979323846;
            return uniform(-pi, pi);
        }

    private:
        engine_t gen_;
    };

} // namespace mist
