/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */

/* Metacity window frame manager widget */

/* 
 * Copyright (C) 2001 Havoc Pennington
 * Copyright (C) 2003 Red Hat, Inc.
 * Copyright (C) 2005, 2006 Elijah Newren
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <config.h>
#include <math.h>
#include <string.h>
#include <meta/boxes.h>
#include "uiframe.h"
#include <meta/util.h>
#include "core.h"
#include "menu.h"
#include <meta/theme.h>
#include <meta/prefs.h>
#include "ui.h"

#include "theme-private.h"

#ifdef HAVE_SHAPE
#include <X11/extensions/shape.h>
#endif

static MetaFrameControl get_control  (MetaUIFrame       *frame,
                                      int                x,
                                      int                y);

G_DEFINE_TYPE (MetaUIFrame, meta_uiframe, GTK_TYPE_WINDOW);

static void
meta_uiframe_finalize (GObject *obj)
{
  MetaUIFrame *frame = META_UIFRAME (obj);

  if (frame->window)
    g_object_unref (frame->window);
}

static void
close_button_clicked (GtkWidget *widget,
                      gpointer   user_data)
{
  MetaUIFrame *frame = META_UIFRAME (user_data);
  Display *display = GDK_DISPLAY_XDISPLAY (gtk_widget_get_display (GTK_WIDGET (frame)));

  meta_core_delete (display, frame->xwindow, gtk_get_current_event_time ());
}

static void
meta_uiframe_init (MetaUIFrame *frame)
{
  GtkWidget *container, *label, *close;

  frame->container = container = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  frame->label = label = gtk_label_new ("");

  /* FIXME: respect button layout */
  close = gtk_button_new_with_label ("X");
  g_signal_connect (close, "clicked", G_CALLBACK (close_button_clicked), frame);

  gtk_container_add (GTK_CONTAINER (frame), container);
  gtk_container_add (GTK_CONTAINER (container), frame->label);
  gtk_container_add (GTK_CONTAINER (container), close);

  gtk_widget_set_hexpand (container, TRUE);
  gtk_widget_set_hexpand (label, TRUE);
  gtk_widget_set_halign (label, GTK_ALIGN_CENTER);
  gtk_widget_set_valign (label, GTK_ALIGN_START);
  gtk_widget_set_halign (close, GTK_ALIGN_END);
  gtk_widget_set_valign (close, GTK_ALIGN_START);

  gtk_widget_show_all (GTK_WIDGET (container));
}

/* In order to use a style with a window it has to be attached to that
 * window. Actually, the colormaps just have to match, but since GTK+
 * already takes care of making sure that its cheap to attach a style
 * to multiple windows with the same colormap, we can just go ahead
 * and attach separately for each window.
 */
void
meta_uiframe_attach_style (MetaUIFrame *frame)
{
  char *theme_name;
  char *variant = NULL;
  GtkStyleContext *style_context;
  GtkSettings *settings;

  frame->theme = meta_theme_get_current ();

  meta_core_get (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()),
                 frame->xwindow,
                 META_CORE_GET_THEME_VARIANT, &variant,
                 META_CORE_GET_END);

  settings = gtk_widget_get_settings (GTK_WIDGET (frame));
  g_object_get (settings,
                "gtk-theme-name", &theme_name,
                NULL);

  /* XXX - what to do with no theme name? */
  if (theme_name == NULL || *theme_name == '\0')
    return;

  style_context = gtk_widget_get_style_context (GTK_WIDGET (frame));

  gtk_style_context_invalidate (style_context);

  if (g_strcmp0 (frame->variant, variant) == 0)
    return;

  g_free (frame->variant);
  frame->variant = g_strdup (variant);

  /* This is a little silly. We have to keep the provider around,
   * as GTK+ doesn't allow us to do our own thing. */
  if (frame->provider != NULL)
    gtk_style_context_remove_provider (style_context, frame->provider);

  frame->provider = GTK_STYLE_PROVIDER (gtk_css_provider_get_named (theme_name, variant));

  if (frame->provider != NULL)
    gtk_style_context_add_provider (style_context, frame->provider,
                                    GTK_STYLE_PROVIDER_PRIORITY_THEME);
}

