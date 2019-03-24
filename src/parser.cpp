#include "parser.hpp"

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <stdexcept>
#include <variant>
#include <cctype>
#include <cstring>
#include <utility>

#include "arguments.hpp"

namespace edsac {

thread_local std::ostream * err;

struct predicate_t {
	virtual int initialize(int inst_n, std::unordered_map<std::string, int> & vars) = 0;
	virtual void resolve(const std::unordered_map<std::string, int> & vars) = 0;
	virtual std::ostream & write_to(std::ostream & out) const = 0;
	virtual ~predicate_t() {}
};

template <typename T>
T min(T a, T b) {
	return a > b ? b : a;
}

int find_word_end(const char * str) {
	int i;
	for (i = 0; !std::isspace(str[i]) && str[i] != 0; i++);
	return i;
}

int find_char(const char * str, char c) {
	int i = 0;
	for (i = 0; str[i] != c; i++)
		if (str[i] == 0)
			throw std::runtime_error(std::string("EOF reached, can't find character '") + c + "'");
	return i;
}

inline void skip_space(const char * str, int & i) {
	while(std::isspace(str[i])) i++;
}

int find_last_bracket(const char * str) {
	char bracket;
	switch (*str) {
	case '[':
		bracket = ']';
		break;
	case '{':
		bracket = '}';
		break;
	case '(':
		bracket = ')';
		break;
	default:
		throw std::invalid_argument(std::string("wrong bracket character '") + *str + "'");
	}
	int i;
	for (i = 0; str[i] != bracket && str[i] != 0; i++)
		if (str[i] == 0)
			throw std::runtime_error(std::string("EOF reached, can't find closing bracket '") + bracket + "'");
	return i;
}

int read_int(const char * str, int & value) {
	value = 0;
	bool minus = false;
	int i = 0;
	if (minus = *str == '-')
		i++;
	for (; std::isdigit(str[i]); i++)
		value = value*10 + str[i] - '0';
	if (minus)
		value = -value;
	return i;
}

struct var_predicate final : public predicate_t {
	std::string name;
	var_predicate(const std::string & n) : name(n) {};
	virtual int initialize(int inst_n, std::unordered_map<std::string, int> & vars) override;
	virtual void resolve(const std::unordered_map<std::string, int> & vars) override;
	virtual std::ostream & write_to(std::ostream & out) const override;
};

int parse_as_var(const char * str, std::vector<std::unique_ptr<predicate_t>> & predicates) {
	int i = 0;
	if (str[0] == '$') {
		i++;
		skip_space(str, i);
		int sz = min(find_word_end(str + i), find_char(str + i, '='));
		predicates.push_back(std::make_unique<var_predicate>(std::string(str + i, sz)));
		return i + sz;
	}
	int offset = *str == ':';
	if (std::isspace(str[offset]))
		throw std::runtime_error("unexpected space character before variable name");
	int sz = find_char(str + offset, ':');
	int j = i + sz + offset;
	if (str[j] != ':')
		throw std::runtime_error(std::string("unexpected symbol after variable name '") + str[j] + "'");
	predicates.push_back(std::make_unique<var_predicate>(std::string(str + offset, sz)));
	return j + 1;
}

int var_predicate::initialize(int inst_n, std::unordered_map<std::string, int> & vars) {
	if (vars.find(name) != vars.end())
		throw std::runtime_error("variable '" + name + "' already exists");
	vars[name] = inst_n;
	return inst_n;
}

void var_predicate::resolve(const std::unordered_map<std::string, int> & vars) {}

std::ostream & var_predicate::write_to(std::ostream & out) const {
	if (arguments.debug)
		out << '[' << name << ":]" << std::endl;
	return out;
}

static int offset = 0;
const std::string char_table = "PQWERTYUIOJ#SZK*.F@D!HNM&LXGABCV";

struct command_predicate : public predicate_t {
	char prefix;
	std::variant<std::string, int> addr;
	bool is_long;
	char suffics;
	int inst_address;
	command_predicate(char pre, const std::variant<std::string, int> & a, char post, bool l) :
		prefix(pre), addr(a), suffics(post), is_long(l) {}
	virtual void resolve(const std::unordered_map<std::string, int> & vars) final override;
	virtual std::ostream & write_to(std::ostream & out) const override;
};

void command_predicate::resolve(const std::unordered_map<std::string, int> & vars) {
	if (std::holds_alternative<std::string>(addr)) {
		const std::string & name = std::get<std::string>(addr);
		auto iter = vars.find(name);
		if (iter == vars.end())
			throw std::runtime_error("no such variable '" + name + "'");
		if (arguments.io == 2) {
			int i = char_table.find_first_of(suffics, 17);
			int a = iter->second;
			if (suffics == 'F' || suffics == 'K') {
				// step 5
			} else if (suffics == '@' || suffics == 'Z') {
				// step 7
				a += - offset;
			} else
				*err << "link time warning: can't link properly \"" << prefix << ' ' << iter->first << ' ' << suffics << "\" "
				"suffix must be F, K, @ or Z";
			if (a < 0)
				throw std::runtime_error(std::string("link result address is lower than 0. "
					"Did you reference to the variable that is out of the scope? Instruction: \"") +
					prefix + ' ' + iter->first + ' ' + suffics + "\"");
			addr = a;
		} else
			addr = iter->second;
	}
	if (prefix == 'G') {
		if (suffics == 'K' || suffics == 'Z') {
			offset = std::get<int>(addr) + inst_address;
		}
		if (suffics == 'Z')
			offset += inst_address;
	}
}

std::ostream & command_predicate::write_to(std::ostream & out) const {
	int address = std::get<int>(addr);
	out << prefix;
	if (address)
		out << address;
	if (is_long)
		out << '#';
	return out << suffics;
}

struct inst_predicate final : public command_predicate {
	inst_predicate(char pre, const std::variant<std::string, int> & a, char post, bool l) :
		command_predicate(pre, a, post, l) {}
	virtual int initialize(int inst_n, std::unordered_map<std::string, int> & vars) override;
	virtual std::ostream & write_to(std::ostream & out) const override;
};

struct direct_predicate final : public command_predicate {
	direct_predicate(char pre, const std::variant<std::string, int> & a, char post, bool l) :
		command_predicate(pre, a, post, l) {}
	virtual int initialize(int inst_n, std::unordered_map<std::string, int> & vars) override;
	virtual std::ostream & write_to(std::ostream & out) const override;
};

int parse_as_inst(const char * str, std::vector<std::unique_ptr<predicate_t>> & predicates) {
	int i = 0;
	char prefix = str[i++];
	std::variant<std::string, int> addr;
	if (std::isspace(str[i]) || std::isdigit(str[i])) {
		skip_space(str, i);
		if (std::isdigit(str[i])) {
			// regular instruction
			int value;
			i += read_int(str + i, value);
			addr = value;
		} else {
			// instruction with variable
			int sz = find_word_end(str + i);
			char c = str[i + sz - 1];
			if (c == '[') {
				// indexing a variable
				std::string name = std::string(str + i, sz);
				i += sz;
				int j = i + find_last_bracket(str + i - 1);
				skip_space(str, i);
				int k = j - 2;
				while (std::isspace(str[k])) k--;
				std::string var = std::string(str + i, k - i + 1);
				addr = name + var + ']';
				i = j;
			} else {
				addr = std::string(str + i, sz);
				i += sz;
			}
		}
		skip_space(str, i);
	} else
		addr = 0;
	bool is_long = false;
	if (arguments.io == 2 && str[i] == '#') {
		is_long = true;
		i++;
	}
	char suffics = str[i++];
	if (suffics == 'K' || suffics == 'Z')
		predicates.push_back(std::make_unique<direct_predicate>(prefix, addr, suffics, is_long));
	else
		predicates.push_back(std::make_unique<inst_predicate>(prefix, addr, suffics, is_long));
	return i;
}

int inst_predicate::initialize(int inst_n, std::unordered_map<std::string, int> & vars) {
	inst_address = inst_n;
	return inst_n + 1;
}

std::ostream & inst_predicate::write_to(std::ostream & out) const {
	if (arguments.debug)
		out << "    [i " << inst_address << "]";
	command_predicate::write_to(out);
	if (arguments.debug)
		out << std::endl;
	return out;
}

int direct_predicate::initialize(int inst_n, std::unordered_map<std::string, int> & vars) {
	inst_address = inst_n;
	return inst_n;
}

std::ostream & direct_predicate::write_to(std::ostream & out) const {
	if (arguments.debug)
		out << "    [d ~]";
	command_predicate::write_to(out);
	if (arguments.debug)
		out << std::endl;
	return out;
}

struct const_predicate final : public predicate_t {
	std::vector<std::string> inst;
	int inst_address;
	const_predicate(const std::vector<std::string> && i) : inst(std::move(i)) {}
	virtual int initialize(int inst_n, std::unordered_map<std::string, int> & vars) override;
	virtual void resolve(const std::unordered_map<std::string, int> & vars) override;
	virtual std::ostream & write_to(std::ostream & out) const override;
};

int parse_as_const(const char * str,  std::vector<std::unique_ptr<predicate_t>> & predicates) {
	int i = 0;
	std::vector<std::string> inst;
	auto write_integer = [&](int value, char suffix) {
		int first = value >> 17;
		bool is_long = suffix == 'l' || ((abs(value) >> 17) > 0 && suffix != 's');
		int bitS = value & 1;
		int bitL = first & 1;
		first >>= 1;
		value >>= 1;
		if (arguments.io == 2) {
			if (is_long)
				inst.push_back(char_table[(first >> 12) & 0b11111] + std::to_string(first & ((1 << 12) - 1)) +
					(bitL ? 'D' : 'F'));
			inst.push_back(char_table[(value >> 12) & 0b11111] + std::to_string(value & ((1 << 12) - 1)) +
				(bitS ? 'D' : 'F'));
		} else {
			if (is_long)
				inst.push_back(char_table[(first >> 12) & 0b11111] + std::to_string(first & ((1 << 12) - 1)) +
					(bitL ? 'L' : 'S'));
			inst.push_back(char_table[(value >> 12) & 0b11111] + std::to_string(value & ((1 << 12) - 1)) +
				(bitS ? 'L' : 'S'));
		}
	};
	if (str[i] == '=') {
		i++;
		skip_space(str, i);
		if (str[i] == '[' || str[i] == '{') {
			// array literal
			int allocate = -1;
			if (str[i] == '[') {
				int j = i + find_last_bracket(str + i);
				i++;
				skip_space(str, i);
				i += read_int(str + i, allocate);
				if (allocate < 0)
					throw std::runtime_error("can't allocate negative " + std::to_string(allocate) + " number of short elements");
				skip_space(str, i);
				if (i != j)
					throw std::runtime_error("only number literal is supported in allocation array block");
				i++;
				skip_space(str, i);
			}
			if (str[i] == '{') {
				int j = i + find_last_bracket(str + i);
				for (i++, skip_space(str, i); i < j;) {
					int value;
					i += read_int(str + i, value);
					char c = str[i];
					if (c != 's' && c != 'l' && c != ',' && c != '}' && !std::isspace(c))
						throw std::runtime_error(std::string("unexpected character in array initialization block '") + c + "'");
					write_integer(value, c);
					if (c == 's' || c == 'l') i++;
					skip_space(str, i);
					if (i == j)
						break;
					if (str[i] != ',')
						throw std::runtime_error("only integer literals supported in array initialization block");
					i++;
					skip_space(str, i);
				}
				i++;
			}
			if (allocate >= 0) {
				int size = allocate - inst.size();
				if (size < 0)
					throw std::runtime_error("allocated number " + std::to_string(allocate) +
						" lower than initializided " + std::to_string(inst.size()));
				while (size--)
					inst.push_back("PS");
			}
		} else {
			// integer literal
			int const_sz = find_word_end(str + i);
			int j = i + const_sz;
			int value;
			i += read_int(str + i, value);
			char c = str[i];
			if (i + !std::isspace(c) != j)
				throw std::runtime_error(std::string("unexpected character in constant literal '") + c + "'");
			if (c == 's' || c == 'l' || std::isspace(c))
				write_integer(value, c);
			else
				throw std::runtime_error("not implemented constant type");
			i = j;
		}
	} else if(!std::strncmp(str + i, "CONST(", 6)) {
		i += 5;
		int value;
		int j = i + find_last_bracket(str + i);
		i++;
		skip_space(str, i);
		i += read_int(str + i, value);
		skip_space(str, i);
		if (str[i++] != ',')
			throw std::runtime_error("function CONST(int n, char postfix) expects 2 parameters");
		skip_space(str, i);
		char c = str[i++];
		skip_space(str, i);
		if (i != j)
			throw std::runtime_error("closing bracket expected");
		inst.push_back(char_table[value >> 12] + std::to_string(value & ((1 << 12) - 1)) + c);
		i++;
	} else
		throw std::invalid_argument("FATAL OTHER BUGS!!! " + std::string(str + i, 10));
	predicates.push_back(std::make_unique<const_predicate>(std::move(inst)));
	return i;
}

int const_predicate::initialize(int inst_n, std::unordered_map<std::string, int> & vars) {
	inst_address = inst_n;
	return inst_n + inst.size();
}

void const_predicate::resolve(const std::unordered_map<std::string, int> & vars) {}

std::ostream & const_predicate::write_to(std::ostream & out) const {
	if (arguments.debug)
		out << "    [$ " << inst_address << "]";
	int k = 0;
	for (const auto & i : inst) {
		if (arguments.debug)
			out << '[' << k++ << ']';
		out << i;
	}
	if (arguments.debug)
		out << std::endl;
	return out;
}

struct txt_predicate final : public predicate_t {
	std::string text;
	txt_predicate(const std::string & str) : text(str) {}
	virtual int initialize(int inst_n, std::unordered_map<std::string, int> & vars) override;
	virtual void resolve(const std::unordered_map<std::string, int> & vars) override;
	virtual std::ostream & write_to(std::ostream & out) const override;
};

int txt_predicate::initialize(int inst_n, std::unordered_map<std::string, int> & vars) {
	return inst_n;
}

void txt_predicate::resolve(const std::unordered_map<std::string, int> & vars) {}
std::ostream & txt_predicate::write_to(std::ostream & out) const {
	return out << text;
}

std::pair<int, int> count_lines(const char * str, int size) {
	int lines, chars, i;
	for (lines = 0, chars = 0, i = 0; i < size; i++, chars++) {
		if (str[i] == '\r' && str[i + 1] == '\n') {
			// skip
		} else if (str[i] == '\r' || str[i] == '\n') {
			chars = 0;
			lines++;
		}
	}
	return std::pair(lines + 1, chars + 1);
}

inline void next_line(const char * str, int & i) {
	for (; str[i] != '\r' && str[i] != '\n' && str[i]; i++) {
		if (!str[i])
			return;
	}
	i++;
	if (str[i] == '\n') i++;
}

const std::string inst_list = "ASHVNTUCRLEGIOFXYZ" "P";

int parser::parse(std::ostream & err) {
	edsac::err = &err;

	std::vector<std::unique_ptr<predicate_t>> predicates;

	std::string text( (std::istreambuf_iterator<char>(input)), (std::istreambuf_iterator<char>()) );
	const char * str = text.c_str();
	int i, size;
	try {
		for (i = 0, size = text.size(), skip_space(str, i); i < size; skip_space(str, i)) {
			int word_sz = find_word_end(str + i);
			int j = i + word_sz;
			
			// analyze word ending
			switch (str[j - 1]) {
				case ':': {
					// my variable type
					i += parse_as_var(str + i, predicates);
					continue;
				}
				default:
					break;
			}

			// analyze word beggining
			switch (str[i]) {
				case '/': {
					// maybe comment section
					if (str[i+1] == '/') {
						// line comment
						next_line(str, i);
						continue;
					} else if (str[i + 1] == '*') {
						// multiline comment
						for (i+=2; str[i - 1] != '*' || str[i] != '/'; i++) {
							if (!str[i])
								throw std::runtime_error("multiline C style comment not closed");
						}
						i++;
						continue;
					}
				}
				case '[': {
					// edsac comment section
					for (; str[i] != ']'; i++){
						if (!str[i])
							throw std::runtime_error("multiline edsak comment not closed");
					}
					i++;
					continue;
				}
				case ':': {
					// their variable type
					i += parse_as_var(str + i, predicates);
					continue;
				}
				case '$': {
					// my variable type
					i += parse_as_var(str + i, predicates);
					skip_space(str, i);
					i += parse_as_const(str + i, predicates);
					continue;
				}
				case 'C': {
					// maybe const
					if (!std::strncmp(str + i, "CONST(", 6)) {
						i += parse_as_const(str + i, predicates);
						continue;
					}
				}
				default:
					break;
			}

			// maybe instruction
			if (int(inst_list.find_first_of(str[i])) >= 0) {
				i += parse_as_inst(str + i, predicates);
				continue;
			}
			
			// something else
			auto pos = count_lines(str, i);
			std::string word(str + i, word_sz);
			err << "warning:" << pos.first << ':' << pos.second << ": not parsable word \"" + word << "\"" << std::endl;
			predicates.push_back(std::make_unique<txt_predicate>(word));

			i = j;
		}
	} catch (const std::exception & e) {
		auto pair = count_lines(str, i);
		err << "compilation error:" << pair.first << ":" << pair.second << ": " << e.what() << std::endl;
		return 0;
	}
	
	try {
		// initialize
		std::unordered_map<std::string, int> vars;
		int n = (arguments.io == 1) ? 31 : 44;
		for (auto & p : predicates)
			n = p->initialize(n, vars);
		vars["LAST_INSTRUCTION"] = n;
		if (arguments.io == 2) {
			vars["ONE"] = 2;
			vars["RETURN"] = 3;
			vars["ZERO"] = 41;
		}
		// link
		for (auto & p : predicates)
			p->resolve(vars);
		// write
		if (arguments.debug)
			output << "[Initial Orders " << arguments.io << ']' << std::endl;
		for (auto & p : predicates)
			p->write_to(output);
		if (arguments.debug) {
			output << "[-------------]" << std::endl << "[VARS SECTION]" << std::endl;
			for (auto & var : vars) {
				output << "[-> " << var.first << "=" << var.second << "]" << std::endl;
			}
		}
	} catch (const std::exception & e) {
		err << "link time error: " << e.what() << std::endl;
		return 0;
	}
	return 0;
}

}
