///////////////////////////////////////////////////////////////////////////////
// Name:        src/gtk/win_gtk.cpp
// Purpose:     native GTK+ widget for wxWindow
// Author:      Paul Cornett
// Copyright:   (c) 2007 Paul Cornett
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#ifndef WX_PRECOMP
    #include "wx/window.h"
#endif

#include "wx/private/access.h"

#include "wx/gtk/private.h"
#include "wx/gtk/private/gtk3-compat.h"
#include "wx/gtk/private/win_gtk.h"
#ifdef __WXGTK3__
    #include "wx/gtk/private/stylecontext.h"
#endif
#include "wx/window.h"

#if wxUSE_ACCESSIBILITY
    #include "wx/gtk/private/access.h"
#endif

// We use GTK accessibility classes, which are only public since GTK 3.8, to
// implement wxPrivate::SetAccessibleElements().
#if GTK_CHECK_VERSION(3,8,0) && !defined(__WXGTK4__)
    #define wxHAS_GTK_ACCESSIBLE

    #include <gtk/gtk-a11y.h>
#endif

/*
wxPizza is a custom GTK+ widget derived from GtkFixed.  A custom widget
is needed to adapt GTK+ to wxWidgets needs in 3 areas: scrolling, window
borders, and RTL.

For scrolling, the "set_scroll_adjustments" signal is implemented
to make wxPizza appear scrollable to GTK+, allowing it to be put in a
GtkScrolledWindow.  Child widget positions are adjusted for the scrolling
position in size_allocate.

For borders, space is reserved in realize and size_allocate.  The border is
drawn on wxPizza's parent GdkWindow.

For RTL, child widget positions are mirrored in size_allocate.
*/

struct wxPizzaChild
{
    GtkWidget* widget;
    int x, y, width, height;
};

static GtkWidgetClass* parent_class;

#if defined(__WXGTK4__) && wxUSE_ACCESSIBILITY
// ----------------------------------------------------------------------------
// accessibility
// ----------------------------------------------------------------------------
//
// wxPizza is the widget behind every custom-drawn wx control, so it is where
// the parts of such a control that have no window of their own -- a wxGrid
// cell, a wxDataViewCtrl item -- have to be attached. GTK4 asks a widget for
// its first accessible child through the GtkAccessible interface, and only a
// re-implementation of that interface can answer with something that is not a
// widget, so wxPizza implements it too and defers to GtkWidget for everything
// else.

static GtkAccessibleInterface* parent_accessible_iface;

extern "C" {

static GtkAccessible* pizza_get_first_accessible_child(GtkAccessible* accessible)
{
    if ( GtkAccessible* const child =
            wxGTKPizzaGetFirstAccessibleChild(GTK_WIDGET(accessible)) )
        return child;

    // No wxAccessible with children here, so this is an ordinary widget with
    // ordinary widget children.
    return parent_accessible_iface->get_first_accessible_child(accessible);
}

// GTK gives every widget class its own accessible role, defaulting to WIDGET
// rather than inheriting the parent class's -- so a wxPizza would report itself
// as a bare widget where the GtkFixed it derives from reports a generic
// container. GENERIC is both more accurate and the only value GTK will let an
// instance override later, should a way of setting the role per instance be
// found: see docs/gtk/gtk4-accessibility.md.
static void pizza_set_default_accessible_role(GtkWidgetClass* widget_class)
{
    gtk_widget_class_set_accessible_role(widget_class,
                                         GTK_ACCESSIBLE_ROLE_GENERIC);
}

static void pizza_accessible_init(void* g_iface, void*)
{
    GtkAccessibleInterface* const iface =
        static_cast<GtkAccessibleInterface*>(g_iface);

    // Re-implementing an interface starts from a copy of the implementation
    // being replaced, so only the one vfunc that differs is assigned here --
    // but the original still has to be kept to fall back on.
    parent_accessible_iface =
        static_cast<GtkAccessibleInterface*>(g_type_interface_peek_parent(iface));

    iface->get_first_accessible_child = pizza_get_first_accessible_child;
}

} // extern "C"
#endif // __WXGTK4__ && wxUSE_ACCESSIBILITY

#ifdef __WXGTK3__
enum {
    PROP_0,
    PROP_HADJUSTMENT,
    PROP_VADJUSTMENT,
    PROP_HSCROLL_POLICY,
    PROP_VSCROLL_POLICY
};
#endif

#ifdef wxHAS_GTK_ACCESSIBLE

// ----------------------------------------------------------------------------
// Accessibility support
// ----------------------------------------------------------------------------

/*
    wxPizza uses its own accessible type, which is just GtkContainerAccessible
    unless one of the functions in wx/private/access.h is called, in which case
    it also reports the elements passed to them as its children, after the
    real ones.

    Each of these elements is represented by a wxPizzaAccessibleElement object,
    which can have children of its own, e.g. a table row has its cells.
*/

namespace
{

// Register a new static type with the name based on the given one but unique:
// this is needed in case several copies of wxWidgets are loaded into the same
// process, see also wxPizza::type().
GType RegisterUniqueType(GType parent, const char* baseName, const GTypeInfo& info)
{
    const char* name = baseName;
    char buf[64];
    for (unsigned i = 0; g_type_from_name(name); i++)
    {
        g_snprintf(buf, sizeof(buf), "%s%u", baseName, i);
        name = buf;
    }

    return g_type_register_static(parent, name, &info, GTypeFlags(0));
}

} // anonymous namespace

extern "C" {

struct wxPizzaAccessibleElement
{
    AtkObject parent;

    // Our parent, which owns us, i.e. either wxPizzaAccessible or another
    // element. May be null if it has been destroyed or if this element has
    // been removed from it.
    AtkObject* container;

    // Array of our own children, all of which we own.
    GPtrArray* children;

    // Rectangle in the client coordinates of the window.
    GdkRectangle rect;

    // Index of the row in the control for the table rows or -1 otherwise.
    long row;

    // Only used for the table rows.
    bool selected;
};

struct wxPizzaAccessibleElementClass
{
    AtkObjectClass parent;
};

struct wxPizzaAccessible
{
    GtkContainerAccessible parent;

    // Array of wxPizzaAccessibleElement objects, all of which we own.
    GPtrArray* elements;

    // Total number of rows if the window is shown as a table or -1.
    long numRows;

    // The current row of the table or -1.
    long currentRow;
};

struct wxPizzaAccessibleClass
{
    GtkContainerAccessibleClass parent;
};

static AtkObjectClass* element_parent_class;
static AtkObjectClass* accessible_parent_class;

static GType wxPizzaAccessibleElement_get_type();
static GType wxPizzaAccessible_get_type();

} // extern "C"

#define WX_PIZZA_ACCESSIBLE_ELEMENT(obj) \
    G_TYPE_CHECK_INSTANCE_CAST(obj, wxPizzaAccessibleElement_get_type(), wxPizzaAccessibleElement)
#define WX_PIZZA_ACCESSIBLE(obj) \
    G_TYPE_CHECK_INSTANCE_CAST(obj, wxPizzaAccessible_get_type(), wxPizzaAccessible)
#define WX_IS_PIZZA_ACCESSIBLE(obj) \
    G_TYPE_CHECK_INSTANCE_TYPE(obj, wxPizzaAccessible_get_type())

