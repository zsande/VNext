// Reading the story's lines: scenes, what's said, choices, if and else, together, the phone's chats and calls, and
// each command (show, play, weather, camera, time, set...) and the actions buttons and keys do.
#include "ScriptParser.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vn {

namespace parse {

namespace {

// A colour as the stage has it: "#rrggbbaa" (one fading down to another: two, "#top-#bottom").
std::string colourKey(SDL_Color c) { return std::format("#{:02x}{:02x}{:02x}{:02x}", c.r, c.g, c.b, c.a); }

} // namespace

// -- Walking blocks ----------------------------------------------------------

// Commands may start with a capital letter ("If", "Show"); such a line only counts as a command
// if it works as one, so narration like "If only I knew." stays text.
void Parser::block(std::span<const Line> lines) {
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const Line& line = lines[i];
        const std::string head = headOf(line.text);
        const std::string second = lower(firstWord(afterWords(line.text, 1)));
        const bool exact = !startsWithCapital(line.text);
        const bool hasBlock = !line.children.empty();
        // (the lines in a choice, an if, a chat...: a jump there leaves the scene before its end)
        if (const auto phone = head == "scene" && hasBlock && phase_ == Phase::Story ? phoneSceneFor(line) : std::nullopt) {
            phoneScene(line, *phone->first, phone->second);
        } else if (head == "if" && (exact || hasBlock)) {
            ++branches_;
            i = ifChain(lines, i);
            --branches_;
        } else if (head == "else" && (exact || hasBlock)) {
            if (phase_ == Phase::Story) error(line, "This 'else' has no 'if' right above it at the same indentation."_tr());
            ++branches_;
            block(line.children);
            --branches_;
        } else if (head == "choice" && (exact || hasBlock)) {
            ++branches_;
            menu(line);
            --branches_;
        } else if (head == "chat" && hasBlock) {
            ++branches_;
            chatBlock(line);
            --branches_;
        } else if (head == "phone" && second == "call" && (exact || hasBlock)) {
            ++branches_;
            callBlock(line);
            --branches_;
        } else if (head == "together" && (exact || hasBlock) && afterWords(line.text, 1).empty()) {
            ++branches_;
            together(line);
            --branches_;
        } else if (std::ranges::contains(kDefinitions, head) && !asText_.contains(&line)) {
            if (phase_ == Phase::Definitions) defs_.push_back(&line);
        } else {
            if (phase_ == Phase::Story && !(chat_ && chatStatement(line))) statement(line);
            block(line.children); // indentation under an ordinary line is just layout
        }
    }
}

// if / else if / else; returns the index of the chain's last line.
std::size_t Parser::ifChain(std::span<const Line> lines, std::size_t i) {
    std::vector<std::size_t> exits;
    for (;; ++i) {
        const Line& line = lines[i];
        const std::string head = headOf(line.text);
        const bool isElse = head == "else" && headOf(afterWords(line.text, 1)) != "if";
        std::string condition(isElse ? "" : afterWords(line.text, head == "else" ? 2 : 1));
        if (condition.ends_with(':')) condition.pop_back();
        std::optional<std::size_t> branch;
        if (phase_ == Phase::Story) {
            if (isElse && !trim(afterWords(line.text, 1)).empty() && trim(afterWords(line.text, 1)) != ":")
                error(line, "'else' takes nothing after it."_tr());
            if (!isElse) branch = emit(cmd::Jump{.unless = formula(line, condition, "What to check is missing."_tr())}, line);
            if (line.children.empty())
                error(line, "There are no lines under it."_tr());
        }
        block(line.children);

        bool more = false;
        if (!isElse && i + 1 < lines.size()) {
            const std::string next = headOf(lines[i + 1].text);
            more = next == "else" && (!startsWithCapital(lines[i + 1].text) || !lines[i + 1].children.empty());
        }
        if (phase_ == Phase::Story) {
            if (more) exits.push_back(emit(cmd::Jump{}, line, true));
            if (branch) jumpAt(*branch).target = s_.code.size();
        }
        if (!more) break;
    }
    for (auto pc : exits) jumpAt(pc).target = s_.code.size();
    return i;
}

// choice [settings] with one indented option per line; each option's own lines run when
// it's picked.
void Parser::menu(const Line& line) {
    if (phase_ == Phase::Definitions) {
        for (const auto& option : line.children) block(option.children);
        return;
    }
    cmd::Menu m;
    std::string rest(choiceSettings(line, afterWords(line.text, 1), m));
    if (rest.ends_with(':')) rest.pop_back();
    if (!trim(rest).empty()) error(line, "'{}' isn't one of a choice's settings (optional=, repeat=, return=, for N seconds, for N lines)."_tr(trim(rest)));
    const bool meanwhile = m.lines > 0; // up while the story goes on: each option comes back when its lines are done
    if (meanwhile && m.repeats) error(line, "A choice that's up for some lines can't repeat."_tr());
    if (chat_ || !callWith_.empty()) { // (in a chat or a call: its options are on the phone)
        if (meanwhile || m.optional) error(line, (chat_ ? "A chat's choice can only have a time limit and repeat."_tr : "A call's choice can only have a time limit and repeat."_tr)());
        m.phone = chat_ ? chat_->with : callWith_;
        m.call = !chat_;
    }
    options(line, line.children, std::move(m));
}

// A choice's options (`optionLines`, under `line`), each with its own lines run when it's picked.
void Parser::options(const Line& line, std::span<const Line> optionLines, cmd::Menu m) {
    const bool meanwhile = m.lines > 0, repeats = m.repeats;
    const std::size_t at = emit(std::move(m), line);
    std::vector<cmd::Choice> choices;
    std::vector<std::size_t> exits;   // jumps to the end of the choice
    std::vector<std::size_t> skipped; // options without lines: continue after the choice
    for (const auto& option : optionLines) {
        cmd::Choice choice = this->option(option);
        if (!option.children.empty()) {
            choice.target = s_.code.size();
            block(option.children);
            if (meanwhile) emit(cmd::Return{}, option, true);
            else if (repeats) emit(cmd::Jump{.target = at, .again = true}, option, true); // (the choice again)
            else exits.push_back(emit(cmd::Jump{}, option, true));
        } else if (meanwhile) {
            choice.target = emit(cmd::Return{}, option, true); // nothing to do: back to the story
        } else if (repeats) {
            choice.target = at;
        } else {
            skipped.push_back(choices.size());
        }
        choices.push_back(std::move(choice));
    }
    for (auto pc : exits) jumpAt(pc).target = s_.code.size();
    for (auto k : skipped) choices[k].target = s_.code.size();
    if (choices.empty())
        error(line, "This choice has no options."_tr());
    auto& emitted = std::get<cmd::Menu>(s_.code[at].command);
    emitted.choices = std::move(choices);
    emitted.after = s_.code.size();
}

// An option of a choice: "Text [(if CONDITION) | (needs CONDITION: what)]".
cmd::Choice Parser::option(const Line& option) {
    std::string text(trim(option.text));
    std::optional<Expr> condition;
    if (text.ends_with(':') && !isQuoted(text)) text.pop_back();
    bool shownLocked = false;
    std::string note;
    if (const auto open = text.rfind("(if "); text.ends_with(')') && open != std::string::npos && !isQuoted(text)) {
        condition = formula(option, std::string_view(text).substr(open + 4, text.size() - open - 5));
        text = std::string(trim(std::string_view(text).substr(0, open)));
    } else if (const auto needs = text.rfind("(needs "); text.ends_with(')') && needs != std::string::npos && !isQuoted(text)) {
        // (needs CONDITION) or (needs CONDITION: what it needs): shown but locked until then
        std::string_view inside = std::string_view(text).substr(needs + 7, text.size() - needs - 8);
        std::size_t colon = std::string_view::npos;
        for (std::size_t k = 0, quotes = 0; k < inside.size(); ++k) {
            if (inside[k] == '"') ++quotes;
            else if (inside[k] == ':' && quotes % 2 == 0) colon = k;
        }
        if (colon != std::string_view::npos) {
            note = unquote(trim(inside.substr(colon + 1)));
            inside = trim(inside.substr(0, colon));
            checkTextOf(option, note);
        }
        condition = formula(option, inside);
        shownLocked = true;
        text = std::string(trim(std::string_view(text).substr(0, needs)));
    }
    text = unquote(text);
    checkTextOf(option, text);
    return cmd::Choice{.text = text, .condition = std::move(condition), .shownLocked = shownLocked, .note = std::move(note),
                       .id = fnv1a(text, fnv1a(currentScene_, fnv1a("choice")))};
}

