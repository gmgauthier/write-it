/* SPDX-License-Identifier: Unlicense */

// A body rebuild is one custom undo step when either document has a table
// or a picture with bytes. Plain text, and a picture with no bytes, are not.

#include "check.hpp"
#include "document.hpp"

namespace {

writeit::Document text_doc(const char* text)
{
  writeit::Document doc;
  writeit::Paragraph paragraph;
  writeit::Run run;
  run.text = text;
  paragraph.runs.push_back(run);
  doc.paragraphs.push_back(paragraph);
  return doc;
}

writeit::Document picture_doc(const std::string& data, int table)
{
  writeit::Document doc = text_doc("Hello");
  writeit::Run run;
  writeit::Image image;
  image.alt = "image";
  image.type = data.empty() ? "" : "png";
  image.data = data;
  run.image = image;
  doc.paragraphs[0].runs.push_back(run);
  doc.paragraphs[0].cell.table = table;
  return doc;
}

writeit::Document table_doc()
{
  writeit::Document doc = text_doc("cell");
  doc.paragraphs[0].cell.table = 1;
  doc.paragraphs[0].cell.rows = 1;
  doc.paragraphs[0].cell.columns = 1;
  return doc;
}

}  // namespace

constexpr int kChecks = 10;

int main()
{
  const writeit::Document text = text_doc("Hello");
  const writeit::Document other = text_doc("world");
  const writeit::Document table = table_doc();
  const writeit::Document empty_pic = picture_doc("", 0);
  const writeit::Document png = picture_doc("png-bytes", 0);
  const writeit::Document cell_png = picture_doc("png-bytes", 2);

  CHECK(!writeit::document_needs_custom_undo(text, other));
  CHECK(writeit::document_needs_custom_undo(table, text));
  CHECK(writeit::document_needs_custom_undo(text, table));
  CHECK(!writeit::document_needs_custom_undo(empty_pic, text));
  CHECK(!writeit::document_needs_custom_undo(text, empty_pic));
  CHECK(writeit::document_needs_custom_undo(png, text));
  CHECK(writeit::document_needs_custom_undo(text, png));
  CHECK(writeit::document_needs_custom_undo(cell_png, text));
  CHECK(writeit::document_needs_custom_undo(text, cell_png));
  CHECK(!writeit::document_needs_custom_undo(writeit::Document{}, writeit::Document{}));

  return suite_test::done("undo-picture", kChecks);
}