namespace
{

// Return the accessible of the window this element belongs to, if any.
wxPizzaAccessible* GetPizzaAccessible(AtkObject* obj)
{
    while ( obj && !WX_IS_PIZZA_ACCESSIBLE(obj) )
        obj = WX_PIZZA_ACCESSIBLE_ELEMENT(obj)->container;

    return obj ? WX_PIZZA_ACCESSIBLE(obj) : nullptr;
}

// Return the widget of the window this element belongs to, if any.
GtkWidget* GetElementWidget(AtkObject* obj)
{
    wxPizzaAccessible* const acc = GetPizzaAccessible(obj);
    return acc ? gtk_accessible_get_widget(GTK_ACCESSIBLE(acc)) : nullptr;
}

// Return the array of elements which are children of the given object, which
// can be either wxPizzaAccessible or wxPizzaAccessibleElement.
GPtrArray* GetChildElements(AtkObject* obj)
{
    return WX_IS_PIZZA_ACCESSIBLE(obj) ? WX_PIZZA_ACCESSIBLE(obj)->elements
                                       : WX_PIZZA_ACCESSIBLE_ELEMENT(obj)->children;
}

// Return the number of children of the given object preceding the elements.
guint GetNumRealChildren(AtkObject* obj)
{
    return WX_IS_PIZZA_ACCESSIBLE(obj)
            ? guint(accessible_parent_class->get_n_children(obj))
            : 0;
}

// Find the element corresponding to the given row of the table.
AtkObject* FindRowElement(wxPizzaAccessible* acc, long row)
{
    if ( row == -1 )
        return nullptr;

    const GPtrArray* const elements = acc->elements;
    for ( guint n = 0; n < elements->len; n++ )
    {
        wxPizzaAccessibleElement* const element =
            WX_PIZZA_ACCESSIBLE_ELEMENT(g_ptr_array_index(elements, n));
        if ( element->row == row )
            return ATK_OBJECT(element);
    }

    return nullptr;
}

// Remove the element from its parent and release it.
void DetachElement(wxPizzaAccessibleElement* element)
{
    // The accessibility clients may still hold references to the element, so
    // it can outlive its parent: ensure it doesn't use a dangling pointer.
    element->container = nullptr;
    g_object_unref(element);
}

} // anonymous namespace

extern "C" {

static AtkObject* element_get_parent(AtkObject* obj)
{
    return WX_PIZZA_ACCESSIBLE_ELEMENT(obj)->container;
}

static int element_get_index_in_parent(AtkObject* obj)
{
    AtkObject* const container = WX_PIZZA_ACCESSIBLE_ELEMENT(obj)->container;
    if ( !container )
        return -1;

    const GPtrArray* const elements = GetChildElements(container);
    for ( guint n = 0; n < elements->len; n++ )
    {
        if ( g_ptr_array_index(elements, n) == obj )
            return int(GetNumRealChildren(container) + n);
    }

    return -1;
}

static gint element_get_n_children(AtkObject* obj)
{
    return int(WX_PIZZA_ACCESSIBLE_ELEMENT(obj)->children->len);
}

static AtkObject* element_ref_child(AtkObject* obj, gint i)
{
    const GPtrArray* const children = WX_PIZZA_ACCESSIBLE_ELEMENT(obj)->children;
    if ( i < 0 || guint(i) >= children->len )
        return nullptr;

    return ATK_OBJECT(g_object_ref(g_ptr_array_index(children, i)));
}

static AtkStateSet* element_ref_state_set(AtkObject* obj)
{
    AtkStateSet* const states = element_parent_class->ref_state_set(obj);

    GtkWidget* const widget = GetElementWidget(obj);
    if ( !widget )
    {
        atk_state_set_add_state(states, ATK_STATE_DEFUNCT);
        return states;
    }

    atk_state_set_add_state(states, ATK_STATE_ENABLED);
    atk_state_set_add_state(states, ATK_STATE_SENSITIVE);

    if ( gtk_widget_get_visible(widget) )
    {
        atk_state_set_add_state(states, ATK_STATE_VISIBLE);

        if ( gtk_widget_get_mapped(widget) )
            atk_state_set_add_state(states, ATK_STATE_SHOWING);
    }

    const wxPizzaAccessibleElement* const
        element = WX_PIZZA_ACCESSIBLE_ELEMENT(obj);
    if ( element->row != -1 )
    {
        atk_state_set_add_state(states, ATK_STATE_SELECTABLE);
        if ( element->selected )
            atk_state_set_add_state(states, ATK_STATE_SELECTED);

        atk_state_set_add_state(states, ATK_STATE_FOCUSABLE);
        if ( GetPizzaAccessible(obj)->currentRow == element->row &&
                gtk_widget_has_focus(widget) )
        {
            atk_state_set_add_state(states, ATK_STATE_FOCUSED);
        }
    }

    return states;
}

static AtkAttributeSet* element_get_attributes(AtkObject* obj)
{
    // Note that AtkObject itself doesn't implement this function.
    AtkAttributeSet* attrs = element_parent_class->get_attributes
                                ? element_parent_class->get_attributes(obj)
                                : nullptr;

    const wxPizzaAccessibleElement* const
        element = WX_PIZZA_ACCESSIBLE_ELEMENT(obj);
    wxPizzaAccessible* const acc = GetPizzaAccessible(obj);
    if ( element->row != -1 && acc )
    {
        // Use the same attributes as ARIA to let the screen readers know the
        // position of the row in the entire control, as only some of its rows
        // are children of the window.
        AtkAttribute* attr = g_new(AtkAttribute, 1);
        attr->name = g_strdup("posinset");
        attr->value = g_strdup_printf("%ld", element->row + 1);
        attrs = g_slist_prepend(attrs, attr);

        attr = g_new(AtkAttribute, 1);
        attr->name = g_strdup("setsize");
        attr->value = g_strdup_printf("%ld", acc->numRows);
        attrs = g_slist_prepend(attrs, attr);
    }

    return attrs;
}

static void element_get_extents(AtkComponent* component,
                                int* x, int* y, int* width, int* height,
                                AtkCoordType coord_type)
{
    const wxPizzaAccessibleElement* const
        element = WX_PIZZA_ACCESSIBLE_ELEMENT(component);

    *width = element->rect.width;
    *height = element->rect.height;

    GtkWidget* const widget = GetElementWidget(ATK_OBJECT(component));
    if ( !widget || !gtk_widget_is_drawable(widget) )
    {
        *x =
        *y = G_MININT;
        return;
    }

    *x = element->rect.x;
    *y = element->rect.y;

    // The window of wxPizza corresponds to the client area of wxWindow, so
    // its origin is the origin of the client coordinates.
    int xOrigin = 0,
        yOrigin = 0;
    switch ( coord_type )
    {
        case ATK_XY_SCREEN:
        case ATK_XY_WINDOW:
            {
                GdkWindow* const window = gtk_widget_get_window(widget);
                gdk_window_get_origin(window, &xOrigin, &yOrigin);

                if ( coord_type == ATK_XY_WINDOW )
                {
                    int xTLW, yTLW;
                    gdk_window_get_origin(gdk_window_get_toplevel(window),
                                          &xTLW, &yTLW);
                    xOrigin -= xTLW;
                    yOrigin -= yTLW;
                }
            }
            break;

        default:
            // This must be ATK_XY_PARENT, only available since ATK 2.30: we
            // don't bother returning the coordinates relative to the parent
            // element for the elements inside other elements, as they're
            // not really used anyhow.
            break;
    }

    *x += xOrigin;
    *y += yOrigin;
}

static void element_component_init(void* g_iface, void*)
{
    AtkComponentIface* const iface = static_cast<AtkComponentIface*>(g_iface);
    iface->get_extents = element_get_extents;
}

static void element_finalize(GObject* obj)
{
    GPtrArray* const children = WX_PIZZA_ACCESSIBLE_ELEMENT(obj)->children;
    for ( guint n = 0; n < children->len; n++ )
        DetachElement(WX_PIZZA_ACCESSIBLE_ELEMENT(g_ptr_array_index(children, n)));

    g_ptr_array_free(children, TRUE);

    G_OBJECT_CLASS(element_parent_class)->finalize(obj);
}

static void element_init(GTypeInstance* instance, void*)
{
    wxPizzaAccessibleElement* const
        element = WX_PIZZA_ACCESSIBLE_ELEMENT(instance);
    element->children = g_ptr_array_new();
    element->row = -1;
}

static void element_class_init(void* g_class, void*)
{
    G_OBJECT_CLASS(g_class)->finalize = element_finalize;

    AtkObjectClass* const klass = ATK_OBJECT_CLASS(g_class);
    klass->get_parent = element_get_parent;
    klass->get_index_in_parent = element_get_index_in_parent;
    klass->get_n_children = element_get_n_children;
    klass->ref_child = element_ref_child;
    klass->ref_state_set = element_ref_state_set;
    klass->get_attributes = element_get_attributes;

    element_parent_class = ATK_OBJECT_CLASS(g_type_class_peek_parent(g_class));
}

static gint accessible_get_n_children(AtkObject* obj)
{
    return accessible_parent_class->get_n_children(obj) +
            int(WX_PIZZA_ACCESSIBLE(obj)->elements->len);
}

static AtkObject* accessible_ref_child(AtkObject* obj, gint i)
{
    const gint numReal = accessible_parent_class->get_n_children(obj);
    if ( i < numReal )
        return accessible_parent_class->ref_child(obj, i);

    const GPtrArray* const elements = WX_PIZZA_ACCESSIBLE(obj)->elements;
    const guint n = guint(i - numReal);
    if ( n >= elements->len )
        return nullptr;

    return ATK_OBJECT(g_object_ref(g_ptr_array_index(elements, n)));
}

static AtkStateSet* accessible_ref_state_set(AtkObject* obj)
{
    AtkStateSet* const states = accessible_parent_class->ref_state_set(obj);

    // When we're used as a table, we manage the focus of our rows ourselves.
    if ( WX_PIZZA_ACCESSIBLE(obj)->numRows != -1 )
        atk_state_set_add_state(states, ATK_STATE_MANAGES_DESCENDANTS);

    return states;
}

static void accessible_finalize(GObject* obj)
{
    GPtrArray* const elements = WX_PIZZA_ACCESSIBLE(obj)->elements;
    for ( guint n = 0; n < elements->len; n++ )
        DetachElement(WX_PIZZA_ACCESSIBLE_ELEMENT(g_ptr_array_index(elements, n)));

    g_ptr_array_free(elements, TRUE);

    G_OBJECT_CLASS(accessible_parent_class)->finalize(obj);
}

static void accessible_init(GTypeInstance* instance, void*)
{
    wxPizzaAccessible* const acc = WX_PIZZA_ACCESSIBLE(instance);
    acc->elements = g_ptr_array_new();
    acc->numRows = -1;
    acc->currentRow = -1;
}

static void accessible_class_init(void* g_class, void*)
{
    G_OBJECT_CLASS(g_class)->finalize = accessible_finalize;

    AtkObjectClass* const klass = ATK_OBJECT_CLASS(g_class);
    klass->get_n_children = accessible_get_n_children;
    klass->ref_child = accessible_ref_child;
    klass->ref_state_set = accessible_ref_state_set;

    accessible_parent_class = ATK_OBJECT_CLASS(g_type_class_peek_parent(g_class));
}

static GType wxPizzaAccessibleElement_get_type()
{
    static GType type;
    if (type == 0)
    {
        const GTypeInfo info = {
            sizeof(wxPizzaAccessibleElementClass),
            nullptr, nullptr,
            element_class_init,
            nullptr, nullptr,
            sizeof(wxPizzaAccessibleElement), 0,
            element_init,
            nullptr
        };
        type = RegisterUniqueType(ATK_TYPE_OBJECT, "wxPizzaAccessibleElement", info);

        const GInterfaceInfo component_info = {
            element_component_init, nullptr, nullptr
        };
        g_type_add_interface_static(type, ATK_TYPE_COMPONENT, &component_info);
    }
    return type;
}

static GType wxPizzaAccessible_get_type()
{
    static GType type;
    if (type == 0)
    {
        const GTypeInfo info = {
            sizeof(wxPizzaAccessibleClass),
            nullptr, nullptr,
            accessible_class_init,
            nullptr, nullptr,
            sizeof(wxPizzaAccessible), 0,
            accessible_init,
            nullptr
        };
        type = RegisterUniqueType(GTK_TYPE_CONTAINER_ACCESSIBLE, "wxPizzaAccessible", info);
    }
    return type;
}

} // extern "C"

