///////////////////////////////////////////////////////////////////////////////
// Name:        tests/controls/pickertest.cpp
// Purpose:     Tests for various wxPickerBase based classes
// Author:      Steven Lamerton
// Created:     2010-08-07
// Copyright:   (c) 2010 Steven Lamerton
///////////////////////////////////////////////////////////////////////////////

#include "testprec.h"

#if wxUSE_COLOURPICKERCTRL || \
    wxUSE_DIRPICKERCTRL    || \
    wxUSE_FILEPICKERCTRL   || \
    wxUSE_FONTPICKERCTRL


#ifndef WX_PRECOMP
    #include "wx/app.h"
#endif // WX_PRECOMP

#include "wx/clrpicker.h"
#include "wx/filepicker.h"
#include "wx/fontpicker.h"
#include "pickerbasetest.h"
#include "asserthelper.h"
#include "testableframe.h"

#include <memory>

#if wxUSE_COLOURPICKERCTRL

class ColourPickerCtrlTestCase : public PickerBaseTestCase
{
public:
    ColourPickerCtrlTestCase();

protected:
    virtual wxPickerBase *GetBase() const override { return m_colour.get(); }

    std::unique_ptr<wxColourPickerCtrl> m_colour;

    wxDECLARE_NO_COPY_CLASS(ColourPickerCtrlTestCase);
};

wxPICKER_BASE_TESTS(ColourPickerCtrlTestCase, "ColourPickerCtrl",
                    "[colourpicker][picker]");

ColourPickerCtrlTestCase::ColourPickerCtrlTestCase()
{
    m_colour = make_unique<wxColourPickerCtrl>(wxTheApp->GetTopWindow(),
                                               wxID_ANY, *wxBLACK,
                                               wxDefaultPosition,
                                               wxDefaultSize,
                                               wxCLRP_USE_TEXTCTRL);
}


TEST_CASE_METHOD(ColourPickerCtrlTestCase,
                 "ColourPickerCtrl::ColourRoundTrip", "[colourpicker]")
{
    const wxColour colour(0x42, 0x69, 0xAB);

    REQUIRE( m_colour->GetColour() != colour );

    m_colour->SetColour(colour);

    CHECK( m_colour->GetColour() == colour );
}

// Setting the colour from code is not the user picking one, so it must not
// raise the event. This is not a hypothetical: under GTK4 the native button
// reports its colour through a property notify, which -- unlike the
// "color-set" signal it replaced -- fires for our own writes as well, so the
// notification has to be suppressed while we write.
TEST_CASE_METHOD(ColourPickerCtrlTestCase,
                 "ColourPickerCtrl::NoEventOnSetColour", "[colourpicker]")
{
    EventCounter changed(m_colour.get(), wxEVT_COLOURPICKER_CHANGED);

    m_colour->SetColour(wxColour(0x12, 0x34, 0x56));
    wxYield();

    CHECK( changed.GetCount() == 0 );
}

#endif //wxUSE_COLOURPICKERCTRL

#if wxUSE_DIRPICKERCTRL

class DirPickerCtrlTestCase : public PickerBaseTestCase
{
public:
    DirPickerCtrlTestCase();

protected:
    virtual wxPickerBase *GetBase() const override { return m_dir.get(); }

    std::unique_ptr<wxDirPickerCtrl> m_dir;

    wxDECLARE_NO_COPY_CLASS(DirPickerCtrlTestCase);
};

wxPICKER_BASE_TESTS(DirPickerCtrlTestCase, "DirPickerCtrl",
                    "[dirpicker][picker]");

DirPickerCtrlTestCase::DirPickerCtrlTestCase()
{
    m_dir = make_unique<wxDirPickerCtrl>(wxTheApp->GetTopWindow(), wxID_ANY,
                                         wxEmptyString, wxDirSelectorPromptStr,
                                         wxDefaultPosition, wxDefaultSize,
                                         wxDIRP_USE_TEXTCTRL);
}


#endif //wxUSE_DIRPICKERCTRL

