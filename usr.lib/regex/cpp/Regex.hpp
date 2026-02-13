#ifndef SUBSTRATE_REGEX_HPP
#define SUBSTRATE_REGEX_HPP

#include <string>
#include <vector>
#include <stdexcept>

extern "C" {
#include <regex.h>
}

namespace substrate {

class Regex {
public:
    explicit Regex(const std::string &pattern, unsigned flags = REGEX_FLAG_EXTENDED)
        : re_(nullptr) {
        regex_err_t err;
        re_ = regex_compile(pattern.c_str(), flags, &err);
        if (!re_) {
            throw std::runtime_error("regex_compile failed");
        }
    }

    ~Regex() {
        regex_free(re_);
    }

    Regex(const Regex &) = delete;
    Regex &operator=(const Regex &) = delete;

    bool match(const std::string &text, std::vector<size_t> *caps = nullptr) const {
        regex_err_t err;
        size_t cap_count = regex_capture_count(re_) * 2;
        std::vector<size_t> local;
        size_t *out = nullptr;
        if (caps) {
            caps->assign(cap_count, (size_t)-1);
            out = caps->data();
        } else {
            local.assign(cap_count, (size_t)-1);
            out = local.data();
        }
        return regex_match(re_, text.c_str(), text.size(), out, cap_count, &err) >= 0;
    }

private:
    regex_t *re_;
};

} /* namespace substrate */

#endif
