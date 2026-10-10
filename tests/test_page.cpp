/* SPDX-License-Identifier: Unlicense */

#include "check.hpp"
#include "document.hpp"
#include "view.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

namespace {

bool has(const std::string& text, const std::string& part)
{
  return text.find(part) != std::string::npos;
}

writeit::Run text_run(const char* text)
{
  writeit::Run run;
  run.text = text;
  return run;
}

// Prints the file when a round trip is not the same document, so a failure
// names the words that moved.
bool same_document(const writeit::Document& doc, const char* name)
{
  const std::string rtf = writeit::rtf_export(doc);
  writeit::Document again;
  if (writeit::rtf_import(rtf, again) && again == doc)
    return true;
  std::cerr << name << " did not round-trip\n" << rtf << "\n";
  return false;
}

void default_bytes()
{
  writeit::Document doc;
  writeit::Paragraph paragraph;
  paragraph.runs.push_back(text_run("Hello"));
  doc.paragraphs.push_back(paragraph);
  const std::string rtf = writeit::rtf_export(doc);
  CHECK(rtf.find("\\paperw") == std::string::npos);
  CHECK(rtf.find("\\paperh") == std::string::npos);
  CHECK(rtf.find("\\margl") == std::string::npos);
  CHECK(rtf.find("\\landscape") == std::string::npos);
  CHECK(rtf.find("\\cols") == std::string::npos);
  CHECK(rtf.find("\\page") == std::string::npos);
  CHECK(rtf.find("\\header") == std::string::npos);
  CHECK(rtf.find("\\footer") == std::string::npos);
  CHECK(rtf.find("\\footnote") == std::string::npos);
  CHECK(rtf.find("\\trowd") == std::string::npos);
  CHECK(rtf.find("\\pict") == std::string::npos);
  CHECK(same_document(doc, "default"));
}

void page_setup()
{
  writeit::Document doc;
  doc.page.paper_width = 15840;
  doc.page.paper_height = 12240;
  doc.page.landscape = true;
  doc.page.margin_left = 1440;
  doc.page.margin_right = 1440;
  doc.page.margin_top = 1440;
  doc.page.margin_bottom = 1440;
  doc.page.columns = 2;
  doc.page.column_gap = 720;
  writeit::Paragraph first;
  first.runs.push_back(text_run("Left"));
  writeit::Paragraph second;
  second.page_break = true;
  second.runs.push_back(text_run("Next"));
  doc.paragraphs.push_back(first);
  doc.paragraphs.push_back(second);
  const std::string rtf = writeit::rtf_export(doc);
  CHECK(has(rtf, "\\paperw15840"));
  CHECK(has(rtf, "\\paperh12240"));
  CHECK(has(rtf, "\\landscape"));
  CHECK(has(rtf, "\\margl1440"));
  CHECK(has(rtf, "\\margr1440"));
  CHECK(has(rtf, "\\margt1440"));
  CHECK(has(rtf, "\\margb1440"));
  CHECK(has(rtf, "\\cols2"));
  CHECK(has(rtf, "\\colsx720"));
  CHECK(has(rtf, "\\page"));
  CHECK(same_document(doc, "page setup"));

  writeit::Document swapped;
  CHECK(writeit::rtf_import(
      "{\\rtf1\\ansi\\paperw11906\\paperh16838\\landscape\\pard Hello\\par}", swapped));
  CHECK(swapped.page.landscape);
  CHECK(swapped.page.paper_width == 16838);
  CHECK(swapped.page.paper_height == 11906);
}

void clamp_limits()
{
  writeit::PageSetup huge;
  huge.paper_width = 999999;
  huge.paper_height = 10;
  huge.margin_left = 999999;
  huge.margin_right = 999999;
  huge.margin_top = 999999;
  huge.margin_bottom = 999999;
  huge.columns = 99;
  huge.column_gap = -5;
  const writeit::PageSetup clamped = writeit::clamp_page(huge);
  CHECK(clamped.paper_width == writeit::kMaxPaperTwips);
  CHECK(clamped.paper_height == writeit::kMinPaperTwips);
  CHECK(clamped.paper_width - clamped.margin_left - clamped.margin_right == writeit::kMinTextTwips);
  CHECK(clamped.paper_height - clamped.margin_top - clamped.margin_bottom ==
        writeit::kMinTextTwips);
  CHECK(clamped.columns == writeit::kMaxColumns);
  CHECK(clamped.column_gap == 0);
  writeit::PageSetup one = writeit::default_page();
  one.column_gap = 100;
  CHECK(writeit::clamp_page(one).column_gap == writeit::default_page().column_gap);
  CHECK(writeit::page_metrics_default(writeit::default_page()));
  writeit::PageSetup columns = writeit::default_page();
  columns.columns = 2;
  CHECK(writeit::page_metrics_default(columns));
  CHECK(!writeit::page_metrics_default(clamped));
}

void tables()
{
  writeit::Document doc;
  writeit::Paragraph intro;
  intro.runs.push_back(text_run("Before"));
  doc.paragraphs.push_back(intro);
  CHECK(writeit::insert_table(doc.paragraphs, 1, 2, 2, writeit::page_text_twips(doc.page)));
  CHECK(!writeit::insert_table(doc.paragraphs, 1, 0, 2, 1000));
  CHECK(!writeit::insert_table(doc.paragraphs, 1, 2, 99, 1000));
  doc.paragraphs[1].runs.push_back(text_run("A"));
  doc.paragraphs[2].runs.push_back(text_run("B"));
  doc.paragraphs[3].runs.push_back(text_run("C"));
  doc.paragraphs[4].runs.push_back(text_run("D"));
  const std::string rtf = writeit::rtf_export(doc);
  CHECK(has(rtf, "\\trowd"));
  CHECK(has(rtf, "\\intbl"));
  CHECK(has(rtf, "\\cell"));
  CHECK(has(rtf, "\\row"));
  CHECK(same_document(doc, "table"));

  CHECK(writeit::in_table(doc.paragraphs, 1));
  CHECK(!writeit::in_table(doc.paragraphs, 0));
  CHECK(writeit::insert_table_row(doc.paragraphs, 1));
  CHECK(doc.paragraphs[1].cell.rows == 3);
  CHECK(doc.paragraphs[3].cell.row == 1 && doc.paragraphs[3].runs.empty());
  CHECK(writeit::insert_table_column(doc.paragraphs, 1));
  CHECK(doc.paragraphs[1].cell.columns == 3);
  CHECK(writeit::delete_table_column(doc.paragraphs, 2));
  CHECK(doc.paragraphs[1].cell.columns == 2);
  CHECK(writeit::delete_table_row(doc.paragraphs, 3));
  CHECK(doc.paragraphs[1].cell.rows == 2);
  CHECK(!writeit::insert_table_row(doc.paragraphs, 0));
  CHECK(!writeit::delete_table_column(doc.paragraphs, 0));
}

void stories()
{
  writeit::Document doc;
  writeit::Paragraph body;
  writeit::Run see;
  see.text = "See ";
  writeit::Run mark;
  mark.text = "1";
  mark.note = 1;
  body.runs.push_back(see);
  body.runs.push_back(mark);
  doc.paragraphs.push_back(body);
  writeit::Paragraph note;
  note.runs.push_back(text_run("A note."));
  doc.notes.push_back({note});
  writeit::Paragraph header;
  header.runs.push_back(text_run("Head"));
  doc.header.push_back(header);
  writeit::Paragraph footer;
  footer.runs.push_back(text_run("Foot"));
  doc.footer.push_back(footer);
  const std::string rtf = writeit::rtf_export(doc);
  CHECK(has(rtf, "{\\header"));
  CHECK(has(rtf, "Head"));
  CHECK(has(rtf, "{\\footer"));
  CHECK(has(rtf, "Foot"));
  CHECK(has(rtf, "{\\footnote"));
  CHECK(has(rtf, "A note."));
  CHECK(same_document(doc, "stories"));
}

void images()
{
  writeit::Document doc;
  writeit::Paragraph paragraph;
  writeit::Run run;
  writeit::Image image;
  image.type = "png";
  image.data = std::string("\x89PNG\r\n\x1a\n", 8) + "fake-bytes";
  image.width = 1440;
  image.height = 720;
  run.image = image;
  paragraph.runs.push_back(run);
  doc.paragraphs.push_back(paragraph);
  const std::string rtf = writeit::rtf_export(doc);
  CHECK(has(rtf, "\\pngblip"));
  CHECK(has(rtf, "\\picwgoal1440"));
  CHECK(has(rtf, "\\pichgoal720"));
  CHECK(same_document(doc, "image"));
  CHECK(writeit::image_type_of(image.data) == "png");
  CHECK(writeit::image_type_of(std::string("\xff\xd8\xff", 3)) == "jpeg");
  CHECK(writeit::image_type_of("nope") == "");

  const std::string path = "/tmp/write-it-m3.png";
  {
    std::ofstream out(path, std::ios::binary);
    out << image.data;
  }
  writeit::Document markdown =
      writeit::markdown_import("Look ![cat](" + path + ") here.", "Sans", 11);
  bool loaded = false;
  for (const writeit::Run& item : markdown.paragraphs[0].runs) {
    if (!item.image)
      continue;
    loaded = item.image->alt == "cat" && item.image->path == path &&
             item.image->type == "png" && item.image->data == image.data;
  }
  CHECK(loaded);
  const std::string written = writeit::markdown_export(markdown);
  CHECK(has(written, "![cat](" + path + ")"));
  writeit::Document missing = writeit::markdown_import("See ![alt](no-such-cat.png) there.", "Sans", 11);
  bool absent = false;
  for (const writeit::Run& item : missing.paragraphs[0].runs) {
    if (!item.image)
      continue;
    absent = item.image->alt == "alt" && item.image->path == "no-such-cat.png" &&
             item.image->data.empty();
  }
  CHECK(absent);
  CHECK(has(writeit::markdown_export(missing), "![alt](no-such-cat.png)"));
  CHECK(writeit::markdown_import(writeit::markdown_export(missing), "Sans", 11) == missing);
  std::remove(path.c_str());
}

void geometry()
{
  using writeit::ViewMode;
  const auto plain = writeit::view_geometry(ViewMode::Page, 1.0);
  const auto same = writeit::view_geometry(ViewMode::Page, 1.0, writeit::default_page());
  CHECK(plain.page_width == same.page_width);
  CHECK(plain.page_height == same.page_height);
  CHECK(plain.margin_left == same.margin_left);
  CHECK(plain.margin_right == same.margin_right);
  CHECK(plain.margin_y == same.margin_y);
  CHECK(plain.margin_bottom == same.margin_bottom);
  CHECK(plain.margin_bottom == plain.margin_y);
  const auto tiny = writeit::view_geometry(ViewMode::Page, 0.1);
  CHECK(tiny.margin_y == 8);
  writeit::PageSetup letter = writeit::default_page();
  letter.paper_width = 15840;
  letter.paper_height = 12240;
  letter.landscape = true;
  const auto wide = writeit::view_geometry(ViewMode::Page, 1.0, letter);
  CHECK(wide.page_width == writeit::twips_to_px(15840, 1));
  CHECK(wide.page_width > plain.page_width);
  CHECK(writeit::page_text_height(writeit::default_page(), 1) == writeit::page_text_height(1));
  writeit::PageSetup columns = writeit::default_page();
  columns.columns = 2;
  const auto sheet = writeit::view_geometry(ViewMode::Page, 1.0, columns);
  CHECK(sheet.page_width == plain.page_width);
  CHECK(sheet.margin_left == plain.margin_left);
}

}  // namespace

// Exactly the checks this suite runs. Update it with the tests.
constexpr int kChecks = 90;

int main()
{
  default_bytes();
  page_setup();
  clamp_limits();
  tables();
  stories();
  images();
  geometry();
  return suite_test::done("page", kChecks);
}
