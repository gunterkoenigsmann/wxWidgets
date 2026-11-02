///////////////////////////////////////////////////////////////////////////////
// Name:        tests/misc/misctests.cpp
// Purpose:     test miscellaneous GUI functions
// Author:      Vadim Zeitlin
// Created:     2008-09-22
// Copyright:   (c) 2008 Vadim Zeitlin
///////////////////////////////////////////////////////////////////////////////

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include "testprec.h"


#include "wx/defs.h"

#ifndef WX_PRECOMP
    #include "wx/gdicmn.h"
    #include "wx/filefn.h"
#endif // !PCH

#include "wx/app.h"
#include "wx/button.h"
#include "wx/clipbrd.h"
#include "wx/dataobj.h"
#include "wx/panel.h"

#if defined(__WXMSW__) && wxUSE_PRINTING_ARCHITECTURE
    #include "wx/msw/wrapwin.h"
    #include "wx/msw/private.h"
    #include "wx/msw/printdlg.h"
#endif

#include "asserthelper.h"
#include "waitfor.h"

#include <memory>

// gcc 16.x gives many bogus warnings about array subscripts being out of
// bounds in basic_string.h, so disable them for this particular version as it
// looks like a compiler bug.
#if wxCHECK_GCC_VERSION(16, 0) && !wxCHECK_GCC_VERSION(16, 3)
wxGCC_WARNING_SUPPRESS(array-bounds)
#endif

// ----------------------------------------------------------------------------
// the tests
// ----------------------------------------------------------------------------

TEST_CASE("GUI::DisplaySize", "[guifuncs]")
{
    // test that different (almost) overloads return the same results
    int w, h;
    wxDisplaySize(&w, &h);
    wxSize sz = wxGetDisplaySize();

    CHECK( sz.x == w );
    CHECK( sz.y == h );

    // test that passing nullptr works as expected, e.g. doesn't crash
    wxDisplaySize(nullptr, nullptr);
    wxDisplaySize(&w, nullptr);
    wxDisplaySize(nullptr, &h);

    CHECK( sz.x == w );
    CHECK( sz.y == h );

    // test that display PPI is something reasonable
    sz = wxGetDisplayPPI();
    CHECK( sz.x < 1000 );
    CHECK( sz.y < 1000 );
}

#if wxUSE_DATAOBJ
TEST_CASE("GUI::TextDataObject", "[guifuncs][clipboard]")
{
    const wxString text("Hello clipboard!");

    wxTextDataObject* const dobj = new wxTextDataObject(text);
    CHECK( dobj->GetText() == text );

    wxClipboardLocker lockClip;
    CHECK( wxTheClipboard->SetData(dobj) );
    wxTheClipboard->Flush();

    wxTextDataObject dobj2;
    REQUIRE( wxTheClipboard->GetData(dobj2) );
    CHECK( dobj2.GetText() == text );
}

#ifdef __WXGTK4__

#include "wx/stopwatch.h"
#include "wx/gtk/private/wrapgtk.h"

// A clipboard owner under the test's control: it either answers a read with
// "hello" or never answers at all, as a hung application would. It also notes
// whether the application's windows could be used while the read was waiting.
namespace
{

bool gs_providerStalls = false;
GTask* gs_stalledWrite = nullptr;
int gs_writesStarted = 0;
bool gs_topEnabledDuringRead = true;

} // anonymous namespace

extern "C" {

typedef struct { GdkContentProvider parent; } WxTestProvider;
typedef struct { GdkContentProviderClass parent_class; } WxTestProviderClass;

}

G_DEFINE_TYPE(WxTestProvider, wx_test_provider, GDK_TYPE_CONTENT_PROVIDER)

static GdkContentFormats* wx_test_provider_ref_formats(GdkContentProvider*)
{
    static const char* mimes[] = { "text/plain;charset=utf-8" };
    return gdk_content_formats_new(mimes, 1);
}