void
meta_uiframe_calc_geometry (MetaUIFrame       *frame,
                            MetaFrameGeometry *fgeom)
{
  int width, height;
  MetaFrameFlags flags;
  MetaFrameType type;
  MetaButtonLayout button_layout;
  
  meta_core_get (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), frame->xwindow,
                 META_CORE_GET_CLIENT_WIDTH, &width,
                 META_CORE_GET_CLIENT_HEIGHT, &height,
                 META_CORE_GET_FRAME_FLAGS, &flags,
                 META_CORE_GET_FRAME_TYPE, &type,
                 META_CORE_GET_END);

  meta_prefs_get_button_layout (&button_layout);
  
  meta_theme_calc_geometry (frame->theme,
                            gtk_widget_get_style_context (GTK_WIDGET (frame)),
                            type,
                            flags,
                            width, height,
                            &button_layout,
                            fgeom);
}

/* The client rectangle surrounds client window; it subtracts both
 * the visible and invisible borders from the frame window's size.
 */
static void
get_client_rect (MetaFrameGeometry     *fgeom,
                 int                    window_width,
                 int                    window_height,
                 cairo_rectangle_int_t *rect)
{
  rect->x = fgeom->borders.total.left;
  rect->y = fgeom->borders.total.top;
  rect->width = window_width - fgeom->borders.total.right - rect->x;
  rect->height = window_height - fgeom->borders.total.bottom - rect->y;
}

void
meta_uiframe_set_title (MetaUIFrame *frame,
                        const char  *title)
{
  gtk_label_set_text (GTK_LABEL (frame->label), title);
}

static gboolean
meta_frame_titlebar_event (MetaUIFrame    *frame,
                           GdkEventButton *event,
                           int            action)
{
  MetaFrameFlags flags;
  Display *display;

  display = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
  
  switch (action)
    {
    case G_DESKTOP_TITLEBAR_ACTION_TOGGLE_SHADE:
      {
        meta_core_get (display, frame->xwindow,
                       META_CORE_GET_FRAME_FLAGS, &flags,
                       META_CORE_GET_END);
        
        if (flags & META_FRAME_ALLOWS_SHADE)
          {
            if (flags & META_FRAME_SHADED)
              meta_core_unshade (display,
                                 frame->xwindow,
                                 event->time);
            else
              meta_core_shade (display,
                               frame->xwindow,
                               event->time);
          }
      }
      break;          
      
    case G_DESKTOP_TITLEBAR_ACTION_TOGGLE_MAXIMIZE:
      {
        meta_core_get (display, frame->xwindow,
                       META_CORE_GET_FRAME_FLAGS, &flags,
                       META_CORE_GET_END);
        
        if (flags & META_FRAME_ALLOWS_MAXIMIZE)
          {
            meta_core_toggle_maximize (display, frame->xwindow);
          }
      }
      break;

    case G_DESKTOP_TITLEBAR_ACTION_TOGGLE_MAXIMIZE_HORIZONTALLY:
      {
        meta_core_get (display, frame->xwindow,
                       META_CORE_GET_FRAME_FLAGS, &flags,
                       META_CORE_GET_END);
        
        if (flags & META_FRAME_ALLOWS_MAXIMIZE)
          {
            meta_core_toggle_maximize_horizontally (display, frame->xwindow);
          }
      }
      break;

    case G_DESKTOP_TITLEBAR_ACTION_TOGGLE_MAXIMIZE_VERTICALLY:
      {
        meta_core_get (display, frame->xwindow,
                       META_CORE_GET_FRAME_FLAGS, &flags,
                       META_CORE_GET_END);
        
        if (flags & META_FRAME_ALLOWS_MAXIMIZE)
          {
            meta_core_toggle_maximize_vertically (display, frame->xwindow);
          }
      }
      break;

    case G_DESKTOP_TITLEBAR_ACTION_MINIMIZE:
      {
        meta_core_get (display, frame->xwindow,
                       META_CORE_GET_FRAME_FLAGS, &flags,
                       META_CORE_GET_END);
        
        if (flags & META_FRAME_ALLOWS_MINIMIZE)
          {
            meta_core_minimize (display, frame->xwindow);
          }
      }
      break;

    case G_DESKTOP_TITLEBAR_ACTION_NONE:
      /* Yaay, a sane user that doesn't use that other weird crap! */
      break;
    
    case G_DESKTOP_TITLEBAR_ACTION_LOWER:
      meta_core_user_lower_and_unfocus (display,
                                        frame->xwindow,
                                        event->time);
      break;

    case G_DESKTOP_TITLEBAR_ACTION_MENU:
      meta_core_show_window_menu (display,
                                  frame->xwindow,
                                  event->x_root,
                                  event->y_root,
                                  event->button,
                                  event->time);
      break;
    }
  
  return TRUE;
}

