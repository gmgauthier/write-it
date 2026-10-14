/* SPDX-License-Identifier: Unlicense */

#include "check.hpp"
#include "main_window.hpp"

#include <vector>

namespace {

void other_stories()
{
  const std::vector<writeit::FindPiece> pieces = {
      {0, "Hello"},
      {1, "Secret"},
  };
  const writeit::PieceHit header = writeit::find_in_pieces(pieces, "Secret", 0, 0, true);
  CHECK(header.id == 1);
  CHECK(header.index == 0);
  CHECK(header.length == 6);
  const writeit::PieceHit back = writeit::find_in_pieces(pieces, "Hello", 1, 0, false);
  CHECK(back.id == 0);
  CHECK(back.index == 0);
  CHECK(back.length == 5);
  const writeit::PieceHit none = writeit::find_in_pieces(pieces, "Nope", 0, 0, true);
  CHECK(none.id < 0);
}

void continues_then_wraps()
{
  const std::vector<writeit::FindPiece> pieces = {
      {0, "Hello Hello"},
      {2, "Cell"},
      {3, "Foot"},
  };
  const writeit::PieceHit second = writeit::find_in_pieces(pieces, "Hello", 0, 6, true);
  CHECK(second.id == 0);
  CHECK(second.index == 6);
  const writeit::PieceHit cell = writeit::find_in_pieces(pieces, "Cell", 0, 0, true);
  CHECK(cell.id == 2);
  CHECK(cell.index == 0);
  const writeit::PieceHit wrapped = writeit::find_in_pieces(pieces, "Hello", 0, 7, true);
  CHECK(wrapped.id == 0);
  CHECK(wrapped.index == 0);
  const writeit::PieceHit foot = writeit::find_in_pieces(pieces, "foot", 0, 0, false);
  CHECK(foot.id == 3);
  CHECK(foot.length == 4);
  const writeit::PieceHit empty = writeit::find_in_pieces({}, "a", 0, 0, true);
  CHECK(empty.id < 0);
}

}  // namespace

constexpr int kChecks = 16;

int main()
{
  other_stories();
  continues_then_wraps();
  return suite_test::done("find-stories", kChecks);
}
