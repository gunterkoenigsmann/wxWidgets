///////////////////////////////////////////////////////////////////////////////
// Name:        wx/gtk/private/dbusmenu.h
// Purpose:     Serve a wxMenu over com.canonical.dbusmenu
// Author:      wxWidgets team
// Copyright:   (c) 2026 wxWidgets team
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_GTK_PRIVATE_DBUSMENU_H_
#define _WX_GTK_PRIVATE_DBUSMENU_H_

#include "wx/defs.h"

#if wxUSE_TASKBARICON && defined(__WXGTK4__)

#include "wx/menu.h"
#include "wx/string.h"
#include "wx/vector.h"
#include "wx/weakref.h"

#include <gio/gio.h>

// A menu as the panel wants it: not a widget handed over, but a tree it can
// ask about over the bus and act on from its own process.
//
// The menu itself stays an ordinary wxMenu owned by the caller. This walks it
// on demand and answers for it, so nothing about how menus are built has to
// change to put one in the tray.
class wxDBusMenu
{
public:
    // What to do when the user picks something. The item is the one that was
    // chosen; the caller decides how to turn that into a wx event.
    class Handler
    {
    public:
        virtual ~Handler() = default;
        virtual void OnMenuItem(wxMenuItem* item) = 0;
    };

    // Exports itself immediately; check IsOk() before using the path.
    wxDBusMenu(GDBusConnection* connection,
               const wxString& path,
               Handler* handler);
    ~wxDBusMenu();

    bool IsOk() const { return m_objectId != 0; }

    const wxString& GetPath() const { return m_path; }

    // The menu to serve, which the caller goes on owning. It may change or be
    // destroyed while it is served: the panel asks about it from another
    // process at any time, so every request reads the menu as it is then.
    void SetMenu(wxMenu* menu);

private:
    friend struct wxDBusMenuVTable;

    // Ids are positions in a flattened walk of the menu, with 0 for the root.
    // Refresh() walks it again and, if anything moved, tells the panel and
    // returns true: ids it sent before then may name a different item now.
    // It is called before every request is answered, so FindItem() only ever
    // returns items that are in the menu at that moment.
    bool Refresh();
    void NotifyLayoutChanged();
    wxMenuItem* FindItem(gint32 id) const;
    static void AppendItems(wxMenu* menu, wxVector<wxMenuItem*>& items);

    // Handles one "clicked" or other event, as Event and EventGroup carry it.
    // Returns false if the id is unknown.
    bool HandleEvent(gint32 id, const char* eventId);

    GVariant* BuildProperties(wxMenuItem* item) const;
    GVariant* BuildLayout(gint32 id, gint32 depth) const;

    const wxString m_path;
    Handler* const m_handler;

    GDBusConnection* m_connection = nullptr;
    guint m_objectId = 0;

    // Weak, as the application may delete the menu while it is served, e.g.
    // one on the stack given to wxTaskBarIcon::PopupMenu().
    wxWeakRef<wxMenu> m_menu;

    // Index is the id; entry 0 is unused, standing for the root. Only ever
    // compared, never dereferenced, before Refresh() has rebuilt it from the
    // live menu.
    wxVector<wxMenuItem*> m_items;

    guint32 m_revision = 1;

    wxDECLARE_NO_COPY_CLASS(wxDBusMenu);
};

#endif // wxUSE_TASKBARICON && __WXGTK4__

#endif // _WX_GTK_PRIVATE_DBUSMENU_H_