// together, with lines indented under it: they all show at once, each in its own textbox (a
// character's textbox=, or the usual one), and the story waits for the last.
void Parser::together(const Line& line) {
    if (phase_ == Phase::Definitions) return block(line.children);
    const std::size_t first = s_.code.size();
    block(line.children);
    std::vector<cmd::Say*> says;
    for (std::size_t pc = first; pc < s_.code.size(); ++pc)
        if (auto* say = std::get_if<cmd::Say>(&s_.code[pc].command)) says.push_back(say);
    if (says.size() < 2) return error(line, "Together needs two lines or more under it."_tr());
    for (std::size_t k = 0; k + 1 < says.size(); ++k) says[k]->together = true;
    for (std::size_t pc = first; pc < s_.code.size(); ++pc) // (the lines before the last show without a voice)
        if (const auto* say = std::get_if<cmd::Say>(&s_.code[pc].command); say && say->together && !say->voice.empty())
            error(Line{.file = s_.code[pc].file, .number = s_.code[pc].line}, "Only the last line said together can have a voice."_tr());
}

Parser::Who Parser::whoOf(std::string_view written, bool expression) const {
    std::string w(trim(written));
    Who who;
    if (expression && w.ends_with(')') && w.find('(') != std::string::npos) { // Mika (happy)
        const auto open = w.rfind('(');
        who.expression = std::string(trim(std::string_view(w).substr(open + 1, w.size() - open - 2)));
        w = std::string(trim(std::string_view(w).substr(0, open)));
    }
    const auto space = w.rfind(' ');
    if (s_.character(lower(w))) {
        who.character = lower(w);
    } else if (expression && who.expression.empty() && space != std::string::npos && s_.character(lower(w.substr(0, space)))) {
        who.character = lower(w.substr(0, space)); // Mika happy
        who.expression = w.substr(space + 1);
    } else {
        who.sender = unquote(w);
    }
    return who;
}

// A person's name as the phone shows it: a character's own.
std::string Parser::shownName(const Who& who) const {
    if (const CharacterDef* def = who.character.empty() ? nullptr : s_.character(who.character)) return def->name;
    return who.sender;
}

// Where the word `word` (lower case) is in `text`: outside quotes, in any case. None: npos.
std::size_t Parser::wordAt(std::string_view text, std::string_view word) {
    for (const auto& [start, length] : wordSpans(text))
        if (length == word.size() && lower(text.substr(start, length)) == word) return start;
    return std::string_view::npos;
}

// chat WHO, with its lines under it: a chat scene. Its lines are the phone's (Mika: words, me: words,
// Mika sends IMAGE...), a choice in it shows its options on the phone, and the rest are the story's, as anywhere.
void Parser::chatBlock(const Line& line) {
    if (phase_ == Phase::Definitions) return block(line.children);
    if (chat_) return error(line, "A chat can't be inside another chat."_tr());
    std::string name(trim(afterWords(line.text, 1)));
    if (name.ends_with(':')) name.pop_back();
    name = unquote(name);
    if (name.empty()) return error(line, "Who the chat is with is missing."_tr());
    const Who who = whoOf(name, false);
    const cmd::Chat c{.with = shownName(who)};
    if (!screenLines_.contains("phone")) error(line, "A chat needs a screen called 'phone'."_tr());
    emit(c, line);
    chat_ = ChatContext{.with = c.with, .character = who.character};
    block(line.children);
    chat_.reset();
    emit(cmd::Message{.kind = cmd::Message::Kind::End, .character = who.character, .sender = who.character.empty() ? c.with : std::string{}}, line,
         true);                    // (a line saying it's over)
    emit(cmd::Wait{}, line, true); // (the last message read: a click, and the phone goes back)
    emit(cmd::Chat{.with = c.with, .end = true}, line, true);
}

// A line in a chat that isn't a command: one of the phone's (Mika: words, me sends IMAGE...), emitted.
// False when it isn't one (then it's the story's).
bool Parser::chatStatement(const Line& line) {
    if (!commandOf(line.text, true).empty() || isQuoted(trim(line.text))) return false;
    cmd::Message m;
    const Parsed p = phoneLine(line, line.text, m);
    if (p == Parsed::Yes) emit(std::move(m), line);
    return p != Parsed::No;
}

// A phone line: who (and how they look), then what: ": words", "sends [sticker | voice] NAME [: caption]",
// "is typing [for N seconds]", "deletes it", "edits it: words", "reacts TEXT" or "reads it", in a chat
// scene: "me" is the player, and the chat is the scene's. No: it isn't one (nothing written); Bad: it is,
// with a mistake (written).
Parser::Parsed Parser::phoneLine(const Line& line, std::string_view text, cmd::Message& m) {
    using Kind = cmd::Message::Kind;
    static constexpr std::pair<std::string_view, Kind> kVerbs[]{{"sends", Kind::Image}, {"is typing", Kind::Typing}, {"deletes it", Kind::Delete},
                                                                {"edits it", Kind::Edit},  {"reacts", Kind::React},     {"reads it", Kind::Read}};
    text = trim(text);
    std::size_t colon = std::string_view::npos; // (the first outside quotes)
    for (std::size_t k = 0, quotes = 0; k < text.size() && colon == std::string_view::npos; ++k) {
        if (text[k] == '"') ++quotes;
        else if (text[k] == ':' && quotes % 2 == 0) colon = k;
    }
    const auto spans = wordSpans(text);
    auto wordIs = [&](std::size_t w, std::string_view word, std::size_t& end) { // (a word, without a colon after it)
        std::string_view written = text.substr(spans[w].first, spans[w].second);
        end = spans[w].first + spans[w].second;
        if (written.ends_with(':')) written.remove_suffix(1), --end;
        return lower(written) == word;
    };
    std::size_t verbAt = std::string_view::npos, verbEnd = 0;
    m.kind = Kind::Text;
    for (std::size_t w = 1; w < spans.size() && verbAt == std::string_view::npos; ++w) {
        if (colon != std::string_view::npos && spans[w].first > colon) break;
        for (const auto& [words, kind] : kVerbs) {
            const auto space = words.find(' ');
            std::size_t end = 0;
            if (!wordIs(w, words.substr(0, space), end)) continue;
            if (space != std::string_view::npos && (w + 1 >= spans.size() || !wordIs(w + 1, words.substr(space + 1), end))) continue;
            verbAt = spans[w].first;
            verbEnd = end;
            m.kind = kind;
            break;
        }
    }
    std::string who;
    std::string_view after;
    if (verbAt != std::string_view::npos) {
        who = std::string(trim(text.substr(0, verbAt)));
        after = trim(text.substr(verbEnd));
    } else if (colon != std::string_view::npos && colon > 0) {
        who = std::string(trim(text.substr(0, colon)));
        after = text.substr(colon + 1);
        // (narration with a colon in it, "At last: rain.", isn't one: as a line's speaker, at most three words)
        if ((std::ranges::count(who, ' ') > 2 || who.find_first_of(",!?\"{};") != std::string::npos)) return Parsed::No;
    } else {
        return Parsed::No;
    }

    // Who: the player ("me"), or someone.
    if (lower(who) == "me") {
        m.mine = true;
        who.clear();
    }
    if (!who.empty()) {
        const Who w = whoOf(who, !m.mine);
        m.character = w.character;
        m.sender = w.sender;
        m.expression = w.expression;
    }

    // What: its words, its image, how long they type...
    switch (m.kind) {
    case Kind::Text:
        m.text = unquote(after);
        if (m.text.empty()) return error(line, who.empty() ? "The player doesn't say anything."_tr() : "{} doesn't say anything."_tr(who)), Parsed::Bad;
        break;
    case Kind::Image: { // sends [sticker | voice] NAME [: caption]
        std::string_view what = after, caption;
        if (const auto c = what.find(':'); c != std::string_view::npos) {
            caption = trim(what.substr(c + 1));
            what = trim(what.substr(0, c));
        }
        if (const std::string first = lower(firstWord(what)); first == "sticker" || first == "voice") {
            m.kind = first == "sticker" ? Kind::Sticker : Kind::Voice;
            what = trim(afterWords(what, 1));
        }
        m.file = unquote(what);
        m.text = unquote(caption);
        if (m.file.empty()) return error(line, m.kind == Kind::Voice ? "Which voice to send is missing."_tr() : "Which visual to send is missing."_tr()), Parsed::Bad;
        if (m.kind == Kind::Voice && nothingToRead(m.text)) // (heard: its words read too)
            return error(line, "A voice message needs what it says after a colon: sends voice {}: ..."_tr(m.file)), Parsed::Bad;
        if (m.file.find(' ') != std::string::npos)
            return error(line, "'sends' needs a visual's name after it, or sticker or voice and a name."_tr()), Parsed::Bad;
        if (m.kind != Kind::Voice) m.file = imageName(m.file);
        break;
    }
    case Kind::Typing: // is typing [for N seconds]
        if (!after.empty()) {
            const auto seconds = parseNumber(firstWord(afterWords(after, 1)));
            const std::string unit = lower(afterWords(after, 2));
            if (lower(firstWord(after)) != "for" || !seconds || (!unit.empty() && unit != "seconds" && unit != "second"))
                return error(line, "After 'is typing' can only come for how many seconds."_tr()), Parsed::Bad;
            m.seconds = std::max(0.0f, *seconds);
        }
        break;
    case Kind::Edit: // edits it: words
        if (after.empty() || (after.starts_with(':') && trim(after.substr(1)).empty())) return error(line, "What to change it to is missing."_tr()), Parsed::Bad;
        if (!after.starts_with(':')) return error(line, "'edits it' needs the new words after a colon."_tr()), Parsed::Bad;
        m.text = unquote(after.substr(1));
        break;
    case Kind::React:
        m.text = unquote(after);
        if (m.text.empty()) return error(line, "Which reaction is missing."_tr()), Parsed::Bad;
        break;
    default: // deletes it, reads it
        if (!after.empty()) return error(line, "'{}' doesn't belong in this line."_tr(after)), Parsed::Bad;
        break;
    }
    if (!m.text.empty()) checkTextOf(line, m.text);

    // Its chat, the chat scene's: only its person and the player write in it.
    if (m.mine) {
        m.character = chat_->character;
        m.sender = chat_->character.empty() ? chat_->with : std::string{};
    } else if (shownName({.character = m.character, .sender = m.sender}) != chat_->with) {
        return error(line, "Only {} and the player (me) write in {}'s chat."_tr(chat_->with, chat_->with)), Parsed::Bad;
    }
    return Parsed::Yes;
}

