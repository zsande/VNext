// The flowchart: the story's places the player has been through (kept across plays), and going back to one.
#include "Engine.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <charconv>
#include <format>
#include <sstream>

namespace vn {

namespace {

constexpr std::string_view kMagic = "vnext-flowchart 1";

// A save is several lines; in flowchart.txt it's one.
std::string flatten(std::string_view text) {
    std::string out;
    for (char c : text) {
        if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

std::string unflatten(std::string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) out += text[++i] == 'n' ? '\n' : text[i];
        else out += text[i];
    }
    return out;
}

} // namespace

void Flowchart::visit(const std::string& id, Kind kind, const std::string& from, int option, std::string state,
                  std::vector<Option> options) {
    auto it = std::ranges::find(nodes_, id, &Node::id);
    if (it == nodes_.end()) it = nodes_.insert(nodes_.end(), Node{.id = id, .order = static_cast<int>(nodes_.size())});
    it->kind = kind;
    it->state = std::move(state);
    if (kind == Kind::Choice) it->options = std::move(options);
    if (!from.empty() && from != id &&
        std::ranges::none_of(edges_, [&](const Edge& e) { return e.from == from && e.to == id && e.option == option; }))
        edges_.push_back({from, id, option});
    changed_ = true;
}

const Flowchart::Node* Flowchart::node(std::string_view id) const {
    auto it = std::ranges::find(nodes_, id, &Node::id);
    return it != nodes_.end() ? &*it : nullptr;
}

void Flowchart::clear() {
    nodes_.clear();
    edges_.clear();
    changed_ = true;
}

// flowchart.txt:  node ID KIND ORDER [OPTION:OPEN ...]   state ID SAVE   edge FROM TO OPTION
bool Flowchart::read(std::string_view saved) {
    nodes_.clear();
    edges_.clear();
    std::istringstream in{std::string(saved)};
    std::string line;
    if (!std::getline(in, line) || line != kMagic) return false;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string key, id;
        ls >> key >> id;
        if (key == "node") {
            Node n{.id = id};
            int kind = 0;
            ls >> kind >> n.order;
            n.kind = static_cast<Kind>(std::clamp(kind, 0, 1));
            for (std::string option; ls >> option;)
                if (const auto colon = option.find(':'); colon != std::string::npos)
                    if (int to = 0; std::from_chars(option.data(), option.data() + colon, to).ec == std::errc{}) n.options.push_back({to, option[colon + 1] == '1'});
            nodes_.push_back(std::move(n));
        } else if (key == "state") {
            std::string state;
            std::getline(ls >> std::ws, state);
            if (auto it = std::ranges::find(nodes_, id, &Node::id); it != nodes_.end()) it->state = unflatten(state);
        } else if (key == "edge") {
            Edge e{.from = id};
            ls >> e.to >> e.option;
            if (!e.to.empty()) edges_.push_back(std::move(e));
        }
    }
    std::ranges::sort(nodes_, {}, &Node::order);
    return true;
}

std::string Flowchart::text() const {
    std::string out = std::format("{}\n", kMagic);
    for (const Node& n : nodes_) {
        out += std::format("node {} {} {}", n.id, static_cast<int>(n.kind), n.order);
        for (const Option& o : n.options) out += std::format(" {}:{}", o.index, o.open ? 1 : 0);
        out += std::format("\nstate {} {}\n", n.id, flatten(n.state));
    }
    for (const Edge& e : edges_) out += std::format("edge {} {} {}\n", e.from, e.to, e.option);
    return out;
}

// The player is at a point on the flowchart (a choice or an ending): it
// remembers the game here, and which way the player came.
void Game::passPoint(std::size_t here, Flowchart::Kind kind, std::vector<Flowchart::Option> options) {
    const auto [label, offset] = script_.locate(here);
    const std::string id = std::format("{}:{}", label.empty() ? "-" : label, offset);
    const std::size_t resume = std::exchange(resumePc_, here); // the game here repeats this command
    std::string state = save();
    resumePc_ = resume;
    ctx_.flowchart.visit(id, kind, flowchartNode_, flowchartOption_, std::move(state), std::move(options));
    flowchartNode_ = id;
    flowchartOption_ = -1;
}

void Engine::goToPlace(const std::string& id, std::string question) {
    if (sampling_) return;
    auto go = [this, id] {
        if (const Flowchart::Node* point = flowchart_.node(id); point && !point->state.empty())
            if (const auto went = resume(point->state); !went) logWarn("Can't go back to that point in the story: {}.", went.error());
    };
    if (mode_ == Mode::Game) ask(std::move(question), go);
    else go();
}

std::optional<std::size_t> Engine::pointCommand(std::string_view id) const {
    const auto colon = id.rfind(':');
    if (colon == std::string_view::npos) return std::nullopt;
    const auto start = script_.findScene(id.substr(0, colon));
    std::size_t offset = 0;
    if (!start || std::from_chars(id.data() + colon + 1, id.data() + id.size(), offset).ec != std::errc{}) return std::nullopt;
    if (*start + offset >= script_.code.size()) return std::nullopt;
    return *start + offset;
}

} // namespace vn
