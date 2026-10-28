// Expressions in the scripts (if, set, {...}): read once, worked out against the variables; and the values they give.
#include "Script.hpp"
#include "System.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <climits>
#include <cmath>
#include <limits>
#include <cstdlib>
#include <expected>
#include <optional>
#include <random>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vn {

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

std::uint64_t newRandomState() {
    return std::random_device{}() * 0x100000001B3ULL ^ std::random_device{}();
}

namespace {
std::uint64_t*& randomSource() { // the state random() draws from now: a game's, while its commands run
    static std::uint64_t own = newRandomState();
    static std::uint64_t* current = &own;
    return current;
}
} // namespace

RandomScope::RandomScope(std::uint64_t& state) : source_(randomSource()), was_(std::exchange(source_, &state)) {}
RandomScope::~RandomScope() { source_ = was_; }

namespace {

struct Token {
    enum class Type : std::uint8_t { Number, String, Ident, Symbol, End };
    Type type = Type::End;
    std::string text;
    Value value = 0;
};

struct Function {
    std::string_view name;
    int minArgs, maxArgs;
};
constexpr std::array kFunctions{
    Function{"random", 1, 2}, Function{"min", 1, 16}, Function{"max", 1, 16}, Function{"abs", 1, 1},
    Function{"len", 1, 1},    Function{"str", 1, 1},  Function{"int", 1, 1},  Function{"upper", 1, 1},
    Function{"lower", 1, 1},  Function{"round", 1, 1}, Function{"if", 3, 3}, // if(condition, then, otherwise): a picture by a yes/no...
};

// Comparisons written as words, longest first: "is at least" -> ">=".
struct WordOp {
    std::array<std::string_view, 3> words;
    std::string_view symbol;
};
constexpr std::array kWordOps{
    WordOp{{"is", "at", "least"}, ">="},    WordOp{{"is", "at", "most"}, "<="},    WordOp{{"is", "more", "than"}, ">"},
    WordOp{{"is", "greater", "than"}, ">"}, WordOp{{"is", "less", "than"}, "<"},   WordOp{{"is", "fewer", "than"}, "<"},
    WordOp{{"at", "least", ""}, ">="},      WordOp{{"at", "most", ""}, "<="},      WordOp{{"more", "than", ""}, ">"},
    WordOp{{"greater", "than", ""}, ">"},   WordOp{{"less", "than", ""}, "<"},     WordOp{{"fewer", "than", ""}, "<"},
    WordOp{{"is", "not", ""}, "!="},        WordOp{{"is", "", ""}, "=="},
};

// The next number in random()'s sequence (splitmix64: its whole state is one number, easy to save).
std::uint64_t nextRandom() {
    std::uint64_t z = (*randomSource() += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

std::expected<std::vector<Token>, std::string> lex(std::string_view src) {
    std::vector<Token> out;
    std::size_t i = 0;
    while (i < src.size()) {
        const char c = src[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            const auto start = i;
            while (i < src.size() && std::isdigit(static_cast<unsigned char>(src[i]))) ++i;
            if (i + 1 < src.size() && src[i] == '.' && std::isdigit(static_cast<unsigned char>(src[i + 1]))) { // 2.5
                for (++i; i < src.size() && std::isdigit(static_cast<unsigned char>(src[i]));) ++i;
                const std::string digits(src.substr(start, i - start));
                out.push_back({Token::Type::Number, digits, number(parseDecimal(digits).value_or(0.0))});
                continue;
            }
            int v = 0;
            if (std::from_chars(src.data() + start, src.data() + i, v).ec != std::errc{})
                return std::unexpected(std::format("the number {} is too big", src.substr(start, i - start)));
            out.push_back({Token::Type::Number, std::string(src.substr(start, i - start)), v});
        } else if (c == '"' || c == '\'') {
            std::string text;
            ++i;
            bool closed = false;
            while (i < src.size()) {
                const char ch = src[i++];
                if (ch == c) { closed = true; break; }
                text += ch;
            }
            if (!closed) return std::unexpected(std::format("a text starting with {} is never closed", c));
            out.push_back({Token::Type::String, std::move(text), 0});
        } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_' || static_cast<unsigned char>(c) >= 0x80) {
            const auto start = i;
            while (i < src.size() && (std::isalnum(static_cast<unsigned char>(src[i])) || src[i] == '_' || src[i] == '.' || static_cast<unsigned char>(src[i]) >= 0x80)) ++i;
            out.push_back({Token::Type::Ident, lower(src.substr(start, i - start)), 0});
        } else {
            static constexpr std::array<std::string_view, 6> twoChar{"==", "!=", "<=", ">=", "&&", "||"};
            std::string sym(1, c);
            for (auto t : twoChar)
                if (src.substr(i, 2) == t) sym = std::string(t);
            if (sym.size() == 1 && std::string_view("+-*/%<>!(),=").find(c) == std::string_view::npos)
                return std::unexpected(std::format("'{}' can't be used here", c));
            i += sym.size();
            out.push_back({Token::Type::Symbol, sym == "=" ? std::string("==") : std::move(sym), 0});
        }
    }

    // Replace word comparisons ("is at least") with their symbols.
    std::vector<Token> merged;
    for (std::size_t k = 0; k < out.size(); ++k) {
        const WordOp* found = nullptr;
        for (const auto& op : kWordOps) {
            std::size_t n = 0;
            while (n < 3 && !op.words[n].empty() && k + n < out.size() && out[k + n].type == Token::Type::Ident &&
                   out[k + n].text == op.words[n])
                ++n;
            if (n > 0 && (n == 3 || op.words[n].empty())) {
                found = &op;
                k += n - 1;
                break;
            }
        }
        if (found) merged.push_back({Token::Type::Symbol, std::string(found->symbol), 0});
        else merged.push_back(std::move(out[k]));
    }
    merged.push_back({Token::Type::End, {}, 0});
    return merged;
}

const Expr* cachedExpr(std::string_view source) {
    static std::unordered_map<std::string, std::optional<Expr>, StringHash, std::equal_to<>> cache;
    auto it = cache.find(source);
    if (it == cache.end()) {
        auto e = Expr::parse(source);
        it = cache.emplace(std::string(source), e ? std::optional<Expr>(std::move(*e)) : std::nullopt).first;
    }
    return it->second ? &*it->second : nullptr;
}

} // namespace

bool truthy(const Value& v) {
    if (const int* i = std::get_if<int>(&v)) return *i != 0;
    if (const double* d = std::get_if<double>(&v)) return *d != 0.0;
    return !std::get<std::string>(v).empty();
}

double toNumber(const Value& v) {
    if (const int* i = std::get_if<int>(&v)) return *i;
    if (const double* d = std::get_if<double>(&v)) return *d;
    const auto& s = std::get<std::string>(v);
    char* end = nullptr;
    const double out = std::strtod(s.c_str(), &end); // (the number at its start: 0 if there's none; see parseDecimal on strtod)
    return std::isfinite(out) ? out : 0.0;
}

int toInt(const Value& v) {
    if (const int* i = std::get_if<int>(&v)) return *i;
    return static_cast<int>(std::clamp(std::trunc(toNumber(v)), double(INT_MIN), double(INT_MAX)));
}

Value number(double n) {
    if (!std::isfinite(n)) return 0;
    if (n == std::trunc(n) && std::abs(n) <= INT_MAX) return static_cast<int>(n);
    return n;
}

std::string toString(const Value& v) {
    if (const int* i = std::get_if<int>(&v)) return std::to_string(*i);
    if (const double* d = std::get_if<double>(&v)) { // (at most 4 places, without the 0s at its end)
        std::string out = std::format("{:.4f}", *d);
        while (out.ends_with('0')) out.pop_back();
        if (out.ends_with('.')) out.pop_back();
        return out == "-0" ? "0" : out;
    }
    return std::get<std::string>(v);
}

Value combine(const Value& old, char op, const Value& v) {
    const bool text = std::holds_alternative<std::string>(old) || std::holds_alternative<std::string>(v);
    if (op == '+' && text) return toString(old) + toString(v);
    return number(op == '+' ? toNumber(old) + toNumber(v) : toNumber(old) - toNumber(v)); // (whole when it comes out whole)
}

std::string encodeValue(const Value& v) {
    if (const int* i = std::get_if<int>(&v)) return std::format("i {}", *i);
    if (const double* d = std::get_if<double>(&v)) return std::format("d {}", *d);
    std::string out = "s ";
    for (char c : std::get<std::string>(v)) {
        if (c == '\\') out += "\\\\";
        else if (c == ' ') out += "\\s";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

Value decodeValue(std::string_view s) {
    if (s.starts_with("i ")) return toInt(std::string(s.substr(2)));
    if (s.starts_with("d ")) return number(toNumber(std::string(s.substr(2))));
    if (!s.starts_with("s ")) return toInt(std::string(s));
    std::string out;
    for (std::size_t i = 2; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 == s.size()) {
            out += s[i];
            continue;
        }
        const char e = s[++i];
        out += e == 's' ? ' ' : e == 'n' ? '\n' : e;
    }
    return out;
}

class ExprParser {
public:
    ExprParser(Expr& expr, std::vector<Token> tokens) : expr_(expr), tokens_(std::move(tokens)) {}

    std::expected<void, std::string> run() {
        expr_.root_ = binary(1);
        if (error_.empty() && peek().type != Token::Type::End)
            error_ = std::format("'{}' is out of place", peek().text);
        if (!error_.empty()) return std::unexpected(error_);
        return {};
    }

private:
    using BinOp = Expr::BinOp;
    using Kind = Expr::Kind;

    const Token& peek() const { return tokens_[pos_]; }
    const Token& next() { return tokens_[pos_ < tokens_.size() - 1 ? pos_++ : pos_]; }

    bool accept(std::string_view sym) {
        const auto& t = peek();
        if ((t.type == Token::Type::Symbol || t.type == Token::Type::Ident) && t.text == sym) {
            ++pos_;
            return true;
        }
        return false;
    }

    int add(Expr::Node node) {
        expr_.nodes_.push_back(std::move(node));
        return static_cast<int>(expr_.nodes_.size()) - 1;
    }

    static std::optional<std::pair<BinOp, int>> binaryOp(const Token& t) {
        if (t.type != Token::Type::Symbol && t.type != Token::Type::Ident) return std::nullopt;
        const std::string_view s = t.text;
        if (s == "||" || s == "or") return std::pair{BinOp::Or, 1};
        if (s == "&&" || s == "and") return std::pair{BinOp::And, 2};
        if (s == "==") return std::pair{BinOp::Eq, 3};
        if (s == "!=") return std::pair{BinOp::Ne, 3};
        if (s == "<") return std::pair{BinOp::Lt, 4};
        if (s == "<=") return std::pair{BinOp::Le, 4};
        if (s == ">") return std::pair{BinOp::Gt, 4};
        if (s == ">=") return std::pair{BinOp::Ge, 4};
        if (s == "+") return std::pair{BinOp::Add, 5};
        if (s == "-") return std::pair{BinOp::Sub, 5};
        if (s == "*") return std::pair{BinOp::Mul, 6};
        if (s == "/") return std::pair{BinOp::Div, 6};
        if (s == "%") return std::pair{BinOp::Mod, 6};
        return std::nullopt;
    }

    // Precedence climbing.
    int binary(int minPrec) {
        int lhs = unary();
        while (error_.empty()) {
            const auto op = binaryOp(peek());
            if (!op || op->second < minPrec) break;
            next();
            const int rhs = binary(op->second + 1);
            lhs = add({.kind = Kind::Binary, .op = op->first, .lhs = lhs, .rhs = rhs});
        }
        return lhs;
    }

    int unary() {
        if (accept("!") || accept("not")) return add({.kind = Kind::Not, .lhs = unary()});
        if (accept("-")) return add({.kind = Kind::Negate, .lhs = unary()});
        return primary();
    }

    int primary() {
        if (!error_.empty()) return -1;
        const Token& t = next();
        switch (t.type) {
        case Token::Type::Number:
            return add({.kind = Kind::Literal, .value = t.value});
        case Token::Type::String:
            return add({.kind = Kind::Literal, .value = t.text});
        case Token::Type::Ident: {
            if (t.text == "true" || t.text == "yes") return add({.kind = Kind::Literal, .value = 1});
            if (t.text == "false" || t.text == "no") return add({.kind = Kind::Literal, .value = 0});
            if (!accept("(")) return add({.kind = Kind::Variable, .name = t.text});
            Expr::Node call{.kind = Kind::Call, .name = t.text};
            if (!accept(")")) {
                do call.args.push_back(binary(1));
                while (error_.empty() && accept(","));
                if (error_.empty() && !accept(")")) error_ = "a ')' is missing";
            }
            const auto fn = std::ranges::find(kFunctions, std::string_view(call.name), &Function::name);
            if (fn == kFunctions.end()) error_ = std::format("there is no function called {}()", call.name);
            else if (static_cast<int>(call.args.size()) < fn->minArgs || static_cast<int>(call.args.size()) > fn->maxArgs)
                error_ = std::format("{}() was given the wrong number of values", call.name);
            return add(std::move(call));
        }
        case Token::Type::Symbol:
            if (t.text == "(") {
                const int inner = binary(1);
                if (!accept(")")) error_ = "a ')' is missing";
                return inner;
            }
            error_ = std::format("'{}' is out of place", t.text);
            return -1;
        case Token::Type::End:
            error_ = "it ends too early";
            return -1;
        }
        return -1;
    }

    Expr& expr_;
    std::vector<Token> tokens_;
    std::size_t pos_ = 0;
    std::string error_;
};

std::expected<Expr, std::string> Expr::parse(std::string_view source) {
    auto tokens = lex(source);
    if (!tokens) return std::unexpected(tokens.error());
    Expr expr;
    if (auto ok = ExprParser(expr, std::move(*tokens)).run(); !ok) return std::unexpected(ok.error());
    return expr;
}

Value Expr::eval(const Lookup& lookup) const {
    return root_ >= 0 ? evalNode(root_, lookup) : Value{0};
}

std::optional<Value> Expr::constant() const {
    if (root_ < 0 || nodes_[static_cast<std::size_t>(root_)].kind != Kind::Literal) return std::nullopt;
    return nodes_[static_cast<std::size_t>(root_)].value;
}

Value Expr::evalNode(int index, const Lookup& lookup) const {
    const Node& n = nodes_[static_cast<std::size_t>(index)];
    auto sub = [&](int i) { return evalNode(i, lookup); };
    switch (n.kind) {
    case Kind::Literal:
        return n.value;
    case Kind::Variable:
        return lookup ? lookup(n.name) : Value{0};
    case Kind::Not:
        return truthy(sub(n.lhs)) ? 0 : 1;
    case Kind::Negate: {
        const Value v = sub(n.lhs);
        return number(-toNumber(v));
    }
    case Kind::Call: {
        if (n.name == "if") return truthy(sub(n.args[0])) ? sub(n.args[1]) : sub(n.args[2]); // (only the one it picks is worked out)
        std::vector<Value> a;
        a.reserve(n.args.size());
        for (int arg : n.args) a.push_back(sub(arg));
        if (n.name == "random") {
            const int lo = a.size() == 2 ? toInt(a[0]) : 0;
            const int hi = a.size() == 2 ? toInt(a[1]) : toInt(a[0]) - 1;
            return randomNumber(lo, hi);
        }
        if (n.name == "min" || n.name == "max") {
            double best = toNumber(a[0]);
            for (const auto& v : a) best = n.name == "min" ? std::min(best, toNumber(v)) : std::max(best, toNumber(v));
            return number(best);
        }
        if (n.name == "abs") return number(std::abs(toNumber(a[0])));
        if (n.name == "round") return number(std::round(toNumber(a[0])));
        if (n.name == "len") return static_cast<int>(utf8::length(toString(a[0])));
        if (n.name == "str") return toString(a[0]);
        if (n.name == "int") return toInt(a[0]);
        return n.name == "upper" ? upper(toString(a[0])) : lower(toString(a[0]));
    }
    case Kind::Binary:
        break;
    }

    const Value a = sub(n.lhs);
    if (n.op == BinOp::And) return (truthy(a) && truthy(sub(n.rhs))) ? 1 : 0;
    if (n.op == BinOp::Or) return (truthy(a) || truthy(sub(n.rhs))) ? 1 : 0;
    const Value b = sub(n.rhs);
    if (std::holds_alternative<std::string>(a) || std::holds_alternative<std::string>(b)) {
        const std::string x = toString(a), y = toString(b);
        switch (n.op) {
        case BinOp::Add: return x + y;
        case BinOp::Eq: return x == y ? 1 : 0;
        case BinOp::Ne: return x != y ? 1 : 0;
        case BinOp::Lt: return x < y ? 1 : 0;
        case BinOp::Le: return x <= y ? 1 : 0;
        case BinOp::Gt: return x > y ? 1 : 0;
        case BinOp::Ge: return x >= y ? 1 : 0;
        default: break; // arithmetic on texts uses their numeric value
        }
    }
    if (!std::holds_alternative<int>(a) || !std::holds_alternative<int>(b) || n.op == BinOp::Div) { // (with fractions)
        const double x = toNumber(a), y = toNumber(b);
        switch (n.op) {
        case BinOp::Add: return number(x + y);
        case BinOp::Sub: return number(x - y);
        case BinOp::Mul: return number(x * y);
        case BinOp::Div: return y != 0.0 ? number(x / y) : Value(0);
        case BinOp::Mod: return y != 0.0 ? number(std::fmod(x, y)) : Value(0);
        case BinOp::Eq: return x == y ? 1 : 0;
        case BinOp::Ne: return x != y ? 1 : 0;
        case BinOp::Lt: return x < y ? 1 : 0;
        case BinOp::Le: return x <= y ? 1 : 0;
        case BinOp::Gt: return x > y ? 1 : 0;
        case BinOp::Ge: return x >= y ? 1 : 0;
        case BinOp::And:
        case BinOp::Or: break;
        }
        return 0;
    }
    const int x = toInt(a), y = toInt(b);
    switch (n.op) { // (worked out as fractions are, so a sum too big for a whole number doesn't wrap round)
    case BinOp::Add: return number(static_cast<double>(x) + y);
    case BinOp::Sub: return number(static_cast<double>(x) - y);
    case BinOp::Mul: return number(static_cast<double>(x) * y);
    case BinOp::Mod: return y == 0 || y == -1 ? Value(0) : Value(x % y);
    case BinOp::Eq: return x == y ? 1 : 0;
    case BinOp::Ne: return x != y ? 1 : 0;
    case BinOp::Lt: return x < y ? 1 : 0;
    case BinOp::Le: return x <= y ? 1 : 0;
    case BinOp::Gt: return x > y ? 1 : 0;
    case BinOp::Ge: return x >= y ? 1 : 0;
    case BinOp::Div: // (with fractions, above)
    case BinOp::And:
    case BinOp::Or: break;
    }
    return 0;
}

bool isTextTag(std::string_view inner) {
    static constexpr std::array<std::string_view, 19> tags{"pause",  "nowait",  "speed", "/speed",    "color",      "/color",
                                                           "reveal", "bold",    "/bold", "italic",    "/italic",    "underline",
                                                           "/underline", "strike", "/strike", "ruby", "/ruby", "tip", "/tip"};
    return std::ranges::contains(tags, lower(inner.substr(0, inner.find_first_of(" ="))));
}

std::string expand(std::string_view text, const Lookup& lookup, const TagHandler& tag) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if ((c == '{' || c == '}') && i + 1 < text.size() && text[i + 1] == c) {
            out += c; // {{ or }}
            ++i;
            continue;
        }
        const auto close = text.find('}', i);
        if (c != '{' || close == std::string_view::npos) {
            out += c;
            continue;
        }
        const std::string_view inner = text.substr(i + 1, close - i - 1);
        i = close;
        if (isTextTag(inner)) {
            const auto split = inner.find_first_of(" =");
            if (tag)
                tag(lower(inner.substr(0, split)), split == std::string_view::npos ? std::string_view{} : trim(inner.substr(split + 1)),
                    utf8::length(out));
        } else if (const Expr* e = cachedExpr(inner)) {
            out += toString(e->eval(lookup));
        } else {
            out += std::format("{{{}}}", inner);
        }
    }
    return out;
}

