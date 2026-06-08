// SPDX-License-Identifier: MIT
//
// tester_io.cxx — exercises mist::io delimited-text readers.

#include <mist/io.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool cond, const char* what) {
    if (!cond) { std::printf("  FAIL: %s\n", what); ++failures; }
}

void write_file(const char* path, const char* content) {
    std::ofstream f(path);
    f << content;
}

}  // namespace

int main() {
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
        write_file("tester_io_tmp3.csv", "a,b,c\n1,2\n");  // short row
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

    mist::logger::set_min_level(prev);

    if (failures) {
        std::printf("[tester_io] %d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("[tester_io] OK");
    return EXIT_SUCCESS;
}
