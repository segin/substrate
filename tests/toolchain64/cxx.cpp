/*
 * cxx.cpp - a C++ program for the x86_64-unknown-substrate cross compiler
 * (tests/toolchain64/check.sh).  It needs the C++ runtime end to end:
 * iostreams, std::string and containers, a virtual call, a thread_local
 * with a constructor, and an exception thrown through a frame with a
 * destructor and caught by type -- which is what exercises the unwinder
 * (libgcc_s, PT_GNU_EH_FRAME, dl_iterate_phdr in ld64.so).
 */
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

static int destroyed;

struct Guard {
    ~Guard() { destroyed++; }
};

struct Shape {
    virtual ~Shape() {}
    virtual long area() const = 0;
};

struct Rect : Shape {
    long w, h;
    Rect(long w_, long h_) : w(w_), h(h_) {}
    long area() const override { return w * h; }
};

struct Counter {
    long n;
    Counter() : n(40) {}
};

static thread_local Counter tls_counter;

static long checked_div(long a, long b) {
    Guard g;
    if (b == 0) throw std::invalid_argument("division by zero");
    return a / b;
}

int main() {
    int bad = 0;

    std::vector<Shape *> shapes;
    shapes.push_back(new Rect(6, 7));
    long area = shapes[0]->area();
    delete shapes[0];

    std::map<std::string, int> m;
    m["one"] = 1;
    m["two"] = 2;
    std::string joined;
    for (const auto &kv : m) joined += kv.first + "=" + std::to_string(kv.second) + ";";

    tls_counter.n += 2;

    bool caught = false;
    std::string what;
    try {
        checked_div(1, 0);
    } catch (const std::invalid_argument &e) {
        caught = true;
        what = e.what();
    } catch (...) {
        what = "wrong handler";
    }

    std::cout << "area=" << area << " map=" << joined
              << " tls=" << tls_counter.n << " caught=" << caught
              << " what=" << what << " destroyed=" << destroyed << std::endl;

    if (area != 42 || joined != "one=1;two=2;" || tls_counter.n != 42) bad = 1;
    if (!caught || what != "division by zero" || destroyed != 1) bad = 1;
    if (checked_div(84, 2) != 42 || destroyed != 2) bad = 1;

    std::cout << "cxx: " << (bad ? "FAIL" : "OK") << std::endl;
    return bad;
}
