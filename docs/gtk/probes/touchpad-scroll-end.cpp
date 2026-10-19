// Does a window that consumes every wxEVT_MOUSEWHEEL still scroll once a
// touchpad gesture ends? wxMaxima does this while a scrollable matrix is
// selected: it scrolls the matrix itself, and the worksheet must stay put.
//
//     touchpad-scroll-end consume   # handler eats every wheel event
//     touchpad-scroll-end skip      # handler skips, so wx scrolls the window
//
// "skip" is the control: it shows the gesture arrived at all, and how far
// the same input moves the window when wx is allowed to scroll it. Drive it
// with "wldrag scroll DY ... scrollstop" under sway, see the README.
#include <wx/wx.h>
#include <stdio.h>

class App : public wxApp
{
public:
    bool OnInit() override
    {
        m_consume = argc > 1 && wxString(argv[1]) == "consume";

        wxFrame* const f = new wxFrame(nullptr, wxID_ANY, "scrollend",
                                       wxPoint(0, 0), wxSize(600, 500));
        m_win = new wxScrolledWindow(f, wxID_ANY);
        m_win->SetScrollRate(10, 10);
        m_win->SetVirtualSize(4000, 40000);
        m_win->Bind(wxEVT_MOUSEWHEEL, [this](wxMouseEvent& e) {
            ++m_wheels;
            m_rotation += e.GetWheelRotation();
            if ( !m_consume )
                e.Skip();
        });
        f->Show();

        m_timer.SetOwner(this);
        m_timer.Start(100);
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
            const int y = m_win->GetViewStart().y;
            if ( m_ticks == 10 )
            {
                const wxRect r = m_win->GetScreenRect();
                printf("ready %d %d\n", r.x + r.width / 2,
                       r.y + r.height / 2);
            }
            if ( m_ticks % 5 == 0 || y != m_lastY )
                printf("t=%d wheels=%d rotation=%d viewstart_y=%d\n",
                       m_ticks, m_wheels, m_rotation, y);
            fflush(stdout);
            m_lastY = y;
            if ( ++m_ticks > 80 )
                ExitMainLoop();
        });
        return true;
    }

private:
    wxScrolledWindow* m_win = nullptr;
    wxTimer m_timer;
    bool m_consume = false;
    int m_wheels = 0;
    int m_rotation = 0;
    int m_ticks = 0;
    int m_lastY = 0;
};

wxIMPLEMENT_APP(App);