// A character's chat= or call= scene, by its scene line: whose, and whether it's their chat's (else their call's).
std::optional<std::pair<const CharacterDef*, bool>> Parser::phoneSceneFor(const Line& line) const {
    std::string name = lower(afterWords(line.text, 1));
    if (name.ends_with(':')) name.pop_back();
    for (const auto& [key, def] : s_.characters)
        if (def.chatScene == name || def.callScene == name) return std::pair{&def, def.chatScene == name};
    return std::nullopt;
}

// scene NAME that a character's chat= or call= names: what happens when the player opens their chat or calls them
// (and their call's answered), played from its top then, its choices on the phone: in a chat, its lines are the
// chat's (Mika: words); a call's over at its end. Run into or jumped to, the scene is nothing: the story goes on
// after it.
void Parser::phoneScene(const Line& line, const CharacterDef& def, bool chat) {
    scene(line);
    const std::size_t past = emit(cmd::Jump{}, line, true);
    s_.phoneScenes.insert_or_assign(currentScene_, s_.code.size());
    if (chat) chat_ = ChatContext{.with = def.name, .character = def.key};
    else callWith_ = def.name;
    block(line.children);
    chat_.reset();
    callWith_.clear();
    sceneEnds(line);
    endsDue_.clear(); // (ended: the next scene line has none of its own to reach)
    emit(cmd::Return{}, line, true);
    jumpAt(past).target = s_.code.size();
}

// phone call WHO [for N seconds]: they call the player, with the call's lines under it and, last, its
// missed: lines (when the player doesn't answer). (The player calls from the phone.)
void Parser::callBlock(const Line& line) {
    if (phase_ == Phase::Definitions) return block(line.children);
    std::string rest(trim(afterWords(line.text, 2)));
    if (rest.ends_with(':')) rest.pop_back();
    cmd::Call c;
    if (const auto at = wordAt(rest, "for"); at != std::string::npos && at > 0) { // for N seconds, at its end
        const std::string_view tail = std::string_view(rest).substr(at);
        const auto n = parseNumber(firstWord(afterWords(tail, 1)));
        const std::string unit = lower(afterWords(tail, 2));
        if (!n || (!unit.empty() && unit != "seconds" && unit != "second"))
            return error(line, "'for' needs how many seconds the call rings."_tr());
        c.seconds = std::max(0.0f, *n);
        rest = std::string(trim(std::string_view(rest).substr(0, at)));
    }
    const Who who = whoOf(rest, false);
    if (who.empty()) return error(line, "Who the call is from is missing."_tr());
    c.character = who.character;
    c.sender = who.sender;
    if (chat_) error(line, "A call can't be inside a chat."_tr());
    if (!screenLines_.contains("phone")) error(line, "A call needs a screen called 'phone'."_tr());
    calling(line, std::move(c), line.children);
}

// A call (`c`), its lines (`lines`: the last may be missed:), its end, and its missed: lines.
void Parser::calling(const Line& line, cmd::Call c, std::span<const Line> lines) {
    const auto missed = std::ranges::find_if(lines, [](const Line& l) {
        const std::string t = lower(trim(l.text));
        return t == "missed:" || t == "missed";
    });
    if (missed != lines.end() && missed + 1 != lines.end()) error(*missed, "'missed:' comes last, after the call's lines."_tr());
    const std::span<const Line> talk(lines.begin(), missed);
    const std::size_t at = emit(std::move(c), line);
    block(talk);
    emit(cmd::Call{.end = true}, line, true);
    const std::size_t exit = emit(cmd::Jump{}, line, true);
    const std::size_t missedAt = s_.code.size();
    if (missed != lines.end()) block(missed->children);
    jumpAt(exit).target = s_.code.size();
    auto& emitted = std::get<cmd::Call>(s_.code[at].command);
    emitted.missed = missedAt;
    emitted.after = s_.code.size();
}

// The settings at the end of a choice's line (optional=yes, repeat=yes, for N seconds); returns the rest.
std::string_view Parser::choiceSettings(const Line& line, std::string_view text, cmd::Menu& m) {
    std::vector<std::string_view> w; // the words, as parts of `text`
    for (std::size_t i = 0; i < text.size();) {
        const auto start = text.find_first_not_of(" \t", i);
        if (start == std::string_view::npos) break;
        const auto end = std::min(text.size(), text.find_first_of(" \t", start));
        w.push_back(text.substr(start, end - start));
        i = end;
    }
    auto cut = [&](std::size_t n) { // the last n words are settings
        text = trim(text.substr(0, static_cast<std::size_t>(w[w.size() - n].data() - text.data())));
        w.resize(w.size() - n);
    };
    bool returns = false; // return= was written
    while (!w.empty()) {
        const std::string last = lower(w.back());
        const bool lines = last == "lines" || last == "line";
        const std::size_t unit = lines || last == "seconds" || last == "second" ? 1 : 0; // for N [seconds] | for N lines
        if (last.starts_with("optional=")) {
            if (!set(m.optional, boolean(line, last.substr(9), "optional"))) return text;
            cut(1);
        } else if (last.starts_with("return=")) {
            if (!set(m.returns, boolean(line, last.substr(7), "return"))) return text;
            returns = true;
            cut(1);
        } else if (last.starts_with("repeat=")) {
            if (!set(m.repeats, boolean(line, last.substr(7), "repeat"))) return text;
            cut(1);
        } else if (w.size() >= 2 + unit && lower(w[w.size() - 2 - unit]) == "for" && parseNumber(w[w.size() - 1 - unit])) {
            const float n = std::max(0.0f, *parseNumber(w[w.size() - 1 - unit]));
            if (lines) m.lines = static_cast<int>(std::lround(n));
            else m.seconds = n;
            cut(2 + unit);
        } else {
            break;
        }
    }
    if (m.lines > 0 && m.seconds > 0.0f) error(line, "A choice is up for some seconds or for some lines, not both."_tr());
    if (returns && m.lines == 0) error(line, "'return=' only goes with a choice that's up for some lines."_tr());
    return text;
}

// -- Story lines ---------------------------------------------------------------
void Parser::statement(const Line& line) {
    const bool lenient = startsWithCapital(line.text);
    const std::string verb = commandOf(line.text, true);
    if (verb == "scene") {
        if (!attempt(lenient, [&] { scene(line); })) say(line);
        return;
    }
    if (verb.empty() || isBlockWord(verb)) return say(line); // real blocks were handled by block()
    std::optional<Command> c;
    if (!attempt(lenient, [&] { c = command(line, verb, true); })) return say(line);
    if (!c) return;
    if (verb == "jump" && branches_ == 0) sceneEnds(line); // (one of the scene's own lines, in no choice or if: the scene ends with it)
    emit(std::move(*c), line);
}

void Parser::scene(const Line& line) {
    std::string name = lower(afterWords(line.text, 1));
    if (name.ends_with(':')) name.pop_back();
    if (!isName(name)) return error(line, "'{}' can't be a scene name (one word of letters, numbers, _ and -)."_tr(name));
    sceneEnds(line);
    emit(cmd::End{}, line, true); // (the scene before ends here: it never runs on into this one)
    if (auto [it, fresh] = s_.scenes.emplace(name, s_.code.size()); !fresh)
        error(line, "There are two scenes called '{}'."_tr(name));
    else if (name == "start" && line.file != builtinFiles_)
        error(line, "'scene start' belongs in main.vnscript, where the story begins."_tr());
    currentScene_ = endsDue_ = name;
    endsLine_ = Line{.file = line.file, .number = line.number};
    lineCounts_.clear();
}