static gboolean
meta_frame_double_click_event (MetaUIFrame    *frame,
                               GdkEventButton *event)
{
  int action = meta_prefs_get_action_double_click_titlebar ();
  
  return meta_frame_titlebar_event (frame, event, action);
}

static gboolean
meta_frame_middle_click_event (MetaUIFrame    *frame,
                               GdkEventButton *event)
{
  int action = meta_prefs_get_action_middle_click_titlebar();
  
  return meta_frame_titlebar_event (frame, event, action);
}

static gboolean
meta_frame_right_click_event(MetaUIFrame     *frame,
                             GdkEventButton  *event)
{
  int action = meta_prefs_get_action_right_click_titlebar();
  
  return meta_frame_titlebar_event (frame, event, action);
}

static gboolean
meta_uiframe_button_press_event (GtkWidget      *widget,
                                 GdkEventButton *event)
{
  MetaUIFrame *frame;
  MetaFrameControl control;
  Display *display;

  frame = META_UIFRAME (widget);

  display = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());

  /* Remember that the display may have already done something with this event.
   * If so there's probably a GrabOp in effect.
   */
  control = get_control (frame, event->x, event->y);

  /* don't do the rest of this if on client area */
  if (control == META_FRAME_CONTROL_CLIENT_AREA)
    return FALSE; /* not on the frame, just passed through from client */
  
  /* We want to shade even if we have a GrabOp, since we'll have a move grab
   * if we double click the titlebar.
   */
  if (control == META_FRAME_CONTROL_TITLE &&
      event->button == 1 &&
      event->type == GDK_2BUTTON_PRESS)
    {
      meta_core_end_grab_op (display, event->time);
      return meta_frame_double_click_event (frame, event);
    }

  if (meta_core_get_grab_op (display) !=
      META_GRAB_OP_NONE)
    return FALSE; /* already up to something */

  if (event->button == 1 &&
           (control == META_FRAME_CONTROL_RESIZE_SE ||
            control == META_FRAME_CONTROL_RESIZE_S ||
            control == META_FRAME_CONTROL_RESIZE_SW ||
            control == META_FRAME_CONTROL_RESIZE_NE ||
            control == META_FRAME_CONTROL_RESIZE_N ||
            control == META_FRAME_CONTROL_RESIZE_NW ||
            control == META_FRAME_CONTROL_RESIZE_E ||
            control == META_FRAME_CONTROL_RESIZE_W))
    {
      MetaGrabOp op;

      op = META_GRAB_OP_NONE;

      switch (control)
        {
        case META_FRAME_CONTROL_RESIZE_SE:
          op = META_GRAB_OP_RESIZING_SE;
          break;
        case META_FRAME_CONTROL_RESIZE_S:
          op = META_GRAB_OP_RESIZING_S;
          break;
        case META_FRAME_CONTROL_RESIZE_SW:
          op = META_GRAB_OP_RESIZING_SW;
          break;
        case META_FRAME_CONTROL_RESIZE_NE:
          op = META_GRAB_OP_RESIZING_NE;
          break;
        case META_FRAME_CONTROL_RESIZE_N:
          op = META_GRAB_OP_RESIZING_N;
          break;
        case META_FRAME_CONTROL_RESIZE_NW:
          op = META_GRAB_OP_RESIZING_NW;
          break;
        case META_FRAME_CONTROL_RESIZE_E:
          op = META_GRAB_OP_RESIZING_E;
          break;
        case META_FRAME_CONTROL_RESIZE_W:
          op = META_GRAB_OP_RESIZING_W;
          break;
        default:
          g_assert_not_reached ();
          break;
        }

      meta_core_begin_grab_op (display,
                               frame->xwindow,
                               op,
                               TRUE,
                               TRUE,
                               event->button,
                               0,
                               event->time,
                               event->x_root,
                               event->y_root);
    }
  else if (control == META_FRAME_CONTROL_TITLE &&
           event->button == 1)
    {
      MetaFrameFlags flags;

      meta_core_get (display, frame->xwindow,
                     META_CORE_GET_FRAME_FLAGS, &flags,
                     META_CORE_GET_END);

      if (flags & META_FRAME_ALLOWS_MOVE)
        {          
          meta_core_begin_grab_op (display,
                                   frame->xwindow,
                                   META_GRAB_OP_MOVING,
                                   TRUE,
                                   TRUE,
                                   event->button,
                                   0,
                                   event->time,
                                   event->x_root,
                                   event->y_root);
        }
    }
  else if (event->button == 2)
    {
      return meta_frame_middle_click_event (frame, event);
    }
  else if (event->button == 3)
    {
      return meta_frame_right_click_event (frame, event);
    }
  
  return TRUE;
}

