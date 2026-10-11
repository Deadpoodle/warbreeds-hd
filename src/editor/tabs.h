// Map editor: the tools panel's tabs, and the hook for tabs that live in
// their own module (Stage 3: the event editor and the Global AI editor).
//
// The panel (editor.cpp) is a fixed pool of the game's text buttons. Every
// frame each tab lists the items it wants (PanelItem: a row, an x, a text and
// a click target), and the pool shows them; buttons keep one size at every
// zoom and are positioned at draw time (smooth zoom changes the frame size
// between the frame tick and the draw).
//
// Rows: 0 the title; 1-2 the tabs; kPageFirstRow .. kPageFirstRow +
// kPageRows - 1 the tab's own page; then Undo/Redo/Check, Save As/Test
// Map/Exit and two status rows, common to all tabs.
//
// To add a tab: implement TabPage and call RegisterTab(kTabEvents, &page)
// once (e.g. from editor::Install's caller). The tab shows in the tab row from
// then on. A page's items use PanelItem::id (the action is kPageAction);
// clicks come back through OnClick. Undoable changes go through PushCustomEdit.
#pragma once
#include <functional>
#include <string>
#include <vector>

namespace editor {

enum Tab { kTabTerrain, kTabThings, kTabUnits, kTabPlayers, kTabMap, kTabEvents, kTabAi, kTabCount };

constexpr int kPageFirstRow = 3;  // panel row of a page's row 0
constexpr int kPageRows = 15;     // rows a page may use (0 .. kPageRows - 1)
constexpr int kPanelWidth = 250;  // game pixels; x in a PanelItem is 0 .. kPanelWidth - 1

struct PanelItem {
    int row = 0;            // page-relative row (0 .. kPageRows - 1)
    int x = 0;              // panel-relative left, game pixels
    std::string text;
    int id = -1;            // >= 0: clickable; OnClick(id, ...)
    bool lit = false;       // highlighted (the current choice)
    int swatch = -1;        // a palette index: a small colour square left of the text
};

class TabPage {
public:
    virtual ~TabPage() {}
    virtual const char* Label() const = 0;                 // in the tab row, about 8 characters at most
    virtual void Items(std::vector<PanelItem>& out) = 0;   // every frame while the tab is shown
    virtual void OnClick(int id, bool right) = 0;          // left or right click on an item
    // A click on the map while the tab is shown (cell and world pixel); true = handled.
    virtual bool OnMapClick(int cellX, int cellY, int worldX, int worldY, bool right) {
        (void)cellX, (void)cellY, (void)worldX, (void)worldY, (void)right;
        return false;
    }
    virtual void OnShow() {}  // the tab was chosen
    virtual void OnClose() {}  // the editor is closing (the mission scene goes)
};

// A page for kTabEvents or kTabAi (the built-in tabs can't be replaced).
void RegisterTab(Tab tab, TabPage* page);
// One undo step done by a page: `apply(true)` redoes it, `apply(false)` undoes
// it. The change itself must already have been made.
void PushCustomEdit(const std::string& what, std::function<void(bool forward)> apply);
// The panel's status rows (and the log).
void SetPanelStatus(const std::string& text);

}  // namespace editor
