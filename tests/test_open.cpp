/* SPDX-License-Identifier: Unlicense */

// Files handed to the program: `write-it a.rtf b.rtf`, a second launch, the
// file manager's Open With. One document, one window.

#include "check.hpp"
#include "open_plan.hpp"
#include "settings.hpp"

#include <string>
#include <vector>

namespace {

using writeit::close_after_open;
using writeit::OpenAction;
using writeit::OpenStep;
using writeit::plan_open;
using writeit::WindowState;

// Action i of a plan matches; false, not a crash, when the plan is short.
bool is(const std::vector<OpenAction>& plan, size_t i, OpenStep step, int window,
        const std::string& path)
{
  if (i >= plan.size())
    return false;
  const OpenAction& action = plan[i];
  return action.step == step && action.window == window && action.path == path;
}

bool step_is(const std::vector<OpenAction>& plan, size_t i, OpenStep step)
{
  return i < plan.size() && plan[i].step == step;
}

const WindowState kPristine{"", true};
const WindowState kEditedUntitled{"", false};

void one_window_per_file()
{
  // First launch with files: no window yet.
  auto plan = plan_open({"/d/a.rtf"}, {});
  CHECK(plan.size() == 1);
  CHECK(is(plan, 0, OpenStep::LoadNew, -1, "/d/a.rtf"));

  // Every file is opened, not just the last.
  plan = plan_open({"/d/a.rtf", "/d/b.rtf", "/d/c.md"}, {});
  CHECK(plan.size() == 3);
  CHECK(is(plan, 0, OpenStep::LoadNew, -1, "/d/a.rtf"));
  CHECK(is(plan, 1, OpenStep::LoadNew, -1, "/d/b.rtf"));
  CHECK(is(plan, 2, OpenStep::LoadNew, -1, "/d/c.md"));

  // Nothing asked for, nothing to do.
  CHECK(plan_open({}, {kPristine}).empty());
}

void pristine_window_is_reused()
{
  // An empty Untitled nobody has typed in takes the first file.
  auto plan = plan_open({"/d/a.rtf"}, {kPristine});
  CHECK(plan.size() == 1);
  CHECK(is(plan, 0, OpenStep::LoadInto, 0, "/d/a.rtf"));

  plan = plan_open({"/d/a.rtf", "/d/b.rtf"}, {kPristine});
  CHECK(plan.size() == 2);
  CHECK(is(plan, 0, OpenStep::LoadInto, 0, "/d/a.rtf"));
  CHECK(is(plan, 1, OpenStep::LoadNew, -1, "/d/b.rtf"));

  // Each pristine window is used once.
  plan = plan_open({"/d/a.rtf", "/d/b.rtf"}, {kPristine, kEditedUntitled, kPristine});
  CHECK(plan.size() == 2);
  CHECK(is(plan, 0, OpenStep::LoadInto, 0, "/d/a.rtf"));
  CHECK(is(plan, 1, OpenStep::LoadInto, 2, "/d/b.rtf"));
}

void documents_are_never_replaced()
{
  // An edited Untitled is a document: the file gets a new window.
  auto plan = plan_open({"/d/a.rtf"}, {kEditedUntitled});
  CHECK(plan.size() == 1);
  CHECK(is(plan, 0, OpenStep::LoadNew, -1, "/d/a.rtf"));

  // So is an open file, saved or not.
  plan = plan_open({"/d/a.rtf"}, {WindowState{"/d/b.rtf", false}});
  CHECK(plan.size() == 1);
  CHECK(is(plan, 0, OpenStep::LoadNew, -1, "/d/a.rtf"));

  // The same file already open, perhaps with unsaved edits: bring that
  // window forward, never reload it.
  plan = plan_open({"/d/a.rtf"}, {kPristine, WindowState{"/d/a.rtf", false}});
  CHECK(plan.size() == 1);
  CHECK(is(plan, 0, OpenStep::Present, 1, "/d/a.rtf"));

  // Asked for twice: opened once.
  plan = plan_open({"/d/a.rtf", "/d/a.rtf"}, {});
  CHECK(plan.size() == 1);
  CHECK(is(plan, 0, OpenStep::LoadNew, -1, "/d/a.rtf"));
}

void not_local()
{
  // No local path (an sftp:// or http:// URI without a mount): refused, and
  // it does not use up the pristine window. It comes after the files that
  // can be opened.
  auto plan = plan_open({"", "/d/a.rtf"}, {kPristine});
  CHECK(plan.size() == 2);
  CHECK(is(plan, 0, OpenStep::LoadInto, 0, "/d/a.rtf"));
  CHECK(step_is(plan, 1, OpenStep::RefuseNotLocal));

  plan = plan_open({""}, {});
  CHECK(plan.size() == 1);
  CHECK(step_is(plan, 0, OpenStep::RefuseNotLocal));
}

void missing_files_last()
{
  // Files that exist open first; missing ones come after, so their error
  // dialogs (modal, as from File > Open) do not hold up the others.
  const auto exists = [](const std::string& path) { return path.find("missing") == std::string::npos; };
  auto plan = plan_open({"/d/missing.rtf", "/d/a.rtf", "", "/d/b.rtf"}, {kPristine}, exists);
  CHECK(plan.size() == 4);
  CHECK(is(plan, 0, OpenStep::LoadInto, 0, "/d/a.rtf"));
  CHECK(is(plan, 1, OpenStep::LoadNew, -1, "/d/b.rtf"));
  CHECK(step_is(plan, 2, OpenStep::RefuseNotLocal));
  CHECK(is(plan, 3, OpenStep::LoadNew, -1, "/d/missing.rtf"));
  // Only a missing file: it may take the pristine window, which stays.
  plan = plan_open({"/d/missing.rtf"}, {kPristine}, exists);
  CHECK(plan.size() == 1);
  CHECK(is(plan, 0, OpenStep::LoadInto, 0, "/d/missing.rtf"));
}

void failed_windows_close()
{
  // A window made for a file that failed is closed when another window is
  // left, so no empty window is left behind.
  CHECK(close_after_open(1, {true}) == std::vector<bool>{true});
  CHECK(close_after_open(1, {false, true}) == (std::vector<bool>{false, true}));
  CHECK(close_after_open(0, {false, true}) == (std::vector<bool>{false, true}));
  CHECK(close_after_open(0, {true, false}) == (std::vector<bool>{true, false}));
  // Unless it is the only window: the program keeps one to show the error
  // and to be used.
  CHECK(close_after_open(0, {true}) == std::vector<bool>{false});
  CHECK(close_after_open(0, {true, true, true}) == (std::vector<bool>{false, true, true}));
  // Windows that loaded stay.
  CHECK(close_after_open(0, {false, false}) == (std::vector<bool>{false, false}));
  CHECK(close_after_open(2, {}).empty());
}

void recent_files()
{
  using writeit::push_recent;
  using V = std::vector<std::string>;
  CHECK(push_recent({}, "/d/a.rtf", 8) == V{"/d/a.rtf"});
  CHECK(push_recent({"/d/a.rtf"}, "/d/b.rtf", 8) == (V{"/d/b.rtf", "/d/a.rtf"}));
  // Opened again: moves to the top, once.
  CHECK(push_recent({"/d/b.rtf", "/d/a.rtf"}, "/d/a.rtf", 8) == (V{"/d/a.rtf", "/d/b.rtf"}));
  // Cut to the count.
  CHECK(push_recent({"/d/1", "/d/2", "/d/3", "/d/4"}, "/d/5", 4) ==
        (V{"/d/5", "/d/1", "/d/2", "/d/3"}));
}

}  // namespace

int main()
{
  one_window_per_file();
  pristine_window_is_reused();
  documents_are_never_replaced();
  not_local();
  missing_files_last();
  failed_windows_close();
  recent_files();
  return suite_test::done("open");
}