namespace
{

// Description of an element used by SyncElements() below.
struct ElementInfo
{
    ElementInfo(AtkRole role_, const wxString& label_, const wxRect& rect_)
        : role(role_), label(label_), rect(rect_)
    {
    }

    AtkRole role;
    wxString label;
    wxRect rect;
    long row = -1;
    bool selected = false;
    std::vector<ElementInfo> children;
};

// Update the element to correspond to the given info, generating the
// notifications for the changes the accessibility clients care about.
void SyncElements(AtkObject* parent, const std::vector<ElementInfo>& infos);

void UpdateElement(wxPizzaAccessibleElement* element, const ElementInfo& info)
{
    AtkObject* const obj = ATK_OBJECT(element);

    element->rect.x = info.rect.x;
    element->rect.y = info.rect.y;
    element->rect.width = info.rect.width;
    element->rect.height = info.rect.height;
    element->row = info.row;

    if ( atk_object_get_role(obj) != info.role )
        atk_object_set_role(obj, info.role);

    // Don't generate a notification if the text didn't change.
    const char* const name = atk_object_get_name(obj);
    if ( !name || wxString::FromUTF8(name) != info.label )
        atk_object_set_name(obj, info.label.utf8_str());

    if ( element->selected != info.selected )
    {
        element->selected = info.selected;
        atk_object_notify_state_change(obj, ATK_STATE_SELECTED, info.selected);
    }

    SyncElements(obj, info.children);
}

void SyncElements(AtkObject* parent, const std::vector<ElementInfo>& infos)
{
    GPtrArray* const current = GetChildElements(parent);
    const guint numReal = GetNumRealChildren(parent);
    const guint numNew = guint(infos.size());

    // Update the existing elements in place rather than replacing them, as
    // this preserves the position of the screen reader in them.
    for ( guint n = 0; n < current->len && n < numNew; n++ )
    {
        UpdateElement(WX_PIZZA_ACCESSIBLE_ELEMENT(g_ptr_array_index(current, n)),
                      infos[n]);
    }

    // Remove the extra elements, if any, starting from the end.
    while ( current->len > numNew )
    {
        const guint n = current->len - 1;

        wxPizzaAccessibleElement* const element =
            WX_PIZZA_ACCESSIBLE_ELEMENT(g_ptr_array_index(current, n));
        g_ptr_array_remove_index(current, n);

        element->container = nullptr;
        g_object_notify(G_OBJECT(element), "accessible-parent");
        g_signal_emit_by_name(parent, "children-changed::remove",
                              numReal + n, element, nullptr);

        g_object_unref(element);
    }

    // And add the new ones.
    for ( guint n = current->len; n < numNew; n++ )
    {
        wxPizzaAccessibleElement* const element = WX_PIZZA_ACCESSIBLE_ELEMENT(
            g_object_new(wxPizzaAccessibleElement_get_type(), nullptr));

        element->container = parent;
        UpdateElement(element, infos[n]);

        g_ptr_array_add(current, element);

        g_object_notify(G_OBJECT(element), "accessible-parent");
        g_signal_emit_by_name(parent, "children-changed::add",
                              numReal + n, element, nullptr);
    }
}

wxPizzaAccessible* GetWindowAccessible(wxWindow* win)
{
    GtkWidget* const widget = win->m_wxwindow;
    if ( !widget )
        return nullptr;

    AtkObject* const obj = gtk_widget_get_accessible(widget);
    if ( !WX_IS_PIZZA_ACCESSIBLE(obj) )
        return nullptr;

    return WX_PIZZA_ACCESSIBLE(obj);
}

} // anonymous namespace