// The endings the scene being read reaches (ending NAME scene=SCENE): kept as reached as it ends, as a set line of their
// own would (persistent.ending.NAME), but none of its lines. It ends past its last line (back to what started it, or
// the story's over), or with a jump of its own (one in no choice or if: there it leaves before its end).
void Parser::sceneEnds(const Line& line) {
    if (endsDue_.empty()) return;
    for (const Script::Record& ending : s_.records(RecordKind::Ending))
        if (ending.scene == endsDue_) emit(cmd::Set{.variable = "persistent.ending." + lower(ending.name), .value = formula(line, "yes")}, line, true);
}

// "Name: text", "Name expression: text" or narration, then voice=NAME if it plays a voice of its own.
void Parser::say(const Line& line) {
    std::optional<std::string> voice;
    const std::string_view t = withoutVoice(line.text, &voice);
    // A screen's part written where the story is (not indented under a screen line): a mistake, not narration.
    if (!isQuoted(t)) {
        const std::string head = lower(t.substr(0, t.find(' ')));
        static constexpr std::string_view kPartSettings[]{" x=", " y=", " width=", " height=", " action=", " style=", " background=", " image=", " keys="};
        if (std::ranges::find(kKinds, std::string_view(head), &KindSpec::name) != kKinds.end() &&
            std::ranges::any_of(kPartSettings, [&](std::string_view key) { return t.contains(key); }))
            return error(line, "'{}' is a screen part: it goes indented under a screen line."_tr(head));
    }
    // A line that starts as commands do, in lower case, without being one: a mistake too (a capital: text).
    if (const std::string word = unknownCommand(t); !word.empty()) {
        if (const std::string verb = commandOf(t, false); !verb.empty())
            return error(line, "'{}' is an action, not a line of the story."_tr(verb));
        return error(line, "'{}' isn't a command: text that starts with a lower-case letter goes in quotes."_tr(word));
    }
    cmd::Say say;
    const auto colon = t.find(':');
    std::string prefix;
    if (!spoken(t)) {
        say.text = unquote(t);
    } else {
        prefix = trim(t.substr(0, colon));
        say.text = unquote(t.substr(colon + 1));
        if (prefix.ends_with(')') && prefix.find('(') != std::string::npos) { // Aiko (happy):
            const auto open = prefix.rfind('(');
            say.expression = std::string(trim(std::string_view(prefix).substr(open + 1, prefix.size() - open - 2)));
            prefix = std::string(trim(std::string_view(prefix).substr(0, open)));
        }
        const auto space = prefix.rfind(' ');
        if (s_.characters.contains(lower(prefix))) {
            say.character = lower(prefix);
        } else if (say.expression.empty() && space != std::string::npos && s_.characters.contains(lower(prefix.substr(0, space)))) {
            say.character = lower(prefix.substr(0, space)); // Aiko happy:
            say.expression = prefix.substr(space + 1);
        } else {
            say.speaker = prefix; // someone who isn't a defined character
        }
        if (!say.character.empty()) say.voiceId = std::format("{}_{}", currentScene_, ++lineCounts_[say.character]);
    }
    if (nothingToRead(say.text)) error(line, prefix.empty() ? "This line doesn't say anything."_tr() : "{} doesn't say anything."_tr(prefix));
    if (voice) {
        if (voice->empty()) error(line, "Which voice to play is missing."_tr());
        say.voice = std::move(*voice);
        readValues(line, say.voice);
    }
    checkTextOf(line, say.text);
    say.id = fnv1a(say.text, fnv1a(say.character.empty() ? say.speaker : say.character));
    emit(std::move(say), line);
}

