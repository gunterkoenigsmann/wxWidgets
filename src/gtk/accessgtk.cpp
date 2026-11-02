///////////////////////////////////////////////////////////////////////////////
// Name:        src/gtk/accessgtk.cpp
// Purpose:     wxAccessible implementation for wxGTK (GTK4)
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include "wx/wxprec.h"

#if wxUSE_ACCESSIBILITY

#include "wx/access.h"

#ifndef WX_PRECOMP
    #include "wx/window.h"
#endif

#include "wx/vector.h"

#include "wx/gtk/private/wrapgtk.h"
#include "wx/gtk/private/win_gtk.h"

// ----------------------------------------------------------------------------
// how the two accessibility models differ, and what follows from it
// ----------------------------------------------------------------------------
//
// wxAccessibleBase is shaped like MSAA: an assistive technology asks the
// application for a name, a role, a state, and the application answers from
// whatever it happens to know at the time. Nothing is stored anywhere, and an
// object that is never asked about costs nothing.
//
// GTK4 inverted that. It keeps the answers in a GtkATContext and expects the
// application to push new ones in when they change; the only questions it ever
// asks back are for bounds, for the tree structure, and for the platform
// states (focusable, focused, active). So:
//
//  - everything else has to be pushed, and the only moments wx knows something
//    might have changed are the wxAccessible::NotifyEvent() calls, which is
//    why this file hangs almost everything off those;
//
//  - a child of a wxAccessible is an integer, not an object, and GTK4 wants an
//    object. One is made per child id -- see wxGtkAccChild below;
//
//  - the tree is walked, not indexed: GTK asks a parent for its first child
//    and then each child for its next sibling. That would allow the objects to
//    be made only as far as a walk actually goes, which matters because
//    wxGridAccessible reports rows*cols children -- except that
//    gtk_accessible_get_next_accessible_sibling() does not call the interface
//    vfunc at all. It reads the value stored by
//    gtk_accessible_set_accessible_parent(), even though
//    gtk_accessible_get_first_accessible_child() right next to it does call
//    its vfunc. So the chain has to exist before the walk starts, and the
//    objects are made all at once the first time anything asks for them.
//    Verified on GTK 4.14.5 by docs/gtk/probes/gtk4-a11y-virtual-child.c.

// ----------------------------------------------------------------------------
// wx vocabulary -> GTK vocabulary
// ----------------------------------------------------------------------------

