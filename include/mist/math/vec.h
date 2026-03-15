#pragma once

/**
 * @file vec.h
 * @brief Lightweight fixed-size mathematical vector for ring-finding utilities.
 *
 * @details
 * Provides `mist::vec<N, T>` — a fixed-size N-dimensional vector with
 * element-wise arithmetic, dot product, cross product (3-D), norm, and
 * distance helpers.  All operations are `constexpr`-friendly and allocate no
 * heap memory.  No external dependencies — pure C++17 STL.
 *
 * ### Convenience aliases
 * | Alias        | Equivalent         |
 * |--------------|--------------------|
 * | `mist::vec2f` | `mist::vec<2, float>`  |
 * | `mist::vec3f` | `mist::vec<3, float>`  |
 * | `mist::vec2d` | `mist::vec<2, double>` |
 * | `mist::vec3d` | `mist::vec<3, double>` |
 *
 * ### Usage
 * @code{.cpp}
 * #include <mist/math/vec.h>
 *
 * mist::vec2f a{3.f, 4.f};
 * mist::vec2f b{1.f, 0.f};
 *
 * auto c      = a + b;             // {4, 4}
 * float d     = mist::dot(a, b);   // 3
 * float r     = a.norm();          // 5
 * auto  hat_a = a.normalized();    // {0.6, 0.8}
 * float dist  = mist::distance(a, b); // ~4.24
 *
 * // 3-D cross product
 * mist::vec3f x{1.f, 0.f, 0.f};
 * mist::vec3f y{0.f, 1.f, 0.f};
 * auto z = mist::cross(x, y);      // {0, 0, 1}
 * @endcode
 */

#include <array>
#include <cassert>
#include <cmath>
#include <initializer_list>
#include <numeric>
#include <ostream>
#include <type_traits>

namespace mist
{
    // =========================================================================
    // vec<N, T>
    // =========================================================================

    /**
     * @brief Fixed-size N-dimensional mathematical vector.
     *
     * @tparam N  Number of dimensions (must be ≥ 1).
     * @tparam T  Element type.  Floating-point types are recommended.
     *            Integer types are supported but `norm()` and `normalized()`
     *            return `double` values via promotion.
     */
    template <std::size_t N, typename T = float>
    struct vec
    {
        static_assert(N >= 1, "mist::vec requires N >= 1");

        using value_type = T;
        static constexpr std::size_t size = N;

        std::array<T, N> data{};

        // ------------------------------------------------------------------
        // Construction
        // ------------------------------------------------------------------

        /// Default — zero-initialised.
        constexpr vec() = default;

        /// Construct from an initializer list (asserts list length == N).
        constexpr vec(std::initializer_list<T> il)
        {
            assert(il.size() == N && "mist::vec: initializer list length must match N");
            std::size_t i = 0;
            for (const T &v : il) data[i++] = v;
        }

        /// Construct from a raw array.
        explicit constexpr vec(const std::array<T, N> &a) : data(a) {}

        // ------------------------------------------------------------------
        // Element access
        // ------------------------------------------------------------------

        constexpr T &      operator[](std::size_t i)       { return data[i]; }
        constexpr const T &operator[](std::size_t i) const { return data[i]; }

        constexpr T x() const { static_assert(N >= 1); return data[0]; }
        constexpr T y() const { static_assert(N >= 2); return data[1]; }
        constexpr T z() const { static_assert(N >= 3); return data[2]; }
        constexpr T w() const { static_assert(N >= 4); return data[3]; }

        // ------------------------------------------------------------------
        // Arithmetic — element-wise
        // ------------------------------------------------------------------

        constexpr vec operator+(const vec &o) const
        {
            vec r;
            for (std::size_t i = 0; i < N; ++i) r[i] = data[i] + o[i];
            return r;
        }

        constexpr vec operator-(const vec &o) const
        {
            vec r;
            for (std::size_t i = 0; i < N; ++i) r[i] = data[i] - o[i];
            return r;
        }

        constexpr vec operator*(T s) const
        {
            vec r;
            for (std::size_t i = 0; i < N; ++i) r[i] = data[i] * s;
            return r;
        }

