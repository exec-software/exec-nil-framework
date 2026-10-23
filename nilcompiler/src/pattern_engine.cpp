#include "pattern_engine.hpp"

#include <algorithm>
#include <bitset>
#include <deque>
#include <memory>
#include <stdexcept>
#include <utility>

namespace ny::patterns {
namespace {

using bytes = std::bitset<256>;
constexpr std::size_t unlimited = static_cast<std::size_t>(-1);

struct failure {
  status code;
  std::string message;
};

[[noreturn]] void exceeded(const char *resource) {
  throw failure{status::limit, std::string("pattern limit exceeded: ") + resource};
}

bool digit(unsigned char value) { return value >= '0' && value <= '9'; }
bool letter(unsigned char value) {
  return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
}
int hex(unsigned char value) {
  if (digit(value)) return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  return -1;
}
bytes singleton(unsigned char value) {
  bytes output;
  output.set(value);
  return output;
}

enum class node_kind { empty, byte_set, sequence, alternative, capture, repeat,
                       begin, end, lookahead };
struct node {
  node_kind kind = node_kind::empty;
  bytes accepted;
  std::vector<std::size_t> children;
  std::size_t group = 0;
  std::size_t minimum = 0;
  std::size_t maximum = 0;
  bool lazy = false;
  bool negative = false;
};

class parser {
public:
  explicit parser(std::string_view source) : source_(source) {
    if (source.size() > limits::pattern_bytes) exceeded("pattern bytes");
    if (source.find('\0') != std::string_view::npos)
      invalid("NUL is not supported by the byte-pattern text profile");
  }

  std::size_t parse() {
    const auto root = expression(0);
    if (position_ != source_.size()) invalid("unmatched closing parenthesis");
    return root;
  }

  std::vector<node> nodes;
  std::vector<std::string> names = {""};

private:
  struct class_item {
    bytes accepted;
    int single = -1;
  };
  std::string_view source_;
  std::size_t position_ = 0;