namespace
{

GtkAccessibleRole wxGTKAccessibleRole(wxAccRole role)
{
    switch ( role )
    {
        case wxROLE_SYSTEM_ALERT:           return GTK_ACCESSIBLE_ROLE_ALERT;
        case wxROLE_SYSTEM_APPLICATION:     return GTK_ACCESSIBLE_ROLE_APPLICATION;
        case wxROLE_SYSTEM_BUTTONDROPDOWN:  return GTK_ACCESSIBLE_ROLE_BUTTON;
        case wxROLE_SYSTEM_BUTTONMENU:      return GTK_ACCESSIBLE_ROLE_BUTTON;
        case wxROLE_SYSTEM_CELL:            return GTK_ACCESSIBLE_ROLE_GRID_CELL;
        case wxROLE_SYSTEM_CHECKBUTTON:     return GTK_ACCESSIBLE_ROLE_CHECKBOX;
        case wxROLE_SYSTEM_COLUMNHEADER:    return GTK_ACCESSIBLE_ROLE_COLUMN_HEADER;
        case wxROLE_SYSTEM_COMBOBOX:        return GTK_ACCESSIBLE_ROLE_COMBO_BOX;
        case wxROLE_SYSTEM_DIALOG:          return GTK_ACCESSIBLE_ROLE_DIALOG;
        case wxROLE_SYSTEM_DOCUMENT:        return GTK_ACCESSIBLE_ROLE_DOCUMENT;
        case wxROLE_SYSTEM_DROPLIST:        return GTK_ACCESSIBLE_ROLE_COMBO_BOX;
        case wxROLE_SYSTEM_GRAPHIC:         return GTK_ACCESSIBLE_ROLE_IMG;
        case wxROLE_SYSTEM_GROUPING:        return GTK_ACCESSIBLE_ROLE_GROUP;
        case wxROLE_SYSTEM_LINK:            return GTK_ACCESSIBLE_ROLE_LINK;
        case wxROLE_SYSTEM_LIST:            return GTK_ACCESSIBLE_ROLE_LIST;
        case wxROLE_SYSTEM_LISTITEM:        return GTK_ACCESSIBLE_ROLE_LIST_ITEM;
        case wxROLE_SYSTEM_MENUBAR:         return GTK_ACCESSIBLE_ROLE_MENU_BAR;
        case wxROLE_SYSTEM_MENUITEM:        return GTK_ACCESSIBLE_ROLE_MENU_ITEM;
        case wxROLE_SYSTEM_MENUPOPUP:       return GTK_ACCESSIBLE_ROLE_MENU;
        case wxROLE_SYSTEM_OUTLINE:         return GTK_ACCESSIBLE_ROLE_TREE;
        case wxROLE_SYSTEM_OUTLINEITEM:     return GTK_ACCESSIBLE_ROLE_TREE_ITEM;
        case wxROLE_SYSTEM_PAGETAB:         return GTK_ACCESSIBLE_ROLE_TAB;
        case wxROLE_SYSTEM_PAGETABLIST:     return GTK_ACCESSIBLE_ROLE_TAB_LIST;
        case wxROLE_SYSTEM_PROGRESSBAR:     return GTK_ACCESSIBLE_ROLE_PROGRESS_BAR;
        case wxROLE_SYSTEM_PUSHBUTTON:      return GTK_ACCESSIBLE_ROLE_BUTTON;
        case wxROLE_SYSTEM_RADIOBUTTON:     return GTK_ACCESSIBLE_ROLE_RADIO;
        case wxROLE_SYSTEM_ROW:             return GTK_ACCESSIBLE_ROLE_ROW;
        case wxROLE_SYSTEM_ROWHEADER:       return GTK_ACCESSIBLE_ROLE_ROW_HEADER;
        case wxROLE_SYSTEM_SCROLLBAR:       return GTK_ACCESSIBLE_ROLE_SCROLLBAR;
        case wxROLE_SYSTEM_SEPARATOR:       return GTK_ACCESSIBLE_ROLE_SEPARATOR;
        case wxROLE_SYSTEM_SLIDER:          return GTK_ACCESSIBLE_ROLE_SLIDER;
        case wxROLE_SYSTEM_SPINBUTTON:      return GTK_ACCESSIBLE_ROLE_SPIN_BUTTON;
        case wxROLE_SYSTEM_STATICTEXT:      return GTK_ACCESSIBLE_ROLE_LABEL;
        case wxROLE_SYSTEM_STATUSBAR:       return GTK_ACCESSIBLE_ROLE_STATUS;
        case wxROLE_SYSTEM_TABLE:           return GTK_ACCESSIBLE_ROLE_GRID;
        case wxROLE_SYSTEM_TEXT:            return GTK_ACCESSIBLE_ROLE_TEXT_BOX;
        case wxROLE_SYSTEM_TOOLBAR:         return GTK_ACCESSIBLE_ROLE_TOOLBAR;
        case wxROLE_SYSTEM_TOOLTIP:         return GTK_ACCESSIBLE_ROLE_TOOLTIP;
        case wxROLE_SYSTEM_WINDOW:          return GTK_ACCESSIBLE_ROLE_WINDOW;

        // These have no ARIA counterpart at all: they describe parts of a
        // window that GTK4 does not model as separate objects, or MSAA
        // concepts that were never carried over.
        case wxROLE_NONE:
        case wxROLE_SYSTEM_ANIMATION:
        case wxROLE_SYSTEM_BORDER:
        case wxROLE_SYSTEM_BUTTONDROPDOWNGRID:
        case wxROLE_SYSTEM_CARET:
        case wxROLE_SYSTEM_CHARACTER:
        case wxROLE_SYSTEM_CHART:
        case wxROLE_SYSTEM_CLIENT:
        case wxROLE_SYSTEM_CLOCK:
        case wxROLE_SYSTEM_COLUMN:
        case wxROLE_SYSTEM_CURSOR:
        case wxROLE_SYSTEM_DIAGRAM:
        case wxROLE_SYSTEM_DIAL:
        case wxROLE_SYSTEM_EQUATION:
        case wxROLE_SYSTEM_GRIP:
        case wxROLE_SYSTEM_HELPBALLOON:
        case wxROLE_SYSTEM_HOTKEYFIELD:
        case wxROLE_SYSTEM_INDICATOR:
        case wxROLE_SYSTEM_PANE:
        case wxROLE_SYSTEM_PROPERTYPAGE:
        case wxROLE_SYSTEM_SOUND:
        case wxROLE_SYSTEM_TITLEBAR:
        case wxROLE_SYSTEM_WHITESPACE:
            break;
    }

    return GTK_ACCESSIBLE_ROLE_GENERIC;
}

// The ARIA name of the role a wxAccRole maps to, e.g. "grid-cell", or an
// empty string if it maps to nothing in particular.
wxString wxGTKAccessibleRoleName(wxAccRole role)
{
    const GtkAccessibleRole gtkRole = wxGTKAccessibleRole(role);
    if ( gtkRole == GTK_ACCESSIBLE_ROLE_GENERIC )
        return wxString();

    GEnumClass* const enumClass =
        static_cast<GEnumClass*>(g_type_class_ref(GTK_TYPE_ACCESSIBLE_ROLE));

    wxString name;
    if ( const GEnumValue* const value = g_enum_get_value(enumClass, gtkRole) )
        name = wxString::FromAscii(value->value_nick);

    g_type_class_unref(enumClass);

    return name;
}

// Push those parts of an MSAA state bitmask that GTK4 has a name for.
//
// The ones it does not are left out rather than approximated: an assistive
// technology that is told nothing about a state falls back on sensible
// defaults, while one that is told the wrong thing does not.
void wxGTKUpdateStates(GtkAccessible* accessible, long state)
{
    gtk_accessible_update_state(accessible,
        GTK_ACCESSIBLE_STATE_BUSY,
            (state & wxACC_STATE_SYSTEM_BUSY) != 0,
        GTK_ACCESSIBLE_STATE_DISABLED,
            (state & wxACC_STATE_SYSTEM_UNAVAILABLE) != 0,
        GTK_ACCESSIBLE_STATE_EXPANDED,
            (state & wxACC_STATE_SYSTEM_EXPANDED) != 0,
        GTK_ACCESSIBLE_STATE_HIDDEN,
            (state & wxACC_STATE_SYSTEM_INVISIBLE) != 0,
        GTK_ACCESSIBLE_STATE_PRESSED,
            (state & wxACC_STATE_SYSTEM_PRESSED)
                ? GTK_ACCESSIBLE_TRISTATE_TRUE : GTK_ACCESSIBLE_TRISTATE_FALSE,
        GTK_ACCESSIBLE_STATE_SELECTED,
            (state & wxACC_STATE_SYSTEM_SELECTED) != 0,
        -1);

    // Checked is a tristate, and "mixed" is a state of its own in MSAA.
    GtkAccessibleTristate checked;
    if ( state & wxACC_STATE_SYSTEM_MIXED )
        checked = GTK_ACCESSIBLE_TRISTATE_MIXED;
    else if ( state & wxACC_STATE_SYSTEM_CHECKED )
        checked = GTK_ACCESSIBLE_TRISTATE_TRUE;
    else
        checked = GTK_ACCESSIBLE_TRISTATE_FALSE;

    gtk_accessible_update_state(accessible,
                                GTK_ACCESSIBLE_STATE_CHECKED, checked,
                                -1);

    gtk_accessible_update_property(accessible,
        GTK_ACCESSIBLE_PROPERTY_READ_ONLY,
            (state & wxACC_STATE_SYSTEM_READONLY) != 0,
        GTK_ACCESSIBLE_PROPERTY_MULTI_SELECTABLE,
            (state & wxACC_STATE_SYSTEM_MULTISELECTABLE) != 0,
        -1);
}

// Ask an accessible for a string, pushing it only if it has one: a control
// with no help text should be reported as having none, not as having "".
void wxGTKUpdateStringProperty(GtkAccessible* accessible,
                               GtkAccessibleProperty property,
                               wxAccStatus status,
                               const wxString& value)
{
    if ( status != wxACC_OK )
        gtk_accessible_reset_property(accessible, property);
    else
        gtk_accessible_update_property(accessible, property, value.utf8_str().data(), -1);
}

// The name a window's accessibility state is found under on its widget.
const char* const wx_ACCESSIBLE_DATA = "wx-accessible";

} // anonymous namespace