// -- Commands (story lines and button actions) ----------------------------------
std::optional<Command> Parser::command(const Line& line, const std::string& verb, bool story, std::string_view text) {
    if (text.empty()) text = line.text;
    const std::string_view rest = afterWords(text, verb.find(' ') == std::string::npos ? 1 : 2);
    auto cursor = words(line, rest);
    if (!cursor) return std::nullopt;
    Cursor& c = *cursor;
    // The command, once nothing is left after it.
    auto finished = [&](Command result) -> std::optional<Command> {
        if (!c.done()) return fail(line, "'{}' doesn't belong in this {} line."_tr(c.peek().text, verb));
        return result;
    };
    auto screenName = [&](std::string_view what) -> std::optional<std::string> {
        const auto name = c.done() || c.at("with") || c.at("and") ? std::nullopt : c.next(); // (open with fade...: no screen yet)
        if (!name) return fail(line, "Which screen to {} is missing."_tr(what));
        // (the built-in textbox's Phone button names the game's phone: it only shows when there's one)
        if (!screenLines_.contains(lower(*name)) && line.file >= builtinFiles_) return fail(line, "There's no screen called '{}'."_tr(*name));
        return lower(*name);
    };
    auto asBackground = [&] { // (as background, anywhere in it: the stage's back)
        for (std::size_t k = 0; k + 1 < c.left(); ++k)
            if (lower(c.peek(k).text) == "as" && lower(c.peek(k + 1).text) == "background") return true;
        return false;
    };
    auto saysAs = [&] { // (as TYPE, anywhere in it)
        for (std::size_t k = 0; k < c.left(); ++k)
            if (lower(c.peek(k).text) == "as") return true;
        return false;
    };

    if ((verb == "show" || verb == "hide") && c.take("screen")) { // show screen NAME / hide screen NAME
        const auto name = screenName(verb + " screen");
        if (!name) return std::nullopt;
        return finished(cmd::Screen{.name = *name, .show = verb == "show"});
    }
    // (a visual with a name of its own: of its type, unless the line says another; a background's, the background)
    const auto namedVisual = verb == "show" && !c.done() ? s_.visuals.find(lower(c.peek().text)) : s_.visuals.end();
    if (verb == "show" && (asBackground() || (namedVisual != s_.visuals.end() && namedVisual->second.type == "background" && !saysAs()))) {
        // show IMAGE as background | show COLOUR as background [bottom=COLOUR] [at PLACE] [y=N] [animation=NAME] [with ANIMATION]; a movie: [loop=no] [volume=50%]
        const auto name = c.at("as") ? std::nullopt : c.next();
        if (!name) return fail(line, "Which visual to show is missing."_tr());
        const std::string shows = namedVisual != s_.visuals.end() ? namedVisual->second.shows : *name;
        cmd::Background background{.image = shows};
        const auto col = parseColour(shows, &s_.palette);
        if (col) background.image = colourKey(*col);
        if (const auto grad = parseGradient(shows, &s_.palette)) background.image = colourKey(grad->first) + "-" + colourKey(grad->second); // (navy-black: fading down to another)
        if (namedVisual != s_.visuals.end()) { // (unless the line says: a visual named as one, its own part that shows, animation and movie's playing)
            const Script::NamedVisual& own = namedVisual->second;
            background.part = {own.comes.x.value_or(0.5f), own.comes.y.value_or(0.5f)};
            background.animation = own.animation;
            background.with = own.animationOnShow;
            background.loop = own.loop.value_or(true), background.volume = own.volume.value_or(1.0f);
        }
        while (!c.done()) {
            if (c.at("as") && c.left() > 1 && lower(c.peek(1).text) == "background") { // (what it is: the background)
                c.skip(2);
            } else if (c.take("at")) { // (the part of a picture bigger than the stage that shows: across, and down)
                StageProps at;
                if (!place(line, c, at)) return std::nullopt;
                background.part.x = *at.x;
            } else if (c.atOption("y")) {
                const auto n = down(line, c.option()->second);
                if (!n) return std::nullopt;
                background.part.y = *n;
            } else if (c.atOption("bottom")) { // (bottom=: the same, as two words)
                const auto opt = c.option();
                if (!col) return fail(line, "A background that fades to a colour needs a colour to start from."_tr());
                const auto bottom = colour(line, opt->second);
                if (!bottom) return std::nullopt;
                background.image = colourKey(*col) + "-" + colourKey(*bottom);
            } else if (c.atOption("animation")) { // (what it plays while it's shown; none: nothing)
                const auto named = animationNamed(line, c.option()->second);
                if (!named) return std::nullopt;
                background.animation = *named;
            } else if (c.atOption("loop")) {
                const auto b = boolean(line, c.option()->second, "loop");
                if (!b) return std::nullopt;
                background.loop = *b;
            } else if (const Took v = amount(line, c, "volume", background.volume); v != Took::No) {
                if (v == Took::Bad) return std::nullopt;
            } else if (const Took t = withAnimation(line, c, background.with); t != Took::No) {
                if (t == Took::Bad) return std::nullopt;
            } else {
                return fail(line, "A background has no '{}'."_tr(c.peek().text));
            }
        }
        return background;
    }
    if (verb == "hide" && c.take("ui")) {
        if (story) return fail(line, "'hide ui' is an action, not a line of the story."_tr());
        return finished(cmd::Ui{.verb = "hide ui"});
    }
    if (verb == "show") return show(line, c, story);
    if (verb == "hide") { // hide NAME [with ANIMATION] [and wait]
        std::string tag;
        if (const CharacterDef* def = takeCharacter(c)) tag = def->key;
        else if (auto name = c.next()) tag = lower(*name);
        else return fail(line, "Who or what to {} is missing."_tr(verb));
        cmd::Hide hide{.tag = tag};
        while (!c.done())
            if (const Took t = withAnimation(line, c, hide.with); t != Took::No) {
                if (t == Took::Bad) return std::nullopt;
            } else if (const Took w = andWait(line, c, hide.wait, story); w != Took::No) {
                if (w == Took::Bad) return std::nullopt;
            } else {
                return fail(line, "'{}' doesn't belong in this {} line."_tr(c.peek().text, verb));
            }
        return hide;
    }
    if (verb == "effect") { // effect NAME... [amount=50%] [fade=2] [for N seconds] (stop effect [fade=2] ends it)
        cmd::Effect e;
        while (!c.done()) {
            if (const Took t = amount(line, c, "fade", e.fade); t != Took::No) {
                if (t == Took::Bad) return std::nullopt;
            } else if (c.take("for")) {
                const auto n = c.number();
                if (!n) return fail(line, "'for' needs a number of seconds."_tr());
                e.seconds = std::max(0.0f, *n);
            } else if (c.atOption("amount")) {
                const auto n = number(line, c.option()->second, "amount");
                if (!n) return std::nullopt;
                e.amount = std::clamp(*n, 0.0f, 1.0f);
            } else if (const auto look = std::ranges::find(kStageLooks, lower(c.peek().text)); look != kStageLooks.end()) {
                e.looks |= 1u << (look - kStageLooks.begin());
                c.skip(1);
            } else {
                return fail(line, "'{}' isn't an effect."_tr(c.peek().text));
            }
        }
        if (e.looks == 0) return fail(line, "Which effect is missing."_tr());
        return e;
    }
    if (verb == "jump") {
        if (c.at("screen")) return fail(line, "'{}' goes to a scene, not a screen."_tr(verb));
        const auto scene = c.next();
        if (!scene) return fail(line, "Which scene to go to is missing."_tr());
        if (!isName(*scene)) return fail(line, "'{}' needs a scene name."_tr(verb));
        return finished(cmd::Jump{.scene = lower(*scene)});
    }
    if (verb == "set") return change(line, rest);
    if (verb == "play") return play(line, c);
    if (verb == "chat") return fail(line, "This chat has no messages."_tr()); // (with its lines: block() reads them)
    if (verb == "phone") return phone(line, rest);
    if (verb == "stop") { // stop NAME | TYPE (a sound by its name, all of a sound type) | weather | effect, then [fade=2]: the one way to stop what goes on
        if (c.take("weather")) {
            cmd::Weather calm;
            if (amount(line, c, "fade", calm.fade) == Took::Bad) return std::nullopt;
            return finished(calm);
        }
        if (c.take("effect")) {
            cmd::Effect none; // (no looks: the effects end)
            if (amount(line, c, "fade", none.fade) == Took::Bad) return std::nullopt;
            return finished(none);
        }
        const auto what = c.done() || c.atOption("fade") ? std::nullopt : c.next();
        if (!what) return fail(line, "What to stop is missing: a sound, a sound type, the weather or the effects."_tr());
        cmd::Play stop{.name = *what, .stop = true};
        readValues(line, stop.name);
        float fade = 0.0f;
        const Took t = amount(line, c, "fade", fade);
        if (t == Took::Bad) return std::nullopt;
        if (t == Took::Yes) stop.fade = fade;
        return finished(stop);
    }
    if (verb == "wait") {
        if (c.done()) return cmd::Wait{};
        const auto n = c.number();
        if (!n) return fail(line, "'wait' takes a number of seconds, or nothing."_tr());
        return finished(cmd::Wait{.seconds = std::max(0.0f, *n)});
    }
    if (verb == "time") return timeLine(line, c);
    if (verb == "weather") return weather(line, c);
    if (verb == "camera") return camera(line, c, story);

    // Menu and engine commands.
    cmd::Ui ui{.verb = verb};
    if (verb == "answer") { // the rest of the line (quotes are optional)
        ui.arg = unquote(rest, true);
        checkTextOf(line, ui.arg);
        return ui;
    }
    if (verb == "open") { // open SCREEN [with ANIMATION] [and close with ANIMATION], or open https://...
        if (const std::string target = unquote(rest, true); target.find("://") != std::string::npos || target.starts_with("www."))
            return ui.arg = target, ui;
        const auto name = screenName("open");
        if (!name) return std::nullopt;
        if (withAnimation(line, c, ui.opening) == Took::Bad) return std::nullopt;
        if (c.at("and") && c.left() > 1 && lower(c.peek(1).text) == "close") { // and close with ANIMATION: how it goes when it closes
            c.skip(2);
            const Took t = withAnimation(line, c, ui.closing);
            if (t == Took::No) return fail(line, "'and close' needs 'with' and an animation after it."_tr());
            if (t == Took::Bad) return std::nullopt;
        }
        ui.arg = *name;
        return finished(ui);
    }
    if (verb == "setting") { // setting NAME VALUE: a player setting (the window's size: bigger or smaller; the language: next, previous or one by name)
        const auto name = c.next();
        if (!name) return fail(line, "Which setting to change is missing."_tr());
        const std::string setting = lower(*name);
        if (!std::ranges::contains(kSettingNames, setting)) return fail(line, "There's no player setting called '{}'."_tr(*name));
        const auto value = c.next();
        if (!value) return fail(line, "What to set {} to is missing."_tr(setting));
        const std::string word = lower(*value);
        std::optional<int> n;
        if (setting == "window_size" && (word == "bigger" || word == "smaller")) n = word == "bigger" ? 1 : -1;
        else if (setting == "window_size") { // or a width and a height, in pixels
            std::optional<float> h;
            if (!c.done())
                if (const auto second = c.next()) h = parseNumber(*second);
            const auto w = parseNumber(word);
            if (!w || !h || *w < 1.0f || *h < 1.0f) return fail(line, "The window's size is a width and a height in pixels, or bigger or smaller."_tr());
            ui.arg = std::format("window_size {} {}", static_cast<int>(*w), static_cast<int>(*h));
            return finished(ui);
        }
        else if (setting == "language" && (word == "next" || word == "previous")) n = word == "next" ? 1 : -1;
        else if (setting == "language") { // (the game's own, or a translation's name or file: known once the game runs)
            ui.arg = std::format("language {}", word);
            return finished(ui);
        }
        else n = settingNumber(line, setting, setting, word);
        if (!n) return std::nullopt;
        ui.arg = std::format("{} {}", setting, *n);
        return finished(ui);
    }
    if (verb == "tools") { // tools WHAT: one of the developer tools' own keys (a control's): nothing in the game
        ui.arg = lower(std::string(trim(rest)));
        if (!std::ranges::contains(kToolsVerbs, ui.arg)) return fail(line, "'{}' isn't one of the developer tools' keys."_tr(ui.arg));
        return ui;
    }
    if (verb == "start" && !c.done()) ui.arg = lower(*c.next());
    if (verb == "open chat" || verb == "phone call" || verb == "open image" || verb == "play song" || verb == "open tip" || verb == "page") {
        // open chat WHO, phone call WHO, open image NAME, play song NAME, open tip NAME, page NAME (in a list: {chat.name}, {song.name}...)
        ui.arg = unquote(rest, true);
        if (ui.arg.empty())
            return fail(line, verb == "open image" ? "Which visual to open is missing."_tr() : verb == "open chat" ? "Whose chat to open is missing."_tr()
                            : verb == "play song"  ? "Which song to play is missing."_tr()   : verb == "open tip"  ? "Which tip to open is missing."_tr()
                            : verb == "page"       ? "Which page to go to is missing: its number, auto or quick."_tr()
                                                   : "Who to call is missing."_tr());
        return ui;
    }
    // choose N (the choice's option N; without N, in a list of options: the one it's on), save N, load N, delete save N
    // (the saves on the page open), replay N (the history's line N's voice, again), rollback N (back to it; without N: a
    // step back): the first is 1. In a list, its item's number: save {save.number}...
    if ((verb == "choose" && !c.done()) || verb == "save" || verb == "load" || verb == "delete save" || verb == "replay" || (verb == "rollback" && !c.done())) {
        const std::string n(trim(rest));
        const auto number = parseNumber(n);
        if (!(n.starts_with('{') && n.ends_with('}')) && (!number || *number < 1.0f || *number != std::floor(*number)))
            return fail(line, verb == "choose"  ? "Which option to choose is missing: its number, 1 for the first."_tr()
                            : verb == "replay"   ? "Which line's voice to play again is missing: its number in the history, 1 for the first."_tr()
                            : verb == "rollback" ? "Which line to go back to is a number in the history, 1 for the first."_tr()
                                                 : "Which save is a number, 1 for the first on the page."_tr());
        if (n.starts_with('{')) readValues(line, n);
        ui.arg = number ? std::format("{}", static_cast<int>(*number)) : n;
        return ui;
    }
    if (verb == "type") { // type LETTER (one: with Shift on, a capital or the sign over it; apostrophe, quote and semicolon: the marks, which
                          // an action can't hold), or type shift, space, left, right (the caret), delete or done: the on-screen keyboard's keys
        const auto key = c.next();
        const std::string word = key ? lower(*key) : std::string{};
        static constexpr std::pair<std::string_view, std::string_view> kMarks[]{{"apostrophe", "'"}, {"quote", "\""}, {"semicolon", ";"}};
        const auto mark = std::ranges::find(kMarks, std::string_view(word), &std::pair<std::string_view, std::string_view>::first);
        static constexpr std::array<std::string_view, 6> kKeys{"shift", "space", "left", "right", "delete", "done"};
        if (!key || !(utf8::length(*key) == 1 || mark != std::end(kMarks) || std::ranges::contains(kKeys, word)))
            return fail(line, "'type' needs one letter after it, or shift, space, left, right, delete or done."_tr());
        ui.arg = utf8::length(*key) == 1 ? *key : mark != std::end(kMarks) ? std::string(mark->second) : word;
        return finished(ui);
    }
    if (verb == "keypad") { // keypad 0 to 9, or keypad back: the phone's lock screen's keypad
        const auto key = c.next();
        if (!key || !(lower(*key) == "back" || (key->size() == 1 && (*key)[0] >= '0' && (*key)[0] <= '9')))
            return fail(line, "'keypad' needs a digit (0 to 9) or back after it."_tr());
        ui.arg = lower(*key);
        return finished(ui);
    }
    if (verb == "open app") { // open app NAME: the phone's app called that
        const auto name = c.next();
        if (!name) return fail(line, "Which app to open is missing."_tr());
        ui.arg = lower(*name);
        if (const auto phone = screenLines_.find("phone"); ui.arg != "home" && ui.arg != "call" && ui.arg != "lock" && !ui.arg.contains('{') && // (the phone's own; a value's: as it goes)
            (phone == screenLines_.end() || !hasAppPart(*phone->second, ui.arg)))
            error(line, "There's no app called '{}'."_tr(*name)); // (said once: the button keeps its action)
    }
    return finished(ui);
}

