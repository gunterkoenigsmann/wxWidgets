// Does dragging a docked wxAUI pane by its caption move it to another dock?
//
// The assertion is the pane's own state, not pixels: a pane that starts docked
// left must end up somewhere else after being dragged there. That is what
// "docking works" means, and it is checkable without looking at the screen.
//
// Prints AIM so a driver knows where the caption is, then reports the outcome.
#include <wx/wx.h>
#include <wx/aui/aui.h>
#include <stdio.h>

static const char* DirName(int d)
{
    switch ( d )
    {
        case wxAUI_DOCK_LEFT:   return "LEFT";
        case wxAUI_DOCK_RIGHT:  return "RIGHT";
        case wxAUI_DOCK_TOP:    return "TOP";
        case wxAUI_DOCK_BOTTOM: return "BOTTOM";
        case wxAUI_DOCK_CENTER: return "CENTER";
        default:                return "NONE";
    }
}

class Frame : public wxFrame
{
public:
    Frame()
        : wxFrame(nullptr, wxID_ANY, "auidock",
                  wxPoint(0, 0), wxSize(900, 700))
    {
        m_mgr.SetManagedWindow(this);

        wxPanel* centre = new wxPanel(this);
        centre->SetBackgroundColour(*wxWHITE);
        m_mgr.AddPane(centre, wxAuiPaneInfo().CenterPane().Name("centre"));

        m_left = new wxPanel(this, wxID_ANY, wxDefaultPosition,
                             wxSize(200, 300));
        m_left->SetBackgroundColour(wxColour(200, 220, 255));
        m_mgr.AddPane(m_left, wxAuiPaneInfo().Left().Caption("Tree Pane")
                          .Name("tree").Floatable(true).BestSize(200, 300));
        m_mgr.Update();
    }
    ~Frame() { m_mgr.UnInit(); }

    void ReportGeometry()
    {
        const wxRect r = m_left->GetRect();
        const wxPoint cap = ClientToScreen(wxPoint(r.x, r.y - 18));
        fprintf(stderr, "AIM %d %d\n", cap.x + r.width / 2, cap.y + 9);

        // Where the driver may drop: the frame's client area on screen, so a
        // drop can be aimed at a dock zone rather than off the edge.
        const wxSize cs = GetClientSize();
        const wxPoint origin = ClientToScreen(wxPoint(0, 0));
        fprintf(stderr, "CLIENT %d %d %d %d\n",
                origin.x, origin.y, cs.x, cs.y);
        fflush(stderr);
    }

    void ReportState(const char* when)
    {
        wxAuiPaneInfo& p = m_mgr.GetPane("tree");
        fprintf(stderr, "STATE %-8s floating=%d docked=%d direction=%s\n",
                when, p.IsFloating() ? 1 : 0, p.IsDocked() ? 1 : 0,
                DirName(p.dock_direction));
        fflush(stderr);
    }

    int Direction() { return m_mgr.GetPane("tree").dock_direction; }
    bool Floating() { return m_mgr.GetPane("tree").IsFloating(); }
private:
    wxAuiManager m_mgr;
    wxPanel* m_left = nullptr;
};

class App : public wxApp
{
public:
    bool OnInit() override
    {
        if ( !wxApp::OnInit() ) return false;
        m_f = new Frame();
        m_f->Show();
        m_f->ReportGeometry();
        m_f->ReportState("start");
        m_startDir = m_f->Direction();

        m_t.SetOwner(this); m_t.Start(500);
        Bind(wxEVT_TIMER, [this](wxTimerEvent&){
            // Docking is a round trip: the pane has to leave its dock while
            // being dragged, and be docked again once it is dropped. Checking
            // only the end state cannot tell a successful round trip from a
            // drag that never started.
            if ( m_f->Floating() )
                m_everFloated = true;

            if ( ++m_n < 40 )
                return;

            m_f->ReportState("end");
            const bool docked = !m_f->Floating();
            fprintf(stderr, "CHECK left-its-dock=%d docked-again=%d\n",
                    m_everFloated ? 1 : 0, docked ? 1 : 0);
            fprintf(stderr, "RESULT %s\n",
                    (m_everFloated && docked)
                        ? "PASS docking round trip"
                        : m_everFloated
                            ? "FAIL dropped but never docked"
                            : "FAIL the drag never floated it");
            fflush(stderr);
            ExitMainLoop();
        });
        return true;
    }
private:
    Frame* m_f = nullptr; wxTimer m_t; int m_n = 0; int m_startDir = 0;
    bool m_everFloated = false;
};
wxIMPLEMENT_APP(App);