// ============================================================================
// wxGtkAccChild: one child id of a wxAccessible, as a GObject
// ============================================================================

extern "C" {

struct wxGtkAccChild
{
    GObject m_object;

    // The accessible this is a child of, and which child. Both are cleared
    // when the wxAccessible goes away: GTK may still hold references to this
    // object afterwards, and it has to answer harmlessly if it does.
    wxAccessible* m_accessible;
    wxGTKAccessibleImpl* m_impl;
    int m_childId;

    GtkATContext* m_context;
    GtkAccessibleRole m_role;
};

struct wxGtkAccChildClass
{
    GObjectClass m_parent;
};

} // extern "C"

// Defined below, once wxGTKAccessibleImpl is complete.
static GtkWidget* wxGTKAccImplGetWidget(wxGTKAccessibleImpl* impl);
static wxGtkAccChild* wxGTKAccImplGetChild(wxGTKAccessibleImpl* impl, int childId);
static int wxGTKAccImplGetChildCount(wxGTKAccessibleImpl* impl);
static bool wxGTKAccImplGetBounds(wxGTKAccessibleImpl* impl, int childId,
                                  wxRect& rect);
static bool wxGTKAccImplIsFocused(wxGTKAccessibleImpl* impl, int childId);

extern "C" {

static GType wx_acc_child_get_type();

#define WX_ACC_CHILD(obj) \
    G_TYPE_CHECK_INSTANCE_CAST(obj, wx_acc_child_get_type(), wxGtkAccChild)

static GtkATContext* wx_acc_child_get_at_context(GtkAccessible* accessible)
{
    wxGtkAccChild* const child = WX_ACC_CHILD(accessible);

    if ( !child->m_context && child->m_accessible )
    {
        child->m_context = gtk_at_context_create(child->m_role, accessible,
                                                 gdk_display_get_default());
    }

    return child->m_context ? GTK_AT_CONTEXT(g_object_ref(child->m_context))
                            : nullptr;
}

static gboolean
wx_acc_child_get_platform_state(GtkAccessible* accessible,
                                GtkAccessiblePlatformState state)
{
    wxGtkAccChild* const child = WX_ACC_CHILD(accessible);

    if ( !child->m_impl )
        return FALSE;

    switch ( state )
    {
        case GTK_ACCESSIBLE_PLATFORM_STATE_FOCUSABLE:
            // Anything that can be reached with the keyboard inside a control
            // is focusable in the sense the AT means; wx has no way to say
            // otherwise about a child id.
            return TRUE;

        case GTK_ACCESSIBLE_PLATFORM_STATE_FOCUSED:
            return wxGTKAccImplIsFocused(child->m_impl, child->m_childId);

        case GTK_ACCESSIBLE_PLATFORM_STATE_ACTIVE:
            return FALSE;
    }

    return FALSE;
}

static GtkAccessible* wx_acc_child_get_accessible_parent(GtkAccessible* accessible)
{
    wxGtkAccChild* const child = WX_ACC_CHILD(accessible);

    if ( !child->m_impl )
        return nullptr;

    GtkWidget* const widget = wxGTKAccImplGetWidget(child->m_impl);

    return widget ? GTK_ACCESSIBLE(g_object_ref(widget)) : nullptr;
}

static GtkAccessible* wx_acc_child_get_first_accessible_child(GtkAccessible*)
{
    // wxAccessible child ids are one level deep.
    return nullptr;
}

static GtkAccessible* wx_acc_child_get_next_accessible_sibling(GtkAccessible* accessible)
{
    wxGtkAccChild* const child = WX_ACC_CHILD(accessible);

    if ( !child->m_impl )
        return nullptr;

    const int next = child->m_childId + 1;
    if ( next > wxGTKAccImplGetChildCount(child->m_impl) )
        return nullptr;

    wxGtkAccChild* const sibling = wxGTKAccImplGetChild(child->m_impl, next);

    return sibling ? GTK_ACCESSIBLE(g_object_ref(sibling)) : nullptr;
}

static gboolean wx_acc_child_get_bounds(GtkAccessible* accessible,
                                        int* x, int* y, int* width, int* height)
{
    wxGtkAccChild* const child = WX_ACC_CHILD(accessible);

    if ( !child->m_impl )
        return FALSE;

    wxRect rect;
    if ( !wxGTKAccImplGetBounds(child->m_impl, child->m_childId, rect) )
        return FALSE;

    *x = rect.x;
    *y = rect.y;
    *width = rect.width;
    *height = rect.height;

    return TRUE;
}

static void wx_acc_child_accessible_init(void* g_iface, void*)
{
    GtkAccessibleInterface* const iface = static_cast<GtkAccessibleInterface*>(g_iface);

    iface->get_at_context = wx_acc_child_get_at_context;
    iface->get_platform_state = wx_acc_child_get_platform_state;
    iface->get_accessible_parent = wx_acc_child_get_accessible_parent;
    iface->get_first_accessible_child = wx_acc_child_get_first_accessible_child;
    iface->get_next_accessible_sibling = wx_acc_child_get_next_accessible_sibling;
    iface->get_bounds = wx_acc_child_get_bounds;
}

// GtkAccessible declares an "accessible-role" property, and GObject requires
// every implementer of an interface to install the interface's properties.
// Without this the object is constructed with a warning and never gets a role.
enum { WX_ACC_CHILD_PROP_ROLE = 1 };

static void wx_acc_child_get_property(GObject* object, guint id,
                                      GValue* value, GParamSpec* pspec)
{
    switch ( id )
    {
        case WX_ACC_CHILD_PROP_ROLE:
            g_value_set_enum(value, WX_ACC_CHILD(object)->m_role);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, id, pspec);
    }
}

