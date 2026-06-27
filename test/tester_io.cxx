// SPDX-License-Identifier: MIT
//
// tester_io.cxx — exercises mist::io delimited-text readers.

#include <mist/io.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace
{

int failures = 0;

void check(bool cond, const char *what)
{
    if (!cond)
    {
        std::printf("  FAIL: %s\n", what);
        ++failures;
    }
}

void write_file(const char *path, const char *content)
{
    std::ofstream f(path);
    f << content;
}

} // namespace

int main()
{
    // Quieten the expected error from the missing-file case.
    const auto prev = mist::logger::get_min_level();

    std::puts("[tester_io] read_csv");
    {
        write_file("tester_io_tmp.csv", "name,x,y\nalpha,1,2\nbeta,3,4\ngamma,5,6\n");
        std::vector<std::string> order;
        const auto t = mist::io::read_csv("tester_io_tmp.csv", &order);
        check(t.size() == 3, "csv: 3 columns");
        check((order == std::vector<std::string>{"name", "x", "y"}), "csv: column order preserved");
        check(t.at("name").size() == 3, "csv: 3 rows");
        check(t.at("name")[1] == "beta", "csv: name[1]");
        check(t.at("x")[2] == "5", "csv: x[2]");
        check(t.at("y")[0] == "2", "csv: y[0]");
        std::remove("tester_io_tmp.csv");
    }

    std::puts("[tester_io] empty fields (CSV preserves)");
    {
        write_file("tester_io_tmp2.csv", "a,b,c\n1,,3\n,5,\n");
        const auto t = mist::io::read_csv("tester_io_tmp2.csv");
        check(t.at("b")[0] == "", "csv: empty middle field preserved");
        check(t.at("a")[1] == "", "csv: empty leading field preserved");
        check(t.at("c")[1] == "", "csv: empty trailing field preserved");
        std::remove("tester_io_tmp2.csv");
    }

    std::puts("[tester_io] read_txt (whitespace collapses)");
    {
        write_file("tester_io_tmp.txt", "col1   col2\t col3\n  10   20   30\n40\t50\t60\n");
        const auto t = mist::io::read_txt("tester_io_tmp.txt");
        check(t.size() == 3, "txt: 3 columns");
        check(t.at("col1").size() == 2, "txt: 2 rows");
        check(t.at("col2")[0] == "20", "txt: collapses runs of whitespace");
        check(t.at("col3")[1] == "60", "txt: tab-separated");
        std::remove("tester_io_tmp.txt");
    }

    std::puts("[tester_io] ragged rows pad with empty");
    {
        write_file("tester_io_tmp3.csv", "a,b,c\n1,2\n"); // short row
        const auto t = mist::io::read_csv("tester_io_tmp3.csv");
        check(t.at("a")[0] == "1", "ragged: a kept");
        check(t.at("c")[0] == "", "ragged: missing c -> empty");
        std::remove("tester_io_tmp3.csv");
    }

    std::puts("[tester_io] missing file -> empty (no throw)");
    {
        const auto t = mist::io::read_csv("does_not_exist_xyz.csv");
        check(t.empty(), "missing file -> empty map");
    }

    std::puts("[tester_io] get<T> and get_column<T>");
    {
        write_file("tester_io_get.csv", "name,count,value\nalpha,3,1.5\nbeta,7,2.5\n");
        const auto t = mist::io::read_csv("tester_io_get.csv");

        // Typed cell access
        check(mist::io::get<std::string>(t, "name", 0) == "alpha", "get<string>: row 0");
        check(mist::io::get<std::string>(t, "name", 1) == "beta", "get<string>: row 1");
        check(mist::io::get<int>(t, "count", 0) == 3, "get<int>: row 0");
        check(mist::io::get<int>(t, "count", 1) == 7, "get<int>: row 1");
        check(std::fabs(mist::io::get<double>(t, "value", 0) - 1.5) < 1e-9, "get<double>: row 0");
        check(std::fabs(mist::io::get<double>(t, "value", 1) - 2.5) < 1e-9, "get<double>: row 1");

        // Missing column returns T{}
        check(mist::io::get<int>(t, "nonexistent", 0) == 0, "get<T>: missing col -> T{}");
        check(mist::io::get<std::string>(t, "gone", 0) == "", "get<string>: missing col -> empty");

        // Out-of-range row returns T{}
        check(mist::io::get<std::string>(t, "name", 99) == "", "get<T>: out-of-range row -> T{}");
        check(mist::io::get<int>(t, "count", 99) == 0, "get<int>: out-of-range row -> T{}");

        // get_column — full column extraction
        const auto names = mist::io::get_column<std::string>(t, "name");
        check(names.size() == 2, "get_column<string>: size");
        check(names[0] == "alpha", "get_column<string>: [0]");
        check(names[1] == "beta", "get_column<string>: [1]");

        const auto counts = mist::io::get_column<int>(t, "count");
        check(counts.size() == 2, "get_column<int>: size");
        check(counts[0] == 3, "get_column<int>: [0]");
        check(counts[1] == 7, "get_column<int>: [1]");

        const auto values = mist::io::get_column<double>(t, "value");
        check(values.size() == 2, "get_column<double>: size");
        check(std::fabs(values[0] - 1.5) < 1e-9, "get_column<double>: [0]");
        check(std::fabs(values[1] - 2.5) < 1e-9, "get_column<double>: [1]");

        // Missing column returns empty vector
        check(mist::io::get_column<double>(t, "missing").empty(), "get_column: missing col -> empty");

        std::remove("tester_io_get.csv");
    }

    mist::logger::set_min_level(prev);

    if (failures)
    {
        std::printf("[tester_io] %d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("[tester_io] OK");
    return EXIT_SUCCESS;
}
