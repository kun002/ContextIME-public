#include "contextime/application_context.h"
#include "contextime/editor_context.h"

#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <string>

namespace {

using contextime::ContextSnapshot;
using contextime::EditorContextStore;
using contextime::EditorContextUpdate;
using contextime::EditorSurface;
using contextime::EditorSyntax;
using contextime::InputMode;
using contextime::OptionalMode;

int failures = 0;
int assertions = 0;

void Expect(bool condition, const char* message) {
  ++assertions;
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

contextime::ApplicationContext VSCode() {
  return contextime::BuildApplicationContext(
      11, 12, L"C:\\Users\\test\\AppData\\Local\\Programs\\Code.exe",
      L"Chrome_WidgetWin_1");
}

EditorContextUpdate Update(EditorSurface surface, EditorSyntax syntax,
                           bool focused = true) {
  EditorContextUpdate update;
  update.window_focused = focused;
  update.surface = surface;
  update.syntax = syntax;
  return update;
}

void ExpectSyntax(EditorSyntax syntax, InputMode expected,
                  const char* fixture) {
  EditorContextStore store;
  store.Publish(Update(EditorSurface::Editor, syntax), 100, 50);
  ContextSnapshot snapshot;
  store.Apply(VSCode(), 149, snapshot);
  Expect(snapshot.syntax_context.has_value,
         (std::string(fixture) + ": syntax rule present").c_str());
  Expect(snapshot.syntax_context.has_value &&
             snapshot.syntax_context.value == expected,
         (std::string(fixture) + ": expected mode").c_str());
}

void TestSyntaxMapping() {
  ExpectSyntax(EditorSyntax::Code, InputMode::English, "code");
  ExpectSyntax(EditorSyntax::Comment, InputMode::Chinese, "comment");
  ExpectSyntax(EditorSyntax::MarkdownText, InputMode::Chinese,
               "markdown text");
  ExpectSyntax(EditorSyntax::MarkdownCode, InputMode::English,
               "markdown code");

  for (const EditorSyntax syntax :
       {EditorSyntax::Unknown, EditorSyntax::String}) {
    EditorContextStore store;
    store.Publish(Update(EditorSurface::Editor, syntax), 100, 50);
    ContextSnapshot snapshot;
    store.Apply(VSCode(), 101, snapshot);
    Expect(!snapshot.syntax_context.has_value,
           "unknown and string syntax keep current mode");
  }
}

void TestSurfaceAndPrecedence() {
  EditorContextStore terminal;
  terminal.Publish(Update(EditorSurface::IntegratedTerminal,
                          EditorSyntax::Comment),
                   10, 10);
  ContextSnapshot terminal_snapshot;
  terminal.Apply(VSCode(), 11, terminal_snapshot);
  Expect(!terminal_snapshot.surface_context.has_value,
         "integrated terminal keeps current mode");
  Expect(!terminal_snapshot.syntax_context.has_value,
         "integrated terminal ignores stale editor syntax");

  ContextSnapshot caller;
  caller.surface_context = OptionalMode::Some(InputMode::Chinese);
  terminal.Apply(VSCode(), 11, caller);
  Expect(caller.surface_context.value == InputMode::Chinese,
         "caller surface rule is preserved");

  EditorContextStore editor;
  editor.Publish(Update(EditorSurface::Editor, EditorSyntax::Code), 10, 10);
  caller.syntax_context = OptionalMode::Some(InputMode::Chinese);
  editor.Apply(VSCode(), 11, caller);
  Expect(caller.syntax_context.value == InputMode::Chinese,
         "caller syntax rule is preserved");
}

void TestForegroundFocusAndTtl() {
  EditorContextStore store;
  store.Publish(Update(EditorSurface::Editor, EditorSyntax::Code), 100, 20);

  ContextSnapshot expired;
  store.Apply(VSCode(), 120, expired);
  Expect(!expired.syntax_context.has_value, "TTL expires at boundary");

  const auto visual_studio = contextime::BuildApplicationContext(
      21, 22, L"C:\\Program Files\\Microsoft Visual Studio\\devenv.exe",
      L"HwndWrapper");
  ContextSnapshot other_editor;
  store.Apply(visual_studio, 110, other_editor);
  Expect(!other_editor.syntax_context.has_value,
         "VS Code adapter never affects another editor");

  store.Publish(Update(EditorSurface::Editor, EditorSyntax::Comment, false),
                200, 20);
  ContextSnapshot unfocused;
  store.Apply(VSCode(), 201, unfocused);
  Expect(!unfocused.syntax_context.has_value,
         "unfocused adapter update keeps mode");

  store.Invalidate();
  ContextSnapshot invalidated;
  store.Apply(VSCode(), 201, invalidated);
  Expect(!invalidated.syntax_context.has_value,
         "invalidated adapter update keeps mode");

  store.Publish(Update(EditorSurface::Editor, EditorSyntax::Code), 300, 0);
  ContextSnapshot zero_ttl;
  store.Apply(VSCode(), 300, zero_ttl);
  Expect(!zero_ttl.syntax_context.has_value, "zero TTL invalidates update");
}

void TestStableNames() {
  Expect(std::string(contextime::ToString(EditorSurface::Editor)) ==
             "EDITOR",
         "editor surface stable name");
  Expect(std::string(contextime::ToString(EditorSyntax::MarkdownText)) ==
             "MARKDOWN_TEXT",
         "markdown syntax stable name");
}

}  // namespace

int main() {
  TestSyntaxMapping();
  TestSurfaceAndPrecedence();
  TestForegroundFocusAndTtl();
  TestStableNames();

  if (failures != 0) {
    std::cerr << failures << " of " << assertions << " assertions failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "Editor Context: " << assertions << " assertions passed\n";
  return EXIT_SUCCESS;
}
