/* SPDX-License-Identifier: Unlicense */

// A section holds the page setup, the columns, and the header and footer
// that 1.0 stored for the whole document. One section still saves as 1.0
// saved it. Even-page and odd-page breaks read as next page. A column
// break reads as continuous.

#include "check.hpp"
#include "document.hpp"

#include <cctype>
#include <string>
#include <vector>

namespace {

bool has_word(const std::string& text, const std::string& word)
{
  for (size_t at = 0; (at = text.find(word, at)) != std::string::npos;) {
    const size_t end = at + word.size();
    if (end >= text.size() ||
        std::isalpha(static_cast<unsigned char>(text[end])) == 0)
      return true;
    at = end;
  }
  return false;
}

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

writeit::Paragraph body(const char* text)
{
  writeit::Paragraph paragraph;
  paragraph.runs.push_back(text_run(text));
  return paragraph;
}

std::string story(const std::vector<writeit::Paragraph>& paragraphs)
{
  std::string out;
  for (const writeit::Paragraph& paragraph : paragraphs)
    for (const writeit::Run& run : paragraph.runs)
      out += run.text;
  return out;
}

bool same_document(const writeit::Document& doc, const char* name)
{
  const std::string rtf = writeit::rtf_export(doc);
  writeit::Document again;
  if (writeit::rtf_import(rtf, again) && again == doc)
    return true;
  std::cerr << name << " did not round-trip\n" << rtf << "\n";
  return false;
}

void one_section()
{
  writeit::Document doc;
  doc.paragraphs.push_back(body("Hello"));
  std::string rtf = writeit::rtf_export(doc);
  CHECK(!has_word(rtf, "\\sect"));
  CHECK(!has_word(rtf, "\\sectd"));
  CHECK(!has_word(rtf, "\\sbkpage"));
  CHECK(!has_word(rtf, "\\sbknone"));
  CHECK(!has_word(rtf, "\\pgwsxn"));
  CHECK(!has_word(rtf, "\\lndscpsxn"));
  CHECK(!has(rtf, "\\paperw"));
  CHECK(doc.sections.empty());
  CHECK(same_document(doc, "one section"));

  doc.page.paper_width = 15840;
  doc.page.paper_height = 12240;
  doc.page.landscape = true;
  doc.page.margin_left = 1440;
  doc.page.margin_right = 1440;
  doc.page.margin_top = 1440;
  doc.page.margin_bottom = 1440;
  doc.page.columns = 2;
  doc.page.column_gap = 720;
  rtf = writeit::rtf_export(doc);
  CHECK(has(rtf, "\\paperw15840"));
  CHECK(has(rtf, "\\cols2"));
  CHECK(!has_word(rtf, "\\sect"));
  CHECK(!has_word(rtf, "\\pgwsxn"));
  CHECK(same_document(doc, "one section page"));
}

void section_equality()
{
  writeit::Document doc;
  doc.paragraphs.push_back(body("One"));
  writeit::Document other = doc;
  CHECK(doc == other);
  other.paragraphs[0].section_break = writeit::SectionBreak::NextPage;
  CHECK(!(doc == other));
  other = doc;
  writeit::Section section;
  section.break_before = writeit::SectionBreak::Continuous;
  section.page.paper_width = 15840;
  other.sections.push_back(section);
  CHECK(!(doc == other));
  doc.sections.push_back(section);
  CHECK(doc == other);
}

void break_next_page()
{
  writeit::Document doc;
  doc.page.columns = 2;
  doc.page.column_gap = 360;
  doc.page.paper_width = 15840;
  doc.page.paper_height = 12240;
  doc.header.push_back(body("H"));
  doc.footer.push_back(body("F"));
  doc.paragraphs.push_back(body("Alpha"));
  doc.paragraphs.push_back(body("Beta"));
  const int at = writeit::chars_before(doc.paragraphs, 1);
  const int got = writeit::break_section(doc, at, writeit::SectionBreak::NextPage);
  CHECK(got >= 0);
  CHECK(got >= 0 && doc.paragraphs.size() == 2);
  CHECK(got >= 0 && doc.paragraphs[0].section_break == writeit::SectionBreak::None);
  CHECK(got >= 0 && doc.paragraphs[1].section_break == writeit::SectionBreak::NextPage);
  CHECK(got >= 0 && !doc.paragraphs[0].page_break && !doc.paragraphs[1].page_break);
  CHECK(got >= 0 && doc.sections.size() == 1);
  CHECK(got >= 0 && doc.sections[0].break_before == writeit::SectionBreak::NextPage);
  CHECK(got >= 0 && doc.sections[0].page.columns == 2);
  CHECK(got >= 0 && doc.sections[0].page.column_gap == 360);
  CHECK(got >= 0 && doc.sections[0].page.paper_width == 15840);
  CHECK(got >= 0 && story(doc.sections[0].header) == "H");
  CHECK(got >= 0 && story(doc.sections[0].footer) == "F");
  CHECK(got >= 0 && story(doc.header) == "H");
  CHECK(got >= 0 && doc.page.columns == 2);
  const std::string rtf = writeit::rtf_export(doc);
  CHECK(got >= 0 && has_word(rtf, "\\sect"));
  CHECK(got >= 0 && has_word(rtf, "\\sectd"));
  CHECK(got >= 0 && has_word(rtf, "\\sbkpage"));
  CHECK(got >= 0 && !has_word(rtf, "\\page"));
  CHECK(got >= 0 && same_document(doc, "next page"));

  const int again = writeit::break_section(doc, got >= 0 ? got : at, writeit::SectionBreak::NextPage);
  CHECK(got >= 0 && again < 0);
  CHECK(got >= 0 && doc.sections.size() == 1);
}

void break_continuous()
{
  writeit::Document doc;
  doc.paragraphs.push_back(body("Alpha"));
  doc.paragraphs.push_back(body("Beta"));
  const int at = writeit::chars_before(doc.paragraphs, 1);
  const int got = writeit::break_section(doc, at, writeit::SectionBreak::Continuous);
  CHECK(got >= 0);
  CHECK(got >= 0 && doc.paragraphs[1].section_break == writeit::SectionBreak::Continuous);
  CHECK(got >= 0 && !doc.paragraphs[1].page_break);
  CHECK(got >= 0 && doc.sections.size() == 1);
  CHECK(got >= 0 && doc.sections[0].break_before == writeit::SectionBreak::Continuous);
  const std::string rtf = writeit::rtf_export(doc);
  CHECK(got >= 0 && has_word(rtf, "\\sbknone"));
  CHECK(got >= 0 && !has_word(rtf, "\\sbkpage"));
  CHECK(got >= 0 && same_document(doc, "continuous"));

  const int changed =
      writeit::break_section(doc, got >= 0 ? got : at, writeit::SectionBreak::NextPage);
  CHECK(got >= 0 && changed >= 0);
  CHECK(got >= 0 && doc.sections.size() == 1 &&
        doc.sections[0].break_before == writeit::SectionBreak::NextPage);
  CHECK(got >= 0 && doc.paragraphs[1].section_break == writeit::SectionBreak::NextPage);
}

void break_splits_text()
{
  writeit::Document doc;
  doc.paragraphs.push_back(body("Hello"));
  const int got = writeit::break_section(doc, 2, writeit::SectionBreak::Continuous);
  CHECK(got >= 0);
  CHECK(got >= 0 && doc.paragraphs.size() == 2);
  CHECK(got >= 0 && story(std::vector<writeit::Paragraph>{doc.paragraphs[0]}) == "He");
  CHECK(got >= 0 && doc.paragraphs.size() == 2 && story(std::vector<writeit::Paragraph>{doc.paragraphs[1]}) == "llo");
  CHECK(got >= 0 && doc.paragraphs[0].section_break == writeit::SectionBreak::None);
  CHECK(got >= 0 && doc.paragraphs.size() == 2 &&
        doc.paragraphs[1].section_break == writeit::SectionBreak::Continuous);
  CHECK(got >= 0 && doc.paragraphs.size() == 2 && !doc.paragraphs[0].page_break &&
        !doc.paragraphs[1].page_break);
}

void break_at_start()
{
  writeit::Document doc;
  doc.page.columns = 3;
  doc.page.column_gap = 360;
  doc.paragraphs.push_back(body("Alpha"));
  const int got = writeit::break_section(doc, 0, writeit::SectionBreak::NextPage);
  CHECK(got >= 0);
  CHECK(got >= 0 && doc.paragraphs.size() == 2);
  CHECK(got >= 0 && doc.paragraphs[0].runs.empty());
  CHECK(got >= 0 && doc.paragraphs[0].section_break == writeit::SectionBreak::None);
  CHECK(got >= 0 && doc.paragraphs[1].section_break == writeit::SectionBreak::NextPage);
  CHECK(got >= 0 && story(std::vector<writeit::Paragraph>{doc.paragraphs[1]}) == "Alpha");
  CHECK(got >= 0 && doc.sections.size() == 1);
  CHECK(got >= 0 && doc.sections[0].page.columns == 3);
  CHECK(got >= 0 && !doc.paragraphs[1].page_break);
}

void break_in_the_middle()
{
  writeit::Document doc;
  doc.page.columns = 2;
  doc.page.column_gap = 360;
  doc.paragraphs.push_back(body("Alpha"));
  doc.paragraphs.push_back(body("Beta"));
  doc.paragraphs.push_back(body("Gamma"));
  const int at_gamma = writeit::chars_before(doc.paragraphs, 2);
  const int first = writeit::break_section(doc, at_gamma, writeit::SectionBreak::NextPage);
  CHECK(first >= 0 && doc.sections.size() == 1);
  if (first >= 0 && !doc.sections.empty())
    doc.sections[0].page.paper_width = 14000;
  const int at_beta = writeit::chars_before(doc.paragraphs, 1);
  const int second = writeit::break_section(doc, at_beta, writeit::SectionBreak::Continuous);
  CHECK(first >= 0 && second >= 0);
  CHECK(first >= 0 && doc.sections.size() == 2);
  CHECK(first >= 0 && doc.paragraphs[0].section_break == writeit::SectionBreak::None);
  CHECK(first >= 0 && doc.paragraphs.size() > 1 &&
        doc.paragraphs[1].section_break == writeit::SectionBreak::Continuous);
  CHECK(first >= 0 && doc.paragraphs.size() > 2 &&
        doc.paragraphs[2].section_break == writeit::SectionBreak::NextPage);
  CHECK(first >= 0 && doc.sections.size() == 2 && doc.sections[0].page.columns == 2);
  CHECK(first >= 0 && doc.sections.size() == 2 && doc.sections[0].page.paper_width == doc.page.paper_width);
  CHECK(first >= 0 && doc.sections.size() == 2 && doc.sections[1].page.paper_width == 14000);
  CHECK(first >= 0 && doc.sections.size() == 2 &&
        doc.sections[1].break_before == writeit::SectionBreak::NextPage);
  int breaks = 0;
  for (const writeit::Paragraph& paragraph : doc.paragraphs) {
    if (paragraph.section_break != writeit::SectionBreak::None)
      ++breaks;
  }
  CHECK(breaks == static_cast<int>(doc.sections.size()));
}

void break_in_a_table()
{
  writeit::Document doc;
  doc.paragraphs.push_back(body("Before"));
  CHECK(writeit::insert_table(doc.paragraphs, 1, 2, 2, 4000));
  size_t cell = 0;
  for (size_t i = 0; i < doc.paragraphs.size(); ++i) {
    if (doc.paragraphs[i].cell.table != 0) {
      cell = i;
      break;
    }
  }
  writeit::Document copy = doc;
  const int at = writeit::chars_before(doc.paragraphs, cell);
  CHECK(writeit::break_section(doc, at, writeit::SectionBreak::NextPage) < 0);
  CHECK(doc == copy);

  writeit::Document front;
  front.paragraphs.push_back(body("Before"));
  CHECK(writeit::insert_table(front.paragraphs, 1, 1, 1, 4000));
  const int edge = writeit::chars_of(front.paragraphs[0]);
  const int got = writeit::break_section(front, edge, writeit::SectionBreak::NextPage);
  CHECK(got >= 0);
  CHECK(got >= 0 && front.paragraphs.size() >= 3);
  CHECK(got >= 0 && front.paragraphs[1].cell.table == 0);
  CHECK(got >= 0 && front.paragraphs[1].section_break == writeit::SectionBreak::NextPage);
  CHECK(got >= 0 && front.paragraphs[0].section_break == writeit::SectionBreak::None);
  CHECK(got >= 0 && front.paragraphs.size() > 2 && front.paragraphs[2].cell.table != 0);
}

void built_section_round_trip()
{
  writeit::Document doc;
  doc.header.push_back(body("H1"));
  doc.footer.push_back(body("F1"));
  doc.paragraphs.push_back(body("Alpha"));
  writeit::Paragraph beta = body("Beta");
  beta.section_break = writeit::SectionBreak::NextPage;
  doc.paragraphs.push_back(beta);
  writeit::Section section;
  section.break_before = writeit::SectionBreak::NextPage;
  section.page.margin_left = 1440;
  section.page.margin_right = 1440;
  section.page.margin_top = 1440;
  section.page.margin_bottom = 1440;
  section.page.columns = 2;
  section.page.column_gap = 360;
  section.header.push_back(body("H2"));
  section.footer.push_back(body("F2"));
  doc.sections.push_back(section);
  const std::string rtf = writeit::rtf_export(doc);
  CHECK(has_word(rtf, "\\sect"));
  CHECK(has_word(rtf, "\\sectd"));
  CHECK(has_word(rtf, "\\sbkpage"));
  CHECK(has_word(rtf, "\\sbknone"));
  CHECK(has(rtf, "\\cols2"));
  CHECK(has(rtf, "\\marglsxn1440"));
  CHECK(!has_word(rtf, "\\page"));
  CHECK(same_document(doc, "built section"));
}

void read_three_sections()
{
  const char* rtf =
      "{\\rtf1\\ansi"
      "\\sectd\\sbknone\\pgwsxn11906\\pghsxn16838"
      "\\marglsxn1440\\margrsxn1440\\margtsxn1440\\margbsxn1440"
      "\\cols1\\colsx720"
      "{\\header \\pard H1\\par}{\\footer \\pard F1\\par}"
      "\\pard Alpha\\par"
      "\\sect"
      "\\sectd\\sbkpage\\pgwsxn15840\\pghsxn12240"
      "\\marglsxn720\\margrsxn720\\margtsxn720\\margbsxn720\\lndscpsxn"
      "\\cols2\\colsx360"
      "{\\header \\pard H2\\par}{\\footer \\pard F2\\par}"
      "\\pard Beta\\par"
      "\\sect"
      "\\sectd\\sbknone\\pgwsxn12240\\pghsxn15840"
      "\\marglsxn720\\margrsxn720\\margtsxn720\\margbsxn720"
      "\\cols1\\colsx720"
      "\\pard Gamma\\par}";
  writeit::Document doc;
  CHECK(writeit::rtf_import(rtf, doc));
  CHECK(doc.paragraphs.size() == 3);
  CHECK(doc.sections.size() == 2);
  CHECK(doc.sections.size() == 2 && doc.paragraphs[0].section_break == writeit::SectionBreak::None);
  CHECK(doc.sections.size() == 2 &&
        doc.paragraphs[1].section_break == writeit::SectionBreak::NextPage);
  CHECK(doc.sections.size() == 2 &&
        doc.paragraphs[2].section_break == writeit::SectionBreak::Continuous);
  CHECK(doc.sections.size() == 2 && !doc.paragraphs[1].page_break && !doc.paragraphs[2].page_break);
  CHECK(doc.sections.size() == 2 && story(doc.paragraphs) == "AlphaBetaGamma");
  CHECK(doc.sections.size() == 2 && doc.page.margin_left == 1440);
  CHECK(doc.sections.size() == 2 && story(doc.header) == "H1");
  CHECK(doc.sections.size() == 2 && story(doc.footer) == "F1");
  CHECK(doc.sections.size() == 2 && doc.sections[0].page.landscape);
  CHECK(doc.sections.size() == 2 && doc.sections[0].page.paper_width == 15840);
  CHECK(doc.sections.size() == 2 && doc.sections[0].page.paper_height == 12240);
  CHECK(doc.sections.size() == 2 && doc.sections[0].page.columns == 2);
  CHECK(doc.sections.size() == 2 && doc.sections[0].page.column_gap == 360);
  CHECK(doc.sections.size() == 2 && story(doc.sections[0].header) == "H2");
  CHECK(doc.sections.size() == 2 && story(doc.sections[0].footer) == "F2");
  CHECK(doc.sections.size() == 2 &&
        doc.sections[1].break_before == writeit::SectionBreak::Continuous);
  CHECK(doc.sections.size() == 2 && doc.sections[1].page.columns == 1);
  CHECK(doc.sections.size() == 2 && doc.sections[1].page.paper_width == 12240);
  CHECK(doc.sections.size() == 2 && doc.sections[1].page.paper_height == 15840);
  CHECK(doc.sections.size() == 2 && !doc.sections[1].page.landscape);
  CHECK(doc.sections.size() == 2 && doc.sections[1].header.empty());
  CHECK(doc.sections.size() == 2 && doc.sections[1].footer.empty());
  CHECK(doc.sections.size() == 2 && same_document(doc, "three sections"));
}

void read_break_aliases()
{
  const char* rtf =
      "{\\rtf1\\ansi\\pard One\\par\\sect\\sbkeven\\pard Two\\par"
      "\\sect\\sbkodd\\pard Three\\par\\sect\\sbkcol\\pard Four\\par}";
  writeit::Document doc;
  CHECK(writeit::rtf_import(rtf, doc));
  CHECK(doc.sections.size() == 3);
  CHECK(doc.sections.size() == 3 &&
        doc.paragraphs[1].section_break == writeit::SectionBreak::NextPage);
  CHECK(doc.sections.size() == 3 &&
        doc.paragraphs[2].section_break == writeit::SectionBreak::NextPage);
  CHECK(doc.sections.size() == 3 &&
        doc.paragraphs[3].section_break == writeit::SectionBreak::Continuous);
  const std::string out = writeit::rtf_export(doc);
  CHECK(doc.sections.size() == 3 && !has_word(out, "\\sbkeven"));
  CHECK(doc.sections.size() == 3 && !has_word(out, "\\sbkodd"));
  CHECK(doc.sections.size() == 3 && !has_word(out, "\\sbkcol"));
  CHECK(doc.sections.size() == 3 && has_word(out, "\\sbkpage"));
  CHECK(doc.sections.size() == 3 && same_document(doc, "aliases"));
}

void read_sectd()
{
  writeit::Document kept;
  CHECK(writeit::rtf_import(
      "{\\rtf1\\ansi\\paperw15840\\paperh12240\\margl1800\\margr1800"
      "\\margt1800\\margb1800\\sectd\\pard Only\\par}",
      kept));
  CHECK(kept.sections.empty());
  CHECK(kept.page.paper_width == 15840);
  CHECK(kept.page.paper_height == 12240);
  CHECK(kept.page.margin_left == 1800);

  writeit::Document overridden;
  CHECK(writeit::rtf_import(
      "{\\rtf1\\ansi\\paperw15840\\paperh12240\\sectd\\pgwsxn12240\\pghsxn15840"
      "\\pard Only\\par}",
      overridden));
  CHECK(overridden.sections.empty());
  CHECK(overridden.page.paper_width == 12240);
  CHECK(overridden.page.paper_height == 15840);

  writeit::Document cleared;
  CHECK(writeit::rtf_import(
      "{\\rtf1\\ansi{\\header \\pard Gone\\par}\\sectd\\pard One\\par\\sect\\pard Two\\par}",
      cleared));
  CHECK(cleared.sections.size() == 1);
  CHECK(cleared.sections.size() == 1 && cleared.header.empty());
  CHECK(cleared.sections.size() == 1 && cleared.paragraphs.size() > 1 &&
        cleared.paragraphs[1].section_break == writeit::SectionBreak::NextPage);

  writeit::Document after;
  CHECK(writeit::rtf_import(
      "{\\rtf1\\ansi\\sectd{\\header \\pard Kept\\par}\\pard One\\par\\sect\\sectd\\pard Two\\par}",
      after));
  CHECK(after.sections.size() == 1 && story(after.header) == "Kept");
  CHECK(after.sections.size() == 1 && after.sections[0].header.empty());

  writeit::Document turned;
  CHECK(writeit::rtf_import(
      "{\\rtf1\\ansi\\pard One\\par\\sect\\sectd\\pgwsxn11906\\pghsxn16838\\lndscpsxn"
      "\\pard Two\\par}",
      turned));
  CHECK(turned.sections.size() == 1 && turned.sections[0].page.landscape);
  CHECK(turned.sections.size() == 1 && turned.sections[0].page.paper_width == 16838);
  CHECK(turned.sections.size() == 1 && turned.sections[0].page.paper_height == 11906);
}

}  // namespace

int main()
{
  one_section();
  section_equality();
  break_next_page();
  break_continuous();
  break_splits_text();
  break_at_start();
  break_in_the_middle();
  break_in_a_table();
  built_section_round_trip();
  read_three_sections();
  read_break_aliases();
  read_sectd();
  return suite_test::done("section", 151);
}
