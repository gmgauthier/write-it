/* SPDX-License-Identifier: Unlicense */

#include "check.hpp"
#include "main_window.hpp"

namespace {

constexpr unsigned kCtrl = GDK_CONTROL_MASK;
constexpr unsigned kCtrlShift = GDK_CONTROL_MASK | GDK_SHIFT_MASK;

void word_keys_pass()
{
  CHECK(writeit::story_key(GDK_KEY_Left, kCtrl) == writeit::StoryKey::Pass);
  CHECK(writeit::story_key(GDK_KEY_Right, kCtrl) == writeit::StoryKey::Pass);
  CHECK(writeit::story_key(GDK_KEY_Up, kCtrl) == writeit::StoryKey::Pass);
  CHECK(writeit::story_key(GDK_KEY_Down, kCtrl) == writeit::StoryKey::Pass);
  CHECK(writeit::story_key(GDK_KEY_Left, kCtrlShift) == writeit::StoryKey::Pass);
  CHECK(writeit::story_key(GDK_KEY_BackSpace, kCtrl) == writeit::StoryKey::Pass);
  CHECK(writeit::story_key(GDK_KEY_Delete, kCtrl) == writeit::StoryKey::Pass);
  CHECK(writeit::story_key(GDK_KEY_Home, kCtrl) == writeit::StoryKey::Pass);
  CHECK(writeit::story_key(GDK_KEY_End, kCtrl) == writeit::StoryKey::Pass);
  CHECK(writeit::story_key(GDK_KEY_a, kCtrl) == writeit::StoryKey::Pass);
  CHECK(writeit::story_key(GDK_KEY_c, kCtrl) == writeit::StoryKey::Pass);
  CHECK(writeit::story_key(GDK_KEY_v, kCtrl) == writeit::StoryKey::Pass);
  CHECK(writeit::story_key(GDK_KEY_x, kCtrl) == writeit::StoryKey::Pass);
  CHECK(writeit::story_key(GDK_KEY_Left, 0) == writeit::StoryKey::Pass);
  CHECK(writeit::story_key(GDK_KEY_z, 0) == writeit::StoryKey::Pass);
}

void undo_redo_stay()
{
  CHECK(writeit::story_key(GDK_KEY_z, kCtrl) == writeit::StoryKey::Undo);
  CHECK(writeit::story_key(GDK_KEY_y, kCtrl) == writeit::StoryKey::Redo);
  CHECK(writeit::story_key(GDK_KEY_z, kCtrlShift) == writeit::StoryKey::Redo);
  CHECK(writeit::story_key(GDK_KEY_Z, kCtrl) == writeit::StoryKey::Redo);
  CHECK(writeit::story_key(GDK_KEY_Z, kCtrlShift) == writeit::StoryKey::Redo);
}

}  // namespace

constexpr int kChecks = 20;

int main()
{
  word_keys_pass();
  undo_redo_stay();
  return suite_test::done("cell-word-keys", kChecks);
}
