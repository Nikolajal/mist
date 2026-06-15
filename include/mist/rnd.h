// SPDX-License-Identifier: MIT
#pragma once
#include <cassert>
#include <concepts>
#include <numbers>
#include <random>
#include <stdexcept>

/**
 * @file rnd.h
 * @brief @ref mist::Rnd — thin RAII wrapper around @c std::mt19937 with
 *        convenient sampling helpers (uniform, normal, Poisson, generate_phi).
 *
 * Header-only; included transitively from @c mist/mist.h.  Construct one
 * instance per thread that needs randomness — the engine state is not
 * thread-safe.  See @ref mist::Rnd for full API documentation.
 */

namespace mist
{
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
     * @note Not thread-safe. Give each thread its own Rnd instance.
     *
     * Example:
     * @code{.cpp}
     * mist::Rnd rng;                    // non-deterministic seed
     * double x = rng.uniform();         // uniform [0, 1)
     * float  y = rng.uniform(0.f, 5.f);
     * int    z = rng.poisson(5);        // Poisson(λ=5)
     * @endcode
     */
class [[nodiscard]] Rnd
{
public:
    using Engine = std::mt19937;

    // ------------------------------------------------------------------
    // Constructors
    // ------------------------------------------------------------------

    /// Non-deterministic seed via std::random_device.
    Rnd() : gen_(std::random_device{}()) {}

    /// Deterministic seed for reproducible sequences.
    explicit Rnd(uint32_t seed) : gen_(seed) {}

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
         *
         * @param lambda Mean of the distribution. Must be > 0.
         *               Accepts non-integer means — physics rates (DCR,
         *               occupancy, etc.) commonly use fractional values.
         * @return Integer sample drawn from Poisson(λ).
         * @throws std::invalid_argument if @p lambda <= 0.
         * @note  Integer literals convert implicitly (e.g. @c rng.poisson(5)
         *        still works), so existing call sites need no changes.
         */
    [[nodiscard]] int poisson(double lambda)
    {
        if (!(lambda > 0.0))
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
         *
         * @par Argument-order contract
         * In debug builds (NDEBUG undefined) the precondition @c start<=end is
         * checked via @c assert(); inverting the arguments fires the assertion
         * immediately, surfacing the misuse during development.
         * In release builds the arguments are silently swapped to preserve the
         * defined behaviour of @c std::uniform_real_distribution (which is UB
         * for inverted bounds). The 1.x API contract is therefore: callers
         * MUST pass @c start<=end; release builds tolerate the violation, but
         * the tolerance is not promised beyond 1.x. See mist:D-01.
         */
    template <std::floating_point T>
    [[nodiscard]] T uniform(T start = T(0), T end = T(1))
    {
        assert(start <= end &&
               "mist::Rnd::uniform: start must be <= end "
               "(release builds silently swap; debug asserts)");
        if (end < start)
            std::swap(start, end);
        std::uniform_real_distribution<T> dist(start, end);
        return dist(gen_);
    }

    /**
         * @brief Sample from N(mean, stdv).
         * @tparam T Floating-point type.
         * @param mean Mean of the distribution (default 0).
         * @param stdv Standard deviation (default 1).
         */
    template <std::floating_point T>
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
         */
    [[nodiscard]] double generate_phi()
    {
        return uniform(-std::numbers::pi, std::numbers::pi);
    }

    // ------------------------------------------------------------------
    // Engine access (escape hatch for hot loops)
    // ------------------------------------------------------------------

    /**
         * @brief Direct access to the underlying Mersenne-Twister engine.
         *
         * Most callers should use @ref uniform, @ref normal, @ref poisson
         * instead — those construct their distribution per call, which is
         * negligible for occasional sampling.
         *
         * In a tight hot loop, however, the per-call construction adds up.
         * Cache a distribution next to your `Rnd` instance and feed the
         * engine directly:
         *
         * @code{.cpp}
         * thread_local mist::Rnd rng;
         * static thread_local std::uniform_real_distribution<float> dist(-1.f, 1.f);
         * float x = dist(rng.engine());     // no per-call construction
         * @endcode
         *
         * @warning Reads and writes to the engine are NOT thread-safe.
         *          Treat this exactly like any other member: one engine per
         *          thread (via @c thread_local Rnd).
         */
    [[nodiscard]] Engine &engine() noexcept { return gen_; }

private:
    Engine gen_; ///< Underlying Mersenne-Twister engine; reseeded via @ref reseed.
};

} // namespace mist