static void wx_acc_child_set_property(GObject* object, guint id,
                                      const GValue* value, GParamSpec* pspec)
{
    switch ( id )
    {
        case WX_ACC_CHILD_PROP_ROLE:
            WX_ACC_CHILD(object)->m_role = GtkAccessibleRole(g_value_get_enum(value));
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, id, pspec);
    }
}

static void wx_acc_child_dispose(GObject* object)
{
    wxGtkAccChild* const child = WX_ACC_CHILD(object);

    g_clear_object(&child->m_context);

    G_OBJECT_CLASS(g_type_class_peek_parent(G_OBJECT_GET_CLASS(object)))
        ->dispose(object);
}

static void wx_acc_child_class_init(void* g_class, void*)
{
    GObjectClass* const gobject_class = G_OBJECT_CLASS(g_class);

    gobject_class->dispose = wx_acc_child_dispose;
    gobject_class->get_property = wx_acc_child_get_property;
    gobject_class->set_property = wx_acc_child_set_property;

    g_object_class_override_property(gobject_class, WX_ACC_CHILD_PROP_ROLE,
                                     "accessible-role");
}

static GType wx_acc_child_get_type()
{
    static GType type;
    if ( type == 0 )
    {
        const GTypeInfo info = {
            sizeof(wxGtkAccChildClass),
            nullptr, nullptr,
            wx_acc_child_class_init,
            nullptr, nullptr,
            sizeof(wxGtkAccChild), 0,
            nullptr, nullptr
        };

        type = g_type_register_static(G_TYPE_OBJECT, "wxGtkAccChild",
                                      &info, GTypeFlags(0));

        const GInterfaceInfo iface_info = {
            wx_acc_child_accessible_init, nullptr, nullptr
        };
        g_type_add_interface_static(type, GTK_TYPE_ACCESSIBLE, &iface_info);
    }

    return type;
}

} // extern "C"

