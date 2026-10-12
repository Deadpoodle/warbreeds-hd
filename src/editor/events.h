#pragma once
// In-game map editor, Stage 3: the event (trigger) editor. The game's own
// event editor scene SEvent (scene id 0x16: compiled into the retail game but
// never reachable) is pushed over the editor's mission scene, with a toolbar
// of our own in place of the 1998 editor's toolbar buttons. The same scene
// also hosts other modal editor panels (the Global AI settings, ai_settings.h).
// See re/editor-events.md.
#include <windows.h>

#include <string>
#include <vector>

namespace editor {
namespace events {

// Once, at startup (patches SEvent's vtable).
void Install(void (*log)(const char*, ...));

// Open the event editor over the map being edited. The scene change is queued
// and happens a few frames later. False if it can't open now.
bool Open();
void Close();    // queued, like Esc
bool Active();   // the event editor (or another panel on its scene) is open or opening
bool Showing();  // the event editor itself is the top scene
void Tick();     // every frame
// Window messages while the scene is open: true = handled (the game must not see it).
bool OnMessage(UINT msg, WPARAM wp, LPARAM lp);

// The 1998 editor's event toolbar (its commands 32797-32809).
enum Command {
    kPrevEvent, kNextEvent, kAddEvent, kRemoveEvent, kPersistent,
    kPrevCondition, kNextCondition, kAddCondition, kRemoveCondition,
    kPrevAction, kNextAction, kAddAction, kRemoveAction,
    kClose,
    kCommandCount
};
void Do(Command c);

// The events differ from the map as opened or last saved (polls the editor's
// save count, so a save through the editor resets it).
bool Dirty();

// A modal panel drawn on the event editor's scene instead of the events
// (frame pixels; the panel's area is the game's tan event panel, 0..245 x 0..413).
class Panel {
public:
    virtual ~Panel() {}
    virtual const char* Name() const = 0;
    virtual void Draw() = 0;
    virtual bool Click(int x, int y) = 0;          // true = used
    virtual bool Key(int vk, int mods, int ascii) = 0;  // true = used
};
bool OpenPanel(Panel* p);  // like Open, showing p instead of the events
Panel* CurrentPanel();     // null while showing the events (or closed)

// Drawing helpers for panels (only while the scene draws).
void DrawPanelBackground();                       // the game's tan event panel
void DrawText(const char* s, int x, int y, int style);
bool Hover(int x0, int y0, int x1, int y1);       // the game cursor is inside
void Highlight(int x0, int y0, int x1, int y1);   // a dark box behind the current choice
// Text word-wrapped onto lines of at most `width` characters.
std::vector<std::string> Wrap(const std::string& s, size_t width);

// For the tests (autopilot): the event registry, one line per event:
// "E<i>[ P] C<type>{field ...} ... -> A<type>{delay field ...} ..."
int EventCount();
std::string Describe();
// Frame pixels (centre) of: a toolbar button, a condition (column 1) or action
// (column 2) box, an entry of the type menu shown for column 1 or 2.
bool CommandCentre(Command c, POINT& frame);
bool BoxCentre(int column, int box, POINT& frame);
bool MenuEntryCentre(int column, int type, POINT& frame);
bool ColourEntryCentre(int colour, POINT& frame);  // the player colour menu of a player box
int SelectedEvent();
std::string Hint();  // the toolbar's hint line

}  // namespace events
}  // namespace editor
