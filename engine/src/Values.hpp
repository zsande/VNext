#pragma once

// Values (numbers and texts), expressions worked out against the variables, random numbers, and texts with {values}.

#include "Basics.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace vn {

// A script value: a whole number (yes/no are 1/0), a number with a fraction (2.5), or a text. A
// sum that comes out whole is a whole number again.
using Value = std::variant<int, double, std::string>;
using Variables = std::map<std::string, Value, std::less<>>;
// Finds a variable's value; names are lower case. Unset variables are 0.
using Lookup = std::function<Value(std::string_view name)>;

[[nodiscard]] bool truthy(const Value& v);        // non-zero / non-empty
[[nodiscard]] int toInt(const Value& v);          // texts are read as numbers, 0 if they aren't; fractions are cut off
[[nodiscard]] double toNumber(const Value& v);    // (with its fraction)
[[nodiscard]] Value number(double n);             // whole when it is
[[nodiscard]] std::string toString(const Value& v); // fractions to at most 4 places: 2.5, 0.3333
// One value per line in save files: "i 42", "d 2.5" or "s text" (spaces, newlines and backslashes escaped).
[[nodiscard]] std::string encodeValue(const Value& v);
[[nodiscard]] Value decodeValue(std::string_view s);

// random()'s numbers come in a sequence (splitmix64: its whole state is one number). A game's commands
// draw from the game's own state, put in effect while they run (a RandomScope); saves, and so rollback,
// keep it, so going back and forward again rolls the same numbers. Anything else (a screen's text
// drawn) draws from a state of its own, and doesn't move the game's on.
class RandomScope {
public:
    explicit RandomScope(std::uint64_t& state);
    ~RandomScope();
    RandomScope(const RandomScope&) = delete;
    RandomScope& operator=(const RandomScope&) = delete;
    RandomScope(RandomScope&&) = delete;
    RandomScope& operator=(RandomScope&&) = delete;

private:
    std::uint64_t*& source_; // (where random() draws from)
    std::uint64_t* was_;
};
[[nodiscard]] std::uint64_t newRandomState(); // (a game's, to begin with: from the system)
// What random(lo, hi) gives: a whole number from lo to hi (the next in that sequence).
[[nodiscard]] int randomNumber(int lo, int hi);

// A formula, as used by set/add, if, choice conditions, {...} in texts and screen conditions.
// Numbers (2, 2.5), "text" or 'text', true/false, variables, + - * / %, parentheses, comparisons
// (= == != < <= > >= or the words is, is not, at least, at most, more than, less than),
// and / or / not, and the functions random(a, b), random(n), min, max, abs, round, len, str, int,
// upper, lower and if(condition, then, otherwise). Names are case-insensitive. Dividing gives a fraction
// when it doesn't come out whole.
class Expr {
public:
    static std::expected<Expr, std::string> parse(std::string_view source);

    [[nodiscard]] Value eval(const Lookup& lookup) const;
    [[nodiscard]] bool test(const Lookup& lookup) const { return truthy(eval(lookup)); }
    [[nodiscard]] std::optional<Value> constant() const; // its value, when it's just one (3, yes, "Mia")
    [[nodiscard]] std::vector<std::string> names() const; // the names it reads (variables and values), each once

private:
    friend class ExprParser;

    enum class Kind : std::uint8_t { Literal, Variable, Not, Negate, Binary, Call };
    enum class BinOp : std::uint8_t { Add, Sub, Mul, Div, Mod, Eq, Ne, Lt, Le, Gt, Ge, And, Or };

    struct Node {
        Kind kind = Kind::Literal;
        BinOp op = BinOp::Add;
        Value value = 0;
        std::string name;
        int lhs = -1;
        int rhs = -1;
        std::vector<int> args;
    };

    [[nodiscard]] Value evalNode(int index, const Lookup& lookup) const;

    std::vector<Node> nodes_;
    int root_ = -1;
};

// Text tags handled by the textbox: {pause}, {pause 0.5}, {no_wait}, {speed 10}...{/speed},
// {colour gold}...{/colour}, {appears line}, {bold}, {italic}, {underline} and {crossed_out} (each to its
// {/...}), {furigana READING}...{/furigana} and {tip NAME}...{/tip}. `at` is the tag's position in the
// expanded text, in codepoints.
using TagHandler = std::function<void(std::string_view name, std::string_view arg, std::size_t at)>;
[[nodiscard]] bool isTextTag(std::string_view inner);

// Fills in the {expression} parts of a text; {{ and }} are literal braces. Text tags are passed
// to `tag` (or dropped without one). An expression that doesn't make sense leaves its {...} as is.
[[nodiscard]] std::string expand(std::string_view text, const Lookup& lookup, const TagHandler& tag = {});
// Fills in {expressions} now, but leaves text tags (and {{ }}) for a later expand(); braces in the
// values are doubled, so they stay text. (Messages and choices, shown later as rich text.)
[[nodiscard]] std::string fillValues(std::string_view text, const Lookup& lookup);
// Checks the {...} parts of a text; returns the first problem.
[[nodiscard]] std::string checkText(std::string_view text);
// The names a text's {...} parts read (not its tags'), each once.
[[nodiscard]] std::vector<std::string> namesIn(std::string_view text);

} // namespace vn