// A phone line is a call (phone call WHO, with its lines under it: callBlock), never a line of its own.
std::optional<Command> Parser::phone(const Line& line, std::string_view) {
    return fail(line, "'phone' only starts a call: phone call WHO, with what's said indented under it."_tr());
}

// A player setting's value as a number: a number, yes / no, or (scaling, mouths) its word; auto_delay's seconds
// as milliseconds.
std::optional<int> Parser::settingNumber(const Line& line, const std::string& setting, std::string_view key, std::string_view value) {
    if (setting == "mouths") return oneOf<int>(line, key, value, kMouths);
    if (setting == "auto_delay") {
        const auto seconds = number(line, value, key);
        return seconds ? std::optional(static_cast<int>(std::lround(*seconds * 1000.0f))) : std::nullopt;
    }
    if (const auto b = parseBool(value)) return *b ? 1 : 0;
    if (setting == "scaling" && !parseNumber(value)) return oneOf<int>(line, key, value, kScalings);
    if (const auto x = number(line, value, key)) return value.ends_with('%') ? static_cast<int>(std::lround(*x * 100.0f)) : static_cast<int>(*x); // (80%: 80)
    return std::nullopt;
}

// show NAME [expression] [at POSITION] [x= y= zoom= ...] [animation=NAME] [with ANIMATION] [and wait]
std::optional<Command> Parser::show(const Line& line, Cursor& c, bool story) {
    cmd::Show show;
    const Script::VisualType* type = nullptr;      // (the one the line names, or a visual named of one's: its layer, what it takes)
    const Script::NamedVisual* ownVisual = nullptr; // (a visual with a name of its own: where it comes, its size)
    std::optional<float> width, height;             // (a colour's box's, the line's own)
    bool typeSaid = false;                    // (as TYPE: its layer now, not only as it comes)
    auto keyword = [&] { return c.at("at") || c.at("as") || c.at("with") || c.at("and") || c.peek().text.find('=') != std::string::npos; };
    if (c.done() || keyword()) return fail(line, "Who or what to show is missing."_tr()); // (show as image: nothing picked yet)
    if (const CharacterDef* def = takeCharacter(c)) {
        show.character = show.tag = def->key;
        show.comes = def->comes; // (how they come, as their character line says)
        show.comesAnimation = def->animation, show.comesWith = def->animationOnShow;
        if (!c.done() && !keyword()) show.expression = *c.next();
    } else if (auto name = c.next()) {
        show.image = *name;
        show.tag = lower(*name);
        if (const auto v = s_.visuals.find(show.tag); v != s_.visuals.end()) { // (a visual with a name of its own: what it shows and where it comes, as its type has it)
            ownVisual = &v->second;
            show.image = v->second.shows;
            show.comes = v->second.comes;
            show.comesAnimation = v->second.animation, show.comesWith = v->second.animationOnShow;
            show.loop = v->second.loop.value_or(true), show.volume = v->second.volume.value_or(1.0f); // (a movie's, unless the line says)
            if (const auto t = s_.visualTypes.find(v->second.type); t != s_.visualTypes.end()) type = &t->second;
        }
    } else {
        return fail(line, "Who or what to show is missing."_tr());
    }
    while (!c.done()) {
        if (c.take("at")) {
            if (!place(line, c, show.props)) return std::nullopt;
        } else if (c.take("as")) { // (a type every game has, or one of the game's own)
            const auto name = c.next();
            if (!name) return fail(line, "'as' needs a type after it."_tr());
            const auto t = s_.visualTypes.find(lower(*name));
            if (t == s_.visualTypes.end())
                return fail(line, s_.soundTypes.contains(lower(*name)) ? "'{}' is a sound type, not a visual one."_tr(*name) : "There's no type called '{}'."_tr(*name));
            // (what it accepts: a character, or not; an image, a movie or a colour is told apart by the files, the editor's)
            const bool character = !show.character.empty(), takesCharacters = t->second.accepts.contains("character");
            if (character && !takesCharacters) return fail(line, "'{}' doesn't take characters."_tr(*name));
            if (!character && t->second.accepts == "character") return fail(line, "'{}' takes characters: there's no character called '{}'."_tr(*name, show.tag));
            type = &t->second;
            typeSaid = true;
        } else if (const Took t = withAnimation(line, c, show.with); t != Took::No) {
            if (t == Took::Bad) return std::nullopt;
        } else if (const Took w = andWait(line, c, show.wait, story); w != Took::No) {
            if (w == Took::Bad) return std::nullopt;
        } else if (c.atOption("animation")) { // (what it plays while it's shown; none: nothing)
            const auto named = animationNamed(line, c.option()->second);
            if (!named) return std::nullopt;
            show.animation = *named;
        } else if (c.atOption("loop")) { // (a movie's)
            const auto b = boolean(line, c.option()->second, "loop");
            if (!b) return std::nullopt;
            show.loop = *b;
        } else if (const Took v = amount(line, c, "volume", show.volume); v != Took::No) {
            if (v == Took::Bad) return std::nullopt;
        } else if (c.atOption("width") || c.atOption("height")) { // (a colour's box: its size, changed when needed)
            const auto [key, value] = *c.option();
            if (!((key == "width" ? width : height) = boxSize(line, key, value))) return std::nullopt;
        } else if (auto opt = c.option()) {
            if (!stageProp(line, show.props, opt->first, opt->second)) return std::nullopt;
        } else {
            return fail(line, "'{}' doesn't belong in a show line."_tr(c.peek().text));
        }
    }
    if ((width || height) && !(type && show.character.empty() && type->accepts.contains("colour") && (parseColour(show.image, &s_.palette) || parseGradient(show.image, &s_.palette))))
        return fail(line, "'width' and 'height' are the size of a colour's box, and '{}' isn't one."_tr(show.tag)); // (a size is a colour's box's)
    if (type) {
        // A colour as a type that takes colours: a box of it (navy-black: one fading down to another), as big as its visual
        // says (none, or no visual of its own: the whole stage).
        if (show.character.empty() && type->accepts.contains("colour")) {
            if (const auto grad = parseGradient(show.image, &s_.palette)) show.image = colourKey(grad->first) + "-" + colourKey(grad->second);
            else if (const auto col = parseColour(show.image, &s_.palette)) show.image = colourKey(*col);
            if (show.image.starts_with('#')) // (the line's, else its visual's own, else the whole stage)
                show.box = SDL_FPoint{width.value_or(ownVisual ? ownVisual->width.value_or(1.0f) : 1.0f), height.value_or(ownVisual ? ownVisual->height.value_or(1.0f) : 1.0f)};
        }
        if (!show.comes.layer) show.comes.layer = type->layer;           // (as it comes: its own, else its type's)
        if (typeSaid && !show.props.layer) show.props.layer = type->layer; // (its type named: its layer now; its own layer=, changing it for now, wins)
    }
    return show;
}

