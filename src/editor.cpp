/* SPDX-License-Identifier: Unlicense */

#include "main_window.hpp"

#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace writeit {
namespace {

constexpr int kFindNext = Gtk::RESPONSE_APPLY;
constexpr int kFindReplace = Gtk::RESPONSE_YES;

std::string lower_copy(std::string text)
{
  for (char& c : text)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return text;
}

std::string extension_of(const std::string& path)
{
  const std::string base = Glib::path_get_basename(path);
  const auto dot = base.rfind('.');
  if (dot == std::string::npos)
    return {};
  return lower_copy(base.substr(dot));
}

std::string with_extension(std::string name, const char* extension)
{
  const std::string ext = extension;
  const std::string lower = lower_copy(name);
  if (lower.size() >= ext.size() && lower.compare(lower.size() - ext.size(), ext.size(), ext) == 0)
    return name;
  const auto dot = name.rfind('.');
  if (dot != std::string::npos) {
    const std::string current = lower_copy(name.substr(dot));
    if (current == ".md" || current == ".markdown" || current == ".txt" || current == ".rtf")
      return name.substr(0, dot) + ext;
  }
  return name + ext;
}

bool known_size(int size)
{
  switch (size) {
    case 8:
    case 9:
    case 10:
    case 11:
    case 12:
    case 14:
    case 16:
    case 18:
    case 24:
    case 36:
      return true;
    default:
      return false;
  }
}

template <class Tag>
std::string tag_name(const Glib::RefPtr<Tag>& tag)
{
  return tag->property_name().get_value();
}

std::string fmt_name(const Run& run)
{
  return std::string("fmt") + '\x1f' + run.font + '\x1f' + std::to_string(run.size) + '\x1f' +
         (run.bold ? "1" : "0") + '\x1f' + (run.italic ? "1" : "0") + '\x1f' +
         (run.underline ? "1" : "0");
}

bool parse_fmt(const std::string& name, Run& run)
{
  std::vector<std::string> parts;
  std::string current;
  for (char c : name) {
    if (c == '\x1f') {
      parts.push_back(current);
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  parts.push_back(current);
  if (parts.size() != 6 || parts[0] != "fmt")
    return false;
  try {
    run.font = parts[1];
    run.size = std::stoi(parts[2]);
  } catch (const std::exception&) {
    return false;
  }
  run.bold = parts[3] == "1";
  run.italic = parts[4] == "1";
  run.underline = parts[5] == "1";
  run.text.clear();
  return true;
}

// Paragraph tags hold the indents in twips and the alignment. Every
// character of a paragraph, its newline included, carries exactly one.
std::string para_name(const ParaFormat& format)
{
  return std::string("para") + '\x1f' + std::to_string(format.indents.left) + '\x1f' +
         std::to_string(format.indents.right) + '\x1f' + std::to_string(format.indents.first) +
         '\x1f' + std::to_string(static_cast<int>(format.align));
}

bool parse_para(const std::string& name, ParaFormat& format)
{
  const std::string prefix = std::string("para") + '\x1f';
  if (name.compare(0, prefix.size(), prefix) != 0)
    return false;
  std::vector<int> values;
  std::string current;
  try {
    for (size_t i = prefix.size(); i <= name.size(); ++i) {
      if (i == name.size() || name[i] == '\x1f') {
        values.push_back(std::stoi(current));
        current.clear();
      } else {
        current.push_back(name[i]);
      }
    }
  } catch (const std::exception&) {
    return false;
  }
  if (values.size() != 4 || values[3] < 0 || values[3] > 2)
    return false;
  format.indents.left = values[0];
  format.indents.right = values[1];
  format.indents.first = values[2];
  format.align = static_cast<Align>(values[3]);
  return true;
}

int heading_level_name(const std::string& name)
{
  const std::string prefix = "heading-";
  if (name.compare(0, prefix.size(), prefix) != 0)
    return 0;
  try {
    const int level = std::stoi(name.substr(prefix.size()));
    if (level >= 1 && level <= 6)
      return level;
  } catch (const std::exception&) {
  }
  return 0;
}

void add_run(Paragraph& paragraph, Run run)
{
  if (run.text.empty())
    return;
  if (!paragraph.runs.empty() && same_format(paragraph.runs.back(), run))
    paragraph.runs.back().text += run.text;
  else
    paragraph.runs.push_back(std::move(run));
}

struct Atom {
  gunichar ch = 0;
  Run format;
  int heading = 0;
  ParaFormat para;
};

bool operator==(const Atom& a, const Atom& b)
{
  return a.ch == b.ch && a.heading == b.heading && a.para == b.para &&
         same_format(a.format, b.format);
}

// One code point per buffer offset, with a newline between paragraphs.
std::vector<Atom> atoms_of(const Document& doc)
{
  std::vector<Atom> atoms;
  for (size_t i = 0; i < doc.paragraphs.size(); ++i) {
    if (i > 0) {
      Atom newline;
      newline.ch = '\n';
      atoms.push_back(newline);
    }
    const Paragraph& paragraph = doc.paragraphs[i];
    for (const Run& run : paragraph.runs) {
      const Glib::ustring text(run.text);
      for (auto it = text.begin(); it != text.end(); ++it) {
        Atom atom;
        atom.ch = *it;
        atom.format = run;
        atom.format.text.clear();
        atom.heading = paragraph.heading;
        atom.para.indents = paragraph.indents;
        atom.para.align = paragraph.align;
        atoms.push_back(atom);
      }
    }
  }
  return atoms;
}

struct Insertion {
  int at = 0;
  int len = 0;
};

// True when `after` is `before` plus one contiguous insertion.
bool one_insertion(const Document& before, const Document& after, Insertion& out)
{
  const std::vector<Atom> old_atoms = atoms_of(before);
  const std::vector<Atom> new_atoms = atoms_of(after);
  if (new_atoms.size() <= old_atoms.size())
    return false;
  size_t prefix = 0;
  while (prefix < old_atoms.size() && old_atoms[prefix] == new_atoms[prefix])
    ++prefix;
  size_t suffix = 0;
  while (suffix < old_atoms.size() - prefix &&
         old_atoms[old_atoms.size() - 1 - suffix] == new_atoms[new_atoms.size() - 1 - suffix])
    ++suffix;
  if (prefix + suffix != old_atoms.size())
    return false;
  out.at = static_cast<int>(prefix);
  out.len = static_cast<int>(new_atoms.size() - old_atoms.size());
  return out.len > 0;
}

}  // namespace

void MainWindow::tell(const char* sentence)
{
  Gtk::MessageDialog dialog(*this, sentence, false, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
  dialog.set_title("Write-It");
  dialog.run();
}

void MainWindow::build_editor()
{
  for (int level = 1; level <= 6; ++level)
    heading_tag(level);
  raise_headings();

  buffer_->signal_begin_user_action().connect(sigc::mem_fun(*this, &MainWindow::on_user_begin));
  buffer_->signal_end_user_action().connect(sigc::mem_fun(*this, &MainWindow::on_user_end));
  buffer_->signal_insert().connect(sigc::mem_fun(*this, &MainWindow::on_inserted));
  buffer_->signal_erase().connect(sigc::mem_fun(*this, &MainWindow::on_erase), false);
  buffer_->signal_mark_set().connect(sigc::mem_fun(*this, &MainWindow::on_mark_set));
  Gtk::Clipboard::get()->signal_owner_change().connect(
      [this](GdkEventOwnerChange*) { update_actions(); });

  auto activate = [](Gtk::MenuItem* item, const sigc::slot<void>& slot) {
    if (item)
      item->signal_activate().connect(slot);
  };
  auto clicked = [](Gtk::ToolButton* button, const sigc::slot<void>& slot) {
    if (button)
      button->signal_clicked().connect(slot);
  };
  activate(new_item_, [this] { new_document(true); });
  clicked(new_tool_, [this] { new_document(true); });
  activate(open_item_, [this] { open_document(); });
  clicked(open_tool_, [this] { open_document(); });
  activate(save_item_, [this] { save_document(); });
  clicked(save_tool_, [this] { save_document(); });
  activate(save_as_item_, [this] { save_document_as(); });
  activate(export_item_, [this] { export_markdown(); });
  activate(close_item_, [this] { close_document(); });
  activate(undo_item_, [this] { undo(); });
  clicked(undo_tool_, [this] { undo(); });
  activate(redo_item_, [this] { redo(); });
  clicked(redo_tool_, [this] { redo(); });
  activate(cut_item_, [this] { buffer_->cut_clipboard(Gtk::Clipboard::get()); });
  activate(context_cut_, [this] { buffer_->cut_clipboard(Gtk::Clipboard::get()); });
  clicked(cut_tool_, [this] { buffer_->cut_clipboard(Gtk::Clipboard::get()); });
  activate(copy_item_, [this] { buffer_->copy_clipboard(Gtk::Clipboard::get()); });
  activate(context_copy_, [this] { buffer_->copy_clipboard(Gtk::Clipboard::get()); });
  clicked(copy_tool_, [this] { buffer_->copy_clipboard(Gtk::Clipboard::get()); });
  activate(paste_item_, [this] { buffer_->paste_clipboard(Gtk::Clipboard::get()); });
  activate(context_paste_, [this] { buffer_->paste_clipboard(Gtk::Clipboard::get()); });
  clicked(paste_tool_, [this] { buffer_->paste_clipboard(Gtk::Clipboard::get()); });
  activate(delete_item_, [this] { buffer_->erase_selection(true, true); });
  activate(select_all_item_, [this] { buffer_->select_range(buffer_->begin(), buffer_->end()); });
  activate(find_item_, [this] { present_find(false); });
  activate(replace_item_, [this] { present_find(true); });
  activate(paragraph_item_, [this] { on_paragraph(); });
  activate(options_item_, [this] { on_options(); });

  const std::string font = settings_.default_font.empty() ? "Sans" : settings_.default_font;
  fill_font_combo(font_combo_, font);
  if (known_size(settings_.default_size))
    size_combo_.set_active_text(std::to_string(settings_.default_size));
  new_document(false);
  connect_format();
  rebuild_recent();
  update_actions();
}

void MainWindow::connect_format()
{
  font_combo_.signal_changed().connect(sigc::mem_fun(*this, &MainWindow::on_font_changed));
  size_combo_.signal_changed().connect(sigc::mem_fun(*this, &MainWindow::on_size_changed));
  font_combo_.property_popup_shown().signal_changed().connect([this] {
    if (!font_combo_.property_popup_shown().get_value())
      text_.grab_focus();
  });
  size_combo_.property_popup_shown().signal_changed().connect([this] {
    if (!size_combo_.property_popup_shown().get_value())
      text_.grab_focus();
  });
  if (bold_toggle_)
    bold_toggle_->signal_toggled().connect([this] { toggle_flag(TextFlag::Bold); });
  if (italic_toggle_)
    italic_toggle_->signal_toggled().connect([this] { toggle_flag(TextFlag::Italic); });
  if (underline_toggle_)
    underline_toggle_->signal_toggled().connect([this] { toggle_flag(TextFlag::Underline); });
  if (align_left_toggle_)
    align_left_toggle_->signal_toggled().connect([this] { on_align_toggled(Align::Left); });
  if (align_center_toggle_)
    align_center_toggle_->signal_toggled().connect([this] { on_align_toggled(Align::Center); });
  if (align_right_toggle_)
    align_right_toggle_->signal_toggled().connect([this] { on_align_toggled(Align::Right); });
  if (align_left_item_)
    align_left_item_->signal_activate().connect([this] { apply_align(Align::Left); });
  if (align_center_item_)
    align_center_item_->signal_activate().connect([this] { apply_align(Align::Center); });
  if (align_right_item_)
    align_right_item_->signal_activate().connect([this] { apply_align(Align::Right); });
  if (bold_item_)
    bold_item_->signal_activate().connect([this] { toggle_flag(TextFlag::Bold); });
  if (italic_item_)
    italic_item_->signal_activate().connect([this] { toggle_flag(TextFlag::Italic); });
  if (underline_item_)
    underline_item_->signal_activate().connect([this] { toggle_flag(TextFlag::Underline); });
}

void MainWindow::fill_font_combo(Gtk::ComboBoxText& combo, const std::string& active)
{
  std::vector<std::string> names;
  names.push_back("Sans");
  if (auto context = text_.create_pango_context()) {
    for (const auto& family : context->list_families())
      names.emplace_back(family->get_name());
  }
  if (!active.empty())
    names.push_back(active);
  std::sort(names.begin(), names.end());
  names.erase(std::unique(names.begin(), names.end()), names.end());
  const bool guard = suppress_format_;
  suppress_format_ = true;
  combo.remove_all();
  for (const auto& name : names)
    combo.append(name);
  if (!active.empty())
    combo.set_active_text(active);
  suppress_format_ = guard;
}

bool MainWindow::new_document(bool prompt)
{
  if (prompt && !confirm_discard_or_save())
    return false;
  undo_.clear();
  redo_.clear();
  save_path_.clear();
  title_name_ = "Untitled";
  typing_ = Run{};
  typing_.font = settings_.default_font.empty() ? "Sans" : settings_.default_font;
  typing_.size = known_size(settings_.default_size) ? settings_.default_size : 11;
  save_point_ = true;
  replace_buffer(blank_document(typing_.font, typing_.size), 0);
  saved_ = capture();
  message_.set_text("");
  update_title();
  update_actions();
  sync_format_controls();
  text_.grab_focus();
  return true;
}

void MainWindow::close_document()
{
  new_document(true);
}

void MainWindow::open_document()
{
  Gtk::FileChooserDialog dialog(*this, "Open", Gtk::FILE_CHOOSER_ACTION_OPEN);
  dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("_Open", Gtk::RESPONSE_ACCEPT);
  dialog.set_default_response(Gtk::RESPONSE_ACCEPT);
  if (!settings_.last_dir.empty() && Glib::file_test(settings_.last_dir, Glib::FILE_TEST_IS_DIR))
    dialog.set_current_folder(settings_.last_dir);

  auto rtf = Gtk::FileFilter::create();
  rtf->set_name("RTF");
  rtf->add_pattern("*.rtf");
  rtf->add_pattern("*.RTF");
  auto markdown = Gtk::FileFilter::create();
  markdown->set_name("Markdown");
  markdown->add_pattern("*.md");
  markdown->add_pattern("*.markdown");
  markdown->add_pattern("*.MD");
  auto plain = Gtk::FileFilter::create();
  plain->set_name("Plain text");
  plain->add_pattern("*.txt");
  plain->add_pattern("*.TXT");
  dialog.add_filter(rtf);
  dialog.add_filter(markdown);
  dialog.add_filter(plain);

  if (dialog.run() != Gtk::RESPONSE_ACCEPT)
    return;
  OpenKind fallback = OpenKind::Rtf;
  if (auto filter = dialog.get_filter()) {
    if (filter->get_name() == "Markdown")
      fallback = OpenKind::Markdown;
    else if (filter->get_name() == "Plain text")
      fallback = OpenKind::Plain;
  }
  open_path(dialog.get_filename(), fallback);
}

void MainWindow::open_path(const std::string& path, OpenKind fallback)
{
  if (!Glib::file_test(path, Glib::FILE_TEST_EXISTS)) {
    tell("That file is missing.");
    return;
  }
  std::string bytes;
  try {
    bytes = Glib::file_get_contents(path);
  } catch (const Glib::Error&) {
    tell("That file could not be opened.");
    return;
  }
  const std::string ext = extension_of(path);
  OpenKind kind = fallback;
  if (ext == ".rtf")
    kind = OpenKind::Rtf;
  else if (ext == ".md" || ext == ".markdown")
    kind = OpenKind::Markdown;
  else if (ext == ".txt")
    kind = OpenKind::Plain;
  Document doc;
  if (kind == OpenKind::Rtf) {
    if (!rtf_import(bytes, doc)) {
      tell("That file could not be opened.");
      return;
    }
  } else if (kind == OpenKind::Markdown) {
    doc = markdown_import(bytes, typing_.font, typing_.size);
  } else {
    doc = plain_import(bytes, typing_.font, typing_.size);
  }
  if (!confirm_discard_or_save())
    return;
  install_loaded(doc, path, kind == OpenKind::Rtf);
}

void MainWindow::install_loaded(const Document& doc, const std::string& path, bool keep_path)
{
  undo_.clear();
  redo_.clear();
  replace_buffer(doc, 0);
  title_name_ = Glib::path_get_basename(path);
  if (keep_path) {
    save_path_ = path;
    save_point_ = true;
    saved_ = capture();
  } else {
    save_path_.clear();
    save_point_ = false;
  }
  settings_.last_dir = Glib::path_get_dirname(path);
  remember_path(path);
  message_.set_text(Glib::ustring("Opened ") + title_name_);
  update_title();
  update_actions();
  sync_format_controls();
  text_.grab_focus();
}

bool MainWindow::save_document()
{
  if (save_path_.empty())
    return save_document_as();
  return write_rtf(save_path_);
}

bool MainWindow::save_document_as()
{
  Gtk::FileChooserDialog dialog(*this, "Save As", Gtk::FILE_CHOOSER_ACTION_SAVE);
  dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("_Save", Gtk::RESPONSE_ACCEPT);
  dialog.set_default_response(Gtk::RESPONSE_ACCEPT);
  dialog.set_do_overwrite_confirmation(true);
  if (!settings_.last_dir.empty() && Glib::file_test(settings_.last_dir, Glib::FILE_TEST_IS_DIR))
    dialog.set_current_folder(settings_.last_dir);
  auto rtf = Gtk::FileFilter::create();
  rtf->set_name("RTF");
  rtf->add_pattern("*.rtf");
  dialog.add_filter(rtf);
  const std::string suggested = save_path_.empty() ? with_extension(title_name_, ".rtf")
                                                   : Glib::path_get_basename(save_path_);
  dialog.set_current_name(with_extension(suggested, ".rtf"));
  if (dialog.run() != Gtk::RESPONSE_ACCEPT)
    return false;
  return write_rtf(with_extension(dialog.get_filename(), ".rtf"));
}

bool MainWindow::write_rtf(const std::string& path)
{
  try {
    Glib::file_set_contents(path, rtf_export(capture()));
  } catch (const Glib::Error&) {
    tell("That file could not be saved.");
    return false;
  }
  save_path_ = path;
  title_name_ = Glib::path_get_basename(path);
  saved_ = capture();
  save_point_ = true;
  settings_.last_dir = Glib::path_get_dirname(path);
  remember_path(path);
  message_.set_text(Glib::ustring("Saved ") + title_name_);
  update_title();
  return true;
}

void MainWindow::export_markdown()
{
  Gtk::FileChooserDialog dialog(*this, "Export", Gtk::FILE_CHOOSER_ACTION_SAVE);
  dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("_Export", Gtk::RESPONSE_ACCEPT);
  dialog.set_default_response(Gtk::RESPONSE_ACCEPT);
  dialog.set_do_overwrite_confirmation(true);
  if (!settings_.last_dir.empty() && Glib::file_test(settings_.last_dir, Glib::FILE_TEST_IS_DIR))
    dialog.set_current_folder(settings_.last_dir);
  auto markdown = Gtk::FileFilter::create();
  markdown->set_name("Markdown");
  markdown->add_pattern("*.md");
  markdown->add_pattern("*.markdown");
  dialog.add_filter(markdown);
  std::string suggested = title_name_ == "Untitled" ? "Untitled" : title_name_;
  if (!save_path_.empty())
    suggested = Glib::path_get_basename(save_path_);
  dialog.set_current_name(with_extension(suggested, ".md"));
  if (dialog.run() != Gtk::RESPONSE_ACCEPT)
    return;
  const std::string path = with_extension(dialog.get_filename(), ".md");
  try {
    Glib::file_set_contents(path, markdown_export(capture()));
  } catch (const Glib::Error&) {
    tell("That file could not be saved.");
    return;
  }
  settings_.last_dir = Glib::path_get_dirname(path);
  remember_path(path);
  message_.set_text(Glib::ustring("Exported ") + Glib::path_get_basename(path));
}

bool MainWindow::confirm_discard_or_save()
{
  if (!dirty())
    return true;
  Gtk::MessageDialog dialog(*this, "Save changes to " + title_name_ + "?", false,
                            Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_NONE, true);
  dialog.set_title("Write-It");
  dialog.add_button("Save", Gtk::RESPONSE_ACCEPT);
  dialog.add_button("Don\u2019t Save", Gtk::RESPONSE_REJECT);
  dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
  dialog.set_default_response(Gtk::RESPONSE_ACCEPT);
  const int response = dialog.run();
  if (response == Gtk::RESPONSE_ACCEPT)
    return save_document();
  if (response == Gtk::RESPONSE_REJECT)
    return true;
  return false;
}

void MainWindow::remember_path(const std::string& path)
{
  auto& recent = settings_.recent;
  recent.erase(std::remove(recent.begin(), recent.end(), path), recent.end());
  recent.insert(recent.begin(), path);
  if (static_cast<int>(recent.size()) > settings_.recent_count)
    recent.resize(static_cast<size_t>(settings_.recent_count));
  settings_.save();
  rebuild_recent();
}

void MainWindow::rebuild_recent()
{
  for (auto* item : recent_items_) {
    recent_menu_.remove(*item);
    delete item;
  }
  recent_items_.clear();
  for (const auto& path : settings_.recent) {
    auto* item = Gtk::manage(new Gtk::MenuItem(Glib::path_get_basename(path)));
    item->set_tooltip_text(path);
    item->signal_activate().connect([this, path] { open_path(path, OpenKind::Rtf); });
    recent_menu_.append(*item);
    recent_items_.push_back(item);
  }
  recent_menu_.show_all();
  if (recent_item_)
    recent_item_->set_sensitive(!settings_.recent.empty());
}

Document MainWindow::capture() const
{
  Document doc;
  Paragraph paragraph;
  Run run;
  bool in_run = false;
  int heading = 0;
  bool have_para = false;
  auto flush_run = [&]() {
    if (!in_run)
      return;
    add_run(paragraph, run);
    run = Run{};
    in_run = false;
  };
  auto flush_paragraph = [&]() {
    flush_run();
    paragraph.heading = heading;
    if (!have_para) {
      // A paragraph no tag reaches: the empty last one, or one that has not
      // been normalised yet. It follows the paragraph above.
      if (pending_para_set_) {
        paragraph.indents = pending_para_.indents;
        paragraph.align = pending_para_.align;
      } else if (!doc.paragraphs.empty()) {
        paragraph.indents = doc.paragraphs.back().indents;
        paragraph.align = doc.paragraphs.back().align;
      }
    }
    doc.paragraphs.push_back(paragraph);
    paragraph = Paragraph{};
    heading = 0;
    have_para = false;
  };
  for (auto iter = buffer_->begin(); !iter.is_end(); ++iter) {
    const gunichar ch = iter.get_char();
    if (!have_para) {
      ParaFormat format;
      if (auto tag = para_tag_at(iter)) {
        if (parse_para(tag_name(tag), format)) {
          paragraph.indents = format.indents;
          paragraph.align = format.align;
          have_para = true;
        }
      }
    }
    if (ch == '\n') {
      flush_paragraph();
      continue;
    }
    if (heading == 0)
      heading = heading_of(iter);
    const Run format = format_of(iter);
    if (!in_run || !same_format(run, format)) {
      flush_run();
      run = format;
      in_run = true;
    }
    run.text += Glib::ustring(1, ch).raw();
  }
  flush_paragraph();
  if (doc.paragraphs.empty())
    doc.paragraphs.push_back(Paragraph{});
  return doc;
}

void MainWindow::replace_buffer(const Document& doc, int offset)
{
  loading_ = true;
  buffer_->set_text("");
  pending_para_set_ = false;
  pending_para_ = ParaFormat{};
  for (size_t i = 0; i < doc.paragraphs.size(); ++i) {
    const Paragraph& paragraph = doc.paragraphs[i];
    const auto para = para_tag(ParaFormat{paragraph.indents, paragraph.align});
    bool any = false;
    for (const Run& run : paragraph.runs) {
      if (run.text.empty())
        continue;
      std::vector<Glib::RefPtr<Gtk::TextTag>> tags;
      tags.push_back(format_tag(run));
      if (paragraph.heading >= 1 && paragraph.heading <= 6)
        tags.push_back(heading_tag(paragraph.heading));
      tags.push_back(para);
      buffer_->insert_with_tags(buffer_->end(), run.text, tags);
      any = true;
    }
    if (i + 1 < doc.paragraphs.size()) {
      buffer_->insert_with_tag(buffer_->end(), "\n", para);
    } else if (!any) {
      pending_para_ = ParaFormat{clamp_indents(paragraph.indents), paragraph.align};
      pending_para_set_ = true;
    }
  }
  // Newlines carry the line height. Tag them from the paragraph they end,
  // before the caret can paint every blank line with the widget font.
  tag_line_breaks(0, buffer_->get_char_count());
  const int count = buffer_->get_char_count();
  const int place = std::max(0, std::min(offset, count));
  buffer_->place_cursor(buffer_->get_iter_at_offset(place));
  text_.scroll_to(buffer_->get_insert());
  loading_ = false;
  apply_page_size();
}

bool MainWindow::dirty() const
{
  if (!save_point_)
    return true;
  return !(capture() == saved_);
}

void MainWindow::update_title()
{
  std::string title = "Write-It - " + title_name_;
  if (dirty())
    title += " *";
  set_title(title);
}

void MainWindow::update_actions()
{
  const bool selection = buffer_ && buffer_->get_has_selection();
  const bool any_text = buffer_ && buffer_->get_char_count() > 0;
  const bool can_undo = !undo_.empty();
  const bool can_redo = !redo_.empty();
  bool can_paste = false;
  if (auto clipboard = Gtk::Clipboard::get())
    can_paste = clipboard->wait_is_text_available();
  auto sens = [](Gtk::Widget* widget, bool on) {
    if (widget)
      widget->set_sensitive(on);
  };
  sens(undo_item_, can_undo);
  sens(redo_item_, can_redo);
  sens(undo_tool_, can_undo);
  sens(redo_tool_, can_redo);
  sens(cut_item_, selection);
  sens(copy_item_, selection);
  sens(delete_item_, selection);
  sens(cut_tool_, selection);
  sens(copy_tool_, selection);
  sens(context_cut_, selection);
  sens(context_copy_, selection);
  sens(paste_item_, can_paste);
  sens(paste_tool_, can_paste);
  sens(context_paste_, can_paste);
  sens(select_all_item_, any_text);
  sens(recent_item_, !settings_.recent.empty());
}

int MainWindow::cursor_offset() const
{
  if (!buffer_)
    return 0;
  return buffer_->get_insert()->get_iter().get_offset();
}

void MainWindow::undo()
{
  if (undo_.empty())
    return;
  Snapshot current;
  current.doc = capture();
  current.offset = cursor_offset();
  redo_.push_back(std::move(current));
  const Snapshot snap = undo_.back();
  undo_.pop_back();
  restoring_ = true;
  replace_buffer(snap.doc, snap.offset);
  restoring_ = false;
  update_title();
  update_actions();
  sync_format_controls();
}

void MainWindow::redo()
{
  if (redo_.empty())
    return;
  Snapshot current;
  current.doc = capture();
  current.offset = cursor_offset();
  undo_.push_back(std::move(current));
  const Snapshot snap = redo_.back();
  redo_.pop_back();
  restoring_ = true;
  replace_buffer(snap.doc, snap.offset);
  restoring_ = false;
  update_title();
  update_actions();
  sync_format_controls();
}

void MainWindow::on_user_begin()
{
  if (loading_ || restoring_)
    return;
  in_user_ = true;
  if (static_cast<int>(undo_.size()) >= kUndoCap)
    undo_.erase(undo_.begin());
  Snapshot snap;
  snap.doc = capture();
  snap.offset = cursor_offset();
  undo_.push_back(std::move(snap));
  redo_.clear();
}

void MainWindow::on_user_end()
{
  if (!in_user_)
    return;
  in_user_ = false;
  finish_pending();
  normalise_paragraphs();
  if (pending_para_set_ && !final_paragraph_empty())
    pending_para_set_ = false;
  const Document current = capture();
  if (!undo_.empty() && undo_.back().doc == current) {
    undo_.pop_back();
  } else {
    coalesce_typing(current);
    last_typed_us_ = g_get_monotonic_time();
  }
  update_title();
  update_actions();
  apply_page_size();
  sync_format_controls();
}

void MainWindow::coalesce_typing(const Document& current)
{
  // A burst of typing is one undo step. A pause, a format change, or a moved
  // caret starts a new one.
  if (undo_.size() < 2 || !redo_.empty() || last_typed_us_ == 0)
    return;
  const gint64 now = g_get_monotonic_time();
  if (now - last_typed_us_ > 1000000)
    return;
  const Snapshot& earlier = undo_[undo_.size() - 2];
  const Snapshot& intermediate = undo_.back();
  Insertion first;
  Insertion second;
  if (!one_insertion(earlier.doc, intermediate.doc, first) ||
      !one_insertion(intermediate.doc, current, second))
    return;
  if (intermediate.offset != first.at + first.len)
    return;
  if (cursor_offset() != second.at + second.len)
    return;
  if (second.at != first.at + first.len)
    return;
  undo_.pop_back();
}

void MainWindow::on_inserted(const Gtk::TextBuffer::iterator& pos, const Glib::ustring& text,
                             int /*bytes*/)
{
  if (loading_ || restoring_ || !in_user_)
    return;
  const int end = pos.get_offset();
  const int start = end - static_cast<int>(text.length());
  if (start < end)
    note_insert(start, end);
}

void MainWindow::on_mark_set(const Gtk::TextBuffer::iterator& /*location*/,
                             const Glib::RefPtr<Gtk::TextBuffer::Mark>& mark)
{
  if (loading_ || restoring_ || in_user_ || !buffer_)
    return;
  if (mark != buffer_->get_insert() && mark != buffer_->get_selection_bound())
    return;
  sync_format_controls();
  update_actions();
}

void MainWindow::note_insert(int start, int end)
{
  if (!pending_insert_) {
    pending_insert_ = true;
    insert_start_ = buffer_->create_mark(buffer_->get_iter_at_offset(start), true);
    insert_end_ = buffer_->create_mark(buffer_->get_iter_at_offset(end), false);
    return;
  }
  if (insert_start_ && start < insert_start_->get_iter().get_offset())
    buffer_->move_mark(insert_start_, buffer_->get_iter_at_offset(start));
  if (insert_end_ && end > insert_end_->get_iter().get_offset())
    buffer_->move_mark(insert_end_, buffer_->get_iter_at_offset(end));
}

void MainWindow::finish_pending()
{
  if (!pending_insert_)
    return;
  pending_insert_ = false;
  int start = 0;
  int end = 0;
  if (insert_start_)
    start = insert_start_->get_iter().get_offset();
  if (insert_end_)
    end = insert_end_->get_iter().get_offset();
  if (insert_start_)
    buffer_->delete_mark(insert_start_);
  if (insert_end_)
    buffer_->delete_mark(insert_end_);
  insert_start_.reset();
  insert_end_.reset();
  // Inserted text joins the paragraph it lands in. Whole paragraphs pasted
  // from this buffer keep their own format; the trailing piece that merges
  // into the destination does not.
  {
    int tail = start;
    for (auto iter = buffer_->get_iter_at_offset(start); iter.get_offset() < end; ++iter) {
      if (iter.get_char() == '\n')
        tail = iter.get_offset() + 1;
    }
    std::vector<Glib::RefPtr<Gtk::TextTag>> stale;
    for (auto iter = buffer_->get_iter_at_offset(tail); iter.get_offset() < end; ++iter) {
      auto tag = para_tag_at(iter);
      if (tag && std::find(stale.begin(), stale.end(), tag) == stale.end())
        stale.push_back(tag);
    }
    for (const auto& tag : stale)
      buffer_->remove_tag(tag, buffer_->get_iter_at_offset(tail), buffer_->get_iter_at_offset(end));
    const auto dest = para_tag(destination_para(start, end));
    for (int i = start; i < end;) {
      int j = i;
      while (j < end && !para_tag_at(buffer_->get_iter_at_offset(j)))
        ++j;
      if (j > i)
        buffer_->apply_tag(dest, buffer_->get_iter_at_offset(i), buffer_->get_iter_at_offset(j));
      while (j < end && para_tag_at(buffer_->get_iter_at_offset(j)))
        ++j;
      i = j;
    }
  }
  // Text inserted inside an existing run inherits that run's tag. That is the
  // surrounding format, not formatting the user pasted in.
  std::vector<std::string> inherited;
  if (start > 0) {
    auto prev = buffer_->get_iter_at_offset(start - 1);
    if (prev.get_char() != '\n') {
      for (const auto& tag : prev.get_tags()) {
        Run run;
        if (parse_fmt(tag_name(tag), run))
          inherited.push_back(tag_name(tag));
      }
    }
  }
  for (int i = start; i < end;) {
    auto iter = buffer_->get_iter_at_offset(i);
    if (iter.get_char() == '\n') {
      ++i;
      continue;
    }
    int j = i;
    while (j < end && buffer_->get_iter_at_offset(j).get_char() != '\n')
      ++j;
    bool foreign = false;
    for (int k = i; k < j && !foreign; ++k) {
      for (const auto& tag : buffer_->get_iter_at_offset(k).get_tags()) {
        Run run;
        if (!parse_fmt(tag_name(tag), run))
          continue;
        if (std::find(inherited.begin(), inherited.end(), tag_name(tag)) == inherited.end())
          foreign = true;
      }
    }
    if (!foreign) {
      auto from = buffer_->get_iter_at_offset(i);
      auto to = buffer_->get_iter_at_offset(j);
      strip_fmt(from, to);
      from = buffer_->get_iter_at_offset(i);
      to = buffer_->get_iter_at_offset(j);
      buffer_->apply_tag(format_tag(typing_), from, to);
      const int heading = heading_near(i);
      if (heading > 0)
        buffer_->apply_tag(heading_tag(heading), from, to);
    }
    i = j;
  }
  tag_line_breaks(start, end);
}

Glib::RefPtr<Gtk::TextTag> MainWindow::format_tag(const Run& run)
{
  Run key = run;
  if (key.font.empty())
    key.font = "Sans";
  if (key.size <= 0)
    key.size = 11;
  const Glib::ustring name = fmt_name(key);
  auto table = buffer_->get_tag_table();
  auto tag = table->lookup(name);
  if (!tag) {
    tag = Gtk::TextTag::create(name);
    tag->property_family() = key.font;
    tag->property_size_points() = key.size * zoom_factor();
    tag->property_weight() = key.bold ? Pango::WEIGHT_BOLD : Pango::WEIGHT_NORMAL;
    tag->property_style() = key.italic ? Pango::STYLE_ITALIC : Pango::STYLE_NORMAL;
    tag->property_underline() = key.underline ? Pango::UNDERLINE_SINGLE : Pango::UNDERLINE_NONE;
    table->add(tag);
    raise_headings();
  }
  return tag;
}

Glib::RefPtr<Gtk::TextTag> MainWindow::heading_tag(int level)
{
  const Glib::ustring name = "heading-" + std::to_string(level);
  auto table = buffer_->get_tag_table();
  auto tag = table->lookup(name);
  if (tag)
    return tag;
  tag = Gtk::TextTag::create(name);
  const double scale[] = {0, 2.0, 1.6, 1.35, 1.15, 1.05, 1.0};
  const int index = level >= 1 && level <= 6 ? level : 1;
  tag->property_scale() = scale[index];
  tag->property_weight() = Pango::WEIGHT_BOLD;
  const double z = std::max(0.5, zoom_factor());
  tag->property_pixels_above_lines() = static_cast<int>(8 * z);
  tag->property_pixels_below_lines() = static_cast<int>(4 * z);
  table->add(tag);
  return tag;
}

void MainWindow::raise_headings()
{
  auto table = buffer_->get_tag_table();
  for (int level = 1; level <= 6; ++level) {
    auto tag = table->lookup("heading-" + std::to_string(level));
    if (tag)
      tag->set_priority(table->get_size() - 1);
  }
}

void MainWindow::restyle_tags()
{
  const double z = zoom_factor();
  buffer_->get_tag_table()->foreach ([&](const Glib::RefPtr<Gtk::TextTag>& tag) {
    Run run;
    if (parse_fmt(tag_name(tag), run))
      tag->property_size_points() = run.size * z;
    const int level = heading_level_name(tag_name(tag));
    if (level > 0) {
      tag->property_pixels_above_lines() = static_cast<int>(8 * z);
      tag->property_pixels_below_lines() = static_cast<int>(4 * z);
    }
    ParaFormat format;
    if (parse_para(tag_name(tag), format))
      style_para_tag(tag, format);
  });
  caret_key_.clear();
  update_caret_font();
}

int MainWindow::margin_x() const
{
  return std::max(8, static_cast<int>(42 * zoom_factor() + 0.5));
}

int MainWindow::indent_px(int twips) const
{
  const double px = static_cast<double>(twips) * kPageW * zoom_factor() / kPageTwips;
  return static_cast<int>(px >= 0 ? px + 0.5 : px - 0.5);
}

void MainWindow::apply_margins()
{
  const double z = zoom_factor();
  const int x = margin_x();
  const int y = std::max(8, static_cast<int>(36 * z + 0.5));
  if (text_.get_left_margin() != x)
    text_.set_left_margin(x);
  if (text_.get_right_margin() != x)
    text_.set_right_margin(x);
  if (text_.get_top_margin() != y)
    text_.set_top_margin(y);
  if (text_.get_bottom_margin() != y)
    text_.set_bottom_margin(y);
}

double MainWindow::zoom_factor() const
{
  if (settings_.zoom == 0) {
    const int width = std::max(120, paste_.get_allocated_width() - 36);
    return static_cast<double>(width) / static_cast<double>(kPageW);
  }
  return static_cast<double>(settings_.zoom) / 100.0;
}

Run MainWindow::format_of(const Gtk::TextIter& iter) const
{
  bool found = false;
  int best = -1;
  Run chosen;
  for (const auto& tag : iter.get_tags()) {
    Run run;
    if (!parse_fmt(tag_name(tag), run))
      continue;
    const int priority = tag->get_priority();
    if (!found || priority > best) {
      found = true;
      best = priority;
      chosen = run;
    }
  }
  if (found)
    return chosen;
  Run run;
  run.font = settings_.default_font.empty() ? "Sans" : settings_.default_font;
  run.size = known_size(settings_.default_size) ? settings_.default_size : 11;
  return run;
}

int MainWindow::heading_of(const Gtk::TextIter& iter) const
{
  int found = 0;
  int best = -1;
  for (const auto& tag : iter.get_tags()) {
    const int level = heading_level_name(tag_name(tag));
    if (level <= 0)
      continue;
    const int priority = tag->get_priority();
    if (priority > best) {
      best = priority;
      found = level;
    }
  }
  return found;
}

int MainWindow::heading_near(int offset) const
{
  if (offset > 0) {
    auto prev = buffer_->get_iter_at_offset(offset - 1);
    if (prev.get_char() != '\n') {
      const int level = heading_of(prev);
      if (level > 0)
        return level;
    }
  }
  for (auto iter = buffer_->get_iter_at_offset(offset); !iter.is_end() && iter.get_char() != '\n';
       ++iter) {
    const int level = heading_of(iter);
    if (level > 0)
      return level;
  }
  return 0;
}

bool MainWindow::has_fmt(const Gtk::TextIter& iter) const
{
  for (const auto& tag : iter.get_tags()) {
    Run run;
    if (parse_fmt(tag_name(tag), run))
      return true;
  }
  return false;
}

void MainWindow::strip_fmt(const Gtk::TextIter& from, const Gtk::TextIter& to)
{
  std::vector<Glib::RefPtr<Gtk::TextTag>> tags;
  for (auto iter = from; iter != to; ++iter) {
    for (const auto& tag : iter.get_tags()) {
      Run ignored;
      if (!parse_fmt(tag_name(tag), ignored))
        continue;
      if (std::find(tags.begin(), tags.end(), tag) == tags.end())
        tags.push_back(tag);
    }
  }
  for (const auto& tag : tags)
    buffer_->remove_tag(tag, from, to);
}

Run MainWindow::line_break_mark(int newline) const
{
  int begin = newline;
  while (begin > 0 && buffer_->get_iter_at_offset(begin - 1).get_char() != '\n')
    --begin;
  bool any = false;
  Run mark;
  for (int k = begin; k < newline; ++k) {
    auto iter = buffer_->get_iter_at_offset(k);
    if (iter.get_char() == '\n')
      continue;
    Run run = format_of(iter);
    run.text.clear();
    if (!any || run.size > mark.size) {
      mark = run;
      any = true;
    }
  }
  if (!any) {
    mark.font = settings_.default_font.empty() ? "Sans" : settings_.default_font;
    mark.size = known_size(settings_.default_size) ? settings_.default_size : 11;
    return mark;
  }
  if (mark.font.empty())
    mark.font = "Sans";
  if (!known_size(mark.size))
    mark.size = 11;
  return mark;
}

void MainWindow::tag_line_breaks(int start, int end)
{
  // The newline's font is part of the line box. Pin a line that has text to
  // that text's size. An untagged newline uses the widget font, which stays
  // on the document default, so blank lines do not follow the caret.
  if (!buffer_)
    return;
  const int count = buffer_->get_char_count();
  if (count == 0)
    return;
  start = std::max(0, std::min(start, count));
  end = std::max(start, std::min(end, count));
  int from = start;
  while (from > 0 && buffer_->get_iter_at_offset(from - 1).get_char() != '\n')
    --from;
  int to = end;
  if (to > 0 && buffer_->get_iter_at_offset(to - 1).get_char() != '\n') {
    while (to < count && buffer_->get_iter_at_offset(to).get_char() != '\n')
      ++to;
    if (to < count)
      ++to;
  }
  for (int n = from; n < to; ++n) {
    if (buffer_->get_iter_at_offset(n).get_char() != '\n')
      continue;
    int begin = n;
    while (begin > 0 && buffer_->get_iter_at_offset(begin - 1).get_char() != '\n')
      --begin;
    auto from_it = buffer_->get_iter_at_offset(n);
    auto to_it = buffer_->get_iter_at_offset(n + 1);
    strip_fmt(from_it, to_it);
    // An empty line has no size in the file. Leave the newline untagged so
    // the gap stays at the document default and the caret keeps its font.
    if (begin == n)
      continue;
    const Run mark = line_break_mark(n);
    from_it = buffer_->get_iter_at_offset(n);
    to_it = buffer_->get_iter_at_offset(n + 1);
    buffer_->apply_tag(format_tag(mark), from_it, to_it);
  }
}

void MainWindow::apply_run_edit(const std::function<void(Run&)>& edit)
{
  Gtk::TextBuffer::iterator start_iter;
  Gtk::TextBuffer::iterator end_iter;
  if (!buffer_->get_selection_bounds(start_iter, end_iter) || start_iter == end_iter) {
    edit(typing_);
    show_format(typing_);
    text_.grab_focus();
    return;
  }
  const int start = start_iter.get_offset();
  const int end = end_iter.get_offset();
  buffer_->begin_user_action();
  for (int i = start; i < end;) {
    auto iter = buffer_->get_iter_at_offset(i);
    if (iter.get_char() == '\n') {
      ++i;
      continue;
    }
    Run run = format_of(iter);
    int j = i + 1;
    while (j < end) {
      auto next = buffer_->get_iter_at_offset(j);
      if (next.get_char() == '\n' || !same_format(format_of(next), run))
        break;
      ++j;
    }
    edit(run);
    auto from = buffer_->get_iter_at_offset(i);
    auto to = buffer_->get_iter_at_offset(j);
    strip_fmt(from, to);
    buffer_->apply_tag(format_tag(run), from, to);
    i = j;
  }
  tag_line_breaks(start, end);
  buffer_->end_user_action();
  buffer_->select_range(buffer_->get_iter_at_offset(start), buffer_->get_iter_at_offset(end));
  show_format(format_of(buffer_->get_iter_at_offset(start)));
  text_.grab_focus();
}

bool MainWindow::text_flag(const Run& run, TextFlag flag)
{
  switch (flag) {
    case TextFlag::Bold:
      return run.bold;
    case TextFlag::Italic:
      return run.italic;
    case TextFlag::Underline:
      return run.underline;
  }
  return false;
}

void MainWindow::set_text_flag(Run& run, TextFlag flag, bool on)
{
  switch (flag) {
    case TextFlag::Bold:
      run.bold = on;
      break;
    case TextFlag::Italic:
      run.italic = on;
      break;
    case TextFlag::Underline:
      run.underline = on;
      break;
  }
}

void MainWindow::toggle_flag(TextFlag flag)
{
  if (suppress_format_)
    return;
  Gtk::TextBuffer::iterator start_iter;
  Gtk::TextBuffer::iterator end_iter;
  if (!buffer_->get_selection_bounds(start_iter, end_iter) || start_iter == end_iter) {
    set_text_flag(typing_, flag, !text_flag(typing_, flag));
    show_format(typing_);
    text_.grab_focus();
    return;
  }
  bool all = true;
  bool any = false;
  for (auto iter = start_iter; iter != end_iter; ++iter) {
    if (iter.get_char() == '\n')
      continue;
    any = true;
    if (!text_flag(format_of(iter), flag)) {
      all = false;
      break;
    }
  }
  if (!any) {
    text_.grab_focus();
    return;
  }
  const bool turn_on = !all;
  apply_run_edit([flag, turn_on](Run& run) { set_text_flag(run, flag, turn_on); });
}

void MainWindow::show_format(const Run& run)
{
  suppress_format_ = true;
  const Glib::ustring font = run.font.empty() ? "Sans" : run.font;
  if (font_combo_.get_active_text() != font) {
    font_combo_.set_active_text(font);
    if (font_combo_.get_active_text() != font) {
      font_combo_.append(font);
      font_combo_.set_active_text(font);
    }
  }
  const Glib::ustring size = std::to_string(run.size);
  if (size_combo_.get_active_text() != size && known_size(run.size))
    size_combo_.set_active_text(size);
  if (bold_toggle_ && bold_toggle_->get_active() != run.bold)
    bold_toggle_->set_active(run.bold);
  if (italic_toggle_ && italic_toggle_->get_active() != run.italic)
    italic_toggle_->set_active(run.italic);
  if (underline_toggle_ && underline_toggle_->get_active() != run.underline)
    underline_toggle_->set_active(run.underline);
  suppress_format_ = false;
  update_caret_font();
}

void MainWindow::sync_format_controls()
{
  if (loading_ || restoring_ || suppress_format_ || in_user_ || !buffer_)
    return;
  ruler_.queue_draw();
  show_align();
  auto iter = buffer_->get_insert()->get_iter();
  if (iter.get_offset() > 0) {
    auto prev = iter;
    if (prev.backward_char() && prev.get_char() != '\n') {
      typing_ = format_of(prev);
      show_format(typing_);
      return;
    }
  }
  if (!iter.is_end() && (iter.get_char() != '\n' || has_fmt(iter))) {
    typing_ = format_of(iter);
    show_format(typing_);
    return;
  }
  show_format(typing_);
}

void MainWindow::update_caret_font()
{
  // Blank lines carry no character of their own, so they render in the widget
  // font. Keep that font on the document default. Following the caret resized
  // every blank line whenever the selection moved.
  const std::string font = settings_.default_font.empty() ? "Sans" : settings_.default_font;
  const int size = known_size(settings_.default_size) ? settings_.default_size : 11;
  const int milli = static_cast<int>(size * zoom_factor() * 100.0);
  const std::string key = font + ":" + std::to_string(milli);
  if (key == caret_key_)
    return;
  caret_key_ = key;
  Pango::FontDescription desc;
  desc.set_family(font);
  desc.set_size(static_cast<int>(size * zoom_factor() * PANGO_SCALE));
  text_.override_font(desc);
}

void MainWindow::on_font_changed()
{
  if (suppress_format_)
    return;
  const Glib::ustring name = font_combo_.get_active_text();
  if (name.empty())
    return;
  apply_run_edit([name](Run& run) { run.font = name.raw(); });
}

void MainWindow::on_size_changed()
{
  if (suppress_format_)
    return;
  int size = 11;
  try {
    size = std::stoi(size_combo_.get_active_text().raw());
  } catch (const std::exception&) {
    return;
  }
  if (!known_size(size))
    return;
  apply_run_edit([size](Run& run) { run.size = size; });
}

Glib::RefPtr<Gtk::TextTag> MainWindow::para_tag(const ParaFormat& raw)
{
  const ParaFormat format{clamp_indents(raw.indents), raw.align};
  const Glib::ustring name = para_name(format);
  auto table = buffer_->get_tag_table();
  auto tag = table->lookup(name);
  if (!tag) {
    tag = Gtk::TextTag::create(name);
    style_para_tag(tag, format);
    table->add(tag);
  }
  return tag;
}

void MainWindow::style_para_tag(const Glib::RefPtr<Gtk::TextTag>& tag,
                                const ParaFormat& format) const
{
  if (format.align == Align::Center)
    tag->property_justification() = Gtk::JUSTIFY_CENTER;
  else if (format.align == Align::Right)
    tag->property_justification() = Gtk::JUSTIFY_RIGHT;
  // No indent leaves the view's page margins in charge.
  const Indents& indents = format.indents;
  if (indents == Indents{})
    return;
  // A tag's margin replaces the view's, so the page margin is added here.
  // GTK hangs a negative indent from the first line; Word hangs the first
  // line out from the rest. Moving the margin left by the hang lines them up.
  const int hang = std::min(0, indents.first);
  tag->property_left_margin() = std::max(0, margin_x() + indent_px(indents.left + hang));
  tag->property_right_margin() = margin_x() + indent_px(indents.right);
  tag->property_indent() = indent_px(indents.first);
}

Glib::RefPtr<Gtk::TextTag> MainWindow::para_tag_at(Gtk::TextIter iter) const
{
  ParaFormat ignored;
  for (const auto& tag : iter.get_tags()) {
    if (parse_para(tag_name(tag), ignored))
      return tag;
  }
  return {};
}

int MainWindow::paragraph_start(int offset) const
{
  // Paragraphs end at '\n' only, as in capture(). GTK's own lines also break
  // at other separators.
  auto iter = buffer_->get_iter_at_offset(offset);
  while (iter.get_offset() > 0) {
    auto prev = iter;
    prev.backward_char();
    if (prev.get_char() == '\n')
      break;
    iter = prev;
  }
  return iter.get_offset();
}

int MainWindow::paragraph_end(int offset) const
{
  // Just past the paragraph's newline, or the end of the buffer.
  auto iter = buffer_->get_iter_at_offset(offset);
  while (!iter.is_end()) {
    const bool newline = iter.get_char() == '\n';
    ++iter;
    if (newline)
      break;
  }
  return iter.get_offset();
}

bool MainWindow::final_paragraph_empty() const
{
  const int count = buffer_->get_char_count();
  return count == 0 || buffer_->get_iter_at_offset(count - 1).get_char() == '\n';
}

ParaFormat MainWindow::para_at(int offset) const
{
  // The same rule as capture(): the paragraph's first tagged character, then
  // the pending format of an empty last paragraph, then the paragraph above.
  auto iter = buffer_->get_iter_at_offset(paragraph_start(offset));
  const auto line_start = iter;
  for (; !iter.is_end(); ++iter) {
    ParaFormat format;
    if (auto tag = para_tag_at(iter)) {
      if (parse_para(tag_name(tag), format))
        return format;
    }
    if (iter.get_char() == '\n')
      break;
  }
  if (iter.is_end() && pending_para_set_)
    return pending_para_;
  if (line_start.get_offset() > 0)
    return para_at(line_start.get_offset() - 1);
  return ParaFormat{};
}

Indents MainWindow::indents_at(int offset) const
{
  return para_at(offset).indents;
}

ParaFormat MainWindow::destination_para(int start, int end) const
{
  // Text before the insertion on the same line, then text after it, decides.
  if (start > 0) {
    auto before = buffer_->get_iter_at_offset(start - 1);
    ParaFormat format;
    if (before.get_char() != '\n') {
      if (auto tag = para_tag_at(before)) {
        if (parse_para(tag_name(tag), format))
          return format;
      }
    }
  }
  for (auto after = buffer_->get_iter_at_offset(end); !after.is_end(); ++after) {
    ParaFormat format;
    if (auto tag = para_tag_at(after)) {
      if (parse_para(tag_name(tag), format))
        return format;
    }
    if (after.get_char() == '\n')
      break;
  }
  if (pending_para_set_)
    return pending_para_;
  if (start > 0)
    return para_at(start - 1);
  return ParaFormat{};
}

void MainWindow::normalise_paragraphs()
{
  // A deleted newline joins two paragraphs. The joined paragraph keeps the
  // first one's indents and alignment, as AbiWord and LibreOffice do.
  const int count = buffer_->get_char_count();
  Glib::RefPtr<Gtk::TextTag> carry = para_tag(ParaFormat{});
  int begin = 0;
  while (begin < count) {
    Glib::RefPtr<Gtk::TextTag> chosen;
    std::vector<Glib::RefPtr<Gtk::TextTag>> seen;
    bool uniform = true;
    int end = begin;
    for (auto iter = buffer_->get_iter_at_offset(begin); !iter.is_end(); ++iter) {
      auto tag = para_tag_at(iter);
      if (tag && !chosen)
        chosen = tag;
      if (tag && std::find(seen.begin(), seen.end(), tag) == seen.end())
        seen.push_back(tag);
      if (!tag || tag != chosen)
        uniform = false;
      end = iter.get_offset() + 1;
      if (iter.get_char() == '\n')
        break;
    }
    if (!chosen) {
      chosen = carry;
      uniform = false;
    }
    if (!uniform) {
      for (const auto& tag : seen)
        buffer_->remove_tag(tag, buffer_->get_iter_at_offset(begin),
                            buffer_->get_iter_at_offset(end));
      buffer_->apply_tag(chosen, buffer_->get_iter_at_offset(begin),
                         buffer_->get_iter_at_offset(end));
    }
    carry = chosen;
    begin = end;
  }
}

void MainWindow::on_erase(const Gtk::TextBuffer::iterator& from,
                          const Gtk::TextBuffer::iterator& to)
{
  // Erasing through to the end of the buffer from the start of a line leaves
  // an empty last paragraph. Like Word's surviving paragraph mark, it keeps
  // the format of the last paragraph that was there.
  if (loading_ || !buffer_ || !to.is_end() || from == to)
    return;
  if (paragraph_start(from.get_offset()) != from.get_offset())
    return;
  pending_para_ = para_at(buffer_->get_char_count());
  pending_para_set_ = true;
}

void MainWindow::apply_para_edit(const std::function<void(ParaFormat&)>& edit)
{
  Gtk::TextBuffer::iterator start_iter;
  Gtk::TextBuffer::iterator end_iter;
  buffer_->get_selection_bounds(start_iter, end_iter);
  const int sel_start = start_iter.get_offset();
  const int sel_end = end_iter.get_offset();
  // Every paragraph the selection touches, each with its newline. A
  // selection that ends just after a newline does not reach the next one.
  const int start = paragraph_start(sel_start);
  const int end = paragraph_end(sel_end > sel_start ? sel_end - 1 : sel_end);
  buffer_->begin_user_action();
  // Each paragraph is edited from its own format, so changing one property
  // keeps the others where the paragraphs differ.
  for (int begin = start; begin < end;) {
    const int stop = paragraph_end(begin);
    ParaFormat format = para_at(begin);
    edit(format);
    std::vector<Glib::RefPtr<Gtk::TextTag>> seen;
    for (auto iter = buffer_->get_iter_at_offset(begin); iter.get_offset() < stop; ++iter) {
      auto tag = para_tag_at(iter);
      if (tag && std::find(seen.begin(), seen.end(), tag) == seen.end())
        seen.push_back(tag);
    }
    for (const auto& tag : seen)
      buffer_->remove_tag(tag, buffer_->get_iter_at_offset(begin),
                          buffer_->get_iter_at_offset(stop));
    buffer_->apply_tag(para_tag(format), buffer_->get_iter_at_offset(begin),
                       buffer_->get_iter_at_offset(stop));
    begin = stop;
  }
  // The empty last paragraph has nothing to tag. Hold its format aside.
  if (end == buffer_->get_char_count() && final_paragraph_empty()) {
    ParaFormat format = para_at(end);
    edit(format);
    pending_para_ = ParaFormat{clamp_indents(format.indents), format.align};
    pending_para_set_ = true;
  }
  buffer_->end_user_action();
  buffer_->select_range(buffer_->get_iter_at_offset(sel_start),
                        buffer_->get_iter_at_offset(sel_end));
  ruler_.queue_draw();
  show_align();
  text_.grab_focus();
}

void MainWindow::apply_align(Align align)
{
  apply_para_edit([align](ParaFormat& format) { format.align = align; });
}

void MainWindow::on_align_toggled(Align align)
{
  if (suppress_format_)
    return;
  // The three buttons act as one group, as in Word 97. Pressing the button
  // already down leaves the paragraph as it is, except Center and Align
  // Right, which go back to left.
  Gtk::ToggleToolButton* button = align == Align::Center  ? align_center_toggle_
                                  : align == Align::Right ? align_right_toggle_
                                                          : align_left_toggle_;
  if (button && !button->get_active())
    align = Align::Left;
  apply_align(align);
}

void MainWindow::show_align()
{
  if (!buffer_)
    return;
  const Align align = para_at(cursor_offset()).align;
  const bool guard = suppress_format_;
  suppress_format_ = true;
  auto show = [](Gtk::ToggleToolButton* button, bool on) {
    if (button && button->get_active() != on)
      button->set_active(on);
  };
  show(align_left_toggle_, align == Align::Left);
  show(align_center_toggle_, align == Align::Center);
  show(align_right_toggle_, align == Align::Right);
  suppress_format_ = guard;
}

void MainWindow::on_paragraph()
{
  // Word 97's Format > Paragraph alignment and indentation, in the unit
  // Tools > Options... chose. Spacing waits for 2.0.
  const Units units = settings_.units;
  const Indents current = indents_at(cursor_offset());
  const Align current_align = para_at(cursor_offset()).align;
  Gtk::Dialog dialog("Paragraph", *this, true);
  dialog.set_resizable(false);
  dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("_OK", Gtk::RESPONSE_OK);
  dialog.set_default_response(Gtk::RESPONSE_OK);
  auto* frame = Gtk::manage(new Gtk::Frame("Indentation"));
  auto* grid = Gtk::manage(new Gtk::Grid());
  grid->set_row_spacing(8);
  grid->set_column_spacing(12);
  grid->set_margin_top(8);
  grid->set_margin_bottom(12);
  grid->set_margin_start(12);
  grid->set_margin_end(12);
  frame->set_margin_top(12);
  frame->set_margin_bottom(12);
  frame->set_margin_start(12);
  frame->set_margin_end(12);
  const double step = units_step(units);
  const double max_value = std::floor(twips_to_units(kMaxIndent, units) * 100.0 + 1e-9) / 100.0;
  // Each field shows its value with the unit's suffix, 0.5" or 1.27 cm, and
  // reads back a bare number, a suffixed one, or the other unit typed out.
  auto spin = [units, step, max_value](int twips) {
    auto* button =
        Gtk::manage(new Gtk::SpinButton(Gtk::Adjustment::create(0, 0, max_value, step, step * 5),
                                        step, static_cast<guint>(units_digits(units))));
    button->set_numeric(false);
    // Text that is not a measure keeps the old value instead of becoming 0.
    button->set_update_policy(Gtk::UPDATE_IF_VALID);
    button->set_width_chars(8);
    button->signal_output().connect(
        [button, units] {
          button->set_text(format_measure(button->get_value(), units));
          return true;
        },
        false);
    // "input" has no accumulator: the last handler's answer wins, and the
    // class handler answers "not handled". Connect after it.
    button->signal_input().connect(
        [button, units](double* value) {
          double parsed = 0;
          if (!parse_measure(button->get_text().raw(), units, parsed))
            return GTK_INPUT_ERROR;
          *value = parsed;
          return 1;
        },
        true);
    // Leaving a field reads what was typed and shows it back in the unit, so
    // "1 in" becomes 2.54 cm and text that is not a measure reverts.
    button->signal_focus_out_event().connect(
        [button, units](GdkEventFocus*) {
          button->update();
          button->set_text(format_measure(button->get_value(), units));
          return false;
        },
        false);
    button->set_value(units_round(twips_to_units(twips, units), units));
    button->set_activates_default(true);
    return button;
  };
  auto label = [](const char* text, Gtk::Widget& target) {
    auto* item = Gtk::manage(new Gtk::Label(text, true));
    item->set_halign(Gtk::ALIGN_START);
    item->set_mnemonic_widget(target);
    return item;
  };
  const int magnitude = current.first < 0 ? -current.first : current.first;
  auto* left = spin(current.left);
  auto* right = spin(current.right);
  auto* special = Gtk::manage(new Gtk::ComboBoxText());
  special->append("(none)");
  special->append("First line");
  special->append("Hanging");
  const int special_was = current.first > 0 ? 1 : current.first < 0 ? 2 : 0;
  special->set_active(special_was);
  auto* by = spin(magnitude);
  by->set_sensitive(current.first != 0);
  // Word fills in half an inch, or 1.27 cm, when Special turns on at zero.
  const double default_by = units == Units::Centimetres ? 1.27 : 0.5;
  special->signal_changed().connect([special, by, default_by] {
    by->set_sensitive(special->get_active_row_number() != 0);
    if (special->get_active_row_number() != 0 && by->get_value() == 0)
      by->set_value(default_by);
  });
  // What each field showed when the dialog opened, to tell an untouched
  // field from an edited one.
  const double left_shown = left->get_value();
  const double right_shown = right->get_value();
  const double by_shown = by->get_value();
  grid->attach(*label("_Left:", *left), 0, 0, 1, 1);
  grid->attach(*left, 1, 0, 1, 1);
  grid->attach(*label("_Right:", *right), 0, 1, 1, 1);
  grid->attach(*right, 1, 1, 1, 1);
  grid->attach(*label("_Special:", *special), 2, 0, 1, 1);
  grid->attach(*special, 2, 1, 1, 1);
  grid->attach(*label("B_y:", *by), 3, 0, 1, 1);
  grid->attach(*by, 3, 1, 1, 1);
  // Alignment sits above Indentation, as in Word 97's dialog.
  auto* alignment = Gtk::manage(new Gtk::ComboBoxText());
  alignment->append("Left");
  alignment->append("Centered");
  alignment->append("Right");
  alignment->set_active(static_cast<int>(current_align));
  auto* align_row = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 12));
  align_row->set_margin_top(12);
  align_row->set_margin_start(12);
  align_row->set_margin_end(12);
  align_row->pack_start(*label("Ali_gnment:", *alignment), Gtk::PACK_SHRINK);
  align_row->pack_start(*alignment, Gtk::PACK_SHRINK);
  dialog.get_content_area()->pack_start(*align_row, Gtk::PACK_SHRINK);

  frame->add(*grid);
  dialog.get_content_area()->pack_start(*frame, Gtk::PACK_SHRINK);
  dialog.show_all_children();
  // Word 97 will not let the first line start left of the left margin. OK
  // on such a choice explains why and goes back to the dialog, with the
  // field to fix focused, rather than quietly changing the value.
  Indents chosen;
  for (;;) {
    if (dialog.run() != Gtk::RESPONSE_OK) {
      text_.grab_focus();
      return;
    }
    left->update();
    right->update();
    by->update();
    // An untouched field keeps the file's twips, so OK on an unchanged
    // dialog changes nothing even where the value on screen is rounded.
    chosen.left = keep_twips(current.left, left_shown, left->get_value(), units);
    chosen.right = keep_twips(current.right, right_shown, right->get_value(), units);
    int amount = keep_twips(magnitude, by_shown, by->get_value(), units);
    const int row = special->get_active_row_number();
    if (row == 2)
      amount = hang_twips(by->get_value(), amount, chosen.left, left->get_value(), units);
    chosen.first = row == 1 ? amount : row == 2 ? -amount : 0;
    if (indents_fit(chosen))
      break;
    Gtk::MessageDialog message(dialog,
                               "The hanging indent is larger than the left indent. The first "
                               "line cannot start to the left of the margin.",
                               false, Gtk::MESSAGE_WARNING, Gtk::BUTTONS_OK, true);
    message.set_title("Paragraph");
    message.run();
    message.hide();
    // The By field is at fault unless only Left changed.
    const bool by_edited = row != special_was || std::fabs(by->get_value() - by_shown) > 1e-9;
    Gtk::SpinButton* offending = by_edited || left->get_value() == left_shown ? by : left;
    offending->grab_focus();
    offending->select_region(0, -1);
  }
  if (chosen == current) {
    // Only what changed is applied, so a new alignment keeps each selected
    // paragraph's own indents, and new indents keep each one's alignment.
    const auto align = static_cast<Align>(alignment->get_active_row_number());
    if (align == current_align) {
      text_.grab_focus();
      return;
    }
    apply_align(align);
    return;
  }
  const auto align = static_cast<Align>(alignment->get_active_row_number());
  const bool align_changed = align != current_align;
  apply_para_edit([chosen, align, align_changed](ParaFormat& format) {
    format.indents = chosen;
    if (align_changed)
      format.align = align;
  });
}

void MainWindow::build_find()
{
  find_dialog_ = std::make_unique<Gtk::Dialog>("Find and Replace", *this, true);
  find_dialog_->set_modal(true);
  find_dialog_->set_resizable(false);
  auto* grid = Gtk::manage(new Gtk::Grid());
  grid->set_row_spacing(8);
  grid->set_column_spacing(12);
  grid->set_margin_top(12);
  grid->set_margin_bottom(12);
  grid->set_margin_start(12);
  grid->set_margin_end(12);
  auto* find_label = Gtk::manage(new Gtk::Label("Find"));
  auto* replace_label = Gtk::manage(new Gtk::Label("Replace"));
  find_label->set_halign(Gtk::ALIGN_START);
  replace_label->set_halign(Gtk::ALIGN_START);
  find_entry_ = Gtk::manage(new Gtk::Entry());
  replace_entry_ = Gtk::manage(new Gtk::Entry());
  match_case_ = Gtk::manage(new Gtk::CheckButton("Match case"));
  find_entry_->set_size_request(260, -1);
  replace_entry_->set_size_request(260, -1);
  grid->attach(*find_label, 0, 0, 1, 1);
  grid->attach(*find_entry_, 1, 0, 1, 1);
  grid->attach(*replace_label, 0, 1, 1, 1);
  grid->attach(*replace_entry_, 1, 1, 1, 1);
  grid->attach(*match_case_, 0, 2, 2, 1);
  find_dialog_->get_content_area()->pack_start(*grid, Gtk::PACK_SHRINK);
  find_dialog_->add_button("Next", kFindNext);
  find_dialog_->add_button("Replace", kFindReplace);
  find_dialog_->add_button("Close", Gtk::RESPONSE_CLOSE);
  find_dialog_->signal_response().connect([this](int response) {
    if (response == kFindNext)
      find_next();
    else if (response == kFindReplace)
      replace_once();
    else
      find_dialog_->hide();
  });
  find_dialog_->signal_delete_event().connect(
      [this](GdkEventAny*) {
        find_dialog_->hide();
        return true;
      },
      false);
  find_dialog_->signal_key_press_event().connect(
      [this](GdkEventKey* event) {
        if (event->keyval == GDK_KEY_Escape) {
          find_dialog_->hide();
          return true;
        }
        return false;
      },
      false);
  find_entry_->signal_activate().connect([this] { find_next(); });
  replace_entry_->signal_activate().connect([this] { replace_once(); });
  find_dialog_->show_all_children();
}

void MainWindow::present_find(bool replace)
{
  if (!find_dialog_)
    build_find();
  Gtk::TextBuffer::iterator start;
  Gtk::TextBuffer::iterator end;
  if (buffer_->get_selection_bounds(start, end)) {
    const Glib::ustring selected = buffer_->get_text(start, end);
    if (!selected.empty() && selected.find('\n') == Glib::ustring::npos)
      find_entry_->set_text(selected);
  }
  find_dialog_->present();
  if (replace)
    replace_entry_->grab_focus();
  else
    find_entry_->grab_focus();
}

void MainWindow::find_next()
{
  if (!find_entry_ || !buffer_)
    return;
  const Glib::ustring needle = find_entry_->get_text();
  if (needle.empty())
    return;
  const Glib::ustring doc = buffer_->get_text();
  const bool match_case = match_case_ && match_case_->get_active();
  const Glib::ustring folded = match_case ? needle : needle.casefold();
  const int count = static_cast<int>(doc.length());
  const int width = static_cast<int>(needle.length());
  int origin = cursor_offset();
  Gtk::TextBuffer::iterator sel_start;
  Gtk::TextBuffer::iterator sel_end;
  if (buffer_->get_selection_bounds(sel_start, sel_end))
    origin = sel_end.get_offset();
  auto matches = [&](int index) {
    if (index < 0 || index + width > count)
      return false;
    const Glib::ustring slice = doc.substr(static_cast<Glib::ustring::size_type>(index),
                                           static_cast<Glib::ustring::size_type>(width));
    if (match_case)
      return slice == needle;
    return slice.casefold() == folded;
  };
  int found = -1;
  for (int i = origin; i + width <= count; ++i) {
    if (matches(i)) {
      found = i;
      break;
    }
  }
  if (found < 0) {
    const int limit = std::min(origin, count);
    for (int i = 0; i < limit; ++i) {
      if (matches(i)) {
        found = i;
        break;
      }
    }
  }
  if (found < 0) {
    message_.set_text("No matches.");
    return;
  }
  auto start = buffer_->get_iter_at_offset(found);
  auto end = buffer_->get_iter_at_offset(found + width);
  buffer_->select_range(start, end);
  text_.scroll_to(start);
}

void MainWindow::replace_once()
{
  if (!find_entry_ || !replace_entry_ || !buffer_)
    return;
  const Glib::ustring needle = find_entry_->get_text();
  if (needle.empty())
    return;
  const Glib::ustring replacement = replace_entry_->get_text();
  const bool match_case = match_case_ && match_case_->get_active();
  Gtk::TextBuffer::iterator start;
  Gtk::TextBuffer::iterator end;
  if (buffer_->get_selection_bounds(start, end)) {
    const Glib::ustring selected = buffer_->get_text(start, end);
    const bool same = match_case ? selected == needle : selected.casefold() == needle.casefold();
    if (same) {
      const Run format = format_of(start);
      const int heading = heading_of(start);
      const int at = start.get_offset();
      const int len = static_cast<int>(replacement.length());
      buffer_->begin_user_action();
      buffer_->erase(start, end);
      if (len > 0) {
        auto iter = buffer_->get_iter_at_offset(at);
        buffer_->insert(iter, replacement);
        // Inserting beside an existing run inherits that run's tag. Keep the
        // match's own format as the only character tag on the new text.
        auto from = buffer_->get_iter_at_offset(at);
        auto to = buffer_->get_iter_at_offset(at + len);
        strip_fmt(from, to);
        from = buffer_->get_iter_at_offset(at);
        to = buffer_->get_iter_at_offset(at + len);
        buffer_->apply_tag(format_tag(format), from, to);
        if (heading > 0)
          buffer_->apply_tag(heading_tag(heading), from, to);
      }
      buffer_->end_user_action();
    }
  }
  find_next();
}

void MainWindow::on_options()
{
  Gtk::Dialog dialog("Options", *this, true);
  dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("_OK", Gtk::RESPONSE_OK);
  dialog.set_default_response(Gtk::RESPONSE_OK);
  auto* grid = Gtk::manage(new Gtk::Grid());
  grid->set_row_spacing(8);
  grid->set_column_spacing(12);
  grid->set_margin_top(12);
  grid->set_margin_bottom(12);
  grid->set_margin_start(12);
  grid->set_margin_end(12);
  auto* font_label = Gtk::manage(new Gtk::Label("Default font"));
  auto* size_label = Gtk::manage(new Gtk::Label("Default size"));
  auto* recent_label = Gtk::manage(new Gtk::Label("Recent files"));
  auto* units_label = Gtk::manage(new Gtk::Label("Measurement units"));
  font_label->set_halign(Gtk::ALIGN_START);
  size_label->set_halign(Gtk::ALIGN_START);
  recent_label->set_halign(Gtk::ALIGN_START);
  units_label->set_halign(Gtk::ALIGN_START);
  auto* font = Gtk::manage(new Gtk::ComboBoxText());
  auto* size = Gtk::manage(new Gtk::ComboBoxText());
  auto* recent = Gtk::manage(new Gtk::ComboBoxText());
  fill_font_combo(*font, settings_.default_font);
  for (const char* item : {"8", "9", "10", "11", "12", "14", "16", "18", "24", "36"})
    size->append(item);
  size->set_active_text(std::to_string(settings_.default_size));
  recent->append("4");
  recent->append("8");
  recent->append("12");
  recent->set_active_text(std::to_string(settings_.recent_count));
  auto* units = Gtk::manage(new Gtk::ComboBoxText());
  units->append("in", "Inches");
  units->append("cm", "Centimetres");
  units->set_active_id(units_text(settings_.units));
  font->set_size_request(220, -1);
  grid->attach(*font_label, 0, 0, 1, 1);
  grid->attach(*font, 1, 0, 1, 1);
  grid->attach(*size_label, 0, 1, 1, 1);
  grid->attach(*size, 1, 1, 1, 1);
  grid->attach(*recent_label, 0, 2, 1, 1);
  grid->attach(*recent, 1, 2, 1, 1);
  grid->attach(*units_label, 0, 3, 1, 1);
  grid->attach(*units, 1, 3, 1, 1);
  dialog.get_content_area()->pack_start(*grid, Gtk::PACK_SHRINK);
  dialog.show_all_children();
  if (dialog.run() != Gtk::RESPONSE_OK)
    return;
  const std::string chosen_font = font->get_active_text();
  if (!chosen_font.empty())
    settings_.default_font = chosen_font;
  try {
    const int chosen_size = std::stoi(size->get_active_text().raw());
    if (known_size(chosen_size))
      settings_.default_size = chosen_size;
  } catch (const std::exception&) {
  }
  try {
    const int count = std::stoi(recent->get_active_text().raw());
    if (count == 4 || count == 8 || count == 12)
      settings_.recent_count = count;
  } catch (const std::exception&) {
  }
  if (static_cast<int>(settings_.recent.size()) > settings_.recent_count)
    settings_.recent.resize(static_cast<size_t>(settings_.recent_count));
  settings_.units = units_from_text(units->get_active_id().raw());
  settings_.save();
  rebuild_recent();
  if (save_path_.empty() && save_point_ && !dirty()) {
    typing_.font = settings_.default_font.empty() ? "Sans" : settings_.default_font;
    typing_.size = settings_.default_size;
    typing_.bold = false;
    typing_.italic = false;
    typing_.underline = false;
    show_format(typing_);
  }
}

}  // namespace writeit
