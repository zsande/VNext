#pragma once

// The story's flowchart: the points the player has passed in every playthrough.

#include "Values.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vn {

// The story's flowchart: the points the player has passed in every playthrough (choices and endings),
// joined by the way they went. Each point keeps the game as it was the last time the player was there,
// so they can go back to it. Kept in flowchart.txt.
class Flowchart {
public:
    enum class Kind : std::uint8_t { Choice, Ending };
    struct Option {
        int index = 0;     // in the choice as written
        bool open = false; // it could be picked (else it was shown locked: needs ...)
    };
    struct Node {
        std::string id;              // "scene:offset": where it is in the scripts
        Kind kind = Kind::Choice;
        int order = 0;               // the order places were first reached in
        std::string state;           // the game there (a save that repeats the place)
        std::vector<Option> options; // a choice's options, as shown last time (hidden ones left out)
    };
    struct Edge {
        std::string from, to;
        int option = -1; // the option picked at `from`, when it's a choice
    };

    // The player reached `id` from `from` (empty: from the start), having picked `option` there.
    void visit(const std::string& id, Kind kind, const std::string& from, int option, std::string state,
               std::vector<Option> options = {});
    [[nodiscard]] const std::vector<Node>& nodes() const { return nodes_; }
    [[nodiscard]] const std::vector<Edge>& edges() const { return edges_; }
    [[nodiscard]] const Node* node(std::string_view id) const;
    void clear();

    bool read(std::string_view saved);     // flowchart.txt's text (false: it isn't one)
    [[nodiscard]] std::string text() const; // ...and the flowchart as that
    [[nodiscard]] bool takeChanged() { return std::exchange(changed_, false); }

private:
    std::vector<Node> nodes_;
    std::vector<Edge> edges_;
    bool changed_ = false;
};

} // namespace vn