static void
update_cursor (MetaUIFrame *frame,
               Display     *display,
               int          x,
               int          y)
{
  MetaGrabOp grab_op = meta_core_get_grab_op (display);

  if (grab_op == META_GRAB_OP_NONE)
    {
      MetaFrameControl control;
      MetaCursor cursor;

      control = get_control (frame, x, y);
      switch (control)
        {
        case META_FRAME_CONTROL_RESIZE_SE:
          cursor = META_CURSOR_SE_RESIZE;
          break;
        case META_FRAME_CONTROL_RESIZE_S:
          cursor = META_CURSOR_SOUTH_RESIZE;
          break;
        case META_FRAME_CONTROL_RESIZE_SW:
          cursor = META_CURSOR_SW_RESIZE;
          break;
        case META_FRAME_CONTROL_RESIZE_N:
          cursor = META_CURSOR_NORTH_RESIZE;
          break;
        case META_FRAME_CONTROL_RESIZE_NE:
          cursor = META_CURSOR_NE_RESIZE;
          break;
        case META_FRAME_CONTROL_RESIZE_NW:
          cursor = META_CURSOR_NW_RESIZE;
          break;
        case META_FRAME_CONTROL_RESIZE_W:
          cursor = META_CURSOR_WEST_RESIZE;
          break;
        case META_FRAME_CONTROL_RESIZE_E:
          cursor = META_CURSOR_EAST_RESIZE;
          break;
        default:
          cursor = META_CURSOR_DEFAULT;
          break;
        }

      meta_core_set_screen_cursor (display, frame->xwindow, cursor);
    }
}

static gboolean
meta_uiframe_motion_notify_event (GtkWidget      *widget,
                                  GdkEventMotion *event)
{
  MetaUIFrame *frame = META_UIFRAME (widget);
  Display *display = GDK_DISPLAY_XDISPLAY (gdk_window_get_display (event->window));
  int x, y;
  gdk_window_get_device_position (frame->window, event->device, &x, &y, NULL);
  update_cursor (frame, display, x, y);
  return FALSE;
}

static void
clip_to_screen (cairo_region_t *region,
                MetaUIFrame    *frame)
{
  cairo_rectangle_int_t frame_area;
  cairo_rectangle_int_t screen_area = { 0, 0, 0, 0 };
  cairo_region_t *tmp_region;
  
  /* Chop off stuff outside the screen; this optimization
   * is crucial to handle huge client windows,
   * like "xterm -geometry 1000x1000"
   */
  meta_core_get (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()),
                 frame->xwindow,
                 META_CORE_GET_FRAME_X, &frame_area.x,
                 META_CORE_GET_FRAME_Y, &frame_area.y,
                 META_CORE_GET_FRAME_WIDTH, &frame_area.width,
                 META_CORE_GET_FRAME_HEIGHT, &frame_area.height,
                 META_CORE_GET_SCREEN_WIDTH, &screen_area.width,
                 META_CORE_GET_SCREEN_HEIGHT, &screen_area.height,
                 META_CORE_GET_END);

  cairo_region_translate (region, frame_area.x, frame_area.y);

  tmp_region = cairo_region_create_rectangle (&frame_area);
  cairo_region_intersect (region, tmp_region);
  cairo_region_destroy (tmp_region);

  tmp_region = cairo_region_create_rectangle (&screen_area);
  cairo_region_intersect (region, tmp_region);
  cairo_region_destroy (tmp_region);

  cairo_region_translate (region, - frame_area.x, - frame_area.y);
}

static void
subtract_client_area (cairo_region_t *region,
                      MetaUIFrame    *frame)
{
  cairo_rectangle_int_t area;
  MetaFrameFlags flags;
  MetaFrameType type;
  MetaFrameBorders borders;
  cairo_region_t *tmp_region;
  Display *display;
  
  display = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());

  meta_core_get (display, frame->xwindow,
                 META_CORE_GET_FRAME_FLAGS, &flags,
                 META_CORE_GET_FRAME_TYPE, &type,
                 META_CORE_GET_CLIENT_WIDTH, &area.width,
                 META_CORE_GET_CLIENT_HEIGHT, &area.height,
                 META_CORE_GET_END);

  meta_theme_get_frame_borders (frame->theme,
                                gtk_widget_get_style_context (GTK_WIDGET (frame)),
                                type, flags,
                                &borders);

  area.x = borders.total.left;
  area.y = borders.total.top;

  tmp_region = cairo_region_create_rectangle (&area);
  cairo_region_subtract (region, tmp_region);
  cairo_region_destroy (tmp_region);
}

