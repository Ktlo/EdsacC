#ifndef ARGUMENTS_H
#define ARGUMENTS_H

#include <vector>
#include <string>

namespace edsac {

struct arguments_t {
    int io = 2;
    std::string input;
    std::string output;
    bool help = false;
    bool debug = false;
    std::vector<std::string> other;
    void init(int argn, const char ** args);
} extern arguments;

} // edsac


#endif // ARGUMENTS_H
