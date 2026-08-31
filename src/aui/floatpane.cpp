///////////////////////////////////////////////////////////////////////////////
// Name:        src/aui/floatpane.cpp
// Purpose:     wxaui: wx advanced user interface - docking window manager
// Author:      Benjamin I. Williams
// Created:     2005-05-17
// Copyright:   (C) Copyright 2005-2006, Kirix Corporation, All Rights Reserved
// Licence:     wxWindows Library Licence, Version 3.1
///////////////////////////////////////////////////////////////////////////////

// ============================================================================
// declarations
// ============================================================================

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include "wx/wxprec.h"


#if wxUSE_AUI

#include "wx/aui/framemanager.h"

#ifdef __WXGTK4__
    #include "wx/gtk/private/wrapgtk.h"
#endif
#include "wx/aui/floatpane.h"
#include "wx/aui/dockart.h"

#ifndef WX_PRECOMP
#endif

#ifdef __WXMSW__
#include "wx/msw/private.h"
#endif

wxIMPLEMENT_CLASS(wxAuiFloatingFrame, wxAuiFloatingFrameBaseClass);

wxAuiFloatingFrame::wxAuiFloatingFrame(wxWindow* parent,
                wxAuiManager* owner_mgr,
                const wxAuiPaneInfo& pane,
                wxWindowID id /*= wxID_ANY*/,
                long style /*=wxRESIZE_BORDER | wxSYSTEM_MENU | wxCAPTION |
                              wxFRAME_NO_TASKBAR | wxFRAME_FLOAT_ON_PARENT |
                              wxCLIP_CHILDREN
                           */)
                : wxAuiFloatingFrameBaseClass(parent, id, wxEmptyString,
                        pane.floating_pos, pane.floating_size,
                        style |
                        (pane.HasCloseButton()?wxCLOSE_BOX:0) |
                        (pane.HasMaximizeButton()?wxMAXIMIZE_BOX:0) |
                        (pane.IsFixed()?0:wxRESIZE_BORDER)
                        )
    , m_ownerMgr(owner_mgr)
{
    m_moving = false;
    m_mgr.SetManagedWindow(this);
    m_mgr.SetArtProvider(owner_mgr->GetArtProvider()->Clone());
    m_solidDrag = true;

    // find out if the system supports solid window drag.
    // on non-msw systems, this is assumed to be the case
#ifdef __WXMSW__
    BOOL b = TRUE;
    SystemParametersInfo(38 /*SPI_GETDRAGFULLWINDOWS*/, 0, &b, 0);
    m_solidDrag = b ? true : false;
#endif

    SetExtraStyle(wxWS_EX_PROCESS_IDLE);

#ifdef __WXGTK4__
    // Only where the frame cannot be dragged the ordinary way: everywhere
    // else the compositor move works and reports itself, and this would
    // replace something that is not broken.
    if ( owner_mgr && !wxAuiManager::CanDragFloatingFrame(
                                     owner_mgr->GetManagedWindow()) )
        GTKAddCaptionDragSource();
#endif // __WXGTK4__
}


#ifdef __WXGTK4__

// Dragging a floating frame's caption normally hands the pointer to the
// compositor, which then moves the window and reports nothing back -- so the
// managed frame is never told where the pointer went and the pane cannot be
// docked again. See #167.
//
// Where that is the case, the caption starts a drag and drop session instead.
// The protocol does report a drag across surfaces, in the coordinates of the
// surface being dropped on, which is exactly what the dock decision needs.
//
// The source is added in the capture phase so that it sees the press before
// wxMiniFrame's own gesture does. When the press is not on the caption it
// declines to produce a payload, no drag starts, and that gesture goes on to
// handle it as before -- so resizing and everything else is untouched.