void
wxPrivate::SetAccessibleElements(wxWindow* win, const AccessibleElements& elements)
{
    wxPizzaAccessible* const acc = GetWindowAccessible(win);
    if ( !acc )
        return;

    std::vector<ElementInfo> infos;
    infos.reserve(elements.size());
    for ( const auto& e : elements )
        infos.emplace_back(ATK_ROLE_LABEL, e.label, e.rect);

    SyncElements(ATK_OBJECT(acc), infos);
}

void
wxPrivate::SetAccessibleTable(wxWindow* win, const AccessibleRows& rows, long numRows)
{
    wxPizzaAccessible* const acc = GetWindowAccessible(win);
    if ( !acc )
        return;

    AtkObject* const obj = ATK_OBJECT(acc);

    // We use the list roles and not the table ones because we don't implement
    // AtkTable interface, which the screen readers expect the tables to have.
    if ( atk_object_get_role(obj) != ATK_ROLE_LIST )
        atk_object_set_role(obj, ATK_ROLE_LIST);

    acc->numRows = numRows;

    std::vector<ElementInfo> infos;
    infos.reserve(rows.size());
    for ( const auto& row : rows )
    {
        // The row label is used by the screen readers when the row is
        // focused, so it must contain the text of all of its cells.
        wxString label;
        for ( const auto& cell : row.cells )
        {
            if ( cell.label.empty() )
                continue;

            if ( !label.empty() )
                label += ", ";
            label += cell.label;
        }

        infos.emplace_back(ATK_ROLE_LIST_ITEM, label, row.rect);

        ElementInfo& info = infos.back();
        info.row = row.index;
        info.selected = row.selected;

        // Only create the cells if there is more than one of them, otherwise
        // the only cell would just duplicate the row itself.
        if ( row.cells.size() > 1 )
        {
            info.children.reserve(row.cells.size());
            for ( const auto& cell : row.cells )
                info.children.emplace_back(ATK_ROLE_LABEL, cell.label, cell.rect);
        }
    }

    SyncElements(obj, infos);
}

void
wxPrivate::SetAccessibleCurrentRow(wxWindow* win, long row)
{
    wxPizzaAccessible* const acc = GetWindowAccessible(win);
    if ( !acc || acc->currentRow == row )
        return;

    if ( AtkObject* const old = FindRowElement(acc, acc->currentRow) )
        atk_object_notify_state_change(old, ATK_STATE_FOCUSED, FALSE);

    acc->currentRow = row;

    if ( AtkObject* const current = FindRowElement(acc, row) )
    {
        atk_object_notify_state_change(current, ATK_STATE_FOCUSED, TRUE);
        g_signal_emit_by_name(acc, "active-descendant-changed", current);
    }
}

#elif defined(__WXGTK3__) && !defined(__WXGTK4__)

// Provide dummy versions if we can't implement them.
void
wxPrivate::SetAccessibleElements(wxWindow* WXUNUSED(win),
                                 const AccessibleElements& WXUNUSED(elements))
{
}

void
wxPrivate::SetAccessibleTable(wxWindow* WXUNUSED(win),
                              const AccessibleRows& WXUNUSED(rows),
                              long WXUNUSED(numRows))
{
}

void
wxPrivate::SetAccessibleCurrentRow(wxWindow* WXUNUSED(win), long WXUNUSED(row))
{
}

#endif // wxHAS_GTK_ACCESSIBLE/wxGTK3 without it

