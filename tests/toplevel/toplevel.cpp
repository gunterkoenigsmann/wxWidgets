///////////////////////////////////////////////////////////////////////////////
// Name:        tests/toplevel/toplevel.cpp
// Purpose:     Tests for wxTopLevelWindow
// Author:      Kevin Ollivier
// Created:     2008-05-25
// Copyright:   (c) 2009 Kevin Ollivier <kevino@theolliviers.com>
///////////////////////////////////////////////////////////////////////////////

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include "testprec.h"


#ifndef WX_PRECOMP
    #include "wx/app.h"
    #include "wx/dialog.h"
    #include "wx/frame.h"
    #include "wx/menu.h"
    #include "wx/textctrl.h"
    #include "wx/toplevel.h"
#endif // WX_PRECOMP

#if wxUSE_MDI
    #include "wx/mdi.h"

#endif // wxUSE_MDI

#include "testableframe.h"
#include "gtklog.h"
#include "waitfor.h"

#if wxUSE_TASKBARICON && defined(__WXGTK4__)
    #include "wx/taskbar.h"
    #include "wx/gtk/private/wrapgdk.h"
#endif

class DestroyOnScopeExit
{
public:
    explicit DestroyOnScopeExit(wxTopLevelWindow* tlw)
        : m_tlw(tlw)
    {
    }

    ~DestroyOnScopeExit()
    {
        m_tlw->Destroy();
    }

private:
    wxTopLevelWindow* const m_tlw;

    wxDECLARE_NO_COPY_CLASS(DestroyOnScopeExit);
};

static void TopLevelWindowShowTest(wxTopLevelWindow* tlw)
{
    CHECK(!tlw->IsShown());

    wxTextCtrl* textCtrl = new wxTextCtrl(tlw, -1, "test");
    textCtrl->SetFocus();

// only run this test on platforms where ShowWithoutActivating is implemented.
#if defined(__WXMSW__) || defined(__WXMAC__)
    wxTheApp->GetTopWindow()->SetFocus();
    tlw->ShowWithoutActivating();
    CHECK(tlw->IsShown());
    CHECK(!tlw->IsActive());

    tlw->Hide();
    CHECK(!tlw->IsShown());
    CHECK(!tlw->IsActive());
#endif

    // Note that at least under MSW, ShowWithoutActivating() still generates
    // wxActivateEvent, so we must only start counting these events after the
    // end of the tests above.
    EventCounter countActivate(tlw, wxEVT_ACTIVATE);

    tlw->Show(true);
    countActivate.WaitEvent();

    // TLWs never become active when running under Xvfb, presumably because
    // there is no WM there.
    if ( !IsRunningUnderXVFB() )
        CHECK(tlw->IsActive());

    CHECK(tlw->IsShown());

    tlw->Hide();
    CHECK(!tlw->IsShown());

    countActivate.WaitEvent();
    CHECK(!tlw->IsActive());
}

TEST_CASE("wxTopLevel::Show", "[tlw][show]")
{
    SECTION("Dialog")
    {
        wxDialog* dialog = new wxDialog(nullptr, -1, "Dialog Test");
        DestroyOnScopeExit destroy(dialog);

        TopLevelWindowShowTest(dialog);
    }

    SECTION("Frame")
    {
        wxFrame* frame = new wxFrame(nullptr, -1, "Frame test");
        DestroyOnScopeExit destroy(frame);

        TopLevelWindowShowTest(frame);
    }
}

// Check that we receive the expected event when showing the TLW.
TEST_CASE("wxTopLevel::ShowEvent", "[tlw][show][event]")
{
    wxFrame* const frame = new wxFrame(nullptr, wxID_ANY, "Maximized frame");
    DestroyOnScopeExit destroy(frame);

    EventCounter countShow(frame, wxEVT_SHOW);

    frame->Maximize();
    frame->Show();

    CHECK( countShow.WaitEvent() );
}

#if wxUSE_MDI

// An MDI parent frame must stay full screen once it has been asked to, and its
// menu bar must survive the switch in both directions.
//
// Under wxGTK it did not. wxFrame::ShowFullScreen() hides the bar without
// detaching it, which is the normal state for a plain frame, but
// wxMDIParentFrame::OnInternalIdle() then found a hidden parent menu bar with
// no child menu bar visible, took that for the state it has to correct, showed
// the bar again and called Attach() on a bar that was still attached. That
// asserted and undid the full screen switch in one go.
//
// Whether the bar object itself is hidden is a port detail rather than part of
// the contract: wxGTK hides it, while wxMSW detaches the menu from the window
// with SetMenu() and leaves the wxMenuBar shown. Only wxGTK is checked for it.
TEST_CASE("wxMDIParentFrame::ShowFullScreen", "[tlw][mdi][fullscreen]")
{
#ifdef __WXGTK__
    const bool checkBarHidden = true;
#else
    const bool checkBarHidden = false;
#endif

    wxMDIParentFrame* const frame =
        new wxMDIParentFrame(nullptr, wxID_ANY, "MDI full screen test",
                             wxDefaultPosition, wxSize(400, 300));
    DestroyOnScopeExit destroy(frame);

    wxMenu* const menu = new wxMenu;
    menu->Append(wxID_EXIT, "E&xit");
    wxMenuBar* const bar = new wxMenuBar;
    bar->Append(menu, "&File");
    frame->SetMenuBar(bar);

    frame->Show();
    YieldForAWhile();

    REQUIRE( bar->IsShown() );
    REQUIRE( bar->GetFrame() == frame );

    REQUIRE( frame->ShowFullScreen(true) );
    YieldForAWhile();

    CHECK( frame->IsFullScreen() );
    CHECK( bar->GetFrame() == frame );
    if ( checkBarHidden )
        CHECK( !bar->IsShown() );

    // Idle processing must not undo this however long the frame stays up.
    YieldForAWhile();
    CHECK( frame->IsFullScreen() );
    if ( checkBarHidden )
        CHECK( !bar->IsShown() );

    REQUIRE( frame->ShowFullScreen(false) );
    YieldForAWhile();

    CHECK( !frame->IsFullScreen() );
    CHECK( bar->IsShown() );
    CHECK( bar->GetFrame() == frame );
}

