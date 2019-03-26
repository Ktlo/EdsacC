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
#include <tuple>
#include <sstream>

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

const std::string tmp_name = "edsacc#tmp";

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
	int index;
	command_predicate(char pre, const std::variant<std::string, int> & a, char post, bool l) :
		prefix(pre), addr(a), suffics(post), is_long(l), index(0) {}
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
			int a = iter->second + index;
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
			addr = iter->second + index;
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
	int index_value = 0;
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
			int j;
			char c;
			for (j = 0; j < sz; j++) {
				c = str[i + j];
				if (c == '[') // indexer
					break;
			}
			if (c == '[') {
				// indexing a variable
				std::string name = std::string(str + i, j);
				i += 1 + j;
				skip_space(str, i);
				if (std::isdigit(str[i])) {
					// static offset
					i += read_int(str + i, index_value);
					if (str[i] != ']' && !std::isspace(str[i]))
						throw std::runtime_error(std::string("unexpected character in array index '") + str[i] + "'");
					skip_space(str, i);
					if (str[i] != ']')
						throw std::runtime_error("closing ']' expected in array index");
					i++;
					addr = name;
				} else if (str[i] == ']') {
					throw std::runtime_error("empty array index brackets");
				} else {
					// named variable index
					j = min(find_char(str + i, ']'), find_word_end(str + i));
					std::string index = std::string(str + i, j);
					i += j;
					skip_space(str, i);
					if (str[i] != ']')
						throw std::runtime_error("closing ']' expected in array index");
					i++;
					// insert index code block
					char s = arguments.io == 2 ? 'F' : 'S';
					if (prefix == 'A' || prefix == 'S' || prefix == 'T' || prefix == 'U') {
						// get or set value
						const char * op = prefix == 'A' ? "add" : (prefix == 'S' ? "sub" : (prefix == 'T' ? "store" : "save"));
						predicates.push_back(std::make_unique<inst_predicate>('T', tmp_name, s, false));
						predicates.push_back(std::make_unique<inst_predicate>('A', index, s, false));
						predicates.push_back(std::make_unique<inst_predicate>('L', 0, arguments.io == 2 ? 'D' : 'L', false));
						predicates.push_back(std::make_unique<inst_predicate>('A', name + '#' + op, s, false));
						std::string var = name +"#mod#" + std::to_string(predicates.size());
						predicates.push_back(std::make_unique<inst_predicate>('T', var, s, false));
						predicates.push_back(std::make_unique<inst_predicate>('A', tmp_name, s, false));
						predicates.push_back(std::make_unique<var_predicate>(var));
						predicates.push_back(std::make_unique<inst_predicate>('P', 0, s, false));
					} else
						throw std::runtime_error(std::string("operation '") + prefix + "' does not support indexing");
					index_value = -1;
				}
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
	if (suffics == 'K' || suffics == 'Z') {
		if (index_value)
			throw std::runtime_error("directives don't support indexing");
		predicates.push_back(std::make_unique<direct_predicate>(prefix, addr, suffics, is_long));
	} else if (index_value != -1) {
		std::unique_ptr<inst_predicate> ptr = std::make_unique<inst_predicate>(prefix, addr, suffics, is_long);
		ptr->index = index_value;
		predicates.push_back(std::move(ptr));
	}
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

static bool is_tmp_not_created = true;

void ensure_tmp_created(std::vector<std::unique_ptr<predicate_t>> & predicates) {
	if (is_tmp_not_created) {
		predicates.push_back(std::make_unique<var_predicate>(tmp_name));
		predicates.push_back(std::make_unique<inst_predicate>('P', 0, (arguments.io == 2) ? 'F' : 'S', false));
		is_tmp_not_created = false;
	}
}

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
			skip_space(str, i);
			char c = str[i];
			static const std::string modifiers = "asturw";
			if (int(modifiers.find_first_of(c)) >= 0) {
				// Add variables for indexing and storing
				const var_predicate & var = dynamic_cast<const var_predicate &>(*predicates.back());
				predicates.push_back(std::make_unique<const_predicate>(std::move(inst)));
				ensure_tmp_created(predicates);
				int sz = find_word_end(str + i);
				char suffics = (arguments.io == 2) ? 'F' : 'S';
				for (; sz--; i++) {
					c = str[i];
					switch (c) {
						case 'a':
						case 'r':
							// create add op for array
							predicates.push_back(std::make_unique<var_predicate>(var.name + "#add"));
							predicates.push_back(std::make_unique<inst_predicate>('A', var.name, suffics, false));
							break;
						case 's':
							// create sub op for array
							predicates.push_back(std::make_unique<var_predicate>(var.name + "#sub"));
							predicates.push_back(std::make_unique<inst_predicate>('S', var.name, suffics, false));
							break;
						case 't':
						case 'w':
							// create store op for array
							predicates.push_back(std::make_unique<var_predicate>(var.name + "#store"));
							predicates.push_back(std::make_unique<inst_predicate>('T', var.name, suffics, false));
							break;
						case 'u':
							// create save op for array
							predicates.push_back(std::make_unique<var_predicate>(var.name + "#save"));
							predicates.push_back(std::make_unique<inst_predicate>('U', var.name, suffics, false));
							break;
						default:
							throw std::runtime_error(std::string("this operation is not supported for array (op code '") + c + "')");
					}
				}
				return i;
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
		out << "    [$ " << inst_address << "] ";
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

enum class layer_t {
	for_loop
};

int parser::parse(std::ostream & err) {
	edsac::err = &err;

	std::vector<std::unique_ptr<predicate_t>> predicates;
	std::unordered_map<std::string, std::string> defines;
	std::vector<std::tuple<layer_t, std::string, std::string>> stack;

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
				case 'f': {
					// maybe for
					if (!std::strncmp(str + i, "for", 3) && std::isspace(str[i + 3])) {
						i += 3;
						skip_space(str, i);
						char s = arguments.io == 2 ? 'F' : 'S';
						bool create_var = str[i] == '$';
						if (create_var) i++;
						int sz = min(min(find_word_end(str + i), find_char(str + i, ',')),find_char(str + i, '='));
						if (sz == 0)
							throw std::runtime_error("new variable name is empty");
						std::string var = std::string(str + i, sz);
						i += sz;
						if (create_var) {
							//create new var
							std::string point = "for#new_var#" + std::to_string(predicates.size());
							predicates.push_back(std::make_unique<inst_predicate>('E', point, s, false));
							predicates.push_back(std::make_unique<inst_predicate>('G', point, s, false));
							predicates.push_back(std::make_unique<var_predicate>(var));
							predicates.push_back(std::make_unique<const_predicate>(std::vector<std::string> { std::string("P") + s } ));
							predicates.push_back(std::make_unique<var_predicate>(point));
						}
						skip_space(str, i);
						if (create_var && str[i] != '=')
							throw std::runtime_error("new var must be initialized");
						if (str[i] == '=') {
							i++;
							skip_space(str, i);
							int value;
							i += read_int(str + i, value);
							bool bit = value & 1;
							value >>= 1;
							std::string inst = char_table[value >> 12] + std::to_string(value) +
								((arguments.io == 2) ? (bit ? 'D' : 'F') : (bit ? 'L' : 'S'));
							if (!std::isspace(str[i]) && str[i] != ',')
								throw std::runtime_error("unexpected symbol in for loop initialisation");
							// create const
							std::string point = "for#init_var#" + std::to_string(predicates.size());
							predicates.push_back(std::make_unique<inst_predicate>('E', point, s, false));
							predicates.push_back(std::make_unique<inst_predicate>('G', point, s, false));
							std::string const_val = "for#const#" + std::to_string(predicates.size());
							predicates.push_back(std::make_unique<var_predicate>(const_val));
							predicates.push_back(std::make_unique<const_predicate>(std::vector<std::string>{ inst }));
							predicates.push_back(std::make_unique<var_predicate>(point));
							// initialize with const
							predicates.push_back(std::make_unique<inst_predicate>('T', tmp_name, s, false));
							predicates.push_back(std::make_unique<inst_predicate>('A', const_val, s, false));
							predicates.push_back(std::make_unique<inst_predicate>('T', var, s, false));
							predicates.push_back(std::make_unique<inst_predicate>('A', tmp_name, s, false));
							skip_space(str, i);
						}
						if (str[i] != ',')
							throw std::runtime_error("coma expected after loop variable");
						i++;
						skip_space(str, i);
						if (std::isdigit(str[i]))
							throw std::runtime_error("not implemented yet");
						sz = find_word_end(str + i);
						std::string border = std::string(str + i, sz);
						i += sz;
						skip_space(str, i);
						if (str[i] != 'd' || str[i + 1] != 'o')
							throw std::runtime_error("'do' expected in loop definition");
						i += 2;
						std::string layer = "for#" + std::to_string(predicates.size());
						// create a loop head
						predicates.push_back(std::make_unique<inst_predicate>('T', tmp_name, s, false));
						predicates.push_back(std::make_unique<var_predicate>(layer + "#redo"));
						predicates.push_back(std::make_unique<inst_predicate>('A', var, s, false));
						predicates.push_back(std::make_unique<inst_predicate>('S', border, s, false));
						predicates.push_back(std::make_unique<inst_predicate>('E', layer + "#end", s, false));
						predicates.push_back(std::make_unique<inst_predicate>('T', std::string("LAST_INSTRUCTION"), s, false));
						predicates.push_back(std::make_unique<inst_predicate>('A', tmp_name, s, false));
						stack.emplace_back(layer_t::for_loop, layer, var);
						continue;
					}
				}
				case 'r': {
					if (!std::strncmp(str + i, "redo", 4) && std::isspace(str[i + 4])) {
						i += 4;
						char s = arguments.io == 2 ? 'F' : 'S';
						auto & layer = stack.back();
						predicates.push_back(std::make_unique<inst_predicate>('T', tmp_name, s, false));
						predicates.push_back(std::make_unique<inst_predicate>('E', std::get<1>(layer) + "#redo", s, false));
						continue;
					}
				}
				case 'b': {
					if (!std::strncmp(str + i, "break", 5) && std::isspace(str[i + 5])) {
						i += 5;
						char s = arguments.io == 2 ? 'F' : 'S';
						auto & layer = stack.back();
						predicates.push_back(std::make_unique<inst_predicate>('T', tmp_name, s, false));
						predicates.push_back(std::make_unique<inst_predicate>('E', std::get<1>(layer) + "#end", s, false));
						continue;
					}
				}
				case 'c': {
					if (!std::strncmp(str + i, "continue", 8) && std::isspace(str[i + 8])) {
						i += 8;
						char s = arguments.io == 2 ? 'F' : 'S';
						auto & layer = stack.back();
						predicates.push_back(std::make_unique<inst_predicate>('E', std::get<1>(layer) + "#cont", s, false));
						predicates.push_back(std::make_unique<inst_predicate>('G', std::get<1>(layer) + "#cont", s, false));
						continue;
					}
				}
				case 'e': {
					if (!std::strncmp(str + i, "end", 3) && std::isspace(str[i + 3])) {
						i += 3;
						skip_space(str, i);
						char s = arguments.io == 2 ? 'F' : 'S';
						auto & layer = stack.back();
						switch (std::get<0>(layer)) {
							case layer_t::for_loop:
								predicates.push_back(std::make_unique<var_predicate>(std::get<1>(layer) + "#cont"));
								predicates.push_back(std::make_unique<inst_predicate>('T', tmp_name, s, false));
								predicates.push_back(std::make_unique<inst_predicate>('A', std::get<2>(layer), s, false));
								predicates.push_back(std::make_unique<inst_predicate>('A', "STEP", s, false));
								predicates.push_back(std::make_unique<inst_predicate>('T', std::get<2>(layer), s, false));
								predicates.push_back(std::make_unique<inst_predicate>('E', std::get<1>(layer) + "#redo", s, false));
								predicates.push_back(std::make_unique<var_predicate>(std::get<1>(layer) + "#end"));
								predicates.push_back(std::make_unique<inst_predicate>('T', "LAST_INSTRUCTION", s, false));
								predicates.push_back(std::make_unique<inst_predicate>('A', tmp_name, s, false));
								break;
						}
						stack.pop_back();
						continue;
					}
				}
				case '~': {
					// preprocessor
					i++;
					skip_space(str, i);
					if (!std::strncmp(str + i, "io", 2) && std::isspace(str[i + 2])) {
						i += 2;
						skip_space(str, i);
						int io;
						i += read_int(str + i, io);
						if (!std::isspace(str[i]))
							throw std::runtime_error("integer number expected after ~io directive");
						if (io > 2 || io < 1)
							throw std::runtime_error("Initial Orders " + std::to_string(i) + " not supported (~io)");
						if (!predicates.empty())
							throw std::runtime_error("Can't switch between Initial Orders type inside a programm");
						arguments.io = io;
					} else if (!std::strncmp(str + i, "define", 6) && std::isspace(str[i + 6])) {
						i += 6;
						skip_space(str, i);
						int sz = find_word_end(str + i);
						std::string name = std::string(str + i, sz);
						i += sz;
						skip_space(str, i);
						int j = i;
						next_line(str, j);
						int k = j;
						for (j--; std::isspace(str[j]); j--);
						j++;
						std::string value = std::string(str + i, j - i);
						std::stringstream stream(value);
						std::string resolved_value;
						while (stream) {
							std::string word;
							stream >> word;
							auto iter = defines.find(word);
							if (iter != defines.end())
								resolved_value = iter->second + ' ';
							else
								resolved_value += word + ' ';
						}
						defines[name] = resolved_value;
						// resolve defines for this new one
						stream = std::stringstream(str + k);
						while (stream) {
							throw std::runtime_error("not implemented yet");
						}
					}
					next_line(str, i);
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
		return 1;
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
		return 2;
	}
	return 0;
}

}