extern "C" {

struct wxPizzaClass
{
    GtkFixedClass parent;
#ifndef __WXGTK3__
    void (*set_scroll_adjustments)(GtkWidget*, GtkAdjustment*, GtkAdjustment*);
#endif
};

#ifdef __WXGTK4__
// GTK4 removed GtkWidget's "size-allocate" signal along with the GtkAllocation
// its handlers were given; only the vfunc is left, which is not something an
// outside observer can connect to. wxTopLevelWindowGTK does need to know when
// its client area has been laid out, so wxPizza provides a signal of its own,
// emitted from the vfunc below. Named distinctly on purpose: it is not GTK3's
// signal under another name and carries no allocation.
static guint gs_signalSizeAllocated;

// GTK4's size_allocate vfunc signature dropped GtkAllocation* (position is
// no longer this widget's own concern -- only its own width/height/baseline
// are, since it no longer owns a window to position) in favor of separate
// width/height/baseline parameters.
static void pizza_size_allocate(GtkWidget* widget, int width, int WXUNUSED(height), int WXUNUSED(baseline))
{
    wxPizza* pizza = WX_PIZZA(widget);
    GtkBorder border;
    pizza->get_border(border);
    int w = width - border.left - border.right;
    if (w < 0) w = 0;

    // See the KNOWN GAP comment in pizza_realize(): BORDER_STYLES decoration
    // rendering needs a real redesign under GTK4. It rests on repositioning a
    // GdkWindow of its own, and GTK4 gives a widget no window to reposition.

    // adjust child positions
    for (const GList* p = pizza->m_children; p; p = p->next)
    {
        const wxPizzaChild* child = static_cast<wxPizzaChild*>(p->data);

        // put() tracks a re-parented toplevel here without making it a child
        // at GTK level, deliberately -- see the comment there. Allocating one
        // anyway is what GTK3 tolerated and GTK4 does not: allocating a widget
        // that is not your child leaves it outside the layout manager
        // ("Unable to present ... unknown auxiliary child ... widget type
        // GtkWindow") and corrupts the CSS node tree, which GTK then aborts
        // on in gtk_css_node_validate().
        if (gtk_widget_get_parent(child->widget) != widget)
            continue;

        // Child visibility as well as visibility: a widget can live here
        // without taking part in the layout, and that is how it says so.
        // wxRendererGTK parks one such widget in the top level's client area --
        // the one it photographs to draw themed controls -- and laying it out
        // at the 1x1 it was put here with is what produced hundreds of
        // "attempt to allocate GtkText text with width -17 and height -1" in a
        // suite run: a GtkDropDown given 1x1 lays its own contents out at 1
        // minus its padding. See #250.
        //
        // Not gtk_widget_should_layout(), which asks about visibility and
        // nativeness but not about this.
        if (gtk_widget_get_visible(child->widget) &&
            gtk_widget_get_child_visible(child->widget))
        {
            pizza->size_allocate_child(
                child->widget, child->x, child->y, child->width, child->height, w);
        }
    }

    g_signal_emit(widget, gs_signalSizeAllocated, 0);
}
#else
static void pizza_size_allocate(GtkWidget* widget, GtkAllocation* alloc)
{
    wxPizza* pizza = WX_PIZZA(widget);
    GtkBorder border;
    pizza->get_border(border);
    int w = alloc->width - border.left - border.right;
    if (w < 0) w = 0;

    if (gtk_widget_get_realized(widget))
    {
        int h = alloc->height - border.top - border.bottom;
        if (h < 0) h = 0;
        const int x = alloc->x + border.left;
        const int y = alloc->y + border.top;

        GdkWindow* window = gtk_widget_get_window(widget);
        int old_x, old_y;
        gdk_window_get_position(window, &old_x, &old_y);

        if (x != old_x || y != old_y ||
            w != gdk_window_get_width(window) || h != gdk_window_get_height(window))
        {
            gdk_window_move_resize(window, x, y, w, h);

            if (border.left + border.right + border.top + border.bottom)
            {
                // old and new border areas need to be invalidated,
                // otherwise they will not be erased/redrawn properly
                GtkAllocation old_alloc;
                gtk_widget_get_allocation(widget, &old_alloc);
                GdkWindow* parent = gtk_widget_get_parent_window(widget);
                gdk_window_invalidate_rect(parent, &old_alloc, false);
                gdk_window_invalidate_rect(parent, alloc, false);
            }
        }
    }

    gtk_widget_set_allocation(widget, alloc);

    // adjust child positions
    for (const GList* p = pizza->m_children; p; p = p->next)
    {
        const wxPizzaChild* child = static_cast<wxPizzaChild*>(p->data);
        if (gtk_widget_get_visible(child->widget))
        {
            pizza->size_allocate_child(
                child->widget, child->x, child->y, child->width, child->height, w);
        }
    }
}
#endif // __WXGTK4__/!__WXGTK4__

static void pizza_realize(GtkWidget* widget)
{
    parent_class->realize(widget);

#ifndef __WXGTK4__
    wxPizza* pizza = WX_PIZZA(widget);
    if (pizza->m_windowStyle & wxPizza::BORDER_STYLES)
    {
        GtkBorder border;
        pizza->get_border(border);
        GtkAllocation a;
        gtk_widget_get_allocation(widget, &a);
        int x = a.x + border.left;
        int y = a.y + border.top;
        int w = a.width - border.left - border.right;
        int h = a.height - border.top - border.bottom;
        if (w < 0) w = 0;
        if (h < 0) h = 0;
        gdk_window_move_resize(gtk_widget_get_window(widget), x, y, w, h);
    }
#endif // !__WXGTK4__
    // See the comment in pizza_size_allocate() -- BORDER_STYLES has the
    // same known gap here, for the same reason.
}

static void pizza_show(GtkWidget* widget)
{
    GtkWidget* parent = gtk_widget_get_parent(widget);
    if (parent && (WX_PIZZA(widget)->m_windowStyle & wxPizza::BORDER_STYLES))
    {
        // invalidate whole allocation so borders will be drawn properly
#ifdef __WXGTK4__
        // gtk_widget_queue_draw_area() (partial-rect invalidation) doesn't
        // exist under GTK4; queue_draw() invalidates the whole parent
        // instead, which is correct if less targeted.
        gtk_widget_queue_draw(parent);
#else
        GtkAllocation a;
        gtk_widget_get_allocation(widget, &a);
        gtk_widget_queue_draw_area(parent, a.x, a.y, a.width, a.height);
#endif
    }

    parent_class->show(widget);
}

static void pizza_hide(GtkWidget* widget)
{
    GtkWidget* parent = gtk_widget_get_parent(widget);
    if (parent && (WX_PIZZA(widget)->m_windowStyle & wxPizza::BORDER_STYLES))
    {
        // invalidate whole allocation so borders will be erased properly
#ifdef __WXGTK4__
        gtk_widget_queue_draw(parent);
#else
        GtkAllocation a;
        gtk_widget_get_allocation(widget, &a);
        gtk_widget_queue_draw_area(parent, a.x, a.y, a.width, a.height);
#endif
    }

    parent_class->hide(widget);
}

// GtkContainer, and with it GtkContainerClass::add/remove, doesn't exist
// under GTK4 -- nothing can call gtk_container_add()/remove() on a wxPizza
// generically any more (the only way to add/remove a child is through
// wxPizza's own put()/RemoveChild(), which already do this bookkeeping
// directly), so these vfunc overrides have no GTK4 equivalent to provide,
// not just a missing API to shim.
#ifndef __WXGTK4__
static void pizza_add(GtkContainer* container, GtkWidget* widget)
{
    WX_PIZZA(container)->put(widget, 0, 0, 1, 1);
}

static void pizza_remove(GtkContainer* container, GtkWidget* widget)
{
    GTK_CONTAINER_CLASS(parent_class)->remove(container, widget);

    wxPizza* pizza = WX_PIZZA(container);
    for (GList* p = pizza->m_children; p; p = p->next)
    {
        wxPizzaChild* child = static_cast<wxPizzaChild*>(p->data);
        if (child->widget == widget)
        {
            pizza->m_children = g_list_delete_link(pizza->m_children, p);
            delete child;
            break;
        }
    }
}
#endif // !__WXGTK4__

#ifdef __WXGTK3__
// Get preferred size of children, to avoid GTK+ warnings complaining
// that they were size-allocated without asking their preferred size
static void children_get_preferred_size(const GList* p)
{
    for (; p; p = p->next)
    {
        const wxPizzaChild* child = static_cast<wxPizzaChild*>(p->data);
        if (gtk_widget_get_visible(child->widget))
        {
            GtkRequisition req;
            gtk_widget_get_preferred_size(child->widget, &req, nullptr);
        }
    }
}

#ifdef __WXGTK4__
// GTK4 merged get_preferred_width/height and adjust_size_request into one
// measure() vfunc. The GtkToolItem special case in the old
// pizza_adjust_size_request() below is gone here because GtkToolItem
// itself doesn't exist under GTK4 (see toolbar.cpp's deferred
// GtkToolbar/GtkToolItem redesign in docs/gtk/gtk4-status.md) -- there is
// currently no way for a wxPizza to be inside one, so always reporting a
// zero minimum (the common case in the GTK3 code below) is correct as-is.
static void pizza_measure(GtkWidget* widget, GtkOrientation orientation, int /* for_size */,
                           int* minimum, int* natural, int* minimum_baseline, int* natural_baseline)
{
    children_get_preferred_size(WX_PIZZA(widget)->m_children);
    *minimum = 0;
    int w = -1, h = -1;
    gtk_widget_get_size_request(widget, &w, &h);
    *natural = orientation == GTK_ORIENTATION_HORIZONTAL ? w : h;
    if (*natural < 0)
        *natural = 0;
    if (minimum_baseline)
        *minimum_baseline = -1;
    if (natural_baseline)
        *natural_baseline = -1;
}
#else
static void pizza_get_preferred_width(GtkWidget* widget, int* minimum, int* natural)
{
    children_get_preferred_size(WX_PIZZA(widget)->m_children);
    *minimum = 0;
    gtk_widget_get_size_request(widget, natural, nullptr);
    if (*natural < 0)
        *natural = 0;
}

static void pizza_get_preferred_height(GtkWidget* widget, int* minimum, int* natural)
{
    children_get_preferred_size(WX_PIZZA(widget)->m_children);
    *minimum = 0;
    gtk_widget_get_size_request(widget, nullptr, natural);
    if (*natural < 0)
        *natural = 0;
}

static void pizza_adjust_size_request(GtkWidget* widget, GtkOrientation orientation, int* minimum, int* natural)
{
    parent_class->adjust_size_request(widget, orientation, minimum, natural);
    // Override adjustments to minimum size. GtkWidgetClass.adjust_size_request()
    // will use the size request, if set, as the minimum.
    // But don't override if in a GtkToolbar, it uses the minimum as actual size.
    GtkWidget* parent = gtk_widget_get_parent(widget);
    if (!GTK_IS_TOOL_ITEM(parent))
        *minimum = 0;
}
#endif // __WXGTK4__/!__WXGTK4__

// GtkScrollable interface
static void pizza_get_property(GObject*, guint property_id, GValue* value, GParamSpec*)
{
    if (property_id == PROP_HSCROLL_POLICY || property_id == PROP_VSCROLL_POLICY)
    {
        // Use natural size, rather than minimum, as virtual size
        g_value_set_enum(value, GTK_SCROLL_NATURAL);
    }
}

static void pizza_set_property(GObject*, guint, const GValue*, GParamSpec*)
{
}
#else
// not used, but needs to exist so gtk_widget_set_scroll_adjustments will work
static void pizza_set_scroll_adjustments(GtkWidget*, GtkAdjustment*, GtkAdjustment*)
{
}

// Marshaller needed for set_scroll_adjustments signal,
// generated with GLib-2.4.6 glib-genmarshal
#define g_marshal_value_peek_object(v)   g_value_get_object (v)
static void
g_cclosure_user_marshal_VOID__OBJECT_OBJECT (GClosure     *closure,
                                             GValue       * /*return_value*/,
                                             guint         n_param_values,
                                             const GValue *param_values,
                                             gpointer      /*invocation_hint*/,
                                             gpointer      marshal_data)
{
  typedef void (*GMarshalFunc_VOID__OBJECT_OBJECT) (gpointer     data1,
                                                    gpointer     arg_1,
                                                    gpointer     arg_2,
                                                    gpointer     data2);
  GMarshalFunc_VOID__OBJECT_OBJECT callback;
  GCClosure *cc = (GCClosure*) closure;
  gpointer data1, data2;

  g_return_if_fail (n_param_values == 3);

  if (G_CCLOSURE_SWAP_DATA (closure))
    {
      data1 = closure->data;
      data2 = g_value_peek_pointer (param_values + 0);
    }
  else
    {
      data1 = g_value_peek_pointer (param_values + 0);
      data2 = closure->data;
    }
  callback = (GMarshalFunc_VOID__OBJECT_OBJECT) (marshal_data ? marshal_data : cc->callback);

  callback (data1,
            g_marshal_value_peek_object (param_values + 1),
            g_marshal_value_peek_object (param_values + 2),
            data2);
}
#endif

#ifdef __WXGTK4__
// GTK4 replaced the "draw" signal with a snapshot vfunc building render nodes.
// wx paints with cairo throughout, so rather than rewrite every wxDC operation
// onto render nodes, take the cairo escape hatch: gtk_snapshot_append_cairo()
// hands back a real cairo_t, and measurement confirms it is in widget-relative
// coordinates, exactly as the GTK3 draw vfunc was for a windowless widget --
// so everything downstream of GTKSendPaintEvents() is unaffected. See
// docs/gtk/gtk4-phase4-paint-model-design.md.
//
// A vfunc carries no user data where the signal carried the wxWindow, so the
// owner is looked up from the widget; wxWindowGTK sets it when it would
// previously have connected the signal.
static void pizza_snapshot(GtkWidget* widget, GtkSnapshot* snapshot)
{
    // Frozen by wxWindow::Freeze(): paint nothing at all, neither this
    // widget's content nor its children, until thawed.
    if ( g_object_get_data(G_OBJECT(widget), "wx-frozen") != nullptr )
        return;

    wxWindow* const win = static_cast<wxWindow*>(
        g_object_get_data(G_OBJECT(widget), "wx-pizza-owner"));

    const int w = gtk_widget_get_width(widget);
    const int h = gtk_widget_get_height(widget);

    if ( win && w > 0 && h > 0 )
    {
        graphene_rect_t bounds;
        bounds.origin.x = 0;
        bounds.origin.y = 0;
        bounds.size.width = float(w);
        bounds.size.height = float(h);

        cairo_t* const cr = gtk_snapshot_append_cairo(snapshot, &bounds);
        win->GTKSendPaintEvents(cr);
        cairo_destroy(cr);
    }

    // Children are no longer drawn by chaining up to a parent draw handler:
    // each has to be snapshotted explicitly.
    for ( GtkWidget* child = gtk_widget_get_first_child(widget);
          child != nullptr;
          child = gtk_widget_get_next_sibling(child) )
    {
        // A child which has not been allocated yet -- shown between its
        // parent's last size_allocate and the next one -- has nothing to draw,
        // and asking GTK to draw it anyway earns a warning for every frame it
        // stays in that state. It is drawn from the first frame after it has
        // been given a size.
        //
        // Deciding this from the size is deliberate: gtk_widget_compute_bounds()
        // and gtk_widget_get_realized() were both tried here and caught exactly
        // the same children, at more cost per child per frame.
        if ( gtk_widget_get_width(child) <= 0 ||
                gtk_widget_get_height(child) <= 0 )
            continue;

        gtk_widget_snapshot_child(widget, child, snapshot);
    }
}
#endif // __WXGTK4__

#ifdef __WXGTK4__
// Under GTK3 the m_children entries were freed by pizza_remove() as
// GtkContainer tore the children down. With no such vfunc under GTK4 the list
// has to be released here, or it leaks one wxPizzaChild per child every time a
// wxPizza is destroyed.
//
// GtkFixed unparents the remaining children itself when it is disposed, so
// this deliberately only drops wx's own bookkeeping; freeing it before
// chaining up means nothing can walk a list of children that are on their way
// out.
static void pizza_dispose(GObject* object)
{
    wxPizza* const pizza = WX_PIZZA(object);

    for (GList* p = pizza->m_children; p; p = p->next)
        delete static_cast<wxPizzaChild*>(p->data);

    g_list_free(pizza->m_children);
    pizza->m_children = nullptr;

    G_OBJECT_CLASS(parent_class)->dispose(object);
}
#endif // __WXGTK4__

static void class_init(void* g_class, void*)
{
    GtkWidgetClass* widget_class = (GtkWidgetClass*)g_class;
    widget_class->size_allocate = pizza_size_allocate;
    widget_class->realize = pizza_realize;
    widget_class->show = pizza_show;
    widget_class->hide = pizza_hide;
#if defined(__WXGTK4__) && wxUSE_ACCESSIBILITY
    pizza_set_default_accessible_role(widget_class);
#endif
#ifndef __WXGTK4__
    // GtkContainerClass doesn't exist under GTK4 -- see the comment above
    // pizza_add()/pizza_remove().
    GtkContainerClass* container_class = (GtkContainerClass*)g_class;
    container_class->add = pizza_add;
    container_class->remove = pizza_remove;
#endif

#ifdef __WXGTK4__
    widget_class->measure = pizza_measure;
    widget_class->snapshot = pizza_snapshot;

    // Use the exported constant rather than repeating the name: window.cpp and
    // toplevel.cpp connect through it, and a silent divergence would cost every
    // window its wxEVT_SIZE without any diagnostic at build time.
    gs_signalSizeAllocated = g_signal_new(wxPIZZA_SIGNAL_SIZE_ALLOCATED,
        G_TYPE_FROM_CLASS(g_class), G_SIGNAL_RUN_LAST, 0,
        nullptr, nullptr, nullptr, G_TYPE_NONE, 0);

    GObjectClass *gobject_class = G_OBJECT_CLASS(g_class);
    gobject_class->set_property = pizza_set_property;
    gobject_class->get_property = pizza_get_property;
    gobject_class->dispose = pizza_dispose;
    g_object_class_override_property(gobject_class, PROP_HADJUSTMENT, "hadjustment");
    g_object_class_override_property(gobject_class, PROP_VADJUSTMENT, "vadjustment");
    g_object_class_override_property(gobject_class, PROP_HSCROLL_POLICY, "hscroll-policy");
    g_object_class_override_property(gobject_class, PROP_VSCROLL_POLICY, "vscroll-policy");
#elif defined(__WXGTK3__)
    widget_class->get_preferred_width = pizza_get_preferred_width;
    widget_class->get_preferred_height = pizza_get_preferred_height;
    widget_class->adjust_size_request = pizza_adjust_size_request;
    GObjectClass *gobject_class = G_OBJECT_CLASS(g_class);
    gobject_class->set_property = pizza_set_property;
    gobject_class->get_property = pizza_get_property;
    g_object_class_override_property(gobject_class, PROP_HADJUSTMENT, "hadjustment");
    g_object_class_override_property(gobject_class, PROP_VADJUSTMENT, "vadjustment");
    g_object_class_override_property(gobject_class, PROP_HSCROLL_POLICY, "hscroll-policy");
    g_object_class_override_property(gobject_class, PROP_VSCROLL_POLICY, "vscroll-policy");
#else
    wxPizzaClass* klass = static_cast<wxPizzaClass*>(g_class);
    // needed to make widget appear scrollable to GTK+
    klass->set_scroll_adjustments = pizza_set_scroll_adjustments;
    widget_class->set_scroll_adjustments_signal =
        g_signal_new(
            "set_scroll_adjustments",
            G_TYPE_FROM_CLASS(g_class),
            G_SIGNAL_RUN_LAST,
            G_STRUCT_OFFSET(wxPizzaClass, set_scroll_adjustments),
            nullptr, nullptr,
            g_cclosure_user_marshal_VOID__OBJECT_OBJECT,
            G_TYPE_NONE, 2, GTK_TYPE_ADJUSTMENT, GTK_TYPE_ADJUSTMENT);
#endif
#ifdef wxHAS_GTK_ACCESSIBLE
    gtk_widget_class_set_accessible_type(widget_class, wxPizzaAccessible_get_type());
#endif

    parent_class = GTK_WIDGET_CLASS(g_type_class_peek_parent(g_class));
}

} // extern "C"