#ifdef __WXGTK4__

// Destroying MDI children has to take their pages out of the notebook.
//
// A child frame is an ordinary child of the client window as far as wx is
// concerned, so it goes through DestroyChildren() and never through the
// notebook's own page removal. GTK+ 3 did not mind. GTK4's notebook keeps its
// pages in a GtkStack, and a page whose child was unparented behind its back
// leaves that list stale -- which the notebook's dispose trips over, saying so
// once per page and carrying on.
//
// The check is on GTK's log because that complaint is the only thing said
// about it: wx reports the same either way, and nothing crashes. See #255.
TEST_CASE("wxMDIChildFrame::DestroyRemovesThePage", "[mdi][gtk]")
{
    const unsigned before = wxTestGetGTKLogCount();

    wxMDIParentFrame* const parent =
        new wxMDIParentFrame(nullptr, wxID_ANY, "mdi",
                             wxDefaultPosition, wxSize(400, 300));

    for ( int i = 0; i < 3; ++i )
        new wxMDIChildFrame(parent, wxID_ANY, wxString::Format("Child %d", i));

    parent->Show();
    YieldForAWhile();

    parent->Destroy();

    // Destroy() is deferred, so the teardown this is about happens in here.
    YieldForAWhile();

    CHECK( wxTestGetGTKLogCount() == before );
}

#endif // __WXGTK4__

#endif // wxUSE_MDI

#if wxUSE_TASKBARICON && defined(__WXGTK4__)

// A click on a tray icon may only raise a window under Wayland if the token
// the panel passes over just before Activate is used for it: KWin refuses to
// raise a window presented without one. KDE's panel passes it with
// ProvideXdgActivationToken.
//
// The item is reached through the session bus like a panel reaches it, so
// this needs one, and is skipped without. Whether the token is then given
// to GDK can only be checked under Wayland, where GDK keeps it.
TEST_CASE("wxTaskBarIcon::ActivationToken", "[taskbaricon][gtk]")
{
    GDBusConnection* const conn =
        g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
    if ( !conn )
    {
        WARN("Skipping the test: no D-Bus session bus.");
        return;
    }

    wxTaskBarIcon icon;

    bool activated = false;
    icon.Bind(wxEVT_TASKBAR_LEFT_DCLICK,
              [&activated](wxTaskBarIconEvent&) { activated = true; });

    // Exports the item on the process's session bus connection, which is
    // the one g_bus_get_sync() above returned, too.
    REQUIRE( icon.SetIcon(wxBitmap(16, 16), "test") );

    // Calls a method of the item through the bus, as the panel does, and
    // returns the error the call failed with, or an empty string.
    const auto call = [conn](const char* method, GVariant* args)
    {
        struct Result
        {
            bool done = false;
            wxString error;
        } result;

        g_dbus_connection_call(
            conn, g_dbus_connection_get_unique_name(conn),
            "/StatusNotifierItem", "org.kde.StatusNotifierItem",
            method, args, nullptr, G_DBUS_CALL_FLAGS_NONE, 5000, nullptr,
            [](GObject* source, GAsyncResult* res, gpointer data)
            {
                Result* const r = static_cast<Result*>(data);

                GError* error = nullptr;
                GVariant* const reply = g_dbus_connection_call_finish(
                    G_DBUS_CONNECTION(source), res, &error);
                if ( reply )
                    g_variant_unref(reply);
                else
                {
                    r->error = wxString::FromUTF8(error->message);
                    g_error_free(error);
                }

                r->done = true;
            },
            &result);

        REQUIRE( WaitFor(method, [&result]() { return result.done; }, 5000) );

        return result.error;
    };

    CHECK( call("ProvideXdgActivationToken",
                g_variant_new("(s)", "wx-test-token")) == "" );

    // The control: if this event does not arrive, nothing above reached the
    // item, and the empty error proves nothing.
    CHECK( call("Activate", g_variant_new("(ii)", 0, 0)) == "" );
    CHECK( activated );

#ifdef GDK_WINDOWING_WAYLAND
    GdkDisplay* const display = gdk_display_get_default();
    if ( GDK_IS_WAYLAND_DISPLAY(display) )
    {
        wxGCC_WARNING_SUPPRESS(deprecated-declarations)

        CHECK( wxString::FromUTF8(
                gdk_wayland_display_get_startup_notification_id(display))
               == "wx-test-token" );

        // A made-up token must not be left for the next window shown.
        gdk_wayland_display_set_startup_notification_id(display, nullptr);

        wxGCC_WARNING_RESTORE(deprecated-declarations)
    }
#endif // GDK_WINDOWING_WAYLAND

    g_object_unref(conn);
}

#endif // wxUSE_TASKBARICON && __WXGTK4__
