/////////////////////////////////////////////////////////////////////////////
// Name:        wx/gtk/minifram.h
// Purpose:     wxMiniFrame class
// Author:      Robert Roebling
// Copyright:   (c) Robert Roebling
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_GTK_MINIFRAME_H_
#define _WX_GTK_MINIFRAME_H_

#include "wx/bitmap.h"
#include "wx/frame.h"

//-----------------------------------------------------------------------------
// wxMiniFrame
//-----------------------------------------------------------------------------

class WXDLLIMPEXP_CORE wxMiniFrame: public wxFrame
{
    wxDECLARE_DYNAMIC_CLASS(wxMiniFrame);

public:
    wxMiniFrame() = default;
    wxMiniFrame(wxWindow *parent,
            wxWindowID id,
            const wxString& title,
            const wxPoint& pos = wxDefaultPosition,
            const wxSize& size = wxDefaultSize,
            long style = wxCAPTION | wxRESIZE_BORDER,
            const wxString& name = wxASCII_STR(wxFrameNameStr))
    {
        Create(parent, id, title, pos, size, style, name);
    }
    ~wxMiniFrame();

    bool Create(wxWindow *parent,
            wxWindowID id,
            const wxString& title,
            const wxPoint& pos = wxDefaultPosition,
            const wxSize& size = wxDefaultSize,
            long style = wxCAPTION | wxRESIZE_BORDER,
            const wxString& name = wxASCII_STR(wxFrameNameStr));

    virtual void SetTitle( const wxString &title ) override;

protected:
    virtual void DoSetSizeHints( int minW, int minH,
                                 int maxW, int maxH,
                                 int incW, int incH ) override;
    virtual void DoGetClientSize(int* width, int* height) const override;

 // implementation
public:
#ifndef __WXGTK4__
    bool m_isDragMove = false;
    wxSize m_dragOffset;
#endif
#ifdef __WXGTK4__
    // A second caption button, for whoever needs one: wxAUI puts a "dock"
    // button here where a pane cannot be docked by dragging it, which is the
    // case under Wayland. See #167.
    //
    // Only the drawing belongs here. What the button means, and what happens
    // when it is pressed, is the business of whatever asked for it -- and the
    // rectangle below is what that code hit-tests against, so the two cannot
    // disagree about where it is.
    void GTKShowExtraCaptionButton(bool show);
    wxRect GTKGetExtraCaptionButtonRect() const;
    bool GTKHasExtraCaptionButton() const;
#endif // __WXGTK4__

    wxBitmap  m_closeButton;
    int m_miniEdge = 0;
    int m_miniTitle = 0;
};

#endif // _WX_GTK_MINIFRAME_H_
