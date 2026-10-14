/* SPDX-License-Identifier: Unlicense */

#include "check.hpp"
#include "document.hpp"

constexpr int kChecks = 8;

int main()
{
  const writeit::Document plain = writeit::plain_import("one\rtwo\rthree", "Sans", 11);
  CHECK(plain.paragraphs.size() == 3);
  CHECK(plain.paragraphs[0].runs[0].text == "one");
  CHECK(plain.paragraphs[1].runs[0].text == "two");
  CHECK(plain.paragraphs[2].runs[0].text == "three");

  const writeit::Document crlf = writeit::plain_import("one\r\ntwo\r\n", "Sans", 11);
  CHECK(crlf.paragraphs.size() == 2);
  CHECK(crlf.paragraphs[0].runs[0].text == "one");
  CHECK(crlf.paragraphs[1].runs[0].text == "two");

  const writeit::Document md = writeit::markdown_import("# Title\r\n\r\nNext", "Sans", 11);
  CHECK(md.paragraphs.size() == 2 && md.paragraphs[0].heading == 1 &&
        md.paragraphs[0].runs[0].text == "Title" && md.paragraphs[1].runs[0].text == "Next");

  return suite_test::done("plain-breaks", kChecks);
}