  [[noreturn]] void invalid(const char *message) const {
    throw failure{status::invalid_pattern,
                  "pattern byte " + std::to_string(position_) + ": " + message};
  }
  bool at(char value) const {
    return position_ < source_.size() && source_[position_] == value;
  }
  std::size_t add(node value) {
    if (nodes.size() >= limits::nodes) exceeded("syntax nodes");
    nodes.push_back(std::move(value));
    return nodes.size() - 1;
  }
  std::size_t compound(node_kind kind, std::vector<std::size_t> children) {
    if (children.empty()) return add(node{});
    if (children.size() == 1) return children.front();
    node value;
    value.kind = kind;
    value.children = std::move(children);
    return add(std::move(value));
  }
  std::size_t expression(std::size_t depth) {
    if (depth > limits::nesting) exceeded("group nesting");
    std::vector<std::size_t> alternatives;
    for (;;) {
      std::vector<std::size_t> sequence;
      while (position_ < source_.size() && !at('|') && !at(')'))
        sequence.push_back(repetition(depth));
      alternatives.push_back(compound(node_kind::sequence, std::move(sequence)));
      if (!at('|')) break;
      ++position_;
    }
    return compound(node_kind::alternative, std::move(alternatives));
  }
  std::size_t count() {
    if (position_ == source_.size() || !digit(source_[position_]))
      invalid("expected a decimal repetition count");
    std::size_t value = 0;
    while (position_ < source_.size() && digit(source_[position_])) {
      const auto next = static_cast<std::size_t>(source_[position_] - '0');
      if (value > (limits::repetition - next) / 10)
        exceeded("repetition count");
      value = value * 10 + next;
      ++position_;
    }
    return value;
  }
  std::size_t repetition(std::size_t depth) {
    const auto child = atom(depth);
    if (position_ == source_.size()) return child;
    node value;
    value.kind = node_kind::repeat;
    value.children = {child};
    if (at('?')) {
      value.maximum = 1;
      ++position_;
    } else if (at('*') || at('+')) {
      value.minimum = at('+') ? 1 : 0;
      value.maximum = unlimited;
      ++position_;
    } else if (at('{')) {
      ++position_;
      value.minimum = count();
      value.maximum = value.minimum;
      if (at(',')) {
        ++position_;
        value.maximum = at('}') ? unlimited : count();
      }
      if (!at('}')) invalid("expected closing brace after repetition count");
      ++position_;
      if (value.maximum != unlimited && value.maximum < value.minimum)
        invalid("maximum repetition count is smaller than minimum");
    } else {
      return child;
    }
    if (nodes[child].kind == node_kind::begin || nodes[child].kind == node_kind::end ||
        nodes[child].kind == node_kind::lookahead)
      invalid("a zero-width assertion cannot be quantified directly");
    if (at('?')) {
      value.lazy = true;
      ++position_;
    }
    if (at('*') || at('+') || at('?') || at('{'))
      invalid("multiple or possessive quantifiers are not supported");
    return add(std::move(value));
  }
  class_item escaped() {
    if (position_ == source_.size()) invalid("unfinished escape");
    const unsigned char value = source_[position_++];
    if (digit(value)) invalid("backreferences and octal escapes are not supported");
    if (value == 'd' || value == 'D' || value == 'w' || value == 'W' ||
        value == 's' || value == 'S') {
      bytes accepted;
      for (unsigned int byte = 1; byte < 256; ++byte) {
        const auto ch = static_cast<unsigned char>(byte);
        const bool included = value == 'd' || value == 'D' ? digit(ch)
            : value == 'w' || value == 'W' ? digit(ch) || letter(ch) || ch == '_'
            : ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
        if (included) accepted.set(byte);
      }
      if (value == 'D' || value == 'W' || value == 'S') {
        accepted.flip();
        accepted.reset(0);
      }
      return {accepted, -1};
    }
    unsigned char literal = value;
    switch (value) {
    case 'n': literal = '\n'; break;
    case 'r': literal = '\r'; break;
    case 't': literal = '\t'; break;
    case 'f': literal = '\f'; break;
    case 'v': literal = '\v'; break;
    case 'x': {
      if (source_.size() - position_ < 2 || hex(source_[position_]) < 0 ||
          hex(source_[position_ + 1]) < 0)
        invalid("hexadecimal byte escapes require exactly two hex digits");
      literal = static_cast<unsigned char>(hex(source_[position_]) * 16 +
                                           hex(source_[position_ + 1]));
      position_ += 2;
      if (literal == 0) invalid("NUL escapes are not supported");
      break;
    }
    default:
      if (letter(value)) invalid("unsupported escape in byte-pattern profile");
    }
    return {singleton(literal), literal};
  }
  class_item class_atom() {
    if (position_ == source_.size()) invalid("unfinished character class");
    const unsigned char value = source_[position_++];
    if (value == '\\') return escaped();
    if (value == '[' && (at(':') || at('.') || at('=')))
      invalid("POSIX character-class extensions are not supported");
    return {singleton(value), value};
  }
  bytes character_class() {
    const bool negative = at('^');
    if (negative) ++position_;
    bytes accepted;
    bool first = true;
    while (position_ < source_.size() && (!at(']') || first)) {
      first = false;
      const auto left = class_atom();
      if (at('-') && position_ + 1 < source_.size() && source_[position_ + 1] != ']') {
        ++position_;
        const auto right = class_atom();
        if (left.single < 0 || right.single < 0)
          invalid("character-class range endpoints must each be a single byte");
        if (left.single > right.single) invalid("descending character-class range");
        for (int byte = left.single; byte <= right.single; ++byte) accepted.set(byte);
      } else {
        accepted |= left.accepted;
      }
    }
    if (!at(']')) invalid("unfinished character class");
    ++position_;
    if (negative) accepted.flip();
    accepted.reset(0);
    return accepted;
  }
  std::size_t atom(std::size_t depth) {
    const unsigned char value = source_[position_++];
    node result;
    if (value == '(') {
      bool capturing = true;
      bool assertion = false;
      bool negative = false;
      std::string name;
      if (at('?')) {
        ++position_;
        if (at(':')) {
          capturing = false;
          ++position_;
        } else if (at('=') || at('!')) {
          capturing = false;
          assertion = true;
          negative = at('!');
          ++position_;
        } else if (at('<')) {
          ++position_;
          if (at('=') || at('!')) invalid("lookbehind is not supported");
          const auto start = position_;
          if (position_ == source_.size() ||
              (!letter(source_[position_]) && !at('_')))
            invalid("capture names must be ASCII identifiers");
          while (position_ < source_.size() &&
                 (letter(source_[position_]) || digit(source_[position_]) || at('_')))
            ++position_;
          if (!at('>')) invalid("expected closing angle bracket after capture name");
          name = std::string(source_.substr(start, position_ - start));
          if (name.size() > 64) exceeded("capture name bytes");
          if (std::find(names.begin(), names.end(), name) != names.end())
            invalid("duplicate capture name");
          ++position_;
        } else {
          invalid("unsupported group extension or flags");
        }
      }
      std::size_t group = 0;
      if (capturing) {
        if (names.size() > limits::captures) exceeded("capture groups");
        group = names.size();
        names.push_back(std::move(name));
      }
      const auto child = expression(depth + 1);
      if (!at(')')) invalid("unfinished group");
      ++position_;
      if (!capturing && !assertion) return child;
      result.kind = assertion ? node_kind::lookahead : node_kind::capture;
      result.children = {child};
      result.group = group;
      result.negative = negative;
    } else if (value == '^' || value == '$') {
      result.kind = value == '^' ? node_kind::begin : node_kind::end;
    } else if (value == '*' || value == '+' || value == '?' || value == '{' || value == '}') {
      invalid("quantifier has no preceding expression or has unmatched braces");
    } else {
      result.kind = node_kind::byte_set;
      if (value == '[') result.accepted = character_class();
      else if (value == '\\') result.accepted = escaped().accepted;
      else if (value == '.') {
        result.accepted.set();
        result.accepted.reset(0);
        result.accepted.reset('\n');
      } else result.accepted = singleton(value);
    }
    return add(std::move(result));
  }
};

enum class operation { byte_set, split, jump, save, begin, end, lookahead,
                       accept, mark, progress };
struct instruction {
  operation code = operation::accept;
  bytes accepted;
  std::size_t first = 0;
  std::size_t second = 0;
  std::size_t slot = 0;
  bool negative = false;
};
struct program {
  std::vector<instruction> code;
  std::vector<std::string> names;
  std::size_t loops = 0;
};

class compiler {
public:
  explicit compiler(const parser &syntax) : syntax_(syntax) {}
  std::shared_ptr<const program> compile(std::size_t root) {
    output_.names = syntax_.names;
    emit(operation::save).slot = 0;
    compile_node(root);
    emit(operation::save).slot = 1;
    emit(operation::accept);
    return std::make_shared<const program>(std::move(output_));
  }

private:
  const parser &syntax_;
  program output_;
  std::size_t steps_ = 0;
  instruction &emit(operation code) {
    if (output_.code.size() >= limits::instructions) exceeded("compiled instructions");
    instruction value;
    value.code = code;
    output_.code.push_back(value);
    return output_.code.back();
  }
  void compile_node(std::size_t index) {
    if (++steps_ > limits::compile_steps) exceeded("compile steps");
    const auto &value = syntax_.nodes[index];
    switch (value.kind) {
    case node_kind::empty: break;
    case node_kind::byte_set: emit(operation::byte_set).accepted = value.accepted; break;
    case node_kind::begin: emit(operation::begin); break;
    case node_kind::end: emit(operation::end); break;
    case node_kind::sequence:
      for (const auto child : value.children) compile_node(child);
      break;
    case node_kind::capture:
      emit(operation::save).slot = value.group * 2;
      compile_node(value.children.front());
      emit(operation::save).slot = value.group * 2 + 1;
      break;
    case node_kind::alternative: {
      std::vector<std::size_t> jumps;
      for (std::size_t child = 0; child < value.children.size(); ++child) {
        if (child + 1 == value.children.size()) {
          compile_node(value.children[child]);
        } else {
          const auto branch = output_.code.size();
          emit(operation::split).first = branch + 1;
          compile_node(value.children[child]);
          jumps.push_back(output_.code.size());
          emit(operation::jump);
          output_.code[branch].second = output_.code.size();
        }
      }
      for (const auto jump : jumps) output_.code[jump].first = output_.code.size();
      break;
    }
    case node_kind::lookahead: {
      const auto assertion = output_.code.size();
      auto &entry = emit(operation::lookahead);
      entry.first = assertion + 1;
      entry.negative = value.negative;
      compile_node(value.children.front());
      emit(operation::accept);
      output_.code[assertion].second = output_.code.size();
      break;
    }
    case node_kind::repeat: {
      for (std::size_t count = 0; count < value.minimum; ++count)
        compile_node(value.children.front());
      if (value.maximum == unlimited) {
        if (output_.loops >= limits::loop_slots) exceeded("repetition loop slots");
        const auto loop = output_.loops++;
        const auto branch = output_.code.size();
        emit(operation::split);
        const auto body = output_.code.size();
        emit(operation::mark).slot = loop;
        compile_node(value.children.front());
        const auto check = output_.code.size();
        auto &progress = emit(operation::progress);
        progress.slot = loop;
        progress.first = branch;
        const auto end = output_.code.size();
        output_.code[check].second = end;
        output_.code[branch].first = value.lazy ? end : body;
        output_.code[branch].second = value.lazy ? body : end;
      } else {
        std::vector<std::size_t> branches;
        for (std::size_t count = value.minimum; count < value.maximum; ++count) {
          const auto branch = output_.code.size();
          branches.push_back(branch);
          emit(operation::split);
          if (value.lazy) output_.code[branch].second = branch + 1;
          else output_.code[branch].first = branch + 1;
          compile_node(value.children.front());
        }
        for (const auto branch : branches) {
          if (value.lazy) output_.code[branch].first = output_.code.size();
          else output_.code[branch].second = output_.code.size();
        }
      }
      break;
    }
    }
  }
};

struct cache_entry {
  std::string source;
  std::shared_ptr<const program> compiled;
  std::size_t bytes;
};
struct program_cache {
  std::vector<cache_entry> entries;
  std::size_t bytes = 0;
};
thread_local program_cache cache;

std::shared_ptr<const program> get_program(std::string_view pattern) {
  for (std::size_t index = 0; index < cache.entries.size(); ++index) {
    if (cache.entries[index].source != pattern) continue;
    auto result = cache.entries[index].compiled;
    std::rotate(cache.entries.begin(), cache.entries.begin() + index,
                cache.entries.begin() + index + 1);
    return result;
  }
  parser syntax(pattern);
  const auto root = syntax.parse();
  auto compiled = compiler(syntax).compile(root);
  std::size_t bytes = sizeof(program) + sizeof(cache_entry) + pattern.size() +
      compiled->code.capacity() * sizeof(instruction) +
      compiled->names.capacity() * sizeof(std::string);
  for (const auto &name : compiled->names) bytes += name.capacity();
  if (bytes <= limits::cache_bytes) {
    while (!cache.entries.empty() &&
           (cache.entries.size() >= limits::cache_entries ||
            bytes > limits::cache_bytes - cache.bytes)) {
      cache.bytes -= cache.entries.back().bytes;
      cache.entries.pop_back();
    }
    cache.entries.insert(cache.entries.begin(), {std::string(pattern), compiled, bytes});
    cache.bytes += bytes;
  }
  return compiled;
}

struct choice {
  std::size_t pc = 0;
  std::size_t position = 0;
  std::vector<std::int64_t> captures;
  std::vector<std::int64_t> progress;
};
struct frame : choice {
  // Pop releases blocks rather than retaining a high-water vector capacity in
  // every nested assertion. The budget charges snapshots; small container and
  // allocator bookkeeping is not a process-RSS measurement.
  std::deque<choice> alternatives;
  bool negative = false;
};

class matcher {
public:
  matcher(const program &compiled, std::string_view input)
      : compiled_(compiled), input_(input),
        state_bytes_(sizeof(frame) + compiled.names.size() * 2 * sizeof(std::int64_t) +
                     compiled.loops * sizeof(std::int64_t)) {}

