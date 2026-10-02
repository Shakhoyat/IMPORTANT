// Shared helpers for the automated tests: every check records id, description, expected, observed, PASS/FAIL.
// Test files register a group with TS_TEST_GROUP(name) { ... } and are run after the built-in groups,
// in file link order. IDs: T1-T59 test_main.cpp, T60-T69 bunker / walk space, T70-T79 base, T80-T89 ground
// vehicles, T90-T99 air + sensors, T100-T119 combat, T120-T129 app / camera / control.
#pragma once

#include <cstdarg>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace ts::testkit {

struct Result {
    std::string id, description, expected, observed;
    bool pass;
};

inline std::vector<Result>& results() {
    static std::vector<Result> r;
    return r;
}

inline void record(const std::string& id, const std::string& desc, const std::string& expected,
                   const std::string& observed, bool pass) {
    results().push_back({id, desc, expected, observed, pass});
    std::printf("[%s] %-5s %s\n      expected: %s\n      observed: %s\n", pass ? "PASS" : "FAIL", id.c_str(),
                desc.c_str(), expected.c_str(), observed.c_str());
}

inline std::string fmt(const char* f, ...) {
    char buf[512];
    va_list args;
    va_start(args, f);
    std::vsnprintf(buf, sizeof(buf), f, args);
    va_end(args);
    return buf;
}

using GroupFn = void (*)();
inline std::vector<std::pair<const char*, GroupFn>>& groups() {
    static std::vector<std::pair<const char*, GroupFn>> g;
    return g;
}
struct Registrar {
    Registrar(const char* name, GroupFn fn) { groups().emplace_back(name, fn); }
};

}  // namespace ts::testkit

#define TS_TEST_CAT2(a, b) a##b
#define TS_TEST_CAT(a, b) TS_TEST_CAT2(a, b)
#define TS_TEST_GROUP(name)                                                                       \
    static void TS_TEST_CAT(tsTestGroup_, name)();                                                \
    static ::ts::testkit::Registrar TS_TEST_CAT(tsTestReg_, name)(#name, &TS_TEST_CAT(tsTestGroup_, name)); \
    static void TS_TEST_CAT(tsTestGroup_, name)()
