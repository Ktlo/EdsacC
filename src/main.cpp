#include "parser.hpp"
#include "arguments.hpp"

#include <iostream>
#include <fstream>

int main(int argn, const char ** args) {
    using namespace edsac;
    arguments.init(argn, args);
    if (arguments.help) {
        using namespace std;
        cout << *args << " [-12dh] [--help] [--io <1|2>] [--debug] [--input <input_filename>] [--output <output_filename>]" << endl;
        cout << "\t-h, --help             shows this help and quits" << endl;
        cout << "\t-1, --io=1             specify \"Initial Orders 1\" for the program" << endl;
        cout << "\t-2, --io=2             specify \"Initial Orders 2\" for the program (default)" << endl;
        cout << "\t    --input=<file>     specify program file (will use stdin if not pointed)" << endl;
        cout << "\t    --output=<file>    specify result program for EDSAC Simulator (stdout by default)" << endl;
        cout << "\t-d, --debug            output some helpfull information in comments within programm" << endl;
        return 0;
    }
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
    int r = p.parse(std::cerr);
    if (!arguments.input.empty())
        delete in;
    if (!arguments.output.empty())
        delete out;
    return r;
}