  result run(std::size_t start, bool full) {
    for (std::size_t candidate = start; candidate <= input_.size(); ++candidate) {
      tick();
      frames_.clear();
      states_ = 0;
      reserve_state();
      frame initial;
      initial.position = candidate;
      initial.captures.assign(compiled_.names.size() * 2, -1);
      initial.progress.assign(compiled_.loops, -1);
      frames_.push_back(std::move(initial));
      bool failing = false;
      while (!frames_.empty()) {
        tick();
        auto &active = frames_.back();
        if (failing) {
          if (!active.alternatives.empty()) {
            auto restore = std::move(active.alternatives.back());
            active.alternatives.pop_back();
            --states_;
            active.pc = restore.pc;
            active.position = restore.position;
            active.captures = std::move(restore.captures);
            active.progress = std::move(restore.progress);
            failing = false;
          } else {
            const bool negative = active.negative;
            frames_.pop_back();
            --states_;
            // A failed negative assertion succeeds in the parent, whose
            // position/captures were never modified. Root failure retries the
            // next candidate with the same total execution budget.
            failing = !negative;
          }
          continue;
        }
        const auto &instruction = compiled_.code[active.pc];
        switch (instruction.code) {
        case operation::byte_set:
          if (active.position == input_.size() ||
              !instruction.accepted.test(static_cast<unsigned char>(input_[active.position])))
            failing = true;
          else { ++active.position; ++active.pc; }
          break;
        case operation::save:
          active.captures[instruction.slot] = static_cast<std::int64_t>(active.position);
          ++active.pc;
          break;
        case operation::begin:
          if (active.position != 0) failing = true;
          else ++active.pc;
          break;
        case operation::end:
          if (active.position != input_.size()) failing = true;
          else ++active.pc;
          break;
        case operation::jump: active.pc = instruction.first; break;
        case operation::split: {
          reserve_state();
          active.alternatives.push_back(choice{instruction.second, active.position,
                                                active.captures, active.progress});
          active.pc = instruction.first;
          break;
        }
        case operation::mark:
          active.progress[instruction.slot] = static_cast<std::int64_t>(active.position);
          ++active.pc;
          break;
        case operation::progress:
          active.pc = active.progress[instruction.slot] == static_cast<std::int64_t>(active.position)
              ? instruction.second : instruction.first;
          break;
        case operation::lookahead: {
          if (frames_.size() > limits::nesting) exceeded("assertion nesting");
          reserve_state();
          frame assertion;
          assertion.pc = instruction.first;
          assertion.position = active.position;
          assertion.captures = active.captures;
          assertion.progress = active.progress;
          assertion.negative = instruction.negative;
          active.pc = instruction.second;
          frames_.push_back(std::move(assertion));
          break;
        }
        case operation::accept:
          if (frames_.size() == 1) {
            if (full && active.position != input_.size()) {
              failing = true;
              break;
            }
            result matched;
            matched.code = status::match;
            matched.spans.reserve(compiled_.names.size());
            for (std::size_t group = 0; group < compiled_.names.size(); ++group) {
              const auto begin = active.captures[group * 2];
              const auto end = active.captures[group * 2 + 1];
              matched.spans.emplace_back(begin < 0 || end < 0 ? -1 : begin,
                                          begin < 0 || end < 0 ? -1 : end - begin);
            }
            return matched;
          } else {
            const bool negative = active.negative;
            auto captured = std::move(active.captures);
            states_ -= active.alternatives.size() + 1;
            frames_.pop_back();
            if (!negative) frames_.back().captures = std::move(captured);
            // Assertions are atomic: their internal alternatives do not leak
            // into the parent's later backtracking.
            failing = negative;
          }
          break;
        }
      }
      if (full) break;
    }
    return {};
  }

private:
  const program &compiled_;
  std::string_view input_;
  const std::size_t state_bytes_;
  std::vector<frame> frames_;
  std::size_t steps_ = 0;
  std::size_t states_ = 0;
  void tick() {
    if (++steps_ > limits::execution_steps) exceeded("execution steps");
  }
  void reserve_state() {
    if (states_ >= limits::backtracking_states ||
        states_ >= limits::backtracking_bytes / state_bytes_)
      exceeded("backtracking storage");
    ++states_;
  }
};

} // namespace

std::string validate(std::string_view pattern) {
  try {
    (void)get_program(pattern);
    return {};
  } catch (const failure &error) {
    return error.message;
  } catch (...) {
    return "pattern engine could not allocate compilation storage";
  }
}

result search(std::string_view pattern, std::string_view input,
              std::int64_t start, bool full) {
  try {
    const auto compiled = get_program(pattern);
    if (input.size() > limits::input_bytes) exceeded("input bytes");
    if (input.find('\0') != std::string_view::npos)
      return {status::error, {}, "pattern input contains NUL; expected NUL-free byte text"};
    if (start < 0 || static_cast<std::uint64_t>(start) > input.size() || (full && start != 0))
      return {status::invalid_offset, {}, "pattern start must be within the input; full matching requires start zero"};
    return matcher(*compiled, input).run(static_cast<std::size_t>(start), full);
  } catch (const failure &error) {
    return {error.code, {}, error.message};
  } catch (...) {
    return {status::error, {}, "pattern engine could not allocate execution storage"};
  }
}

std::int64_t group_index(std::string_view pattern, std::string_view name) {
  if (name.empty() || name.size() > 64 || name.find('\0') != std::string_view::npos) return -1;
  try {
    const auto compiled = get_program(pattern);
    for (std::size_t index = 1; index < compiled->names.size(); ++index)
      if (compiled->names[index] == name) return static_cast<std::int64_t>(index);
  } catch (...) {}
  return -1;
}

} // namespace ny::patterns