// ============================================================================
// wxGTKAccessibleImpl
// ============================================================================

class wxGTKAccessibleImpl
{
public:
    explicit wxGTKAccessibleImpl(wxAccessible* acc)
        : m_acc(acc)
    {
    }

    ~wxGTKAccessibleImpl()
    {
        Detach();
    }

    // The widget this accessible speaks for, or null if it has no window yet.
    GtkWidget* GetWidget()
    {
        wxWindow* const win = m_acc->GetWindow();
        if ( !win )
            return nullptr;

        GtkWidget* const widget = win->GetConnectWidget();
        if ( widget != m_widget )
        {
            ClearWidgetData();

            m_widget = widget;
            if ( m_widget )
            {
                g_object_set_data(G_OBJECT(m_widget), wx_ACCESSIBLE_DATA, this);
                g_object_add_weak_pointer(G_OBJECT(m_widget),
                                          reinterpret_cast<gpointer*>(&m_widget));
            }
        }

        return m_widget;
    }

    // The accessible to ask about childId, and the id to ask it about.
    //
    // MSAA lets a child id be answered either by the object it belongs to or
    // by an object of the child's own, and wxGrid uses both: wxGridAccessible
    // answers GetChildCount() itself but hands out a wxGridCellAccessible for
    // everything about a particular cell. That object is transient -- wxGrid
    // rebuilds it whenever a different cell is asked about -- so it is looked
    // up again for every question and never stored.
    wxAccessible* QueryTarget(int childId, int* askId)
    {
        wxAccessible* child = nullptr;
        if ( m_acc->GetChild(childId, &child) == wxACC_OK && child &&
                child != m_acc )
        {
            *askId = wxACC_SELF;
            return child;
        }

        *askId = childId;
        return m_acc;
    }

    bool GetChildBounds(int childId, wxRect& rect)
    {
        int askId;
        wxAccessible* const target = QueryTarget(childId, &askId);

        if ( target->GetLocation(rect, askId) != wxACC_OK )
            return false;

        wxWindow* const win = m_acc->GetWindow();
        if ( !win )
            return false;

        // wx reports screen coordinates, GTK wants them relative to the parent.
        rect.SetPosition(win->ScreenToClient(rect.GetTopLeft()));

        return true;
    }

    bool IsChildFocused(int childId)
    {
        int focusedId = 0;
        wxAccessible* focusedChild = nullptr;
        if ( m_acc->GetFocus(&focusedId, &focusedChild) != wxACC_OK )
            return false;

        return focusedId == childId;
    }

    int GetChildCount()
    {
        int count = 0;
        if ( m_acc->GetChildCount(&count) != wxACC_OK )
            return 0;

        return count;
    }

    // The object standing for childId, building all of them first if nothing
    // has asked for them yet.
    wxGtkAccChild* GetChild(int childId)
    {
        EnsureChildren();

        return FindChild(childId);
    }

    // The object standing for childId if it has been built, never building it.
    wxGtkAccChild* FindChild(int childId) const
    {
        if ( childId < 1 || size_t(childId) > m_children.size() )
            return nullptr;

        return m_children[childId - 1];
    }

    // True while EnsureChildren() is running, during which the children are
    // reported as not being there yet: see below.
    bool IsBuildingChildren() const { return m_building; }

