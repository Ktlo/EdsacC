#include "parser.hpp"
#include "arguments.hpp"

#include <iostream>
#include <fstream>

int main(int argn, const char ** args) {
    using namespace edsac;
    arguments.init(argn, args);
    std::istream * in;
    std::ostream * out;
    if (arguments.input.empty())
        in = &std::cin;
    else
        in = new std::ifstream(arguments.input);
    if (arguments.output.empty())
        out = &std::cout;
    else
        out = new std::ofstream(arguments.output);
    edsac::parser p(*in, *out);
    p.parse(std::cerr);
    if (!arguments.input.empty())
        delete in;
    if (!arguments.output.empty())
        delete out;
}