extern "C" {

// The payload has a type of its own rather than being a string.
//
// A string is offered to the whole desktop: any application that takes text
// accepts it, and dropping a pane on a browser sent it navigating to
// "wxaui-pane:tree" as though it were an address. It is also what GTK builds
// its default drag icon from, which is where the window containing that text
// came from. A private type is understood by this manager's drop target and
// by nothing else, so a pane dropped anywhere else is simply refused.
struct wxAuiPaneDragData
{
    char* name;
};

static gpointer wx_aui_pane_drag_data_copy(gpointer boxed)
{
    wxAuiPaneDragData* const from = static_cast<wxAuiPaneDragData*>(boxed);
    wxAuiPaneDragData* const to = g_new0(wxAuiPaneDragData, 1);
    to->name = g_strdup(from->name);
    return to;
}

static void wx_aui_pane_drag_data_free(gpointer boxed)
{
    wxAuiPaneDragData* const data = static_cast<wxAuiPaneDragData*>(boxed);
    g_free(data->name);
    g_free(data);
}

GType wx_aui_pane_drag_data_get_type(void)
{
    static gsize type = 0;
    if ( g_once_init_enter(&type) )
    {
        const GType t = g_boxed_type_register_static(
                            "wxAuiPaneDragData",
                            wx_aui_pane_drag_data_copy,
                            wx_aui_pane_drag_data_free);
        g_once_init_leave(&type, t);
    }
    return static_cast<GType>(type);
}

// Set when this frame has seen a button press of its own. Undocking creates
// the frame while the button is already down, and without this GTK starts a
// drag from that in-flight press: the pane is then being dragged by wxAUI's
// mouse capture and by a drag session at the same time, and releasing outside
// any window leaves the drag icon stranded on screen with nothing to end it.
static void
wxgtk_aui_caption_pressed(GtkGestureClick* gesture,
                          int WXUNUSED(n_press), double x, double y,
                          gpointer data)
{
    if ( wxGetEnv("WXAUI_DRAGLOG", nullptr) )
    {
        fprintf(stderr, "AUIFLOAT press handler entered\n");
        fflush(stderr);
    }

    wxAuiFloatingFrame* const self = static_cast<wxAuiFloatingFrame*>(data);
    GtkWidget* const widget = self ? self->GetHandle() : nullptr;
    if ( !widget )
        return;

    // Decide on every press, and clear the flags when this one is not on the
    // caption at all: leaving them set from an earlier press makes the next
    // drag do whatever the last one did.
    const bool onCaption = self && y < self->GTKGetCaptionHeight();

    // The dock button first: it sits inside the caption, so a press on it
    // would otherwise start a drag.
    if ( self->GTKGetExtraCaptionButtonRect().Contains(wxRound(x), wxRound(y)) )
    {
        if ( wxGetEnv("WXAUI_DRAGLOG", nullptr) )
        {
            fprintf(stderr, "AUIFLOAT dock button pressed\n");
            fflush(stderr);
        }

        g_object_set_data(G_OBJECT(widget), "wx-caption-press-seen",
                          GINT_TO_POINTER(0));
        g_object_set_data(G_OBJECT(widget), "wx-caption-drag-taken",
                          GINT_TO_POINTER(1));

        self->GTKDockFromButton();
        gtk_gesture_set_state(GTK_GESTURE(gesture),
                              GTK_EVENT_SEQUENCE_CLAIMED);
        return;
    }

    const GdkModifierType state =
        gtk_event_controller_get_current_event_state(
            GTK_EVENT_CONTROLLER(gesture));
    // A plain drag moves the window, which is what a caption drag does for
    // every other window on the desktop and what it did here before docking
    // was possible at all: the pane follows the cursor and stays where it is
    // dropped, inside the managed frame or outside it. Shift docks instead.
    //
    // Moving is much the commoner of the two, which is why it is the one
    // that costs no modifier.
    //
    // WXAUI_CAPTION_DRAG forces one branch or the other, so a driver that
    // cannot hold a modifier down can still reach both. Fork-only, like the
    // logging; see #112.
    wxString forced;
    wxGetEnv("WXAUI_CAPTION_DRAG", &forced);

    bool wantsMove = onCaption && (state & GDK_SHIFT_MASK) == 0;
    if ( onCaption && forced == "dock" )
        wantsMove = false;
    else if ( onCaption && forced == "move" )
        wantsMove = true;

    g_object_set_data(G_OBJECT(widget), "wx-caption-press-seen",
                      GINT_TO_POINTER(onCaption && !wantsMove ? 1 : 0));
    g_object_set_data(G_OBJECT(widget), "wx-caption-drag-taken",
                      GINT_TO_POINTER(onCaption ? 1 : 0));

    if ( wxGetEnv("WXAUI_DRAGLOG", nullptr) )
    {
        fprintf(stderr, "AUIFLOAT press y=%.1f caption=%d shift=%d -> %s\n",
                y, onCaption ? 1 : 0, (state & GDK_SHIFT_MASK) ? 1 : 0,
                !onCaption ? "wxMiniFrame's own handling"
                           : wantsMove ? "compositor move"
                                       : "drag and drop (docking)");
        fflush(stderr);
    }

    if ( !wantsMove )
        return;

    // Perform the move here rather than letting wxMiniFrame do it. Its
    // handler tests its resize border before the caption, so a press near the
    // top edge became a resize -- which is what shift-dragging a pane did.
    GdkSurface* const surface = gtk_native_get_surface(GTK_NATIVE(widget));
    if ( !surface || !GDK_IS_TOPLEVEL(surface) )
        return;

    GdkEventSequence* const seq =
        gtk_gesture_single_get_current_sequence(GTK_GESTURE_SINGLE(gesture));
    GdkEvent* const event = gtk_gesture_get_last_event(GTK_GESTURE(gesture),
                                                       seq);
    if ( !event )
        return;

    gdk_toplevel_begin_move(GDK_TOPLEVEL(surface),
                            gdk_event_get_device(event),
                            gtk_gesture_single_get_current_button(
                                GTK_GESTURE_SINGLE(gesture)),
                            x, y, gdk_event_get_time(event));

    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
}

static GdkContentProvider*
wxgtk_aui_caption_drag_prepare(GtkDragSource* WXUNUSED(source),
                               double WXUNUSED(x), double y, gpointer data)
{
    wxAuiFloatingFrame* const self = static_cast<wxAuiFloatingFrame*>(data);

    GtkWidget* const widget = self->GetHandle();
    if ( !widget || !g_object_get_data(G_OBJECT(widget),
                                       "wx-caption-press-seen") )
        return nullptr;

    if ( wxGetEnv("WXAUI_DRAGLOG", nullptr) )
    {
        fprintf(stderr, "AUIFLOAT prepare y=%.1f captionHeight=%d name=%s\n",
                y, self->GTKGetCaptionHeight(),
                static_cast<const char*>(self->GTKGetPaneName().utf8_str()));
        fflush(stderr);
    }

    if ( y >= self->GTKGetCaptionHeight() )
        return nullptr;

    const wxString name = self->GTKGetPaneName();
    if ( name.empty() )
        return nullptr;

    const wxScopedCharBuffer utf8 = name.utf8_str();

    wxAuiPaneDragData payload;
    payload.name = const_cast<char*>(utf8.data());

    GValue value = G_VALUE_INIT;
    g_value_init(&value, wx_aui_pane_drag_data_get_type());
    g_value_set_boxed(&value, &payload);

    GdkContentProvider* const provider =
        gdk_content_provider_new_for_value(&value);
    g_value_unset(&value);

    if ( wxGetEnv("WXAUI_DRAGLOG", nullptr) )
    {
        // What the rest of the desktop is being offered. Anything text-like
        // here is something another application can accept: a text drop on
        // the Plasma desktop becomes a sticky note, which is what the stray
        // windows turned out to be.
        GdkContentFormats* const formats =
            gdk_content_provider_ref_formats(provider);
        char* const desc = gdk_content_formats_to_string(formats);
        fprintf(stderr, "AUIFLOAT drag offers: %s\n", desc);
        fflush(stderr);
        g_free(desc);
        gdk_content_formats_unref(formats);
    }

    return provider;
}

} // extern "C"