static void
wx_test_provider_write_async(GdkContentProvider* provider,
                             const char*,
                             GOutputStream* stream,
                             int,
                             GCancellable* cancellable,
                             GAsyncReadyCallback callback,
                             gpointer data)
{
    gs_writesStarted++;
    gs_topEnabledDuringRead = wxTheApp->GetTopWindow()->IsEnabled();

    GTask* const task = g_task_new(provider, cancellable, callback, data);

    if ( gs_providerStalls )
    {
        gs_stalledWrite = task;
        return;
    }

    // Asynchronously, as the reader runs on this same thread, and closing
    // the stream at the end, as the reader waits for end of stream.
    GInputStream* const source =
        g_memory_input_stream_new_from_data("hello", 5, nullptr);
    g_output_stream_splice_async(
        stream, source, G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET,
        G_PRIORITY_DEFAULT, cancellable,
        [](GObject* out, GAsyncResult* res, gpointer data)
        {
            GTask* const task = static_cast<GTask*>(data);
            g_task_return_boolean(task,
                g_output_stream_splice_finish(G_OUTPUT_STREAM(out), res,
                                              nullptr) == 5);
            g_object_unref(task);
        },
        task);
    g_object_unref(source);
}

static gboolean
wx_test_provider_write_finish(GdkContentProvider*,
                              GAsyncResult* result,
                              GError** error)
{
    return g_task_propagate_boolean(G_TASK(result), error);
}

static void wx_test_provider_class_init(WxTestProviderClass* klass)
{
    GdkContentProviderClass* const c = GDK_CONTENT_PROVIDER_CLASS(klass);
    c->ref_formats = wx_test_provider_ref_formats;
    c->write_mime_type_async = wx_test_provider_write_async;
    c->write_mime_type_finish = wx_test_provider_write_finish;
}

static void wx_test_provider_init(WxTestProvider*)
{
}

// Another application owning the clipboard may be slow or hung, and while
// GetData() waits for it nothing else can run normally. It must give up after
// a while, and the user must not be able to act on the application meanwhile.
TEST_CASE("wxClipboard::ReadFromHungOwner", "[guifuncs][clipboard][gtk]")
{
    GdkClipboard* const clipboard =
        gdk_display_get_clipboard(gdk_display_get_default());

    const auto offer = [clipboard](bool stalls)
    {
        gs_providerStalls = stalls;
        gs_writesStarted = 0;
        gs_topEnabledDuringRead = true;

        GdkContentProvider* const provider = GDK_CONTENT_PROVIDER(
            g_object_new(wx_test_provider_get_type(), nullptr));
        REQUIRE( gdk_clipboard_set_content(clipboard, provider) );
        g_object_unref(provider);
    };

    wxClipboardLocker lockClip;

    // The control: an owner that answers is read, so the provider really is
    // what GetData() reads from below.
    offer(false);
    {
        wxTextDataObject text;
        CHECK( wxTheClipboard->GetData(text) );
        CHECK( text.GetText() == "hello" );
        CHECK( gs_writesStarted == 1 );
        CHECK_FALSE( gs_topEnabledDuringRead );
    }

    offer(true);
    {
        wxTextDataObject text;
        wxStopWatch sw;
        CHECK_FALSE( wxTheClipboard->GetData(text) );
        const long elapsed = sw.Time();

        CHECK( gs_writesStarted == 1 );
        CHECK_FALSE( gs_topEnabledDuringRead );
        CHECK( elapsed >= 4000 );
        CHECK( elapsed < 15000 );
    }

    CHECK( wxTheApp->GetTopWindow()->IsEnabled() );

    if ( gs_stalledWrite )
    {
        g_task_return_new_error(gs_stalledWrite, G_IO_ERROR,
                                G_IO_ERROR_CANCELLED, "test is over");
        g_object_unref(gs_stalledWrite);
        gs_stalledWrite = nullptr;
    }

    gdk_clipboard_set_content(clipboard, nullptr);
    YieldForAWhile();
}

#endif // __WXGTK4__

TEST_CASE("GUI::URLDataObject", "[guifuncs][clipboard]")
{
    // this tests for buffer overflow, see #11102
    const char * const
        url = "http://something.long.to.overwrite.plenty.memory.example.com";
    wxURLDataObject * const dobj = new wxURLDataObject(url);
    CHECK( dobj->GetURL() == url );

    wxClipboardLocker lockClip;
    CHECK( wxTheClipboard->SetData(dobj) );
    wxTheClipboard->Flush();

    wxURLDataObject dobj2;
    REQUIRE( wxTheClipboard->GetData(dobj2) );
    CHECK( dobj2.GetURL() == url );
}

