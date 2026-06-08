// SPDX-License-Identifier: MIT
#pragma once

/**
 * @file io.h
 * @brief Minimal delimited-text (CSV / whitespace TXT) reader.
 *
 * Reads a table whose first row is a header into a column-keyed map. ROOT-free,
 * header-only. Diagnostics route through @ref mist::logger; a missing or
 * unreadable file is logged and yields an empty result — never an exception.
 * See @ref mist::io.
 */

#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <mist/logger/logger.h>

namespace mist::io
{
    namespace detail
    {
        /// Split @p line on any character in @p delimiters.
        /// @param line        The text to split.
        /// @param delimiters  Characters treated as field separators.
        /// @param collapse  When true, runs of delimiters are one separator and
        ///                  leading/trailing delimiters produce no empty fields
        ///                  (whitespace-TXT semantics). When false, every
        ///                  delimiter is a field boundary and empty fields are
        ///                  preserved (CSV semantics).
        [[nodiscard]] inline std::vector<std::string>
        split(std::string_view line, std::string_view delimiters, bool collapse)
        {
            std::vector<std::string> fields;
            std::string current;
            const auto flush = [&]
            {
                fields.push_back(current);
                current.clear();
            };
            for (const char c : line)
            {
                if (delimiters.find(c) != std::string_view::npos)
                {
                    if (collapse)
                    {
                        if (!current.empty()) flush();
                    }
                    else
                    {
                        flush();
                    }
                }
                else
                {
                    current.push_back(c);
                }
            }
            if (!collapse || !current.empty()) flush();
            return fields;
        }
    } // namespace detail

    /**
     * @brief Read a delimited file into a column-keyed table.
     *
     * The first non-empty row is the header; its fields become the map keys.
     * Each subsequent row is split into fields and appended column-wise. Rows
     * with fewer fields than the header pad the missing columns with empty
     * strings; extra trailing fields are ignored.
     *
     * @param path          File path.
     * @param delimiters    Characters that separate fields.
     * @param collapse      See @ref detail::split (CSV: false; whitespace: true).
     * @param header_order  Optional out-parameter receiving the column names in
     *                      file order (a @c std::map does not preserve it).
     * @return Map from column name to that column's values, in row order. Empty
     *         if the file cannot be opened (an error is logged) or has no header.
     */
    [[nodiscard]] inline std::map<std::string, std::vector<std::string>>
    read_delimited(const std::string &path, std::string_view delimiters,
                   bool collapse,
                   std::vector<std::string> *header_order = nullptr)
    {
        std::map<std::string, std::vector<std::string>> table;
        if (header_order) header_order->clear();

        std::ifstream file(path);
        if (!file)
        {
            mist::logger::error("(mist::io) cannot open file: " + path);
            return table;
        }

        std::vector<std::string> columns;
        std::string line;
        bool have_header = false;
        while (std::getline(file, line))
        {
            // Skip blank lines (including a leading blank before the header).
            if (line.find_first_not_of(" \t\r\n") == std::string::npos) continue;
            // Strip a trailing CR (CRLF files).
            if (!line.empty() && line.back() == '\r') line.pop_back();

            auto fields = detail::split(line, delimiters, collapse);
            if (!have_header)
            {
                columns = std::move(fields);
                for (const auto &name : columns) table.emplace(name, std::vector<std::string>{});
                if (header_order) *header_order = columns;
                have_header = true;
                continue;
            }
            for (std::size_t i = 0; i < columns.size(); ++i)
                table[columns[i]].push_back(i < fields.size() ? fields[i] : std::string{});
        }

        if (!have_header)
            mist::logger::warning("(mist::io) no header row found in: " + path);
        return table;
    }

    /// Read a comma-separated file (empty fields preserved).
    [[nodiscard]] inline std::map<std::string, std::vector<std::string>>
    read_csv(const std::string &path, std::vector<std::string> *header_order = nullptr)
    {
        return read_delimited(path, ",", /*collapse=*/false, header_order);
    }

    /// Read a whitespace-separated file (runs of whitespace collapse to one).
    [[nodiscard]] inline std::map<std::string, std::vector<std::string>>
    read_txt(const std::string &path, std::vector<std::string> *header_order = nullptr)
    {
        return read_delimited(path, " \t", /*collapse=*/true, header_order);
    }

} // namespace mist::io
