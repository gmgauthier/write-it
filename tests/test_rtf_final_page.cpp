/* SPDX-License-Identifier: Unlicense */

#include "check.hpp"
#include "document.hpp"

constexpr int kChecks = 8;

int main()
{
  writeit::Document ended;
  CHECK(writeit::rtf_import("{\\rtf1\\ansi Hello\\page}", ended));
  CHECK(ended.paragraphs.size() == 2);
  CHECK(ended.paragraphs[0].runs.size() == 1 && ended.paragraphs[0].runs[0].text == "Hello");
  CHECK(!ended.paragraphs[0].page_break);
  CHECK(ended.paragraphs[1].page_break);
  CHECK(ended.paragraphs[1].runs.empty());

  writeit::Document closed;
  CHECK(writeit::rtf_import("{\\rtf1\\ansi Hello\\page\\par}", closed));
  CHECK(closed.paragraphs.size() == 2 && closed.paragraphs[1].page_break &&
        closed.paragraphs[0].runs[0].text == "Hello");

  return suite_test::done("rtf-final-page", kChecks);
}