// The character named by the next words, if any (names may have spaces: "Mr. Tanaka").
const CharacterDef* Parser::takeCharacter(Cursor& c) {
    for (std::size_t n = std::min<std::size_t>(3, c.left()); n >= 1; --n) {
        std::string name;
        for (std::size_t k = 0; k < n; ++k) name += (k ? " " : "") + c.peek(k).text;
        if (const CharacterDef* def = s_.character(lower(name))) {
            c.skip(n);
            return def;
        }
    }
    return nullptr;
}

// at PLACE: a name from a "place" line, or a number from -1 (left) to 1 (right).
bool Parser::place(const Line& line, Cursor& c, StageProps& props) {
    const auto word = c.next();
    if (!word) return error(line, "'at' needs a place on the stage."_tr()), false;
    props.x = across(line, *word);
    return props.x.has_value();
}

std::optional<float> Parser::across(const Line& line, std::string_view value) {
    if (const auto n = stageCoordinate(value, false)) return n;
    if (const auto it = places_.find(lower(value)); it != places_.end()) return it->second;
    return error(line, "There's no place called '{}'."_tr(value)), std::nullopt;
}

std::optional<float> Parser::boxSize(const Line& line, const std::string& key, const std::string& value) {
    const bool pixels = lower(value).ends_with("px");
    const auto n = parseNumber(pixels ? std::string_view(value).substr(0, value.size() - 2) : std::string_view(value));
    if (!n || *n <= 0.0f) return error(line, "'{}' needs a number above 0 (a part of the stage's, or pixels: 640px), not '{}'."_tr(key, value)), std::nullopt;
    return pixels ? *n / static_cast<float>(key == "width" ? s_.gameSettings.width : s_.gameSettings.height) : *n;
}

Parser::Took Parser::comesSetting(const Line& line, const std::string& key, const std::string& value, StageProps& comes, std::optional<std::string>& animation,
                                  std::optional<std::string>& onShow) {
    if (key == "at") return (comes.x = across(line, value)) ? Took::Yes : Took::Bad;
    if (key == "y" || key == "zoom" || key == "opacity" || key == "rotation" || key == "flip" || key == "tint" || key == "layer")
        return stageProp(line, comes, key, value) ? Took::Yes : Took::Bad;
    if (key != "animation" && key != "animation_on_show") return Took::No;
    const auto named = animationNamed(line, value);
    if (!named) return Took::Bad;
    (key == "animation" ? animation : onShow) = *named;
    return Took::Yes;
}

std::optional<float> Parser::down(const Line& line, std::string_view value) {
    const auto n = stageCoordinate(value, true);
    if (!n) error(line, "'y' needs a number from -1 (the bottom) to 1 (the top), not '{}'."_tr(value));
    return n;
}

// An animation the scripts have, by the name a line gives it ("none": none, an empty name); a mistake
// when there's none by that name.
std::optional<std::string> Parser::animationNamed(const Line& line, std::string_view name) {
    const std::string animation = lower(name);
    if (animation == "none") return std::string{};
    if (!s_.animations.contains(animation)) return error(line, "There's no animation called '{}'."_tr(name)), std::nullopt;
    return animation;
}

// with ANIMATION: one of the scripts' animations (none: none) that the line plays.
Parser::Took Parser::withAnimation(const Line& line, Cursor& c, std::optional<std::string>& out) {
    if (!c.take("with")) return Took::No;
    const auto name = c.next();
    if (!name) return error(line, "'with' needs an animation after it."_tr()), Took::Bad;
    const auto named = animationNamed(line, *name);
    if (!named) return Took::Bad;
    out = *named;
    return Took::Yes;
}

// and wait: the story waits for what the line does to end (an action, a click's or a key's, can't).
Parser::Took Parser::andWait(const Line& line, Cursor& c, bool& wait, bool story) {
    if (!c.at("and") || c.left() < 2 || lower(c.peek(1).text) != "wait") return Took::No;
    c.skip(2);
    if (!story) return error(line, "An action can't wait for this to end."_tr()), Took::Bad;
    wait = true;
    return Took::Yes;
}

// y= zoom= opacity= rotation= flip= layer= tint= (across: at)
bool Parser::stageProp(const Line& line, StageProps& p, const std::string& key, const std::string& value) {
    if (key == "flip") {
        const auto b = boolean(line, value, key);
        p.flip = b;
        return b.has_value();
    }
    if (key == "tint") return (p.tint = colour(line, value)).has_value();
    static constexpr auto keys = std::to_array<std::string_view>({"y", "zoom", "opacity", "rotation", "layer"});
    if (!std::ranges::contains(keys, key))
        return error(line, "A show line has no setting '{}'."_tr(key)), false;
    if (key == "y") return (p.y = down(line, value)).has_value();
    const auto n = parseNumber(value);
    if (!n) return error(line, "'{}' needs a number, not '{}'."_tr(key, value)), false;
    if (key == "zoom") p.zoom = *n;
    else if (key == "opacity") p.opacity = *n;
    else if (key == "rotation") p.rotation = *n;
    else if (*n != std::floor(*n)) return error(line, "'layer' needs a whole number (under 0: behind the background), not '{}'."_tr(value)), false;
    else p.layer = static_cast<int>(*n);
    return true;
}

// set X to V, V a formula: set score to score + 1, set seen to not seen (the one way to change a variable)
std::optional<Command> Parser::change(const Line& line, std::string_view rest) {
    cmd::Set set;
    std::string_view value;
    if (lower(firstWord(rest)) != "to") { // (no variable yet: set to 1)
        set.variable = lower(firstWord(rest));
        if (set.variable.empty()) return fail(line, "Which variable to set is missing."_tr());
        value = trim(afterWords(rest, 1));
        if (lower(firstWord(value)) == "to") {
            value = afterWords(value, 1);
        } else if (value.empty()) {
            return fail(line, "What to set {} to is missing."_tr(set.variable));
        } else {
            const std::string low = lower(rest);
            if (const auto to = low.find(" to "); to != std::string::npos) // ("set my score to 1": a name that's more than a word)
                return fail(line, "'{}' can't be a variable name (one word of letters, numbers and _)."_tr(trim(std::string_view(low).substr(0, to))));
            return fail(line, "The word 'to' after '{}' is missing."_tr(set.variable));
        }
    }
    if (set.variable.empty()) return fail(line, "Which variable to set is missing."_tr());
    if (!isVariable(set.variable))
        return fail(line, "'{}' can't be a variable name (one word of letters, numbers and _)."_tr(set.variable));
    for (std::size_t k = 0; k < kRecordKinds.size(); ++k) // (a record's, persistent.ending.NAME: one the game lists)
        if (const std::string head = std::format("persistent.{}.", kRecordKinds[k]); set.variable.starts_with(head)) {
            const auto kind = static_cast<RecordKind>(k);
            if (!s_.record(kind, set.variable.substr(head.size())))
                return fail(line, "There's no {} called '{}'."_tr(kind == RecordKind::Gallery ? "gallery image" : kRecordKinds[k], set.variable.substr(head.size())));
        }
    wrote(set.variable);
    set.value = formula(line, value, "What to set {} to is missing."_tr(set.variable));
    if (!set.value) return std::nullopt;
    return set;
}

