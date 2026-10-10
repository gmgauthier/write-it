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
  // Custom: undo, then redo.
  std::shared_ptr<std::pair<std::function<void()>, std::function<void()>>> custom;
};

struct UndoHistory::Step {
  std::uint64_t id = 0;
  std::vector<Op> ops;
  // Where undo puts the caret; on the redo stack, where redo puts it.
  int caret = 0;
  bool selection = false;
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
  undo_.clear();
  redo_.clear();
  base_id_ = next_id_++;
  emit();
}

std::size_t UndoHistory::top_ops() const
{
  return undo_.empty() ? 0 : undo_.back()->ops.size();
}

void UndoHistory::open(int caret, bool selection)
{
  current_ = std::make_unique<Step>();
  current_->caret = caret;
  current_->selection = selection;
  open_ = true;
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

void UndoHistory::record_custom(std::function<void()> undo, std::function<void()> redo)
{
  if (!open_ || replaying_)
    return;
  Op op;
  op.kind = Op::Kind::Custom;
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
  if (step.erases > 0) {
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
  ++step.erases;
  step.may_insert = false;
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

UndoHistory::Closed UndoHistory::close(int caret, bool may_merge)
{
  if (!open_)
    return Closed::Dropped;
  open_ = false;
  std::unique_ptr<Step> step = std::move(current_);
  if (step->ops.empty())
    return Closed::Dropped;
  if (step->may_insert && step->has_block && step->len > 0 && step->erases == 0 &&
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
  if (may_merge && step->shape != Shape::Other && !undo_.empty() &&
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
      for (Op& op : step->ops)
        top.ops.push_back(std::move(op));
      // A merged step is another document state: saved at "ab", "abc" is
      // not saved, though one undo still takes all of it back.
      top.id = next_id_++;
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
  replaying_ = true;
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
  replaying_ = false;
}

int UndoHistory::undo(int caret)
{
  if (undo_.empty() || open_ || !buffer_)
    return -1;
  std::unique_ptr<Step> step = std::move(undo_.back());
  undo_.pop_back();
  play(*step, false);
  const int back = step->caret;
  step->caret = caret;
  redo_.push_back(std::move(step));
  emit();
  return back;
}

int UndoHistory::redo(int caret)
{
  if (redo_.empty() || open_ || !buffer_)
    return -1;
  std::unique_ptr<Step> step = std::move(redo_.back());
  redo_.pop_back();
  play(*step, true);
  const int back = step->caret;
  step->caret = caret;
  undo_.push_back(std::move(step));
  emit();
  return back;
}

}  // namespace writeit
