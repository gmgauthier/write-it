/* SPDX-License-Identifier: Unlicense */

#include "check.hpp"
#include "document.hpp"

constexpr int kChecks = 16;

writeit::Document paragraph(const char* text)
{
  writeit::Document doc = writeit::blank_document("Sans", 11);
  writeit::Run run;
  run.text = text;
  run.font = "Sans";
  run.size = 11;
  doc.paragraphs[0].runs.push_back(run);
  return doc;
}

int main()
{
  const std::string star = writeit::markdown_export(paragraph("2 * 3"));
  CHECK(star == "2 \\* 3");
  const writeit::Document star_back = writeit::markdown_import(star, "Sans", 11);
  CHECK(star_back.paragraphs.size() == 1);
  CHECK(star_back.paragraphs[0].runs.size() == 1);
  CHECK(star_back.paragraphs[0].runs[0].text == "2 * 3");
  CHECK(!star_back.paragraphs[0].runs[0].italic);

  const std::string hash = writeit::markdown_export(paragraph("# Hello"));
  CHECK(hash == "\\# Hello");
  const writeit::Document hash_back = writeit::markdown_import(hash, "Sans", 11);
  CHECK(hash_back.paragraphs[0].heading == 0);
  CHECK(hash_back.paragraphs[0].runs[0].text == "# Hello");

  const std::string dash = writeit::markdown_export(paragraph("- item"));
  CHECK(dash == "\\- item");
  const writeit::Document dash_back = writeit::markdown_import(dash, "Sans", 11);
  CHECK(dash_back.paragraphs[0].list.kind == writeit::ListKind::None);
  CHECK(dash_back.paragraphs[0].runs[0].text == "- item");

  const std::string under = writeit::markdown_export(paragraph("a_b"));
  CHECK(under == "a\\_b");
  const writeit::Document under_back = writeit::markdown_import(under, "Sans", 11);
  CHECK(under_back.paragraphs[0].runs.size() == 1);
  CHECK(under_back.paragraphs[0].runs[0].text == "a_b");
  CHECK(!under_back.paragraphs[0].runs[0].italic);

  const writeit::Document bold = writeit::markdown_import("A **bold** word", "Sans", 11);
  CHECK(bold.paragraphs[0].runs[1].text == "bold" && bold.paragraphs[0].runs[1].bold);

  return suite_test::done("markdown-escape", kChecks);
}
