// ============================================================================
// file_dialog.hpp — the desktop's "Add game files…": the system's open dialog
// ============================================================================
// SDL2 has no file dialog, so each system's own: GetOpenFileNameW on Windows,
// osascript on macOS, zenity or kdialog elsewhere. The dialog runs on its own
// thread, so the window keeps drawing; the chosen paths are collected with
// takeChosenFiles(). Not on the web, where the page's buttons do this.
#pragma once
#include <string>
#include <vector>

namespace ch::import {

// Opens the dialog (multiple files) unless one is open already.
void openFileDialog();
[[nodiscard]] bool fileDialogOpen();
// The files chosen since the last call; with no dialog to run, a note on why in `error`.
std::vector<std::string> takeChosenFiles(std::string* error = nullptr);

} // namespace ch::import