wxString wxAuiFloatingFrame::GTKGetPaneName() const
{
    if ( !m_ownerMgr || !m_paneWindow )
        return wxString();

    return m_ownerMgr->GetPane(m_paneWindow).name;
}

void wxAuiFloatingFrame::GTKDockFromButton()
{
    if ( !m_ownerMgr || !m_paneWindow )
        return;

    // Not from inside the press: docking destroys this frame, and this is a
    // callback running on one of its widgets.
    wxWindow* const pane = m_paneWindow;
    wxAuiManager* const mgr = m_ownerMgr;

    mgr->CallAfter([mgr, pane]()
        {
            wxAuiPaneInfo& info = mgr->GetPane(pane);
            if ( !info.IsOk() )
                return;

            info.Dock();
            mgr->Update();
        });
}

void wxAuiFloatingFrame::GTKAddCaptionDragSource()
{
    // Dragging cannot dock a pane here, so offer a button that can. See #167.
    GTKShowExtraCaptionButton(true);

    if ( wxGetEnv("WXAUI_DRAGLOG", nullptr) )
    {
        const wxRect r = GTKGetExtraCaptionButtonRect();
        fprintf(stderr, "AUIFLOAT dock button at %d,%d %dx%d (frame %dx%d)\n",
                r.x, r.y, r.width, r.height, GetSize().x, GetSize().y);
        fflush(stderr);
    }

    GtkWidget* const widget = GetHandle();
    if ( wxGetEnv("WXAUI_DRAGLOG", nullptr) )
    {
        fprintf(stderr, "AUIFLOAT drag source %s\n",
                widget ? "attached" : "NOT attached, no widget yet");
        fflush(stderr);
    }
    if ( !widget )
        return;

    GtkGesture* const press = gtk_gesture_click_new();
    g_signal_connect(press, "pressed",
                     G_CALLBACK(wxgtk_aui_caption_pressed), this);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(press),
                                               GTK_PHASE_CAPTURE);
    gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(press));
    wxUnusedVar(press);

    GtkDragSource* const source = gtk_drag_source_new();
    gtk_drag_source_set_actions(source, GDK_ACTION_MOVE);
    g_signal_connect(source, "prepare",
                     G_CALLBACK(wxgtk_aui_caption_drag_prepare), this);

    // Drag the pane's own likeness rather than GTK's default icon for a
    // string, which is a label showing the payload -- that is where the
    // "wxaui-pane:tree" window came from.
    GdkPaintable* const icon = gtk_widget_paintable_new(widget);
    gtk_drag_source_set_icon(source, icon, 0, 0);
    g_object_unref(icon);

    gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(source));
}