#ifdef __WXGTK4__
// "extern" is what makes the visibility attribute apply here: without it a
// const object at namespace scope is not yet known to have external linkage
// when GCC processes the attribute, so GCC drops it and warns. The exported
// symbol came out right anyway, because the declaration in win_gtk.h carries
// the same attribute, but the warning appeared in every build.
//
// The initialiser is a constant expression, so this is initialised before any
// code runs and class_init() above may use it despite coming first.
extern WXDLLIMPEXP_DATA_CORE(const char* const)
wxPIZZA_SIGNAL_SIZE_ALLOCATED = "wx-size-allocated";
#endif // __WXGTK4__

GType wxPizza::type()
{
    static GType type;
    if (type == 0)
    {
        const char* name = "wxPizza";
        char buf[30];
        for (unsigned i = 0; g_type_from_name(name); i++)
        {
            g_snprintf(buf, sizeof(buf), "wxPizza%u", i);
            name = buf;
        }
        const GTypeInfo info = {
            sizeof(wxPizzaClass),
            nullptr, nullptr,
            class_init,
            nullptr, nullptr,
            sizeof(wxPizza), 0,
            nullptr, nullptr
        };
        type = g_type_register_static(
            GTK_TYPE_FIXED, name, &info, GTypeFlags(0));
#ifdef __WXGTK3__
        const GInterfaceInfo interface_info = { nullptr, nullptr, nullptr };
        g_type_add_interface_static(type, GTK_TYPE_SCROLLABLE, &interface_info);
#endif
#if defined(__WXGTK4__) && wxUSE_ACCESSIBILITY
        const GInterfaceInfo accessible_info =
            { pizza_accessible_init, nullptr, nullptr };
        g_type_add_interface_static(type, GTK_TYPE_ACCESSIBLE, &accessible_info);
#endif
    }
    return type;
}