void
meta_uiframe_paint (MetaUIFrame  *frame,
                    cairo_t      *cr)
{
  MetaFrameFlags flags;
  MetaFrameType type;
  GdkPixbuf *mini_icon;
  GdkPixbuf *icon;
  int w, h;
  MetaButtonLayout button_layout;
  Display *display;
  cairo_region_t *region;
  cairo_rectangle_int_t clip;

  gdk_cairo_get_clip_rectangle (cr, &clip);

  region = cairo_region_create_rectangle (&clip);

  clip_to_screen (region, frame);
  subtract_client_area (region, frame);

  if (cairo_region_num_rectangles (region) == 0)
    goto out;

  gdk_cairo_region (cr, region);
  cairo_clip (cr);

  display = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());

  meta_core_get (display, frame->xwindow,
                 META_CORE_GET_FRAME_FLAGS, &flags,
                 META_CORE_GET_FRAME_TYPE, &type,
                 META_CORE_GET_MINI_ICON, &mini_icon,
                 META_CORE_GET_ICON, &icon,
                 META_CORE_GET_CLIENT_WIDTH, &w,
                 META_CORE_GET_CLIENT_HEIGHT, &h,
                 META_CORE_GET_END);

  meta_prefs_get_button_layout (&button_layout);

  meta_theme_draw_frame_with_style (frame->theme,
                                    gtk_widget_get_style_context (GTK_WIDGET (frame)),
                                    cr,
                                    type,
                                    flags,
                                    w, h,
                                    &button_layout,
                                    NULL,
                                    mini_icon, icon);

  GTK_WIDGET_CLASS (meta_uiframe_parent_class)->draw (GTK_WIDGET (frame), cr);

 out:
  cairo_region_destroy (region);
}

static gboolean
meta_uiframe_draw (GtkWidget *widget,
                   cairo_t   *cr)
{
  cairo_save (cr);
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgba (cr, 1, 1, 1, 1);
  cairo_paint (cr);
  cairo_restore (cr);

  meta_uiframe_paint (META_UIFRAME (widget), cr);

  return TRUE;
}

static void
meta_uiframe_size_allocate (GtkWidget     *widget,
                            GtkAllocation *allocation)
{
  MetaUIFrame *frame = META_UIFRAME (widget);
  GtkWidget *child = gtk_bin_get_child (GTK_BIN (widget));

  gtk_widget_set_allocation (widget, allocation);

  if (child && gtk_widget_get_visible (child))
    {
      MetaFrameGeometry fgeom;
      GtkAllocation child_allocation;
      GtkBorder *invisible;

      meta_uiframe_calc_geometry (frame, &fgeom);
      invisible = &(fgeom.borders.invisible);

      child_allocation = *allocation;
      child_allocation.x += invisible->left;
      child_allocation.y += invisible->top;
      child_allocation.width -= invisible->left + invisible->right;
      child_allocation.height -= invisible->top + invisible->bottom;

      gtk_widget_size_allocate (child, &child_allocation);
    }
}

static gboolean
meta_uiframe_enter_notify_event (GtkWidget        *widget,
                                 GdkEventCrossing *event)
{
  MetaUIFrame *frame = META_UIFRAME (widget);
  Display *display = GDK_DISPLAY_XDISPLAY (gdk_window_get_display (event->window));
  update_cursor (frame, display, event->x, event->y);
  return FALSE;
}

static gboolean
meta_uiframe_leave_notify_event (GtkWidget        *widget,
                                 GdkEventCrossing *event)
{
  MetaUIFrame *frame = META_UIFRAME (widget);
  Display *display = GDK_DISPLAY_XDISPLAY (gdk_window_get_display (event->window));
  meta_core_set_screen_cursor (display, frame->xwindow, META_CURSOR_DEFAULT);
  return FALSE;
}