TEST_CASE("GUI::HTMLDataObject", "[guifuncs][clipboard]")
{
    const wxString text("<h1>Hello clipboard!</h1>");

    wxHTMLDataObject* const dobj = new wxHTMLDataObject(text);
    CHECK( dobj->GetHTML() == text );

    wxClipboardLocker lockClip;
    CHECK( wxTheClipboard->SetData(dobj) );
    wxTheClipboard->Flush();

    wxHTMLDataObject dobj2;
    REQUIRE( wxTheClipboard->GetData(dobj2) );
    CHECK( dobj2.GetHTML() == text );
}

// This disabled by default test allows to check that we retrieve HTML data
// from the system clipboard correctly.
TEST_CASE("GUI::ShowHTML", "[.]")
{
    wxClipboardLocker lockClip;

    wxHTMLDataObject dobj;
    REQUIRE( wxTheClipboard->GetData(dobj) );

    WARN("Clipboard contents:\n---start---\n" << dobj.GetHTML() << "\n---end--");
}

TEST_CASE("GUI::DataFormatCompare", "[guifuncs][dataformat]")
{
    const wxDataFormat df(wxDF_TEXT);
    CHECK( df == wxDF_TEXT );
    CHECK( df != wxDF_INVALID );
}
#endif // wxUSE_DATAOBJ

TEST_CASE("GUI::ParseFileDialogFilter", "[guifuncs]")
{
    wxArrayString descs,
                  filters;

    REQUIRE
    (
        wxParseCommonDialogsFilter("Image files|*.jpg;*.png", descs, filters)
        == 1
    );

    CHECK( descs[0] == "Image files" );
    CHECK( filters[0] == "*.jpg;*.png" );

    REQUIRE
    (
        wxParseCommonDialogsFilter
        (
            "All files (*.*)|*.*|Python source (*.py)|*.py",
            descs, filters
        )
        == 2
    );

    CHECK( filters[0] == "*.*" );
    CHECK( filters[1] == "*.py" );

    // Test some invalid ones too.
    WX_ASSERT_FAILS_WITH_ASSERT
    (
        wxParseCommonDialogsFilter
        (
            "All files (*.*)|*.*|Python source (*.py)|*.py|",
            descs, filters
        )
    );
}

#if defined(__WXMSW__) && wxUSE_PRINTING_ARCHITECTURE

TEST_CASE("wxPrintData::MSWDuplexDefault", "[print][msw]")
{
    wxWindowsPrintNativeData nativeData;
    GlobalPtr hDevMode(sizeof(DEVMODE), GMEM_FIXED | GMEM_ZEROINIT);
    REQUIRE( static_cast<HGLOBAL>(hDevMode) );

    DEVMODE * const devMode =
        static_cast<DEVMODE *>(static_cast<HGLOBAL>(hDevMode));
    devMode->dmSize = sizeof(DEVMODE);
    devMode->dmFields = DM_DUPLEX;
    devMode->dmDuplex = DMDUP_VERTICAL;

    // wxWindowsPrintNativeData takes ownership and frees the handle.
    nativeData.SetDevMode(hDevMode.Release());

    SECTION("Default")
    {
        wxPrintData data;

        REQUIRE( nativeData.TransferFrom(data) );

        CHECK( (devMode->dmFields & DM_DUPLEX) != 0 );
        CHECK( devMode->dmDuplex == DMDUP_VERTICAL );
    }

    SECTION("ExplicitSimplex")
    {
        wxPrintData data;
        data.SetDuplex(wxDUPLEX_SIMPLEX);

        REQUIRE( nativeData.TransferFrom(data) );

        CHECK( (devMode->dmFields & DM_DUPLEX) != 0 );
        CHECK( devMode->dmDuplex == DMDUP_SIMPLEX );
    }
}

#endif // defined(__WXMSW__) && wxUSE_PRINTING_ARCHITECTURE

