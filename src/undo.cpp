/* SPDX-License-Identifier: Unlicense */

#include "undo.hpp"

#include <algorithm>
#include <map>

namespace writeit {

struct UndoHistory::Op {
  enum class Kind : unsigned char { Insert, Erase, Apply, Remove, Custom };
  struct Span {
    Glib::RefPtr<Gtk::TextTag> tag;
    int from = 0;
    int to = 0;
  };
  Kind kind = Kind::Insert;
  // Insert and Erase: the text, at `start`, `length` characters long.
  int start = 0;
  int length = 0;
  Glib::ustring text;
  // Erase: the tags the erased text had, relative to `start`.
  std::vector<Span> spans;
  // Apply and Remove: the tag, and the ranges it really changed.
  Glib::RefPtr<Gtk::TextTag> tag;
  std::vector<std::pair<int, int>> ranges;
  // Custom: undo, then redo, and whether the document shows the change.
  bool seen = true;
  std::shared_ptr<std::pair<std::function<void()>, std::function<void()>>> custom;
};

struct UndoHistory::Step {
  std::uint64_t id = 0;
  std::vector<Op> ops;
  // Where undo puts the caret: where the step began (the start of a
  // selection it replaced).
  int caret = 0;
  // Where redo puts it: where the action left it, at the change.
  int after = 0;
  bool selection = false;
  // It began by erasing the selection: typing over a selection. Its insert
  // may start a burst of typing, never join one.
  bool replaced = false;
  Shape shape = Shape::Other;
  // The shape's range: the text inserted, or erased by the run.
  int at = 0;
  int len = 0;
  // While open: what the step has done so far.
  bool may_insert = true;
  bool has_block = false;
  int inserts = 0;
  int erases = 0;
  int erase_start = 0;
  int erase_end = 0;
};

UndoHistory::UndoHistory() = default;

UndoHistory::~UndoHistory()
{
  detach();
}

void UndoHistory::attach(const Glib::RefPtr<Gtk::TextBuffer>& buffer, IgnoreTag ignore)
{
  detach();
  buffer_ = buffer;
  ignore_ = std::move(ignore);
  if (!buffer_)
    return;
  // Before the default handlers, while the buffer still holds what the
  // change replaces.
  connections_.push_back(buffer_->signal_insert().connect(
      [this](const Gtk::TextIter& pos, const Glib::ustring& text, int) { on_insert(pos, text); },
      false));
  connections_.push_back(buffer_->signal_erase().connect(
      [this](const Gtk::TextIter& start, const Gtk::TextIter& end) { on_erase(start, end); },
      false));
  connections_.push_back(buffer_->signal_apply_tag().connect(
      [this](const Glib::RefPtr<Gtk::TextTag>& tag, const Gtk::TextIter& start,
             const Gtk::TextIter& end) { on_tag(tag, start, end, true); },
      false));
  connections_.push_back(buffer_->signal_remove_tag().connect(
      [this](const Glib::RefPtr<Gtk::TextTag>& tag, const Gtk::TextIter& start,
             const Gtk::TextIter& end) { on_tag(tag, start, end, false); },
      false));
}

void UndoHistory::detach()
{
  for (auto& connection : connections_)
    connection.disconnect();
  connections_.clear();
  buffer_.reset();
}

std::uint64_t UndoHistory::state_id() const
{
  return undo_.empty() ? base_id_ : undo_.back()->id;
}

void UndoHistory::clear()
{
  // A step left open would record the next document into itself.
  open_ = false;
  current_.reset();
  stashed_redo_.clear();
  undo_.clear();
  redo_.clear();
  base_id_ = next_id_++;
  emit();
}

std::size_t UndoHistory::top_ops() const
{
  return undo_.empty() ? 0 : undo_.back()->ops.size();
}

std::string UndoHistory::describe_top() const
{
  std::string out;
  if (undo_.empty())
    return out;
  const char* kinds[] = {"insert", "erase", "apply", "remove", "custom"};
  for (const Op& op : undo_.back()->ops) {
    out += kinds[static_cast<int>(op.kind)];
    if (op.kind == Op::Kind::Insert || op.kind == Op::Kind::Erase)
      out += " " + std::to_string(op.start) + " \"" + op.text.raw() + "\"";
    if (op.tag) {
      out += " " + op.tag->property_name().get_value();
      for (const auto& range : op.ranges)
        out += " [" + std::to_string(range.first) + "," + std::to_string(range.second) + ")";
    }
    out += "\n";
  }
  return out;
}

void UndoHistory::open(int caret, bool selection)
{
  current_ = std::make_unique<Step>();
  current_->caret = caret;
  current_->selection = selection;
  open_ = true;
  // Only an edit drops the redo steps: kept aside until close() knows.
  stashed_redo_ = std::move(redo_);
  redo_.clear();
}

bool UndoHistory::recording()
{
  if (replaying_ || !buffer_)
    return false;
  if (open_)
    return true;
  // Changed behind the history's back: its steps no longer fit the buffer.
  if (!undo_.empty() || !redo_.empty()) {
    ++stray_;
    clear();
  }
  return false;
}

void UndoHistory::record_custom(std::function<void()> undo, std::function<void()> redo, bool seen)
{
  if (!open_ || replaying_)
    return;
  Op op;
  op.kind = Op::Kind::Custom;
  op.seen = seen;
  op.custom = std::make_shared<std::pair<std::function<void()>, std::function<void()>>>(
      std::move(undo), std::move(redo));
  current_->ops.push_back(std::move(op));
}

void UndoHistory::push_op(Op&& op)
{
  std::vector<Op>& ops = current_->ops;
  // A tag taken off and put back over the same text (or put on and taken
  // off) is no change: the format edits strip a range's character tag and
  // apply the one it ends up with, often the same. Tag operations on other
  // tags commute with it, so look back past them, not past text.
  if (op.kind == Op::Kind::Apply || op.kind == Op::Kind::Remove) {
    const Op::Kind opposite = op.kind == Op::Kind::Apply ? Op::Kind::Remove : Op::Kind::Apply;
    int looked = 0;
    for (auto it = ops.rbegin(); it != ops.rend() && looked < 32; ++it, ++looked) {
      if (it->kind != Op::Kind::Apply && it->kind != Op::Kind::Remove)
        break;
      if (it->tag != op.tag)
        continue;
      if (it->kind == opposite && it->ranges == op.ranges) {
        ops.erase(std::next(it).base());
        return;
      }
      break;
    }
  }
  // Text inserted and erased again at once is no change either.
  if (op.kind == Op::Kind::Erase && !ops.empty() && ops.back().kind == Op::Kind::Insert &&
      ops.back().start == op.start && ops.back().text == op.text) {
    ops.pop_back();
    return;
  }
  // Typing on from the end of the text just inserted is one longer
  // insertion: undo then erases a burst at once.
  if (op.kind == Op::Kind::Insert && !ops.empty() && ops.back().kind == Op::Kind::Insert &&
      op.start == ops.back().start + ops.back().length) {
    ops.back().text += op.text;
    ops.back().length += op.length;
    return;
  }
  ops.push_back(std::move(op));
}

void UndoHistory::on_insert(const Gtk::TextIter& pos, const Glib::ustring& text)
{
  if (text.empty() || !recording())
    return;
  Op op;
  op.kind = Op::Kind::Insert;
  op.start = pos.get_offset();
  op.text = text;
  op.length = static_cast<int>(text.length());
  Step& step = *current_;
  ++step.inserts;
  if (step.erases > 0 && !(step.replaced && step.erases == 1)) {
    step.may_insert = false;
  } else if (step.replaced && !step.has_block && op.start != step.erase_start) {
    step.may_insert = false;
  } else if (!step.has_block) {
    step.has_block = true;
    step.at = op.start;
    step.len = op.length;
  } else if (op.start >= step.at && op.start <= step.at + step.len) {
    step.len += op.length;
  } else {
    step.may_insert = false;
  }
  push_op(std::move(op));
}

void UndoHistory::on_erase(const Gtk::TextIter& start, const Gtk::TextIter& end)
{
  if (start == end || !recording())
    return;
  Op op;
  op.kind = Op::Kind::Erase;
  op.start = start.get_offset();
  op.length = end.get_offset() - op.start;
  op.text = buffer_->get_slice(start, end, true);
  // The erased text's tags, a span per tag per stretch, walked from toggle
  // to toggle rather than character by character.
  std::map<GtkTextTag*, size_t> open_span;
  for (auto it = start; it.compare(end) < 0;) {
    auto next = it;
    if (!next.forward_to_tag_toggle(Glib::RefPtr<Gtk::TextTag>()) || next.compare(end) > 0)
      next = end;
    const int from = it.get_offset() - op.start;
    const int to = next.get_offset() - op.start;
    for (const auto& tag : it.get_tags()) {
      if (ignore_ && ignore_(tag))
        continue;
      auto found = open_span.find(tag->gobj());
      if (found != open_span.end() && op.spans[found->second].to == from) {
        op.spans[found->second].to = to;
      } else {
        open_span[tag->gobj()] = op.spans.size();
        op.spans.push_back(Op::Span{tag, from, to});
      }
    }
    it = next;
  }
  Step& step = *current_;
  // Typing over a selection erases it first; the typing may still be an
  // insertion that the next keys join.
  if (step.selection && step.erases == 0 && step.inserts == 0 && step.ops.empty())
    step.replaced = true;
  else
    step.may_insert = false;
  ++step.erases;
  step.erase_start = op.start;
  step.erase_end = op.start + op.length;
  push_op(std::move(op));
}

void UndoHistory::on_tag(const Glib::RefPtr<Gtk::TextTag>& tag, const Gtk::TextIter& start,
                         const Gtk::TextIter& end, bool apply)
{
  if (!tag || start.compare(end) >= 0 || (ignore_ && ignore_(tag)) || !recording())
    return;
  // Only the stretches the tag really changes: applied where it was not,
  // removed where it was. Undo then puts back exactly what was there.
  Op op;
  op.kind = apply ? Op::Kind::Apply : Op::Kind::Remove;
  op.tag = tag;
  for (auto it = start; it.compare(end) < 0;) {
    const bool has = it.has_tag(tag);
    auto next = it;
    if (!next.forward_to_tag_toggle(tag) || next.compare(end) > 0)
      next = end;
    if (has != apply)
      op.ranges.emplace_back(it.get_offset(), next.get_offset());
    it = next;
  }
  if (op.ranges.empty())
    return;
  note_tag_shape(op.ranges);
  push_op(std::move(op));
}

void UndoHistory::note_tag_shape(const std::vector<std::pair<int, int>>& ranges)
{
  // Typing stays an insertion while its tag changes stay on the inserted
  // text, or on a paragraph's newline (which holds no character the
  // document's text shows): finish_pending() and tag_line_breaks().
  Step& step = *current_;
  if (!step.may_insert)
    return;
  for (const auto& range : ranges) {
    if (step.has_block && range.first >= step.at && range.second <= step.at + step.len)
      continue;
    if (range.second - range.first == 1 &&
        buffer_->get_iter_at_offset(range.first).get_char() == '\n')
      continue;
    step.may_insert = false;
    return;
  }
}

bool UndoHistory::is_edit(const Step& step) const
{
  // Greg's rule: an action is an edit if it inserted or deleted text, even
  // text that puts back what was there (typing or pasting over a selection
  // with the same text), or if it left some character's tags other than
  // they were. A tag taken off and put back, or put on and taken off, is
  // not. Only the window's own state changing (seen == false) is not either.
  std::map<Gtk::TextTag*, std::vector<std::pair<int, int>>> toggles;
  for (const Op& op : step.ops) {
    switch (op.kind) {
      case Op::Kind::Insert:
      case Op::Kind::Erase:
        return true;
      case Op::Kind::Custom:
        if (op.seen)
          return true;
        break;
      case Op::Kind::Apply:
      case Op::Kind::Remove: {
        // Each range is where the tag really changed (on_tag()), and a step
        // with no text change keeps every offset: a character ends up
        // changed exactly when an odd number of them cover it.
        auto& points = toggles[op.tag.get()];
        for (const auto& range : op.ranges) {
          points.emplace_back(range.first, 1);
          points.emplace_back(range.second, -1);
        }
        break;
      }
    }
  }
  for (auto& entry : toggles) {
    auto& points = entry.second;
    std::sort(points.begin(), points.end());
    int depth = 0;
    for (size_t i = 0; i < points.size(); ++i) {
      depth += points[i].second;
      const bool more = i + 1 < points.size();
      if (depth % 2 != 0 && more && points[i + 1].first > points[i].first)
        return true;
    }
  }
  return false;
}

UndoHistory::Closed UndoHistory::close(int caret, bool may_merge)
{
  if (!open_)
    return Closed::Dropped;
  open_ = false;
  std::unique_ptr<Step> step = std::move(current_);
  if (step->ops.empty() || !is_edit(*step)) {
    // Not an edit: the redo steps still fit, and undoing the step below
    // takes this back too; with none, nothing older can be replayed over it.
    redo_ = std::move(stashed_redo_);
    stashed_redo_.clear();
    if (!undo_.empty())
      for (Op& op : step->ops)
        undo_.back()->ops.push_back(std::move(op));
    return Closed::Dropped;
  }
  stashed_redo_.clear();
  step->after = caret;
  const bool clean_insert = step->erases == 0 || (step->replaced && step->erases == 1);
  if (step->may_insert && step->has_block && step->len > 0 && clean_insert &&
      caret == step->at + step->len) {
    step->shape = Shape::Insertion;
  } else if (step->erases == 1 && step->inserts == 0 && !step->selection) {
    if (step->caret == step->erase_end && caret == step->erase_start)
      step->shape = Shape::Backspace;
    else if (step->caret == step->erase_start && caret == step->erase_start)
      step->shape = Shape::Delete;
    step->at = step->erase_start;
    step->len = step->erase_end - step->erase_start;
  }
  if (may_merge && step->shape != Shape::Other && !step->replaced && !undo_.empty() &&
      undo_.back()->shape == step->shape) {
    Step& top = *undo_.back();
    bool merge = false;
    switch (step->shape) {
      case Shape::Insertion:
        // Typed on from where the last burst ended.
        merge = step->caret == top.at + top.len && step->at == top.at + top.len;
        if (merge)
          top.len += step->len;
        break;
      case Shape::Backspace:
        merge = step->caret == top.at && step->erase_end == top.at;
        if (merge) {
          top.at = step->at;
          top.len += step->len;
        }
        break;
      case Shape::Delete:
        merge = step->caret == top.at && step->at == top.at;
        if (merge)
          top.len += step->len;
        break;
      case Shape::Other:
        break;
    }
    if (merge) {
      for (Op& op : step->ops) {
        // A burst of typing stays one insertion.
        Op* last = top.ops.empty() ? nullptr : &top.ops.back();
        if (op.kind == Op::Kind::Insert && last && last->kind == Op::Kind::Insert &&
            op.start == last->start + last->length) {
          last->text += op.text;
          last->length += op.length;
          continue;
        }
        top.ops.push_back(std::move(op));
      }
      // A merged step is another document state: saved at "ab", "abc" is
      // not saved, though one undo still takes all of it back.
      top.id = next_id_++;
      top.after = caret;
      emit();
      return Closed::Merged;
    }
  }
  step->id = next_id_++;
  if (undo_.size() >= kCap) {
    // Undoing everything now ends after the forgotten step, not before it.
    base_id_ = undo_.front()->id;
    undo_.erase(undo_.begin());
  }
  undo_.push_back(std::move(step));
  emit();
  return Closed::Pushed;
}

void UndoHistory::play(Step& step, bool forward)
{
  // Recording stays off only while replaying, whatever happens; a replay
  // cut short leaves the buffer between states, so no step fits it any more.
  struct Guard {
    bool& flag;
    explicit Guard(bool& f)
        : flag(f)
    {
      flag = true;
    }
    ~Guard()
    {
      flag = false;
    }
  };
  try {
    Guard guard(replaying_);
    play_ops(step, forward);
  } catch (...) {
    clear();
    throw;
  }
}

void UndoHistory::play_ops(Step& step, bool forward)
{
  auto at = [this](int offset) { return buffer_->get_iter_at_offset(offset); };
  auto insert = [&](const Op& op) { buffer_->insert(at(op.start), op.text); };
  auto erase = [&](const Op& op) { buffer_->erase(at(op.start), at(op.start + op.length)); };
  auto tags = [&](const Op& op, bool apply) {
    for (const auto& range : op.ranges) {
      if (apply)
        buffer_->apply_tag(op.tag, at(range.first), at(range.second));
      else
        buffer_->remove_tag(op.tag, at(range.first), at(range.second));
    }
  };
  auto run = [&](const Op& op) {
    switch (op.kind) {
      case Op::Kind::Insert:
        if (forward)
          insert(op);
        else
          erase(op);
        break;
      case Op::Kind::Erase:
        if (forward) {
          erase(op);
        } else {
          // Inserted text takes the tags of the text before it; it gets
          // exactly the ones it had instead.
          insert(op);
          buffer_->remove_all_tags(at(op.start), at(op.start + op.length));
          for (const auto& span : op.spans)
            buffer_->apply_tag(span.tag, at(op.start + span.from), at(op.start + span.to));
        }
        break;
      case Op::Kind::Apply:
        tags(op, forward);
        break;
      case Op::Kind::Remove:
        tags(op, !forward);
        break;
      case Op::Kind::Custom:
        if (forward)
          op.custom->second();
        else
          op.custom->first();
        break;
    }
  };
  if (forward) {
    for (const Op& op : step.ops)
      run(op);
  } else {
    for (auto it = step.ops.rbegin(); it != step.ops.rend(); ++it)
      run(*it);
  }
}

int UndoHistory::undo()
{
  if (undo_.empty() || open_ || !buffer_)
    return -1;
  std::unique_ptr<Step> step = std::move(undo_.back());
  undo_.pop_back();
  play(*step, false);
  const int back = step->caret;
  redo_.push_back(std::move(step));
  emit();
  return back;
}

int UndoHistory::redo()
{
  if (redo_.empty() || open_ || !buffer_)
    return -1;
  std::unique_ptr<Step> step = std::move(redo_.back());
  redo_.pop_back();
  play(*step, true);
  const int back = step->after;
  undo_.push_back(std::move(step));
  emit();
  return back;
}

}  // namespace writeit