    // Build the objects for all the child ids and chain them together.
    //
    // Nothing is built until something first asks for a child, but then all
    // of them are. That is GTK's requirement rather than a choice: see the
    // note at the top of this file.
    //
    // GTK may walk the tree while this runs. Since GTK 4.18, setting the
    // parent of an object realizes its AT-SPI context, and realizing it asks
    // the parent for its children, to find the new one's index -- so the walk
    // comes back here, from inside the loop below. While that happens the
    // window answers as if it had no virtual children yet. Building them
    // again from inside the walk would nest once per child, deep enough to
    // run a large wxGrid out of stack, and each level would walk all of them
    // again.
    void EnsureChildren()
    {
        if ( m_building )
            return;

        const int count = GetChildCount();
        if ( count < 1 || count == m_linkedCount )
            return;

        GtkWidget* const widget = GetWidget();
        if ( !widget )
            return;

        m_building = true;

        if ( m_children.size() < size_t(count) )
            m_children.resize(count, nullptr);

        // The objects are made, and told everything about themselves, before
        // any of them gets a parent: an object without one cannot be found,
        // and its context is not realized, so nothing is announced and
        // nothing comes back here.
        for ( int childId = 1; childId <= count; childId++ )
        {
            if ( !m_children[childId - 1] )
            {
                m_children[childId - 1] = MakeChild(childId);
                UpdateChild(childId);
            }
        }

        // Back to front, so that each child can be given its successor when
        // its parent is set. The two-argument form of
        // gtk_accessible_set_accessible_parent() is the only safe way to say
        // what comes next, because
        // gtk_accessible_update_next_accessible_sibling() drops a reference
        // on the parent widget -- see
        // docs/gtk/probes/gtk4-a11y-virtual-child.c.
        for ( int childId = count; childId >= 1; childId-- )
        {
            wxGtkAccChild* const next = childId < count
                                            ? m_children[childId]
                                            : nullptr;

            gtk_accessible_set_accessible_parent(
                GTK_ACCESSIBLE(m_children[childId - 1]),
                GTK_ACCESSIBLE(widget),
                next ? GTK_ACCESSIBLE(next) : nullptr);
        }

        m_linkedCount = count;
        m_building = false;
    }

    // Copy what the wxAccessible says about childId into GTK's cache.
    void UpdateChild(int childId)
    {
        if ( childId < 1 )
            return;

        wxGtkAccChild* const child = FindChild(childId);
        if ( !child )
            return;

        // Everything is asked before anything is pushed, and target is not
        // used after that. Pushing a change can make GTK walk the tree, the
        // walk can ask about other children, and wxGrid answers those with a
        // new object that replaces -- and deletes -- the one asked here.
        int askId;
        wxAccessible* const target = QueryTarget(childId, &askId);

        wxString name;
        const wxAccStatus nameStatus = target->GetName(askId, &name);

        wxString description;
        const wxAccStatus descriptionStatus =
            target->GetDescription(askId, &description);

        wxString shortcut;
        const wxAccStatus shortcutStatus =
            target->GetKeyboardShortcut(askId, &shortcut);

        long state = 0;
        const bool hasState = target->GetState(askId, &state) == wxACC_OK;

        GtkAccessible* const accessible = GTK_ACCESSIBLE(child);

        wxGTKUpdateStringProperty(accessible,
                                  GTK_ACCESSIBLE_PROPERTY_LABEL,
                                  nameStatus, name);
        wxGTKUpdateStringProperty(accessible,
                                  GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,
                                  descriptionStatus, description);
        wxGTKUpdateStringProperty(accessible,
                                  GTK_ACCESSIBLE_PROPERTY_KEY_SHORTCUTS,
                                  shortcutStatus, shortcut);
        if ( hasState )
            wxGTKUpdateStates(accessible, state);
    }

    // Copy what the wxAccessible says about the window itself.
    void UpdateSelf()
    {
        GtkWidget* const widget = GetWidget();
        if ( !widget )
            return;

        GtkAccessible* const accessible = GTK_ACCESSIBLE(widget);

        wxAccRole role = wxROLE_NONE;
        if ( m_acc->GetRole(wxACC_SELF, &role) == wxACC_OK )
        {
            // The widget's own role cannot be set here. It is a GObject
            // property, not an accessible attribute, and GTK refuses to change
            // it once the widget has an AT context -- which a wx window
            // already has by the time an application attaches a wxAccessible
            // to it, so every attempt is a g_critical() and no change.
            //
            // The role description is the ARIA way of saying what something is
            // when the role itself cannot say it, so the role's own name goes
            // there instead. Children are not affected: their objects are made
            // here, and their roles are set when their contexts are.
            const wxString roleName = wxGTKAccessibleRoleName(role);
            wxGTKUpdateStringProperty(accessible,
                                      GTK_ACCESSIBLE_PROPERTY_ROLE_DESCRIPTION,
                                      roleName.empty() ? wxACC_NOT_SUPPORTED
                                                       : wxACC_OK,
                                      roleName);
        }

        // The name set with wxWindow::SetAccessibleName() takes precedence
        // over anything else, but only for the object itself and not its
        // children.
        wxString name = m_acc->GetNameOverride();
        const wxAccStatus nameStatus = name.empty()
                                        ? m_acc->GetName(wxACC_SELF, &name)
                                        : wxACC_OK;
        wxGTKUpdateStringProperty(accessible, GTK_ACCESSIBLE_PROPERTY_LABEL,
                                  nameStatus, name);

        wxString description;
        wxGTKUpdateStringProperty(accessible, GTK_ACCESSIBLE_PROPERTY_DESCRIPTION,
                                  m_acc->GetDescription(wxACC_SELF, &description),
                                  description);

        long state = 0;
        if ( m_acc->GetState(wxACC_SELF, &state) == wxACC_OK )
            wxGTKUpdateStates(accessible, state);
    }