#define TOP_RESIZE_HEIGHT 4
static MetaFrameControl
get_control (MetaUIFrame *frame,
             int x, int y)
{
  MetaFrameGeometry fgeom;
  MetaFrameFlags flags;
  MetaFrameType type;
  gboolean has_vert, has_horiz;
  gboolean has_north_resize;
  cairo_rectangle_int_t client;

  meta_uiframe_calc_geometry (frame, &fgeom);
  get_client_rect (&fgeom, fgeom.width, fgeom.height, &client);

  if (POINT_IN_RECT (x, y, client))
    return META_FRAME_CONTROL_CLIENT_AREA;

  meta_core_get (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()),
                 frame->xwindow,
                 META_CORE_GET_FRAME_FLAGS, &flags,
                 META_CORE_GET_FRAME_TYPE, &type,
                 META_CORE_GET_END);

  has_north_resize = (type != META_FRAME_TYPE_ATTACHED);
  has_vert = (flags & META_FRAME_ALLOWS_VERTICAL_RESIZE) != 0;
  has_horiz = (flags & META_FRAME_ALLOWS_HORIZONTAL_RESIZE) != 0;

  /* South resize always has priority over north resize,
   * in case of overlap.
   */

  if (y >= (fgeom.height - fgeom.borders.total.bottom) &&
      x >= (fgeom.width - fgeom.borders.total.right))
    {
      if (has_vert && has_horiz)
        return META_FRAME_CONTROL_RESIZE_SE;
      else if (has_vert)
        return META_FRAME_CONTROL_RESIZE_S;
      else if (has_horiz)
        return META_FRAME_CONTROL_RESIZE_E;
    }
  else if (y >= (fgeom.height - fgeom.borders.total.bottom) &&
           x <= fgeom.borders.total.left)
    {
      if (has_vert && has_horiz)
        return META_FRAME_CONTROL_RESIZE_SW;
      else if (has_vert)
        return META_FRAME_CONTROL_RESIZE_S;
      else if (has_horiz)
        return META_FRAME_CONTROL_RESIZE_W;
    }
  else if (y < (fgeom.borders.invisible.top) &&
           x <= fgeom.borders.total.left && has_north_resize)
    {
      if (has_vert && has_horiz)
        return META_FRAME_CONTROL_RESIZE_NW;
      else if (has_vert)
        return META_FRAME_CONTROL_RESIZE_N;
      else if (has_horiz)
        return META_FRAME_CONTROL_RESIZE_W;
    }
  else if (y < (fgeom.borders.invisible.top) &&
           x >= fgeom.width - fgeom.borders.total.right && has_north_resize)
    {
      if (has_vert && has_horiz)
        return META_FRAME_CONTROL_RESIZE_NE;
      else if (has_vert)
        return META_FRAME_CONTROL_RESIZE_N;
      else if (has_horiz)
        return META_FRAME_CONTROL_RESIZE_E;
    }
  else if (y < (fgeom.borders.invisible.top + TOP_RESIZE_HEIGHT))
    {
      if (has_vert && has_north_resize)
        return META_FRAME_CONTROL_RESIZE_N;
    }
  else if (y >= (fgeom.height - fgeom.borders.total.bottom))
    {
      if (has_vert)
        return META_FRAME_CONTROL_RESIZE_S;
    }
  else if (x <= fgeom.borders.total.left)
    {
      if (has_horiz)
        return META_FRAME_CONTROL_RESIZE_W;
    }
  else if (x >= (fgeom.width - fgeom.borders.total.right))
    {
      if (has_horiz)
        return META_FRAME_CONTROL_RESIZE_E;
    }

  if (y >= fgeom.borders.total.top)
    return META_FRAME_CONTROL_NONE;
  else
    return META_FRAME_CONTROL_TITLE;
}

static void
meta_uiframe_class_init (MetaUIFrameClass *class)
{
  GObjectClass   *gobject_class;
  GtkWidgetClass *widget_class;

  gobject_class = G_OBJECT_CLASS (class);
  widget_class = GTK_WIDGET_CLASS (class);

  gobject_class->finalize = meta_uiframe_finalize;

  widget_class->draw = meta_uiframe_draw;
  widget_class->size_allocate = meta_uiframe_size_allocate;
  widget_class->button_press_event = meta_uiframe_button_press_event;
  widget_class->motion_notify_event = meta_uiframe_motion_notify_event;
  widget_class->enter_notify_event = meta_uiframe_enter_notify_event;
  widget_class->leave_notify_event = meta_uiframe_leave_notify_event;
}