#endif // __WXGTK4__

wxAuiFloatingFrame::~wxAuiFloatingFrame()
{
    // if we do not do this, then we can crash...
    if (m_ownerMgr && m_ownerMgr->m_actionWindow == this)
    {
        m_ownerMgr->m_actionWindow = nullptr;
    }

    m_mgr.UnInit();
}

void wxAuiFloatingFrame::SetPaneWindow(const wxAuiPaneInfo& pane)
{
    m_paneWindow = pane.window;
    m_paneWindow->Reparent(this);

    wxAuiPaneInfo contained_pane = pane;
    contained_pane.Dock().Center().Show().
                    CaptionVisible(false).
                    PaneBorder(false);

    // Carry over the minimum size
    wxSize pane_min_size = pane.window->GetMinSize();

    // if the frame window's max size is greater than the min size
    // then set the max size to the min size as well
    wxSize cur_max_size = GetMaxSize();
    if (cur_max_size.IsFullySpecified() &&
          (cur_max_size.x < pane.min_size.x ||
           cur_max_size.y < pane.min_size.y)
       )
    {
        SetMaxSize(pane_min_size);
    }

    SetMinSize(pane.window->GetMinSize());

    m_mgr.AddPane(m_paneWindow, contained_pane);
    m_mgr.Update();

    if (pane.min_size.IsFullySpecified())
    {
        // because SetSizeHints() calls Fit() too (which sets the window
        // size to its minimum allowed), we keep the size before calling
        // SetSizeHints() and reset it afterwards...
        wxSize tmp = GetSize();
        GetSizer()->SetSizeHints(this);
        SetSize(tmp);
    }

    SetTitle(pane.caption);

    // This code is slightly awkward because we need to reset wxRESIZE_BORDER
    // before calling SetClientSize() below as doing it after setting the
    // client size would actually change it, at least under MSW, where the
    // total window size doesn't change and hence, as the borders size changes,
    // the client size does change.
    //
    // So we must call it first but doing it generates a size event and updates
    // pane.floating_size from inside it so we must also record its original
    // value before doing it.
    const bool hasFloatingSize = pane.floating_size != wxDefaultSize ||
                                    pane.floating_client_size != wxDefaultSize;
    if (pane.IsFixed())
    {
        SetWindowStyleFlag(GetWindowStyleFlag() & ~wxRESIZE_BORDER);
    }

    if ( hasFloatingSize )
    {
        // give floating_client_size precedence over floating_size
        if (pane.floating_client_size != wxDefaultSize)
        {
            SetClientSize(pane.floating_client_size);
        }
        else
        {
            SetSize(pane.floating_size);
        }
    }
    else
    {
        wxSize size = pane.best_size;
        if (size == wxDefaultSize)
            size = pane.min_size;
        if (size == wxDefaultSize)
            size = m_paneWindow->GetSize();
        if (m_ownerMgr && pane.HasGripper())
        {
            const int gripperSize = m_ownerMgr->m_art->GetMetricForWindow(wxAUI_DOCKART_GRIPPER_SIZE, m_paneWindow);
            if (pane.HasGripperTop())
                size.y += gripperSize;
            else
                size.x += gripperSize;
        }

        SetClientSize(size);
    }
}

