// SPDX-License-Identifier: MIT
#pragma once

/**
 * @file time.h
 * @brief Parse/format the compact "YYYYMMDD-HHMMSS" timestamp.
 *
 * ROOT-free, header-only. The format is the one commonly used in run/log file
 * names. Conversions go through the C calendar functions in local time, so a
 * value round-trips (parse → to_string) within the same time zone. See
 * @ref mist::time.
 */

#include <ctime>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>

namespace mist::time
{
    /// The timestamp format string: 8-digit date, dash, 6-digit time.
    inline constexpr const char *kFormat = "%Y%m%d-%H%M%S";

    /**
     * @brief Parse "YYYYMMDD-HHMMSS" into a @c std::time_t.
     * @param text Timestamp string.
     * @return The time, or @c std::nullopt if @p text does not match the format
     *         or is not a representable calendar time.
     */
    [[nodiscard]] inline std::optional<std::time_t> parse(const std::string &text)
    {
        std::tm tm{};
        std::istringstream stream(text);
        stream >> std::get_time(&tm, kFormat);
        if (stream.fail())
            return std::nullopt;
        tm.tm_isdst = -1; // let mktime determine DST
        const std::time_t t = std::mktime(&tm);
        if (t == static_cast<std::time_t>(-1))
            return std::nullopt;
        return t;
    }

    /**
     * @brief Format a @c std::time_t as "YYYYMMDD-HHMMSS" (local time).
     * @param t Time value.
     * @return The formatted string, or an empty string if @p t cannot be
     *         converted to a local calendar time.
     */
    [[nodiscard]] inline std::string to_string(std::time_t t)
    {
        const std::tm *tm = std::localtime(&t);
        if (!tm)
            return {};
        char buffer[32];
        if (std::strftime(buffer, sizeof(buffer), kFormat, tm) == 0)
            return {};
        return std::string(buffer);
    }

} // namespace mist::time