    // GTK4 has no way to say "this child now has the focus" -- focus is one of
    // the platform states, which are pulled and never pushed. The ARIA way to
    // say it for a composite widget is to point the widget at the item that
    // currently stands in for it, which is what this does. The relation needs
    // an object to point at, so this builds the children if nothing has yet.
    void UpdateFocus()
    {
        GtkWidget* const widget = GetWidget();
        if ( !widget )
            return;

        int childId = 0;
        wxAccessible* childObject = nullptr;
        if ( m_acc->GetFocus(&childId, &childObject) != wxACC_OK )
            return;

        if ( childId > 0 )
        {
            if ( wxGtkAccChild* const child = GetChild(childId) )
            {
                gtk_accessible_update_relation(
                    GTK_ACCESSIBLE(widget),
                    GTK_ACCESSIBLE_RELATION_ACTIVE_DESCENDANT,
                    GTK_ACCESSIBLE(child),
                    -1);
                return;
            }
        }

        gtk_accessible_reset_relation(GTK_ACCESSIBLE(widget),
                                      GTK_ACCESSIBLE_RELATION_ACTIVE_DESCENDANT);
    }

    void Update(int objectId)
    {
        if ( objectId == wxACC_SELF )
            UpdateSelf();
        else
            UpdateChild(objectId);
    }

    void UpdateAll()
    {
        UpdateSelf();

        // Only the children that have been built: if none have, nothing has
        // asked about them and there is nothing to refresh.
        for ( size_t n = 0; n < m_children.size(); n++ )
        {
            if ( m_children[n] )
                UpdateChild(int(n) + 1);
        }

        UpdateFocus();
    }

    void Detach()
    {
        // Taken out of m_children before any is released: releasing one can
        // make GTK walk the tree, and the walk must not find the others.
        wxVector<wxGtkAccChild*> children;
        children.swap(m_children);
        m_linkedCount = 0;

        // Nor may the widget go on pointing at one of them.
        if ( m_widget && !children.empty() )
        {
            gtk_accessible_reset_relation(
                GTK_ACCESSIBLE(m_widget),
                GTK_ACCESSIBLE_RELATION_ACTIVE_DESCENDANT);
        }

        for ( wxGtkAccChild* const child : children )
        {
            if ( child )
            {
                // GTK may still hold a reference to this: leave it able to
                // answer, just with nothing to say.
                child->m_accessible = nullptr;
                child->m_impl = nullptr;
                g_object_unref(child);
            }
        }

        ClearWidgetData();
    }

private:
    // A new object for childId, with the role it will keep, and no parent.
    wxGtkAccChild* MakeChild(int childId)
    {
        int askId;
        wxAccRole role = wxROLE_NONE;
        if ( QueryTarget(childId, &askId)->GetRole(askId, &role) != wxACC_OK )
            role = wxROLE_NONE;

        wxGtkAccChild* const child = WX_ACC_CHILD(
            g_object_new(wx_acc_child_get_type(), nullptr));

        child->m_accessible = m_acc;
        child->m_impl = this;
        child->m_childId = childId;
        child->m_role = wxGTKAccessibleRole(role);

        return child;
    }

    void ClearWidgetData()
    {
        if ( !m_widget )
            return;

        if ( g_object_get_data(G_OBJECT(m_widget), wx_ACCESSIBLE_DATA) == this )
            g_object_set_data(G_OBJECT(m_widget), wx_ACCESSIBLE_DATA, nullptr);

        g_object_remove_weak_pointer(G_OBJECT(m_widget),
                                     reinterpret_cast<gpointer*>(&m_widget));
        m_widget = nullptr;
    }

    wxAccessible* const m_acc;
    wxVector<wxGtkAccChild*> m_children;

    // How many of m_children are chained together under the widget, or 0 if
    // they have not been yet.
    int m_linkedCount = 0;

    bool m_building = false;

    // Weak: the widget can be destroyed while the wxAccessible is still alive.
    GtkWidget* m_widget = nullptr;

    wxDECLARE_NO_COPY_CLASS(wxGTKAccessibleImpl);
};

static GtkWidget* wxGTKAccImplGetWidget(wxGTKAccessibleImpl* impl)
{
    return impl->GetWidget();
}