wxAuiManager* wxAuiFloatingFrame::GetOwnerManager() const
{
    return m_ownerMgr;
}

bool wxAuiFloatingFrame::IsTopNavigationDomain(NavigationKind kind) const
{
    switch ( kind )
    {
        case Navigation_Tab:
            break;

        case Navigation_Accel:
            // Floating frames are often used as tool palettes and it's
            // convenient for the accelerators defined in the parent frame to
            // work in them, so don't block their propagation.
            return false;
    }

    return wxAuiFloatingFrameBaseClass::IsTopNavigationDomain(kind);
}

void wxAuiFloatingFrame::OnSize(wxSizeEvent& WXUNUSED(event))
{
    if (m_ownerMgr)
    {
        m_ownerMgr->OnFloatingPaneResized(m_paneWindow, GetRect());
    }
}

void wxAuiFloatingFrame::OnClose(wxCloseEvent& evt)
{
    if (m_ownerMgr)
    {
        m_ownerMgr->OnFloatingPaneClosed(m_paneWindow, evt);
    }
    if (!evt.GetVeto())
    {
        m_mgr.DetachPane(m_paneWindow);
        Destroy();
    }
}

// Diagnostic logging shared with framemanager.cpp, enabled by WXAUI_DRAGLOG.
// Remove with the rest of the fork-only changes; see #112.
static bool wxAuiFloatLogging()
{
    static const bool s_on = wxGetEnv("WXAUI_DRAGLOG", nullptr);
    return s_on;
}

void wxAuiFloatingFrame::OnMoveEvent(wxMoveEvent& event)
{
    // Always sync pane's floating_pos with frame's position
    if (m_ownerMgr)
    {
        m_ownerMgr->GetPane(m_paneWindow).
            floating_pos = GetRect().GetPosition();
    }

    if (!m_solidDrag)
    {
        // systems without solid window dragging need to be
        // handled slightly differently, due to the lack of
        // the constant stream of EVT_MOVING events
        if (!isMouseDown())
            return;
        OnMoveStart();
        OnMoving(event.GetRect(), wxNORTH);
        m_moving = true;
        return;
    }


    wxRect winRect = GetRect();

    if ( wxAuiFloatLogging() )
    {
        fprintf(stderr, "AUIFLOAT move type=%s rect=%d,%d %dx%d "
                        "last=%d,%d %dx%d moving=%d\n",
                event.GetEventType() == wxEVT_MOVING ? "MOVING" : "MOVE",
                winRect.x, winRect.y, winRect.width, winRect.height,
                m_lastRect.x, m_lastRect.y,
                m_lastRect.width, m_lastRect.height, m_moving ? 1 : 0);
        fflush(stderr);
    }

    if (winRect == m_lastRect)
    {
        if ( wxAuiFloatLogging() )
            fprintf(stderr, "AUIFLOAT   bail: rect unchanged\n");
        return;
    }

    // skip the first move event
    if (m_lastRect.IsEmpty())
    {
        if ( wxAuiFloatLogging() )
            fprintf(stderr, "AUIFLOAT   bail: first event\n");
        m_lastRect = winRect;
        return;
    }

    // as on OSX moving windows are not getting all move events, only sporadically, this difference
    // is almost always big on OSX, so avoid this early exit opportunity
#ifndef __WXOSX__
    // skip if moving too fast to avoid massive redraws and
    // jumping hint windows
    // TODO: Should 3x3px threshold increase on Retina displays?
    if ((abs(winRect.x - m_lastRect.x) > 3) ||
        (abs(winRect.y - m_lastRect.y) > 3))
    {
        m_last3Rect = m_last2Rect;
        m_last2Rect = m_lastRect;
        m_lastRect = winRect;

        return;
    }
#endif

    // prevent frame redocking during resize
    if (m_lastRect.GetSize() != winRect.GetSize())
    {
        m_last3Rect = m_last2Rect;
        m_last2Rect = m_lastRect;
        m_lastRect = winRect;
        return;
    }

    wxDirection dir = wxALL;

    int horiz_dist = abs(winRect.x - m_last3Rect.x);
    int vert_dist = abs(winRect.y - m_last3Rect.y);

    if (vert_dist >= horiz_dist)
    {
        if (winRect.y < m_last3Rect.y)
            dir = wxNORTH;
        else
            dir = wxSOUTH;
    }
    else
    {
        if (winRect.x < m_last3Rect.x)
            dir = wxWEST;
        else
            dir = wxEAST;
    }

    m_last3Rect = m_last2Rect;
    m_last2Rect = m_lastRect;
    m_lastRect = winRect;

    if (!isMouseDown())
        return;

    if (!m_moving)
    {
        OnMoveStart();
        m_moving = true;
    }

    if (m_last3Rect.IsEmpty())
    {
        if ( wxAuiFloatLogging() )
            fprintf(stderr, "AUIFLOAT   bail: last3Rect empty\n");
        return;
    }

    if ( event.GetEventType() == wxEVT_MOVING )
        OnMoving(event.GetRect(), dir);
    else
        OnMoving(wxRect(event.GetPosition(),GetSize()), dir);
}

