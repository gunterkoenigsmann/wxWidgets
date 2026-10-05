///////////////////////////////////////////////////////////////////////////////
// Name:        tests/controls/toolbooktest.cpp
// Purpose:     wxToolbook unit test
// Author:      Steven Lamerton
// Created:     2010-07-02
// Copyright:   (c) 2010 Steven Lamerton
///////////////////////////////////////////////////////////////////////////////

#include "testprec.h"

#if wxUSE_TOOLBOOK


#ifndef WX_PRECOMP
    #include "wx/app.h"
    #include "wx/panel.h"
#endif // WX_PRECOMP

#include "wx/toolbook.h"
#include "wx/toolbar.h"
#include "bookctrlbasetest.h"

#include <memory>

#ifdef __WXGTK4__
    #include "wx/gtk/private/wrapgtk.h"
#endif // __WXGTK4__

class ToolbookTestCase : public BookCtrlBaseTestCase
{
public:
    ToolbookTestCase();

protected:
    virtual wxBookCtrlBase *GetBase() const override
    { return m_toolbook.get(); }

    virtual wxEventType GetChangedEvent() const override
    { return wxEVT_TOOLBOOK_PAGE_CHANGED; }

    virtual wxEventType GetChangingEvent() const override
    { return wxEVT_TOOLBOOK_PAGE_CHANGING; }

    virtual void Realize() override { m_toolbook->GetToolBar()->Realize(); }

    std::unique_ptr<wxToolbook> m_toolbook;

    wxDECLARE_NO_COPY_CLASS(ToolbookTestCase);
};

wxBOOK_CTRL_BASE_TESTS(ToolbookTestCase, "Toolbook",
                       "[toolbook][book]");

ToolbookTestCase::ToolbookTestCase()
{
    m_toolbook = make_unique<wxToolbook>(wxTheApp->GetTopWindow(), wxID_ANY,
                                         wxDefaultPosition, wxSize(400, 200));
    AddPanels();
}


TEST_CASE_METHOD(ToolbookTestCase, "Toolbook::ToolBar", "[toolbook]")
{
    wxToolBar* toolbar = static_cast<wxToolBar*>(m_toolbook->GetToolBar());

    CHECK(toolbar);
    CHECK(toolbar->GetToolsCount() == 3);
}

#ifdef __WXGTK4__
TEST_CASE_METHOD(ToolbookTestCase, "Toolbook::ToolPacking", "[toolbook]")
{
    wxToolBar* const toolbar =
        static_cast<wxToolBar*>(m_toolbook->GetToolBar());
    GtkBox* const box = GTK_BOX(toolbar->GTKGetToolbar());

    CHECK( toolbar->GetToolPacking() == 0 );
    CHECK( gtk_box_get_spacing(box) == 0 );

    toolbar->SetToolPacking(7);
    CHECK( toolbar->GetToolPacking() == 7 );
    CHECK( gtk_box_get_spacing(box) == 7 );

    toolbar->SetToolPacking(0);
    CHECK( toolbar->GetToolPacking() == 0 );
    CHECK( gtk_box_get_spacing(box) == 0 );

    // Invalid negative values must not put the wx value and the native value
    // out of sync. This also keeps the sample's Decrease command at zero.
    toolbar->SetToolPacking(-1);
    CHECK( toolbar->GetToolPacking() == 0 );
    CHECK( gtk_box_get_spacing(box) == 0 );

    wxToolBar toolbarCreatedLater;
    toolbarCreatedLater.SetToolPacking(5);
    CHECK( toolbarCreatedLater.Create(wxTheApp->GetTopWindow(), wxID_ANY) );
    GtkBox* const later =
        GTK_BOX(toolbarCreatedLater.GTKGetToolbar());
    CHECK( gtk_box_get_spacing(later) == 5 );
}
#endif // __WXGTK4__

#endif //wxUSE_TOOLBOOK
