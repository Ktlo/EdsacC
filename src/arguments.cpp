#include "arguments.hpp"

#include <cstring>
#include <stdexcept>
#include <limits>

namespace edsac {

arguments_t arguments;

const char * get_arg_value(char const **& it, const char ** end) {
    const char * curr = *it;
    while(char c = *(curr++)) {
        if (c == '=') {
            return curr;
        }
    }
    if (++it != end) {
        return *it;
    }
    throw std::invalid_argument(std::string("program argument '") + *(it - 1) + "' requiers value");
}

bool is_arg_name(const char * arg, const char * name) {
    size_t n = std::strlen(name);
    return !std::strcmp(arg, name) || (!std::strncmp(arg, name, n) && arg[n] == '=');
}

template <typename T>
T assert_arg_range(int x, const char * name) {
    constexpr T max_value = std::numeric_limits<T>::max();
    if (x < 0 || x > max_value)
        throw std::invalid_argument(std::string("'") + name + "' value do not fit in range [0:" + std::to_string(max_value) + "]");
    return static_cast<T>(x);
}

void arguments_t::init(int argn, const char ** args) {
    const char ** end = args + argn;
    for (const char ** it = args + 1; it != end; it++) {
        const char * curr = *it;
        if (!std::strncmp(curr, "--", 2)) {
            // full name conf
            const char * arg = curr + 2;
            if (is_arg_name(arg, "io")) {
                io = std::atoi(get_arg_value(it, end));
                if (io != 1 && io != 2)
                    throw std::invalid_argument("unsupported specification: Initial Orders " + std::to_string(io));
            } else if (is_arg_name(arg, "input"))
                input = get_arg_value(it, end);
            else if (is_arg_name(arg, "output"))
                output = get_arg_value(it, end);
            else if (is_arg_name(arg, "debug"))
                debug = true;
            else if (is_arg_name(arg, "help"))
                help = true;
            else
                throw std::invalid_argument("urecognized argument '" + std::string(arg) + "'");
        } else if (*curr == '-') {
            // literal options
            const char * arg = curr + 1;
            while(char c = *(arg++)) {
                if (c == '1')
                    io = 1;
                else if (c == '2')
                    io = 2;
                else if (c == 'd')
                    debug = true;
                else if (c == 'h')
                    help = true;
                else
                    throw std::invalid_argument(std::string("unrecognized option '") + c + "'");
            }
        } else {
            other.emplace_back(curr);
        }
    }
}

}