static wxGtkAccChild* wxGTKAccImplGetChild(wxGTKAccessibleImpl* impl, int childId)
{
    return impl->GetChild(childId);
}

static int wxGTKAccImplGetChildCount(wxGTKAccessibleImpl* impl)
{
    return impl->GetChildCount();
}

static bool wxGTKAccImplGetBounds(wxGTKAccessibleImpl* impl, int childId,
                                  wxRect& rect)
{
    return impl->GetChildBounds(childId, rect);
}

static bool wxGTKAccImplIsFocused(wxGTKAccessibleImpl* impl, int childId)
{
    return impl->IsChildFocused(childId);
}

// ============================================================================
// what wxPizza asks for: the virtual children of the window it backs
// ============================================================================

extern "C" {

GtkAccessible* wxGTKPizzaGetFirstAccessibleChild(GtkWidget* widget)
{
    wxGTKAccessibleImpl* const impl = static_cast<wxGTKAccessibleImpl*>(
        g_object_get_data(G_OBJECT(widget), wx_ACCESSIBLE_DATA));

    // No wxAccessible attached, or one that describes no children: the widget
    // keeps whatever accessible children GTK finds for it by itself.
    if ( !impl || impl->IsBuildingChildren() || impl->GetChildCount() < 1 )
        return nullptr;

    impl->EnsureChildren();

    wxGtkAccChild* const child = impl->GetChild(1);

    return child ? GTK_ACCESSIBLE(g_object_ref(child)) : nullptr;
}

} // extern "C"

// ============================================================================
// wxAccessible
// ============================================================================

wxAccessible::wxAccessible(wxWindow* win)
    : wxAccessibleBase(win),
      m_impl(new wxGTKAccessibleImpl(this))
{
}

wxAccessible::~wxAccessible()
{
    delete m_impl;
}

void wxAccessible::Update()
{
    m_impl->UpdateAll();
}

/* static */
void wxAccessible::NotifyEvent(int eventType, wxWindow* window,
                               wxAccObject WXUNUSED(objectType), int objectId)
{
    if ( !window )
        return;

    // Except when the window is going away: whatever describes it has nothing
    // more to describe, and making one now would ask a window that is half
    // destructed what it is -- wxGrid reports this from its destructor, after
    // deleting its own wxAccessible.
    if ( eventType == wxACC_EVENT_OBJECT_DESTROY )
    {
        if ( wxAccessible* const acc = window->GetAccessible() )
            acc->m_impl->Detach();
        return;
    }

    // Not GetAccessible(): a control that describes itself does so by
    // overriding CreateAccessible(), and nothing under GTK4 asks for the
    // result. wxMSW gets that call from the platform, when an assistive
    // technology sends WM_GETOBJECT; GTK4 has no equivalent, and its
    // accessibility data has to be there before anything comes looking. The
    // first event a control reports is the earliest moment to build it.
    wxAccessible* const acc = window->GetOrCreateAccessible();
    if ( !acc )
        return;

    switch ( eventType )
    {
        case wxACC_EVENT_OBJECT_FOCUS:
            acc->m_impl->UpdateFocus();
            break;

        case wxACC_EVENT_OBJECT_DESTROY:
            // Handled above, without creating anything.
            break;

        case wxACC_EVENT_OBJECT_CREATE:
            // Queued rather than done here, unlike the two below. This event
            // is sent from a base class Create(), so the most derived one has
            // not returned yet and the control cannot answer questions about
            // itself: a wxDataViewCtrl is associated with its model by the
            // class deriving from it, after wxDataViewCtrl::Create() is done,
            // and asking a model-less one for its role dereferences null.
            // Not a dataview problem either -- any control that finishes
            // itself after its base Create() returns can be asked too early
            // in the same way.
            // Anything reading this comes through the event loop, which does
            // not run until construction has finished, so nothing can look
            // before the queued call has caught up.
            window->CallAfter([window]()
                {
                    if ( wxAccessible* const later = window->GetAccessible() )
                    {
                        later->m_impl->Detach();
                        later->m_impl->UpdateAll();
                    }
                });
            break;

        case wxACC_EVENT_OBJECT_REORDER:
        case wxACC_EVENT_OBJECT_PARENTCHANGE:
            // The shape of the tree changed, so nothing already handed out can
            // be trusted. GTK 4.14 has no way to say so -- there is no
            // equivalent of MSAA's "the children changed" -- so the objects
            // are rebuilt and an assistive technology will see the new ones
            // the next time it walks.  Unlike the creation above, these two
            // reach a control that has been alive for a while, so asking it
            // about itself is safe.
            acc->m_impl->Detach();
            acc->m_impl->UpdateAll();
            break;

        case wxACC_EVENT_OBJECT_SELECTIONWITHIN:
            acc->m_impl->UpdateAll();
            break;

        default:
            acc->m_impl->Update(objectId);
            break;
    }
}

#endif // wxUSE_ACCESSIBILITY