GtkWidget* wxPizza::New(long windowStyle)
{
    GtkWidget* widget = GTK_WIDGET(g_object_new(type(), nullptr));
    wxPizza* pizza = WX_PIZZA(widget);
    pizza->m_children = nullptr;
    pizza->m_scroll_x = 0;
    pizza->m_scroll_y = 0;
    pizza->m_windowStyle = windowStyle;
#ifdef __WXGTK4__
    // GtkFixed installs a GtkFixedLayout layout manager, and GTK4 dispatches
    // both measure() and size_allocate() to a widget's layout manager *instead
    // of* to its class vfuncs when it has one. So wxPizza's own layout code --
    // pizza_measure() and pizza_size_allocate(), which is what positions every
    // wx child window -- was never being called at all, and GtkFixedLayout was
    // laying children out instead: at the origin, at their own measured size,
    // which for a wxPizza with no natural size of its own is 0x0.
    //
    // Nothing about this is diagnosable from the code: the vfuncs are assigned
    // in class_init exactly as under GTK3, and GTK simply never asks for them.
    // See docs/gtk/probes/gtk4-layout-manager.c.
    //
    // wx does all of its own layout, so it wants no layout manager. put()
    // below therefore cannot use gtk_fixed_put() either, as that reaches
    // through the layout manager to set the child's transform.
    gtk_widget_set_layout_manager(widget, nullptr);

    // Neither gtk_widget_set_has_window() nor event masks exist under
    // GTK4: no widget (other than a toplevel's implicit surface) has its
    // own window any more, and event delivery goes entirely through
    // GtkEventController objects instead of enabling raw event types via
    // a mask -- see docs/gtk/gtk4-phase3-input-model-design.md, not yet
    // implemented.
#elif defined(__WXGTK3__)
    gtk_widget_set_has_window(widget, true);
    gtk_widget_add_events(widget,
        GDK_EXPOSURE_MASK |
        GDK_SCROLL_MASK |
#if GTK_CHECK_VERSION(3,4,0)
        GDK_SMOOTH_SCROLL_MASK |
#endif
        GDK_POINTER_MOTION_MASK |
        GDK_POINTER_MOTION_HINT_MASK |
        GDK_BUTTON_MOTION_MASK |
        GDK_BUTTON1_MOTION_MASK |
        GDK_BUTTON2_MOTION_MASK |
        GDK_BUTTON3_MOTION_MASK |
        GDK_BUTTON_PRESS_MASK |
        GDK_BUTTON_RELEASE_MASK |
        GDK_KEY_PRESS_MASK |
        GDK_KEY_RELEASE_MASK |
        GDK_ENTER_NOTIFY_MASK |
        GDK_LEAVE_NOTIFY_MASK |
        GDK_FOCUS_CHANGE_MASK);
#else
    gtk_fixed_set_has_window(GTK_FIXED(widget), true);
    gtk_widget_add_events(widget,
        GDK_EXPOSURE_MASK |
        GDK_SCROLL_MASK |
        GDK_POINTER_MOTION_MASK |
        GDK_POINTER_MOTION_HINT_MASK |
        GDK_BUTTON_MOTION_MASK |
        GDK_BUTTON1_MOTION_MASK |
        GDK_BUTTON2_MOTION_MASK |
        GDK_BUTTON3_MOTION_MASK |
        GDK_BUTTON_PRESS_MASK |
        GDK_BUTTON_RELEASE_MASK |
        GDK_KEY_PRESS_MASK |
        GDK_KEY_RELEASE_MASK |
        GDK_ENTER_NOTIFY_MASK |
        GDK_LEAVE_NOTIFY_MASK |
        GDK_FOCUS_CHANGE_MASK);
#endif // __WXGTK4__/__WXGTK3__/!__WXGTK3__
    return widget;
}

void wxPizza::move(GtkWidget* widget, int x, int y, int width, int height)
{
    for (const GList* p = m_children; p; p = p->next)
    {
        wxPizzaChild* child = static_cast<wxPizzaChild*>(p->data);
        if (child->widget == widget)
        {
            child->x = x;
            child->y = y;
            child->width = width;
            child->height = height;
            // normally a queue-resize would be needed here, but we know
            // wxWindowGTK::DoMoveWindow() will take care of it
            break;
        }
    }
}

void wxPizza::size_allocate_child(
    GtkWidget* child, int x, int y, int width, int height, int parent_width)
{
    if (width <= 0 || height <= 0)
    {
#ifdef __WXGTK4__
        // Returning without allocating leaves GTK4 believing the child still
        // needs one, while gtk_widget_get_width() goes on reporting whatever
        // it was given last time. pizza_snapshot() then sees a child with a
        // size and draws it, and GTK warns "Trying to snapshot ... without a
        // current allocation" for every frame the child stays that way -- all
        // 37 of the ones left after b7e4c4d are this, a wxAUI pane wx has
        // sized to zero width but GTK still thinks is 3x27.
        //
        // Give it the size wx actually asked for. GTK then knows the child is
        // laid out, its reported size is the truth, and the guard in
        // pizza_snapshot() skips it like any other empty child.
        GtkAllocation empty;
        empty.x = x - m_scroll_x;
        empty.y = y - m_scroll_y;
        empty.width = 0;
        empty.height = 0;
        gtk_widget_size_allocate(child, &empty, -1);
#endif // __WXGTK4__
        return;
    }

    GtkAllocation child_alloc;
    // note that child positions do not take border into account, they need to
    // be relative to widget->window, which has already been adjusted
    child_alloc.x = x - m_scroll_x;
    child_alloc.y = y - m_scroll_y;
    child_alloc.width  = width;
    child_alloc.height = height;
    if (gtk_widget_get_direction(GTK_WIDGET(this)) == GTK_TEXT_DIR_RTL)
    {
        if (parent_width < 0)
        {
            GtkBorder border;
            get_border(border);
            GtkAllocation alloc;
            gtk_widget_get_allocation(GTK_WIDGET(this), &alloc);
            parent_width = alloc.width - border.left - border.right;
        }
        child_alloc.x = parent_width - child_alloc.x - child_alloc.width;
    }
#ifdef __WXGTK4__
    // gtk_widget_size_allocate() gained a baseline parameter under GTK4;
    // -1 means "no baseline alignment", matching the previous behavior.
    gtk_widget_size_allocate(child, &child_alloc, -1);
#else
    gtk_widget_size_allocate(child, &child_alloc);
#endif
}

