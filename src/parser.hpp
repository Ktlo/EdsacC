#include <istream>
#include <ostream>

namespace edsac {

class parser {
private:
    std::istream & input;
    std::ostream & output;

public:
    constexpr parser(std::istream & in, std::ostream & out) : input(in), output(out) {}

    int parse(std::ostream & err);
};

}