std::string fillValues(std::string_view text, const Lookup& lookup) {
    auto literal = [](std::string_view value) { // braces stay braces
        std::string out;
        for (char c : value) {
            out += c;
            if (c == '{' || c == '}') out += c;
        }
        return out;
    };
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if ((c == '{' || c == '}') && i + 1 < text.size() && text[i + 1] == c) {
            out += text.substr(i, 2); // {{ or }} stays as it is
            ++i;
            continue;
        }
        const auto close = text.find('}', i);
        if (c != '{' || close == std::string_view::npos) {
            out += c;
            continue;
        }
        const std::string_view inner = text.substr(i + 1, close - i - 1);
        i = close;
        if (isTextTag(inner)) out += std::format("{{{}}}", inner);
        else if (const Expr* e = cachedExpr(inner)) out += literal(toString(e->eval(lookup)));
        else out += literal(std::format("{{{}}}", inner));
    }
    return out;
}

std::string checkText(std::string_view text) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        if ((text[i] == '{' || text[i] == '}') && i + 1 < text.size() && text[i + 1] == text[i]) {
            ++i;
            continue;
        }
        const auto close = text.find('}', i);
        if (text[i] != '{' || close == std::string_view::npos) continue;
        const std::string_view inner = text.substr(i + 1, close - i - 1);
        i = close;
        if (isTextTag(inner)) continue;
        if (auto e = Expr::parse(inner); !e) return std::format("There's a mistake in {{{}}}: {}", inner, e.error());
    }
    return {};
}

int randomNumber(int lo, int hi) {
    return hi < lo ? lo : lo + static_cast<int>(nextRandom() % (static_cast<std::uint64_t>(hi) - static_cast<std::uint64_t>(lo) + 1));
}

} // namespace vn