void wxPizza::put(GtkWidget* widget, int x, int y, int width, int height)
{
    // Re-parenting a TLW under a child window is possible at wx level but
    // using a TLW as child at GTK+ level results in problems, so don't do it.
#ifdef __WXGTK4__
    // gtk_widget_is_toplevel() doesn't exist under GTK4; GTK_IS_WINDOW()
    // is the direct equivalent for "is this a toplevel-capable widget".
    if (!GTK_IS_WINDOW(widget))
    {
        // Not gtk_fixed_put(): it asks the layout manager for the child's
        // GtkFixedLayoutChild in order to set a transform, and wxPizza has no
        // layout manager, for the reason given in New(). Parenting the child
        // is all that is wanted here in any case -- its position and size come
        // from size_allocate_child().
        gtk_widget_set_parent(widget, GTK_WIDGET(this));
        gtk_widget_set_size_request(widget, -1, -1);
    }
#else
    if (!gtk_widget_is_toplevel(GTK_WIDGET(widget)))
    {
        gtk_fixed_put(GTK_FIXED(this), widget, 0, 0);
        gtk_widget_set_size_request(widget, -1, -1);
    }
#endif

    wxPizzaChild* child = new wxPizzaChild;
    child->widget = widget;
    child->x = x;
    child->y = y;
    child->width = width;
    child->height = height;
    m_children = g_list_append(m_children, child);
}

#ifdef __WXGTK4__
// The counterpart to put(), which GTK3 got for free: GtkContainer's "remove"
// vfunc (pizza_remove() above) fired whenever a child left, and kept
// m_children in step.
//
// GTK4 has no GtkContainer and so no such vfunc, and nothing was maintaining
// m_children at all: every child that went away left behind an entry pointing
// at a freed GtkWidget, which pizza_size_allocate(), pizza_measure() and
// pizza_snapshot() then walked. gtk_widget_unparent() also bypasses
// gtk_fixed_remove(), so GtkFixed's own layout-child bookkeeping was left
// stale in the same way -- the two together showed up as
// "unknown auxiliary child surface" from the layout manager and eventually as
// GTK aborting inside gtk_css_node_validate().
void wxPizza::remove(GtkWidget* widget)
{
    for (GList* p = m_children; p; p = p->next)
    {
        wxPizzaChild* const child = static_cast<wxPizzaChild*>(p->data);
        if (child->widget == widget)
        {
            m_children = g_list_delete_link(m_children, p);
            delete child;
            break;
        }
    }

    // put() does not parent toplevels, so this is not redundant.
    if (gtk_widget_get_parent(widget) == GTK_WIDGET(this))
    {
        // The window has to be told before the widget goes: see
        // wx_gtk_widget_forget_in_root().
        wx_gtk_widget_forget_in_root(widget);

        // The counterpart of the gtk_widget_set_parent() in put():
        // gtk_fixed_remove() would go through the layout manager wxPizza
        // deliberately does not have.
        gtk_widget_unparent(widget);
    }
}
#endif // __WXGTK4__

#ifndef __WXGTK4__
struct AdjustData {
    GdkWindow* window;
    int dx, dy;
};

// Adjust allocations for all widgets using the GdkWindow which was just scrolled
extern "C" {
static void scroll_adjust(GtkWidget* widget, void* data)
{
    if (!gtk_widget_get_visible(widget))
        return;

    const AdjustData* p = static_cast<AdjustData*>(data);
    GtkAllocation a;
    gtk_widget_get_allocation(widget, &a);
    a.x += p->dx;
    a.y += p->dy;
    gtk_widget_set_allocation(widget, &a);

    if (gtk_widget_get_window(widget) == p->window)
    {
        // GtkFrame requires a queue_resize, otherwise parts of
        // the frame newly exposed by the scroll are not drawn.
        // To be safe, do it for all widgets.
        gtk_widget_queue_resize_no_redraw(widget);
        if (GTK_IS_CONTAINER(widget))
            gtk_container_forall(GTK_CONTAINER(widget), scroll_adjust, data);
    }
}
}
#endif // !__WXGTK4__

void wxPizza::scroll(int dx, int dy)
{
    GtkWidget* widget = GTK_WIDGET(this);
#ifndef __WXGTK3__
    if (gtk_widget_get_direction(widget) == GTK_TEXT_DIR_RTL)
        dx = -dx;
#endif
    m_scroll_x -= dx;
    m_scroll_y -= dy;
#ifdef __WXGTK4__
    // No more low-level pixel-blit scrolling under GTK4: GdkWindow and
    // gdk_window_scroll() don't exist any more, and there's no direct
    // replacement (compositing handles this differently now). A normal
    // re-allocate is enough to get correct (if not blit-optimized)
    // behavior, since size_allocate_child() above already positions
    // every child from m_scroll_x/m_scroll_y on every allocation pass.
    gtk_widget_queue_allocate(widget);
    gtk_widget_queue_draw(widget);
#else
    GdkWindow* window = gtk_widget_get_window(widget);
    if (window)
    {
        gdk_window_scroll(window, dx, dy);
        // Adjust child allocations. Doing a queue_resize on the children is not
        // enough, sometimes they redraw in the wrong place during fast scrolling.
        AdjustData data = { window, dx, dy };
        gtk_container_forall(GTK_CONTAINER(widget), scroll_adjust, &data);
    }
#endif // __WXGTK4__/!__WXGTK4__
}

void wxPizza::get_border(GtkBorder& border)
{
#ifndef __WXUNIVERSAL__
    if (m_windowStyle & wxBORDER_SIMPLE)
        border.left = border.right = border.top = border.bottom = 1;
    else if (m_windowStyle & (wxBORDER_RAISED | wxBORDER_SUNKEN | wxBORDER_THEME))
    {
#ifdef __WXGTK4__
        // Only the border is wanted here -- this is the thickness of the
        // frame wx draws, so adding the padding to it would draw too thick a
        // one. See stylecontext.h for how it is measured now that
        // gtk_style_context_get_border() is gone.
        wxGTKGetStyleMetrics(
            m_windowStyle & (wxHSCROLL | wxVSCROLL)
                ? wxGTKPrivate::GetTreeWidget()
                : wxGTKPrivate::GetEntryWidget(),
            nullptr, &border);
#elif defined(__WXGTK3__)
        GtkStyleContext* sc;
        if (m_windowStyle & (wxHSCROLL | wxVSCROLL))
            sc = gtk_widget_get_style_context(wxGTKPrivate::GetTreeWidget());
        else
            sc = gtk_widget_get_style_context(wxGTKPrivate::GetEntryWidget());

        gtk_style_context_set_state(sc, GTK_STATE_FLAG_NORMAL);
        gtk_style_context_get_border(sc, GTK_STATE_FLAG_NORMAL, &border);
#else // !__WXGTK3__
        GtkStyle* style;
        if (m_windowStyle & (wxHSCROLL | wxVSCROLL))
            style = gtk_widget_get_style(wxGTKPrivate::GetTreeWidget());
        else
            style = gtk_widget_get_style(wxGTKPrivate::GetEntryWidget());

        border.left = border.right = style->xthickness;
        border.top = border.bottom = style->ythickness;
#endif // !__WXGTK3__
    }
    else
#endif // !__WXUNIVERSAL__
    {
        border.left = border.right = border.top = border.bottom = 0;
    }
}