// play NAME [as TYPE] [loop=yes|no] [volume=50%] [fade=2] [fade_out=1]: a sound named by a sound line or a file in
// sounds/ (a {value}'s too), as its type plays it unless the line says (as TYPE: a sound type every game has, or the
// game's own).
std::optional<Command> Parser::play(const Line& line, Cursor& c) {
    cmd::Play p;
    const auto name = c.done() || c.at("as") ? std::nullopt : c.next();
    if (!name || name->contains('=')) return fail(line, "Which sound to play is missing."_tr()); // (left out, and what comes after it in its place)
    p.name = *name;
    readValues(line, p.name);
    while (!c.done()) {
        float n = 0.0f;
        if (c.take("as")) { // (a sound type)
            const auto type = c.next();
            if (!type) return fail(line, "'as' needs a type after it."_tr());
            p.type = lower(*type);
            if (!s_.soundTypes.contains(p.type))
                return fail(line, s_.visualTypes.contains(p.type) ? "'{}' is a visual type, not a sound one."_tr(*type) : "There's no sound type called '{}'."_tr(*type));
        } else if (const Took t = amount(line, c, "fade", n); t != Took::No) {
            if (t == Took::Bad) return std::nullopt;
            p.fade = n;
        } else if (const Took o = amount(line, c, "fade_out", n); o != Took::No) {
            if (o == Took::Bad) return std::nullopt;
            p.fadeOut = n;
        } else if (const Took v = amount(line, c, "volume", n); v != Took::No) {
            if (v == Took::Bad) return std::nullopt;
            p.volume = n;
        } else if (c.atOption("loop")) {
            const auto b = boolean(line, c.option()->second, "loop");
            if (!b) return std::nullopt;
            p.loop = *b;
        } else {
            return fail(line, "'{}' doesn't belong in this play line."_tr(c.peek().text));
        }
    }
    return p;
}

// weather rain|snow|petals|leaves|embers [amount=150%] [speed=2] [wind=-0.5] [tint=C] [image=NAME] [behind=yes] [fade=2]
std::optional<Command> Parser::weather(const Line& line, Cursor& c) {
    static constexpr auto kinds = std::to_array<std::string_view>({"rain", "snow", "petals", "leaves", "embers"});
    cmd::Weather w{.kind = lower(c.next().value_or(""))};
    if (w.kind.empty()) return fail(line, "Which weather is missing."_tr());
    if (!std::ranges::contains(kinds, std::string_view(w.kind)))
        return fail(line, "'{}' isn't a kind of weather."_tr(w.kind));
    while (!c.done()) {
        if (const Took t = amount(line, c, "fade", w.fade); t != Took::No) {
            if (t == Took::Bad) return std::nullopt;
            continue;
        }
        const auto opt = c.option();
        if (!opt) return fail(line, "'{}' doesn't belong in a weather line."_tr(c.peek().text));
        const auto& [key, value] = *opt;
        if (key == "image") { // (a visual named by a visual line or a character: what it shows)
            w.image = imageName(s_.visualOf(value));
        } else if (key == "tint") {
            if (!(w.tint = colour(line, value))) return std::nullopt;
        } else if (key == "behind") {
            const auto b = boolean(line, value, key);
            if (!b) return std::nullopt;
            w.behind = *b;
        } else if (key == "amount" || key == "speed" || key == "wind") {
            const auto n = number(line, value, key);
            if (!n) return std::nullopt;
            (key == "amount" ? w.amount : key == "speed" ? w.speed : w.wind) = *n;
        } else {
            return fail(line, "'weather' has no setting '{}'."_tr(key));
        }
    }
    w.amount = std::clamp(w.amount, 0.0f, 10.0f);
    w.speed = std::clamp(w.speed, 0.0f, 10.0f);
    return w;
}

// camera [animation=NAME] [with NAME] [and wait]
std::optional<Command> Parser::camera(const Line& line, Cursor& c, bool story) {
    cmd::Camera cam;
    while (!c.done()) {
        if (c.atOption("animation")) { // (what it keeps playing; none: nothing)
            const auto named = animationNamed(line, c.option()->second);
            if (!named) return std::nullopt;
            cam.animation = *named;
        } else if (const Took t = withAnimation(line, c, cam.with); t != Took::No) {
            if (t == Took::Bad) return std::nullopt;
        } else if (const Took w = andWait(line, c, cam.wait, story); w != Took::No) {
            if (w == Took::Bad) return std::nullopt;
        } else {
            return fail(line, "'{}' doesn't belong in a camera line."_tr(c.peek().text));
        }
    }
    if (!cam.animation && !cam.with) return fail(line, "Which animation to play is missing."_tr());
    return cam;
}

// time WHEN [day=N] (WHEN: a weekday, and a time of day or a part of the day; none: the same time) | time passes [N minutes|hours|days]
std::optional<Command> Parser::timeLine(const Line& line, Cursor& c) {
    const GameSettings& g = s_.gameSettings;
    if (g.time == TimeKind::None) return fail(line, "A time line needs the game's time: choose one in Settings > Time."_tr());
    std::string parts; // (the game's, as the errors list them)
    for (const auto& [name, start] : g.dayParts) parts += (parts.empty() ? "" : ", ") + name;
    // What it can go on to, as the game's time has it: a clock time (not with days and parts of the day), a part of
    // the day, a weekday (real time's calendar, or a date or a weekday it starts at), a day number.
    const bool clocked = g.time != TimeKind::Days, weekdays = g.time == TimeKind::Real || g.timeStartDate >= 0 || g.timeStartWeekday >= 0;
    const std::string when = std::format("{}{}{} {}", clocked ? "a time of day ({}), "_tr(g.clock12 ? "6:30 pm" : "18:30") : std::string{},
                                         "a part of the day ({})"_tr(parts), weekdays ? std::string(", a weekday"_tr()) : std::string{}, "or a day number"_tr());
    cmd::Time t;
    if (c.take("passes")) { // (how long: minutes, hours or days; nothing: to the next part of the day)
        t.passes = true;
        if (c.done()) return t;
        const auto n = c.number();
        int unit = 0;
        for (const auto& [word, minutes] : {std::pair{"minute", 1}, {"minutes", 1}, {"hour", 60}, {"hours", 60}, {"day", 1440}, {"days", 1440}})
            if (!unit && c.take(word)) unit = minutes;
        if (!n || *n <= 0.0f || !unit || !c.done())
            return fail(line, clocked ? "How long is a number of minutes, hours or days: 30 minutes, 2 hours, 1 day (or nothing: to the next part of the day)."_tr()
                                    : "How long is a number of days: 1 day, 2 days (or nothing: to the next part of the day)."_tr());
        t.passing = std::max(1, static_cast<int>(std::lround(*n * static_cast<float>(unit))));
        return t;
    }
    while (!c.done()) {
        if (c.atOption("day")) { // day=N: that day of the story (the first is 1)
            const auto n = parseNumber(c.option()->second);
            if (!n || *n < 1.0f || *n != std::floor(*n)) return fail(line, "The day number is 1 or more: 1 is the first day."_tr());
            t.day = static_cast<int>(*n);
            continue;
        }
        const std::string word = *c.next();
        std::string clock = word; // (6:30 pm: the am or pm as a word of its own)
        if (c.at("am") || c.at("pm")) clock += *c.next();
        if (const auto weekday = weekdayNamed(word); weekday && t.weekday < 0) {
            t.weekday = *weekday;
        } else if (const auto minutes = clockMinutes(clock); minutes && t.minutes < 0 && t.part.empty()) {
            t.minutes = *minutes;
        } else if (const auto part = std::ranges::find(g.dayParts, lower(word), &std::pair<std::string, int>::first);
                   part != g.dayParts.end() && t.minutes < 0 && t.part.empty()) {
            t.part = part->first;
        } else {
            return fail(line, "'{}' isn't {}, or it's said twice."_tr(word, when));
        }
    }
    if (t.weekday < 0 && t.day == 0 && t.minutes < 0 && t.part.empty())
        return fail(line, "When it goes on to is missing: {}."_tr(when));
    if (t.weekday >= 0 && !weekdays)
        return fail(line, "A weekday needs one to start from: Settings > Time > Start day (a date or a weekday)."_tr());
    return t;
}

// Button, key and hook actions: commands separated by ';'.
Commands Parser::actions(const Line& line, std::string_view text) {
    Commands out;
    char quote = 0;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        const char ch = i < text.size() ? text[i] : ';';
        if (quote) {
            if (ch == quote) quote = 0;
        } else if (ch == '"' || ch == '\'') {
            quote = ch;
        } else if (ch == ';') {
            const std::string_view part = trim(text.substr(start, i - start));
            start = i + 1;
            if (part.empty()) continue;
            readValues(line, part); // (open chat {chat.name}...)
            const std::string verb = commandOf(part, false);
            if (verb.empty()) {
                error(line, "'{}' isn't an action."_tr(firstWord(part)));
                continue;
            }
            auto c = command(line, verb, false, part);
            if (!c) continue;
            const auto* jump = std::get_if<cmd::Jump>(&*c);
            const auto* ui = std::get_if<cmd::Ui>(&*c);
            const std::string scene = jump ? jump->scene : ui && ui->verb == "start" ? ui->arg : std::string{};
            if (!scene.empty() && !s_.scenes.contains(scene)) error(line, "There's no scene called '{}'."_tr(scene));
            out.push_back(std::move(*c));
        }
    }
    return out;
}

} // namespace parse

} // namespace vn
