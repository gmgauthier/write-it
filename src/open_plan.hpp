/* SPDX-License-Identifier: Unlicense */

#pragma once

#include <string>
#include <vector>

namespace writeit {

// Opening files handed to the program: `write-it a.rtf b.md`, a second
// launch while it runs, or the file manager's Open and Open With. One
// document, one window: each file gets a window, and no window holding a
// document is ever reloaded, so nothing unsaved is lost.

// A window as the planner sees it.
struct WindowState {
  std::string path;       // the RTF file it saves to, or "" (Untitled or an import)
  bool pristine = false;  // Untitled, never edited: a new launch's empty window
};

enum class OpenStep {
  Present,         // the file is already open in `window`: bring it forward
  LoadInto,        // load into `window`, a pristine one
  LoadNew,         // load into a new window
  RefuseNotLocal,  // no local path (a non-local URI): say so, open nothing
};

struct OpenAction {
  OpenStep step;
  int window = -1;  // index into the windows given, for Present and LoadInto
  std::string path;
};

// One action per requested file, in order. `paths` are local paths from
// Gio::File::get_path(), "" for a file with none. A file already open, or
// asked for twice, is presented rather than loaded again. The first file may
// take one pristine window; the others get new windows.
std::vector<OpenAction> plan_open(const std::vector<std::string>& paths,
                                  const std::vector<WindowState>& windows);

// After the actions run: for each window created for them, in order,
// whether it failed to load (missing, unreadable, not a document) and so
// holds only an empty Untitled. Returns which of those to close: all of
// them while any other window remains, else all but the first, so the
// program always has a window to show its error in.
std::vector<bool> close_after_open(int existing_windows, const std::vector<bool>& created_failed);

}  // namespace writeit