void wxAuiFloatingFrame::OnIdle(wxIdleEvent& event)
{
    if (m_moving)
    {
        if (!isMouseDown())
        {
            if ( wxAuiFloatLogging() )
                fprintf(stderr, "AUIFLOAT idle: button released, finishing\n");
            m_moving = false;
            OnMoveFinished();
        }
        else
        {
            event.RequestMore();
        }
    }
}

void wxAuiFloatingFrame::OnMoveStart()
{
    // notify the owner manager that the pane has started to move
    if (m_ownerMgr)
    {
        m_ownerMgr->OnFloatingPaneMoveStart(m_paneWindow);
    }
}

void wxAuiFloatingFrame::OnMoving(const wxRect& WXUNUSED(window_rect), wxDirection dir)
{
    // notify the owner manager that the pane is moving
    if (m_ownerMgr)
    {
        m_ownerMgr->OnFloatingPaneMoving(m_paneWindow, dir);
    }
    m_lastDirection = dir;
}

void wxAuiFloatingFrame::OnMoveFinished()
{
    // notify the owner manager that the pane has finished moving
    if (m_ownerMgr)
    {
        m_ownerMgr->OnFloatingPaneMoved(m_paneWindow, m_lastDirection);
    }
}

void wxAuiFloatingFrame::OnActivate(wxActivateEvent& event)
{
    if (m_ownerMgr && event.GetActive())
    {
        m_ownerMgr->OnFloatingPaneActivated(m_paneWindow);
    }
}

// utility function which determines the state of the mouse button
// (independent of having a wxMouseEvent handy) - utimately a better
// mechanism for this should be found (possibly by adding the
// functionality to wxWidgets itself)
bool wxAuiFloatingFrame::isMouseDown()
{
    return wxGetMouseState().LeftIsDown();
}


wxBEGIN_EVENT_TABLE(wxAuiFloatingFrame, wxAuiFloatingFrameBaseClass)
    EVT_SIZE(wxAuiFloatingFrame::OnSize)
    EVT_MOVE(wxAuiFloatingFrame::OnMoveEvent)
    EVT_MOVING(wxAuiFloatingFrame::OnMoveEvent)
    EVT_CLOSE(wxAuiFloatingFrame::OnClose)
    EVT_IDLE(wxAuiFloatingFrame::OnIdle)
    EVT_ACTIVATE(wxAuiFloatingFrame::OnActivate)
wxEND_EVENT_TABLE()


#endif // wxUSE_AUI