TEST_CASE("GUI::ClientToScreen", "[guifuncs]")
{
    wxWindow* const tlw = wxTheApp->GetTopWindow();
    REQUIRE( tlw );

    SECTION("Right to left layout [RTL]") { }
    {
        tlw->SetLayoutDirection(wxLayout_RightToLeft);
    }

    SECTION("Left to right layout [LTR]")
    {
        tlw->SetLayoutDirection(wxLayout_LeftToRight);
    }

    tlw->Refresh();
    tlw->Update();
    wxYield();

    auto const p1 = make_unique<wxPanel>(tlw, wxID_ANY, wxPoint(0, 0),
                                         wxSize(100, 50));
    auto const p2 = make_unique<wxPanel>(tlw, wxID_ANY, wxPoint(0, 50),
                                         wxSize(100, 50));
    wxWindow* const
        b = new wxWindow(p2.get(), wxID_ANY, wxPoint(10, 10), wxSize(30, 10));

    // We need this to realize the windows created above under wxGTK.
    YieldForAWhile();

    const wxPoint tlwOrig = tlw->ClientToScreen(wxPoint(0, 0));
    const int xx = tlw->GetLayoutDirection() == wxLayout_RightToLeft ? -10 : 10;

    wxPoint c2sCoords = p2->ClientToScreen(wxPoint(0, 0));
    wxPoint c2sCoordsExpected = tlwOrig + wxPoint(0, 50);

    CHECK( c2sCoords == c2sCoordsExpected );

    c2sCoords = b->ClientToScreen(wxPoint(0, 0));
    c2sCoordsExpected = tlwOrig + wxPoint(xx, 60);

    CHECK( c2sCoords == c2sCoordsExpected );

    // Ensure that "ScreenToClient(ClientToScreen(coords)) == coords" is also true.
    c2sCoords = b->ScreenToClient(c2sCoords);
    c2sCoordsExpected = wxPoint(0, 0);
    CHECK( c2sCoords == c2sCoordsExpected );
}

namespace
{

// This class is used as a test window here. We can't use a real wxButton
// because we can't create other windows as its children in wxGTK.
class TestButton : public wxWindow
{
public:
    TestButton(wxWindow* parent, const wxString& label, const wxPoint& pos)
        : wxWindow(parent, wxID_ANY, pos, wxSize(100, 50))
    {
        SetLabel(label);
    }
};

// Helper function returning the label of the window at the given point or
// "NONE" if there is no window there.
wxString GetLabelOfWindowAtPoint(wxWindow* parent, int x, int y)
{
    wxWindow* const
        win = wxFindWindowAtPoint(parent->ClientToScreen(wxPoint(x, y)));
    return win ? win->GetLabel() : wxString("NONE");
}

} // anonymous namespace

TEST_CASE("GUI::FindWindowAtPoint", "[guifuncs]")
{
    wxWindow* const parent = wxTheApp->GetTopWindow();
    REQUIRE( parent );

    // Set a label to allow distinguishing it from the other windows in the
    // assertion messages.
    parent->SetLabel("parent");

    auto btn1 = make_unique<TestButton>(parent, "1", wxPoint(10, 10));
    auto btn2 = make_unique<TestButton>(parent, "2", wxPoint(10, 90));

    // No need to use std::unique_ptr<> for this one, it will be deleted by btn2.
    wxWindow* btn3 = new TestButton(btn2.get(), "3", wxPoint(20, 20));

    // We need this to realize the windows created above under wxGTK.
    wxYield();

    INFO("No window for a point outside of the window");
    CHECK( GetLabelOfWindowAtPoint(parent, 900, 900) == "NONE" );

    INFO( "Point over a child control corresponds to it" );
    CHECK( GetLabelOfWindowAtPoint(parent, 11, 11) == btn1->GetLabel() );

    INFO("Point outside of any child control returns the TLW itself");
    CHECK( GetLabelOfWindowAtPoint(parent, 5, 5) == parent->GetLabel() );

    btn2->Disable();
    INFO("Point over a disabled child control still corresponds to it");
    CHECK( GetLabelOfWindowAtPoint(parent, 11, 91) == btn2->GetLabel() );

    btn2->Hide();
    INFO("Point over a hidden child control doesn't take it into account");
    CHECK( GetLabelOfWindowAtPoint(parent, 11, 91) == parent->GetLabel() );

    btn2->Show();
    INFO("Point over child control corresponds to the child");
    CHECK( GetLabelOfWindowAtPoint(parent, 31, 111) == btn3->GetLabel() );

    btn3->Disable();
    INFO("Point over disabled child controls still corresponds to this child");
    CHECK( GetLabelOfWindowAtPoint(parent, 31, 111) == btn3->GetLabel() );
}

TEST_CASE("wxWindow::Dump", "[window]")
{
    CHECK_NOTHROW( wxDumpWindow(nullptr) );

    auto button = make_unique<wxButton>(wxTheApp->GetTopWindow(), wxID_ANY,
                                        "bloordyblop");

    const std::string s = wxDumpWindow(button.get()).utf8_string();

    CHECK_THAT( s, Catch::Contains("wxButton") );
    CHECK_THAT( s, Catch::Contains("bloordyblop") );
}