#if wxUSE_FILEPICKERCTRL

class FilePickerCtrlTestCase : public PickerBaseTestCase
{
public:
    FilePickerCtrlTestCase();

protected:
    virtual wxPickerBase *GetBase() const override { return m_file.get(); }

    std::unique_ptr<wxFilePickerCtrl> m_file;

    wxDECLARE_NO_COPY_CLASS(FilePickerCtrlTestCase);
};

wxPICKER_BASE_TESTS(FilePickerCtrlTestCase, "FilePickerCtrl",
                    "[filepicker][picker]");

FilePickerCtrlTestCase::FilePickerCtrlTestCase()
{
    m_file = make_unique<wxFilePickerCtrl>(wxTheApp->GetTopWindow(), wxID_ANY,
                                           wxEmptyString,
                                           wxFileSelectorPromptStr,
                                           wxFileSelectorDefaultWildcardStr,
                                           wxDefaultPosition, wxDefaultSize,
                                           wxFLP_USE_TEXTCTRL);
}


#endif //wxUSE_FILEPICKERCTRL

#if wxUSE_FONTPICKERCTRL

class FontPickerCtrlTestCase : public PickerBaseTestCase
{
public:
    FontPickerCtrlTestCase();

protected:
    virtual wxPickerBase *GetBase() const override { return m_font.get(); }

    std::unique_ptr<wxFontPickerCtrl> m_font;

    wxDECLARE_NO_COPY_CLASS(FontPickerCtrlTestCase);
};

wxPICKER_BASE_TESTS(FontPickerCtrlTestCase, "FontPickerCtrl",
                    "[fontpicker][picker]");

FontPickerCtrlTestCase::FontPickerCtrlTestCase()
{
    m_font = make_unique<wxFontPickerCtrl>(wxTheApp->GetTopWindow(), wxID_ANY,
                                           wxNullFont,
                                           wxDefaultPosition, wxDefaultSize,
                                           wxFNTP_USE_TEXTCTRL);
}


TEST_CASE_METHOD(FontPickerCtrlTestCase, "FontPickerCtrl::ColourSelection",
                 "[fontpicker]")
{
    wxColour selectedColour(0xFF4269UL);

    CHECK( m_font->GetSelectedColour() != selectedColour );

    m_font->SetSelectedColour(selectedColour);

    INFO("Font picker did not react to color selection");
    CHECK(selectedColour == m_font->GetSelectedColour());
}

TEST_CASE_METHOD(FontPickerCtrlTestCase,
                 "FontPickerCtrl::FontRoundTrip", "[fontpicker]")
{
    // A face the picker cannot possibly be showing already, in a size that is
    // not the default either, so that neither half can pass by accident.
    wxFont font(*wxNORMAL_FONT);
    font.SetPointSize(font.GetPointSize() + 3);
    font.SetWeight(wxFONTWEIGHT_BOLD);
    font.SetStyle(wxFONTSTYLE_ITALIC);

    REQUIRE( m_font->GetSelectedFont() != font );

    m_font->SetSelectedFont(font);

    const wxFont& got = m_font->GetSelectedFont();
    CHECK( got.GetPointSize() == font.GetPointSize() );
    CHECK( got.GetWeight() == font.GetWeight() );
    CHECK( got.GetStyle() == font.GetStyle() );
}

// See the comment on ColourPickerCtrlTestCase::NoEventOnSetColour(): the same
// property-notify trap applies to the GTK4 font button.
TEST_CASE_METHOD(FontPickerCtrlTestCase,
                 "FontPickerCtrl::NoEventOnSetFont", "[fontpicker]")
{
    EventCounter changed(m_font.get(), wxEVT_FONTPICKER_CHANGED);

    wxFont font(*wxNORMAL_FONT);
    font.SetPointSize(font.GetPointSize() + 5);
    m_font->SetSelectedFont(font);
    wxYield();

    CHECK( changed.GetCount() == 0 );
}
#endif //wxUSE_FONTPICKERCTRL

#endif