        constexpr vec operator/(T s) const
        {
            vec r;
            for (std::size_t i = 0; i < N; ++i) r[i] = data[i] / s;
            return r;
        }

        constexpr vec &operator+=(const vec &o) { *this = *this + o; return *this; }
        constexpr vec &operator-=(const vec &o) { *this = *this - o; return *this; }
        constexpr vec &operator*=(T s)          { *this = *this * s; return *this; }
        constexpr vec &operator/=(T s)          { *this = *this / s; return *this; }

        constexpr vec operator-() const
        {
            vec r;
            for (std::size_t i = 0; i < N; ++i) r[i] = -data[i];
            return r;
        }

        // ------------------------------------------------------------------
        // Comparison
        // ------------------------------------------------------------------

        constexpr bool operator==(const vec &o) const { return data == o.data; }
        constexpr bool operator!=(const vec &o) const { return data != o.data; }

        // ------------------------------------------------------------------
        // Geometric operations
        // ------------------------------------------------------------------

        /// Squared Euclidean norm — avoids the sqrt.
        constexpr T norm2() const
        {
            T acc{};
            for (std::size_t i = 0; i < N; ++i) acc += data[i] * data[i];
            return acc;
        }

        /// Euclidean norm.
        double norm() const { return std::sqrt(static_cast<double>(norm2())); }

        /**
         * @brief Return a unit vector in the same direction.
         *
         * Returns the zero vector when the norm is (near) zero to avoid
         * division by zero.
         */
        vec<N, double> normalized() const
        {
            const double n = norm();
            vec<N, double> r;
            for (std::size_t i = 0; i < N; ++i)
                r[i] = (n > 0.0) ? static_cast<double>(data[i]) / n : 0.0;
            return r;
        }

        // ------------------------------------------------------------------
        // Iterators (for range-for and STL algorithms)
        // ------------------------------------------------------------------

        constexpr auto begin()  { return data.begin();  }
        constexpr auto end()    { return data.end();    }
        constexpr auto begin()  const { return data.begin();  }
        constexpr auto end()    const { return data.end();    }
        constexpr auto cbegin() const { return data.cbegin(); }
        constexpr auto cend()   const { return data.cend();   }
    };

    // =========================================================================
    // Free functions
    // =========================================================================

    /// Dot product of two vectors.
    template <std::size_t N, typename T>
    constexpr T dot(const vec<N, T> &a, const vec<N, T> &b)
    {
        T acc{};
        for (std::size_t i = 0; i < N; ++i) acc += a[i] * b[i];
        return acc;
    }

    /**
     * @brief 3-D cross product.
     *
     * Only available for N == 3.  Returns a new vec<3, T>.
     */
    template <typename T>
    constexpr vec<3, T> cross(const vec<3, T> &a, const vec<3, T> &b)
    {
        return {a[1] * b[2] - a[2] * b[1],
                a[2] * b[0] - a[0] * b[2],
                a[0] * b[1] - a[1] * b[0]};
    }

    /// Euclidean distance between two vectors.
    template <std::size_t N, typename T>
    double distance(const vec<N, T> &a, const vec<N, T> &b)
    {
        return (a - b).norm();
    }

    /// Scalar × vector (left-hand side scalar).
    template <std::size_t N, typename T>
    constexpr vec<N, T> operator*(T s, const vec<N, T> &v)
    {
        return v * s;
    }

    /// Stream output: "(x, y, z, …)".
    template <std::size_t N, typename T>
    std::ostream &operator<<(std::ostream &os, const vec<N, T> &v)
    {
        os << "(";
        for (std::size_t i = 0; i < N; ++i)
        {
            os << v[i];
            if (i + 1 < N) os << ", ";
        }
        os << ")";
        return os;
    }

    // =========================================================================
    // Convenience aliases
    // =========================================================================

    using vec2f = vec<2, float>;
    using vec3f = vec<3, float>;
    using vec2d = vec<2, double>;
    using vec3d = vec<3, double>;
    using vec2i = vec<2, int>;
    using vec3i = vec<3, int>;

} // namespace mist
