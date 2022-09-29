/*****************************************************************************/
/*  bookmarks.c - routines for the bookmarks                                 */
/*  Copyright (C) 1998-2007 Brian Masney <masneyb@gftp.org>                  */
/*                                                                           */
/*  This program is free software; you can redistribute it and/or modify     */
/*  it under the terms of the GNU General Public License as published by     */
/*  the Free Software Foundation; either version 2 of the License, or        */
/*  (at your option) any later version.                                      */
/*                                                                           */
/*  This program is distributed in the hope that it will be useful,          */
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of           */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            */
/*  GNU General Public License for more details.                             */
/*                                                                           */
/*  You should have received a copy of the GNU General Public License        */
/*  along with this program; if not, write to the Free Software              */
/*  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111 USA      */
/*****************************************************************************/

#include "gftp-gtk.h"

static GtkWidget * edit_bm_entry_dlg = NULL, * edit_bookmarks_dialog = NULL;
static GtkWidget * bm_hostedit, * bm_portedit, * bm_localdiredit,
                 * bm_remotediredit, * bm_useredit, * bm_passedit,
                 * bm_acctedit, * anon_chk, * bm_pathedit, * combo_protocol;

// -- TreeView
static GtkTreeView *btree;
static GtkTreeView *btree_create (void);
struct btree_bookmark
{
   gftp_bookmarks_var * entry;
   GtkTreeIter iter;
   gpointer data;
};
static void btree_add_node (gftp_bookmarks_var * entry, int copy, GtkTreeIter * parent_iter, gboolean edit);
static gftp_bookmarks_var * btree_get_selected_bookmark (GtkTreeIter * out_iter);
static void btree_remove_selected_node (void);
static void btree_expand_collapse_selected_row (int key);
static void gtktreemodel_to_gftp (void);
static void gtktreemodel_find_bookmark (const char *path, struct btree_bookmark * out_bookmark);

static GdkPixbuf *opendir_pixbuf = NULL;
static GdkPixbuf *closedir_pixbuf = NULL;
static GdkPixbuf *bookmark_pixbuf = NULL;
enum {
   BTREEVIEW_COL_ICON,
   BTREEVIEW_COL_TEXT,
   BTREEVIEW_COL_BOOKMARK, // hidden, pointer to gftp_bookmarks_var
   BTREEVIEW_NUM_COLS
};
// --

static guint bm_menu_merge_id = 0; // factory
static GtkActionGroup *dynamic_bm_menu = NULL;

static GtkMenu * bmenu_file;
static GtkWidget * create_bm_dlg_menubar (GtkWindow *window);
static void bmenu_setup (int from_popup);

void
on_menu_run_bookmark (GtkAction *action, gpointer path_str)
{
  int refresh_local;
  char *path = (char *) path_str;

  if (window1.request->stopable || window2.request->stopable)
    {
      ftp_log (gftp_logging_misc, NULL,
               _("%s: Please hit the stop button first to do anything else\n"),
               _("Run Bookmark"));
      return;
    }

  // must always reset the request struct
  gftpui_disconnect (current_wdata);

  if (gftp_parse_bookmark (current_wdata->request, other_wdata->request,
                           path, &refresh_local) < 0)
    return;

  if (refresh_local)
    gftpui_refresh (other_wdata, 0);

  ftp_connect (current_wdata, current_wdata->request);
}


static void
doadd_bookmark (gpointer * data, gftp_dialog_data * ddata)
{
  const char *edttxt, *spos;
  gftp_bookmarks_var * tempentry;
  char *dpos, *proto;

  edttxt = ddata->entry_text;
  if (*edttxt == '\0')
    {
      ftp_log (gftp_logging_error, NULL,
               _("Add Bookmark: You must enter a name for the bookmark\n"));
      return;
    }

  if (g_hash_table_lookup (gftp_bookmarks_htable, edttxt) != NULL)
    {
      ftp_log (gftp_logging_error, NULL,
               _("Add Bookmark: Cannot add bookmark %s because that name already exists\n"), edttxt);
      return;
    }

  tempentry = g_malloc0 (sizeof (*tempentry));

  dpos = tempentry->path = g_malloc0 ((gulong) strlen (edttxt) + 1);
  for (spos = edttxt; *spos != '\0';)
    {
      *dpos++ = *spos++;
      if (*spos == '/')
        {
          *dpos++ = '/';
          for (; *spos == '/'; spos++);
        }
    }
  *dpos = '\0';

  edttxt = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(hostedit));
  tempentry->hostname = g_strdup (edttxt);

  edttxt = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(portedit));
  tempentry->port = strtol (edttxt, NULL, 10);

  proto = gftp_protocols[current_wdata->request->protonum].name;
  tempentry->protocol = g_strdup (proto);

  edttxt = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(other_wdata->dir_combo));
  tempentry->local_dir = g_strdup (edttxt);

  edttxt = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(current_wdata->dir_combo));
  tempentry->remote_dir = g_strdup (edttxt);

  if ( (edttxt = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(useredit))) != NULL)
    {
      tempentry->user = g_strdup (edttxt);

      edttxt = gtk_entry_get_text (GTK_ENTRY (passedit));
      tempentry->pass = g_strdup (edttxt);
      tempentry->save_password = ddata->checkbox_is_ticked;
    }

  gftp_add_bookmark (tempentry);

  gftp_write_bookmarks_file ();
  build_bookmarks_menu();
}


void
add_bookmark (gpointer data)
{
  const char *edttxt;

  if (!check_status (_("Add Bookmark"), current_wdata, 0, 0, 0, 1))
    return;

  edttxt = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(hostedit));
  if (*edttxt == '\0')
    {
      ftp_log (gftp_logging_error, NULL,
	       _("Add Bookmark: You must enter a hostname\n"));
      return;
    }

  TextEntryDialog (NULL, _("Add Bookmark"),
                   _("Enter the name of the bookmark you want to add\nYou can separate items by a / to put it into a submenu\n(ex: Linux Sites/Debian)"),
                   NULL, 1, _("Remember password"),
                   gftp_dialog_button_create, doadd_bookmark, data, NULL, NULL);
}


void
build_bookmarks_menu (void)
{
  GtkAction *action;
  gftp_bookmarks_var * tempentry;
  char *slash;

  if (bm_menu_merge_id) {
    gtk_ui_manager_remove_ui(factory, bm_menu_merge_id);
  }
  bm_menu_merge_id = gtk_ui_manager_new_merge_id(factory);

  if (!dynamic_bm_menu) {
     dynamic_bm_menu = gtk_action_group_new("bm_menu");
     gtk_ui_manager_insert_action_group(factory, dynamic_bm_menu, 0);
     g_object_unref(dynamic_bm_menu);
  }

  tempentry = gftp_bookmarks->children;
  while (tempentry != NULL)
    {
      action = gtk_action_group_get_action (dynamic_bm_menu, tempentry->path);
      if (action) {
        gtk_action_group_remove_action (dynamic_bm_menu, action);
        action = NULL;
      }

      // ---------------------------
      GtkUIManagerItemType itemType;
      gchar * action_name, * action_label, * ui_manager_path;
      gchar * tmp = NULL;

      if (tempentry->isfolder) {
         itemType = GTK_UI_MANAGER_MENU;
      } else {
         itemType = GTK_UI_MANAGER_MENUITEM;
      }

      slash = strrchr(tempentry->path, '/');

      if (!slash) {
        // BSD Sites
        action_name  = tempentry->path;
        action_label = tempentry->path;
        ui_manager_path = "/M/BookmarksMenu";
      } else {
        // BSD Sites/FreeBSD
        char oldchar = *slash;
        *slash = 0;
        tmp = g_strconcat("/M/BookmarksMenu/", tempentry->path, NULL);
        *slash = oldchar;
        action_name  = tempentry->path; // BSD Sites/FreeBSD
        action_label = slash + 1;       // FreeBSD
        ui_manager_path = tmp;          // /M/BookmarksMenu/BSD Sites
      }

      action = gtk_action_new (action_name, action_label, NULL, NULL);
      gtk_action_group_add_action (dynamic_bm_menu, action);
      gtk_ui_manager_add_ui (factory, bm_menu_merge_id,
                             ui_manager_path, action_name, action_name,
                             itemType, FALSE);

      if (!tempentry->isfolder) {
         g_signal_connect (G_OBJECT(action), "activate",
                           G_CALLBACK (on_menu_run_bookmark),
                           (gpointer) tempentry->path);
      }
      if (tmp) g_free (tmp);
      g_object_unref(action);
      // ---------------------------

      if (tempentry->children != NULL)
        {
          tempentry = tempentry->children;
          continue;
        }
      while (tempentry->next == NULL && tempentry->parent != NULL)
        tempentry = tempentry->parent;

      tempentry = tempentry->next;
    }
}

static void
_free_menu_entry (gftp_bookmarks_var * entry)
{
  GtkAction *action;
  if (dynamic_bm_menu) {
    action = gtk_action_group_get_action (dynamic_bm_menu, entry->path);
    if (action) {
        gtk_action_group_remove_action (dynamic_bm_menu, action);
    }
  }
}


static void
bm_apply_changes ()
{
  gftp_bookmarks_var * tempentry, * delentry;

  if (edit_bm_entry_dlg != NULL)
    {
      gtk_widget_grab_focus (edit_bm_entry_dlg);
      return;
    }

  if (gftp_bookmarks != NULL)
    {
      tempentry = gftp_bookmarks->children;
      while (tempentry != NULL)
        {
          if (tempentry->children != NULL)
            tempentry = tempentry->children;
          else
            {
              while (tempentry->next == NULL && tempentry->parent != NULL)
                {
                  delentry = tempentry;
                  tempentry = tempentry->parent;
                  _free_menu_entry (delentry);
                  gftp_free_bookmark (delentry, 1);
                }

              delentry = tempentry;
              tempentry = tempentry->next;
              if (tempentry != NULL)
                _free_menu_entry (delentry);

              gftp_free_bookmark (delentry, 1);
            }
        }

      g_hash_table_remove_all (gftp_bookmarks_htable);
    }

  // populate `gftp_bookmarks` / `gftp_bookmarks_htable` again
  gtktreemodel_to_gftp ();

  build_bookmarks_menu ();
  gftp_write_bookmarks_file ();
}


static void
on_gtk_dialog_response_BookmarkDlg (GtkDialog * dialog,
                                    gint response,
                                    gpointer user_data)
{ /* ok | cancel | close dialog */
  if (response == GTK_RESPONSE_OK) {
     bm_apply_changes ();
  }

  if (edit_bm_entry_dlg != NULL)
    return;

  if (edit_bookmarks_dialog != NULL)
    {
      gtk_widget_destroy (edit_bookmarks_dialog);
      edit_bookmarks_dialog = NULL;
    }
}


static void
do_make_new (gpointer data, gftp_dialog_data * ddata)
{
  gboolean isfolder = data ? TRUE : FALSE;
  gftp_bookmarks_var * newentry;
  GtkTreeIter parent_iter;
  gftp_bookmarks_var * parent_entry = btree_get_selected_bookmark (&parent_iter);
  const char * str;
  char * str2 = NULL;
  struct btree_bookmark bm;

  const char *error = NULL;
  const char *error1 = _("You must specify a name for the bookmark.");
  const char *error2 = _("Cannot add bookmark because that name already exists\n");

  str = ddata->entry_text;
  if (!error && *str == '\0') {
     error = error1;
  }
  if (!error)
  {
     if (!isfolder && parent_entry && *parent_entry->path) {
        // build path including folder path
        str2 = g_strconcat (parent_entry->path, "/", str, NULL);
        str = str2;
     }
     gtktreemodel_find_bookmark (str, &bm);
     if (bm.entry) {
         error = error2;
     }
  }
  if (error)
  {
     GtkWidget *m;
     m = gtk_message_dialog_new (GTK_WINDOW (edit_bookmarks_dialog),
                                 GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                 GTK_MESSAGE_ERROR,
                                 GTK_BUTTONS_OK,
                                 "%s", error);
     g_signal_connect_swapped (m, "response", G_CALLBACK (gtk_widget_destroy), m);
     gtk_widget_show (m);
     if (str2) g_free (str2);
     return;
  }

  newentry = g_malloc0 (sizeof (*newentry));
  newentry->path = g_strdup (str);

  if (isfolder) {
     newentry->isfolder = 1;
     // a folder can only be a child of the root node (gftp_bookmarks)
     parent_entry = NULL;
  }

  if (parent_entry) {
     newentry->parent = parent_entry;
     btree_add_node (newentry, 0, &parent_iter, TRUE); /* 0 = don't copy bookmark */
  } else {
     newentry->parent = gftp_bookmarks;
     btree_add_node (newentry, 0, NULL, TRUE); /* 0 = don't copy bookmark */
  }
  if (str2) g_free (str2);
}


static void
new_bookmark_entry (int folder)
{
  if (folder) {
    TextEntryDialog (GTK_WINDOW (edit_bookmarks_dialog),
                     _("New Folder"),
                     _("Enter the name of the new folder to create"), NULL, 1,
                     NULL, gftp_dialog_button_create, 
                     do_make_new, (gpointer) 0x1, NULL, NULL);
  } else {
    TextEntryDialog (GTK_WINDOW (edit_bookmarks_dialog),
                     _("New Item"),
                     _("Enter the name of the new item to create"), NULL, 1,
                     NULL, gftp_dialog_button_create,
                     do_make_new, NULL, NULL, NULL);
  }
}

static void
do_delete_entry (gftp_bookmarks_var * entry, gftp_dialog_data * ddata)
{
  btree_remove_selected_node ();

  gftp_free_bookmark (entry, 1);
}


static void
delete_entry (gpointer data)
{
  gftp_bookmarks_var * entry = NULL;
  char *tempstr, *pos;

  entry = btree_get_selected_bookmark (NULL);

  if (entry == NULL)
    return;

  if (!entry->isfolder)
    do_delete_entry (entry, NULL);
  else
    {
      if ((pos = strrchr (entry->path, '/')) == NULL)
        pos = entry->path;
      else
        pos++;

      tempstr = g_strdup_printf (_("Are you sure you want to erase the bookmark\n%s and all its children?"), pos);
      YesNoDialog (GTK_WINDOW (edit_bookmarks_dialog),
                   _("Delete Bookmark"), tempstr,
                   do_delete_entry, entry, NULL, NULL);
      g_free (tempstr);
    }
}


static void
set_userpass_visible (GtkWidget * checkbutton, GtkWidget * entry)
{
  gboolean active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (anon_chk));
  gtk_widget_set_sensitive (bm_useredit, !active);
  gtk_widget_set_sensitive (bm_passedit, !active);
  gtk_widget_set_sensitive (bm_acctedit, !active);
}


static void build_bookmarks_tree (void)
{
  // convert a singly linked list to GtkTreeModel (pointer in a 'column')
  gftp_bookmarks_var * entry;

  btree_add_node (gftp_bookmarks, 1, NULL, FALSE);

  entry = gftp_bookmarks->children;
  while (entry != NULL)
  {
     btree_add_node (entry, 1, NULL, FALSE);
     if (entry->children != NULL)
     {
        entry = entry->children;
        continue;
     }
     while (entry->next == NULL && entry->parent != NULL) {
        entry = entry->parent;
     }
     entry = entry->next;
  }
}


static void
entry_apply_changes (GtkWidget * widget, gftp_bookmarks_var * entry)
{
  char *pos, *newpath, tempchar, *tempstr;
  const char *str;

  tempstr = g_strdup (gtk_entry_get_text (GTK_ENTRY (bm_pathedit)));
  while ((pos = strchr (tempstr, '/')) != NULL)
    *pos = ' ';

  if ((pos = strrchr (entry->path, '/')) != NULL)
    {
      tempchar = *pos;
      *pos = '\0';
      newpath = gftp_build_path (NULL, entry->path, tempstr, NULL);
      *pos = tempchar;
      g_free (tempstr);
    }
  else
    newpath = tempstr;

  str = gtk_entry_get_text (GTK_ENTRY (bm_hostedit));
  if (entry->hostname != NULL)
    g_free (entry->hostname);
  entry->hostname = g_strdup (str);

  str = gtk_entry_get_text (GTK_ENTRY (bm_portedit));
  entry->port = strtol (str, NULL, 10);

  str = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo_protocol));
  if (entry->protocol != NULL)
    g_free (entry->protocol);
  entry->protocol = g_strdup (str);

  str = gtk_entry_get_text (GTK_ENTRY (bm_remotediredit));
  if (entry->remote_dir != NULL)
    g_free (entry->remote_dir);
  entry->remote_dir = g_strdup (str);

  str = gtk_entry_get_text (GTK_ENTRY (bm_localdiredit));
  if (entry->local_dir != NULL)
    g_free (entry->local_dir);
  entry->local_dir = g_strdup (str);

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (anon_chk)))
    str = GFTP_ANONYMOUS_USER;
  else
    str = gtk_entry_get_text (GTK_ENTRY (bm_useredit));

  if (entry->user != NULL)
    g_free (entry->user);
  entry->user = g_strdup (str);

  if (strcasecmp (entry->user, GFTP_ANONYMOUS_USER) == 0)
    str = "@EMAIL@";
  else
    str = gtk_entry_get_text (GTK_ENTRY (bm_passedit));
  if (entry->pass != NULL)
    g_free (entry->pass);
  entry->pass = g_strdup (str);
  entry->save_password = *entry->pass != '\0';

  str = gtk_entry_get_text (GTK_ENTRY (bm_acctedit));
  if (entry->acct != NULL)
    g_free (entry->acct);
  entry->acct = g_strdup (str);

  gftp_gtk_save_bookmark_options ();

  if (strcmp (entry->path, newpath) != 0)
    {
      // get label/description (doesn't include parent directory)
      pos = strrchr (newpath, '/');
      if (pos) pos++;
      else pos = newpath;

      // set new entry->path
      g_free (entry->path);
      entry->path = g_strdup (newpath);

      // set GtkTreeView node text/bookmark
      GtkTreeStore *store = GTK_TREE_STORE (gtk_tree_view_get_model (btree));
      GtkTreeSelection *select = gtk_tree_view_get_selection (btree);
      GtkTreeIter iter;
      if (gtk_tree_selection_get_selected (select, NULL, &iter))
      {
         gtk_tree_store_set (store, &iter,  BTREEVIEW_COL_TEXT, pos, -1);
      }
    }

  g_free (newpath);
}


static void
on_gtk_dialog_response_EditEntryDlg (GtkDialog *dialog,
                                    gint response,
                                    gpointer user_data)
{

  if (response == GTK_RESPONSE_OK)
  {
    gftp_bookmarks_var *entry = (gftp_bookmarks_var *) user_data;
    GtkWidget *m;
    struct btree_bookmark found_bm;

    const char *gtkentry_text = gtk_entry_get_text (GTK_ENTRY (bm_pathedit));
    char *p, tempchar;
    char *new_path;

    p = strrchr (entry->path, '/');
    if (p) {
       // directory, build dir/xxx
       tempchar = *p;
       *p = 0;
       new_path = g_strdup_printf ("%s/%s", entry->path, gtkentry_text);
       *p = tempchar;
    } else {
       new_path = g_strdup (gtkentry_text);
    }

    gtktreemodel_find_bookmark (new_path, &found_bm);
    if (new_path) g_free (new_path);

    /* don't allow bookmark path collision */
    if (found_bm.entry && found_bm.entry != entry)
    {
       g_signal_stop_emission_by_name (dialog, "response");
       m = gtk_message_dialog_new (GTK_WINDOW (edit_bm_entry_dlg),
                                   GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                   GTK_MESSAGE_ERROR,
                                   GTK_BUTTONS_OK,
                                   _("A bookmark with that description already exists."));
       g_signal_connect_swapped (m, "response", G_CALLBACK (gtk_widget_destroy), m);
       gtk_widget_show (m);
       return;
    }

    entry_apply_changes (GTK_WIDGET (dialog), user_data);
  }

  gtk_widget_destroy (GTK_WIDGET(dialog));
  edit_bm_entry_dlg = NULL;
}   


static void edit_entry_dlg (gftp_bookmarks_var * xentry)
{
  GtkWidget * table, * tempwid, * notebook, *main_vbox;
  gftp_bookmarks_var * entry;
  char *pos;

  if (edit_bm_entry_dlg != NULL)
    {
      gtk_widget_grab_focus (edit_bm_entry_dlg);
      return;
    }

  if (xentry) {
    entry = xentry;
  } else {
    entry = btree_get_selected_bookmark (NULL);
  }

  if (!entry || !entry->path || !*entry->path)
    return;

  edit_bm_entry_dlg = gtk_dialog_new_with_buttons (
              _("Edit Entry"),
              GTK_WINDOW (edit_bookmarks_dialog),
              GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
              "gtk-cancel", GTK_RESPONSE_CANCEL,
              "gtk-save",   GTK_RESPONSE_OK,
              NULL);

  gtk_window_set_role (GTK_WINDOW (edit_bm_entry_dlg), "Edit Bookmark Entry");
  gtk_window_set_position (GTK_WINDOW (edit_bm_entry_dlg), GTK_WIN_POS_MOUSE);
  gtk_widget_realize (edit_bm_entry_dlg);

  set_window_icon(GTK_WINDOW(edit_bm_entry_dlg), NULL);

  main_vbox = gtk_dialog_get_content_area (GTK_DIALOG (edit_bm_entry_dlg));
  gtk_container_set_border_width (GTK_CONTAINER (main_vbox), 10);

  notebook = gtk_notebook_new ();
  gtk_box_pack_start (GTK_BOX (main_vbox), notebook, TRUE, TRUE, 0);

  table = gtk_table_new (11, 2, FALSE);
  gtk_container_set_border_width (GTK_CONTAINER (table), 5);
  gtk_table_set_row_spacings (GTK_TABLE (table), 5);
  gtk_table_set_col_spacings (GTK_TABLE (table), 5);

  tempwid = gtk_label_new (_("Bookmark"));

  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), table, tempwid);

  tempwid = gtk_label_new (_("Description:"));
  gtkcompat_widget_set_halign_right (tempwid);
  gtk_table_attach_defaults (GTK_TABLE (table), tempwid, 0, 1, 0, 1);

  bm_pathedit = gtk_entry_new ();
  gtk_table_attach_defaults (GTK_TABLE (table), bm_pathedit, 1, 2, 0, 1);
  if ((pos = strrchr (entry->path, '/')) == NULL)
    pos = entry->path;
  else
    pos++;
  if (pos)
    gtk_entry_set_text (GTK_ENTRY (bm_pathedit), pos);

  tempwid = gtk_label_new (_("Hostname:"));
  gtkcompat_widget_set_halign_right (tempwid);
  gtk_table_attach_defaults (GTK_TABLE (table), tempwid, 0, 1, 1, 2);

  bm_hostedit = gtk_entry_new ();
  gtk_table_attach_defaults (GTK_TABLE (table), bm_hostedit, 1, 2, 1, 2);
  if (entry->isfolder)
    gtk_widget_set_sensitive (bm_hostedit, 0);
  else if (entry->hostname)
    gtk_entry_set_text (GTK_ENTRY (bm_hostedit), entry->hostname);

  tempwid = gtk_label_new (_("Port:"));
  gtkcompat_widget_set_halign_right (tempwid);
  gtk_table_attach_defaults (GTK_TABLE (table), tempwid, 0, 1, 2, 3);

  bm_portedit = gtk_entry_new ();
  gtk_table_attach_defaults (GTK_TABLE (table), bm_portedit, 1, 2, 2, 3);
  if (entry->isfolder)
    gtk_widget_set_sensitive (bm_portedit, 0);
  else if (entry->port)
    {
      pos = g_strdup_printf ("%d", entry->port);
      gtk_entry_set_text (GTK_ENTRY (bm_portedit), pos);
      g_free (pos);
    }

  tempwid = gtk_label_new (_("Protocol:"));
  gtkcompat_widget_set_halign_right (tempwid);
  gtk_table_attach_defaults (GTK_TABLE (table), tempwid, 0, 1, 3, 4);

  combo_protocol = gtk_combo_box_text_new ();
  gtk_table_attach_defaults (GTK_TABLE (table), combo_protocol, 1, 2, 3, 4);

  populate_combo_and_select_protocol (combo_protocol, entry->protocol);

  tempwid = gtk_label_new (_("Remote Directory:"));
  gtkcompat_widget_set_halign_right (tempwid);
  gtk_table_attach_defaults (GTK_TABLE (table), tempwid, 0, 1, 4, 5);

  bm_remotediredit = gtk_entry_new ();
  gtk_table_attach_defaults (GTK_TABLE (table), bm_remotediredit, 1, 2, 4, 5);
  if (entry->isfolder)
    gtk_widget_set_sensitive (bm_remotediredit, 0);
  else if (entry->remote_dir)
    gtk_entry_set_text (GTK_ENTRY (bm_remotediredit), entry->remote_dir);

  tempwid = gtk_label_new (_("Local Directory:"));
  gtkcompat_widget_set_halign_right (tempwid);
  gtk_table_attach_defaults (GTK_TABLE (table), tempwid, 0, 1, 5, 6);

  bm_localdiredit = gtk_entry_new ();
  gtk_table_attach_defaults (GTK_TABLE (table), bm_localdiredit, 1, 2, 5, 6);
  if (entry->isfolder)
    gtk_widget_set_sensitive (bm_localdiredit, 0);
  else if (entry->local_dir)
    gtk_entry_set_text (GTK_ENTRY (bm_localdiredit), entry->local_dir);

  tempwid = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
  gtk_table_attach_defaults (GTK_TABLE (table), tempwid, 0, 2, 7, 8);

  tempwid = gtk_label_new (_("Username:"));
  gtkcompat_widget_set_halign_right (tempwid);
  gtk_table_attach_defaults (GTK_TABLE (table), tempwid, 0, 1, 8, 9);

  bm_useredit = gtk_entry_new ();
  gtk_table_attach_defaults (GTK_TABLE (table), bm_useredit, 1, 2, 8, 9);
  if (entry->isfolder)
    gtk_widget_set_sensitive (bm_useredit, 0);
  else if (entry->user)
    gtk_entry_set_text (GTK_ENTRY (bm_useredit), entry->user);

  tempwid = gtk_label_new (_("Password:"));
  gtkcompat_widget_set_halign_right (tempwid);
  gtk_table_attach_defaults (GTK_TABLE (table), tempwid, 0, 1, 9, 10);

  bm_passedit = gtk_entry_new ();
  gtk_table_attach_defaults (GTK_TABLE (table), bm_passedit, 1, 2, 9, 10);
  gtk_entry_set_visibility (GTK_ENTRY (bm_passedit), FALSE);
  if (entry->isfolder)
    gtk_widget_set_sensitive (bm_passedit, 0);
  else if (entry->pass)
    gtk_entry_set_text (GTK_ENTRY (bm_passedit), entry->pass);

  tempwid = gtk_label_new (_("Account:"));
  gtkcompat_widget_set_halign_right (tempwid);
  gtk_table_attach_defaults (GTK_TABLE (table), tempwid, 0, 1, 10, 11);

  bm_acctedit = gtk_entry_new ();
  gtk_table_attach_defaults (GTK_TABLE (table), bm_acctedit, 1, 2, 10, 11);
  gtk_entry_set_visibility (GTK_ENTRY (bm_acctedit), FALSE);
  if (entry->isfolder)
    gtk_widget_set_sensitive (bm_acctedit, 0);
  else if (entry->acct)
    gtk_entry_set_text (GTK_ENTRY (bm_acctedit), entry->acct);

  anon_chk = gtk_check_button_new_with_label (_("Log in as ANONYMOUS"));
  gtk_table_attach_defaults (GTK_TABLE (table), anon_chk, 0, 2, 11, 12);
  if (entry->isfolder)
    gtk_widget_set_sensitive (anon_chk, 0);
  else
    {
      g_signal_connect (G_OBJECT (anon_chk), "toggled",
			  G_CALLBACK (set_userpass_visible), NULL);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (anon_chk), entry->user
				    && strcmp (entry->user, "anonymous") == 0);
    }

  g_signal_connect (G_OBJECT (edit_bm_entry_dlg),
                    "response",
                    G_CALLBACK (on_gtk_dialog_response_EditEntryDlg),
                    (gpointer) entry);

  gftp_gtk_setup_bookmark_options (notebook, entry);

  gtk_widget_show_all (edit_bm_entry_dlg);

  // switch to page 0 (widget must be visible)
  gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), 0);
}


static gboolean
on_gtk_treeview_KeyReleaseEvent_btree (GtkWidget * widget,
                       GdkEventKey * event, gpointer data)
{
  switch (event->keyval)
  {
    case GDK_KEY_KP_Delete:
    case GDK_KEY_Delete:
       delete_entry (NULL);
       break;
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
       edit_entry_dlg (NULL);
       break;
    case GDK_KEY_Left:
       btree_expand_collapse_selected_row (GDK_KEY_Left);
       break;
    case GDK_KEY_Right:
       btree_expand_collapse_selected_row (GDK_KEY_Right);
       break;
  }
  return (FALSE);
}

static gboolean
on_gtk_treeview_ButtonReleaseEvent_btree (GtkWidget * widget,
                                          GdkEventButton * event, gpointer data)
{
  if (event->button == 3)
    { // right click
      if (!gtk_tree_view_get_path_at_pos (btree, event->x, event->y, NULL, NULL, NULL, NULL)) {
         // a row doesn't exist at event->x / event->y
         // must clear the current selection
         GtkTreeSelection * sel = gtk_tree_view_get_selection (btree);
         gtk_tree_selection_unselect_all (sel);
      }
      bmenu_setup (1);
      gtk_menu_popup (bmenu_file, NULL, NULL, NULL, NULL, event->button, event->time);
    }
  return (FALSE);
}

static gboolean
on_gtk_treeview_ButtonPressEvent_btree (GtkWidget * widget,
                                        GdkEventButton * event, gpointer data)
{
  if (event->type == GDK_2BUTTON_PRESS && event->button == 1) {
     edit_entry_dlg (NULL); // double left click
  }
  return FALSE;
}


void edit_bookmarks (gpointer data)
{
  GtkWidget * main_vbox, * scroll, * menubar;

  if (edit_bookmarks_dialog != NULL)
    {
      gtk_widget_grab_focus (edit_bookmarks_dialog);
      return;
    }

  edit_bookmarks_dialog = gtk_dialog_new ();
  //gtk_window_set_transient_for (GTK_WINDOW (edit_bookmarks_dialog), GTK_WINDOW (main_window));
  gtk_window_set_position (GTK_WINDOW (edit_bookmarks_dialog), GTK_WIN_POS_MOUSE);
  gtk_window_set_title (GTK_WINDOW (edit_bookmarks_dialog), _("Edit Bookmarks"));
  set_window_icon (GTK_WINDOW(edit_bookmarks_dialog), NULL);

  gtk_dialog_add_button (GTK_DIALOG (edit_bookmarks_dialog), "gtk-cancel", GTK_RESPONSE_CANCEL);
  gtk_dialog_add_button (GTK_DIALOG (edit_bookmarks_dialog), "gtk-save",   GTK_RESPONSE_OK);

  gtk_widget_realize (edit_bookmarks_dialog);

  main_vbox = gtk_dialog_get_content_area (GTK_DIALOG (edit_bookmarks_dialog));
  gtk_container_set_border_width (GTK_CONTAINER (main_vbox), 3);

  menubar = create_bm_dlg_menubar (GTK_WINDOW (edit_bookmarks_dialog));
  gtk_box_pack_start (GTK_BOX (main_vbox), menubar, FALSE, FALSE, 0);

  scroll = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
				  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_size_request (scroll, 350, 380);

  gtk_box_pack_start (GTK_BOX (main_vbox), scroll, TRUE, TRUE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (scroll), 3);

  btree = btree_create();
  gtk_container_add (GTK_CONTAINER (scroll), GTK_WIDGET (btree));

  g_signal_connect (G_OBJECT (edit_bookmarks_dialog), // GtkDialog
                    "response",
                    G_CALLBACK (on_gtk_dialog_response_BookmarkDlg),
                    NULL);

  gtk_widget_show_all (edit_bookmarks_dialog);

  build_bookmarks_tree ();
  // expand root node
  GtkTreePath  *rpath = gtk_tree_path_new_from_string ("0");
  gtk_tree_view_expand_row (btree, rpath, FALSE);
  gtk_tree_path_free (rpath);
}


/* ===================================================================
 * Menubar
 * ===================================================================
 */

static void on_gtk_MenuItem_activate_newfolder (GtkMenuItem *menuitem, gpointer data) {
	new_bookmark_entry (1);
}
static void on_gtk_MenuItem_activate_newitem (GtkMenuItem *menuitem, gpointer data) {
	new_bookmark_entry (0);
}
static void on_gtk_MenuItem_activate_delete (GtkMenuItem *menuitem, gpointer data) {
	delete_entry (data);
}
static void on_gtk_MenuItem_activate_properties (GtkMenuItem *menuitem, gpointer data) {
	edit_entry_dlg (NULL);
}
static void on_gtk_MenuItem_activate_close (GtkMenuItem *menuitem, gpointer data) {
	gtk_dialog_response (GTK_DIALOG (edit_bookmarks_dialog), GTK_RESPONSE_CLOSE);
}

// ----

static GtkMenuItem *bmenu_file_newfolder;
static GtkMenuItem *bmenu_file_newitem;
static GtkMenuItem *bmenu_file_delete;
static GtkMenuItem *bmenu_file_edit;

static void bmenu_setup (int from_popup)
{
   gboolean newfolder_state = FALSE;
   gboolean newitem_state   = FALSE;
   gboolean edit_state      = FALSE;
   gboolean delete_state    = FALSE;
   gftp_bookmarks_var * entry = btree_get_selected_bookmark (NULL);
   if (entry)
   {
      edit_state   = TRUE;
      delete_state = TRUE;
      if (!entry->path || !*entry->path) {
         newfolder_state = TRUE; /* root node */
      }
      if (entry->isfolder) {
         newitem_state = TRUE;
      }
   }
   if (!from_popup || !entry) {
      // menubar, these 2 should always be enabled
      // calling from menubar: create an entry in root or folder
      //     but subfolders are not allowed (see do_make_new)
      newfolder_state = TRUE;
      newitem_state   = TRUE;
   }
   gtk_widget_set_sensitive (GTK_WIDGET (bmenu_file_newfolder), newfolder_state);
   gtk_widget_set_sensitive (GTK_WIDGET (bmenu_file_newitem),   newitem_state);
   gtk_widget_set_sensitive (GTK_WIDGET (bmenu_file_edit),      edit_state);
   gtk_widget_set_sensitive (GTK_WIDGET (bmenu_file_delete),    delete_state);
}

static void on_gtk_MenuItem_activate_fileMenu (GtkMenuItem *menuitem, gpointer data)
{
   bmenu_setup (0);
}

// ----

static GtkWidget *
create_bm_dlg_menubar (GtkWindow *window)
{
   GtkMenuBar *menu_bar = GTK_MENU_BAR (gtk_menu_bar_new ());
   GtkMenu * menu_file  = GTK_MENU (gtk_menu_new ());
   bmenu_file = menu_file;
   GtkMenuItem *item;

   GtkAccelGroup *accel_group = gtk_accel_group_new ();
   gtk_window_add_accel_group (GTK_WINDOW (window), accel_group);

   item = new_menu_item (menu_file, _("New _Folder..."), NULL,
                         on_gtk_MenuItem_activate_newfolder, NULL);
   bmenu_file_newfolder = item;

   item = new_menu_item (menu_file, _("New _Item..."), "gtk-new",
                         on_gtk_MenuItem_activate_newitem, NULL);
   bmenu_file_newitem = item;
   gtk_widget_add_accelerator (GTK_WIDGET (item), "activate",
                               accel_group,
                               GDK_KEY_n, GDK_CONTROL_MASK,
                               GTK_ACCEL_VISIBLE);
   item = new_menu_item (menu_file, _("_Delete"), "gtk-delete",
                         on_gtk_MenuItem_activate_delete, NULL);
   bmenu_file_delete = item;

   item = new_menu_item (menu_file, _("_Properties"), "gtk-properties",
                         on_gtk_MenuItem_activate_properties, NULL);
   bmenu_file_edit = item;

   item = new_menu_item (menu_file, _("_Close"), "gtk-close",
                         on_gtk_MenuItem_activate_close, NULL);
   gtk_widget_add_accelerator (GTK_WIDGET (item), "activate",
                               accel_group,
                               GDK_KEY_w, GDK_CONTROL_MASK,
                               GTK_ACCEL_VISIBLE);

   /* special menuitem that becomes the parent of menu_file, displays "_File" */
   item = new_menu_item (NULL, _("_File"), NULL,
                         on_gtk_MenuItem_activate_fileMenu, NULL);
   gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), GTK_WIDGET (menu_file));

   /* append to menubar */
   gtk_menu_shell_append (GTK_MENU_SHELL (menu_bar),
                          GTK_WIDGET     (item));

   gtk_widget_show_all (GTK_WIDGET (menu_bar));

   return (GTK_WIDGET (menu_bar));
}


/* ===================================================================
 * TreeView
 * ===================================================================
 */

GtkTreeView *
btree_create()
{
   // pixbufs..
   if (!opendir_pixbuf) {
      opendir_pixbuf  = gftp_get_pixbuf("open_dir.xpm");
   }
   if (!closedir_pixbuf) {
      closedir_pixbuf = gftp_get_pixbuf("dir.xpm");
   }
   if (!bookmark_pixbuf) {
      bookmark_pixbuf = gftp_get_pixbuf("txt.xpm");
   }

   // create tree store
   GtkTreeStore  *store;
   store = gtk_tree_store_new (BTREEVIEW_NUM_COLS,
                               GDK_TYPE_PIXBUF, // icon
                               G_TYPE_STRING,   // text
                               G_TYPE_POINTER); // hidden: pointer to bookmark
   // create treeview
   GtkTreeModel      *model;
   GtkTreeViewColumn *col;
   GtkTreeView       *tree;
   GtkCellRenderer   *renderer;
   GtkTreeSelection  *sel;

   model = GTK_TREE_MODEL(store);
   tree = g_object_new (GTK_TYPE_TREE_VIEW,
                        "model",             model,
                        "show-expanders",    TRUE,
                        "enable-tree-lines", TRUE,
                        "enable-grid-lines", GTK_TREE_VIEW_GRID_LINES_NONE,
                        "headers-visible",   FALSE,
                        "reorderable",       FALSE,
                        "enable-search",     FALSE,
                        NULL, NULL);
   g_object_unref (model);

   sel = gtk_tree_view_get_selection (tree);
   gtk_tree_selection_set_mode (sel, GTK_SELECTION_SINGLE);

   /* COLUMNS */
   col = g_object_new (GTK_TYPE_TREE_VIEW_COLUMN,
                       "title", "Bookmarks",
                       NULL);
   // icon
   renderer = g_object_new (GTK_TYPE_CELL_RENDERER_PIXBUF, NULL);
   gtk_tree_view_column_pack_start (col, renderer, FALSE);
   gtk_tree_view_column_set_attributes (col, renderer,
                                       "pixbuf", BTREEVIEW_COL_ICON,
                                       NULL);
   g_object_set (G_OBJECT (renderer),
                 "pixbuf-expander-closed", closedir_pixbuf,
                 "pixbuf-expander-open",   opendir_pixbuf,
                 "is-expanded",            TRUE,
                 "is-expander",            TRUE,
                 "xalign",                 0.0,
                 NULL, NULL);

   // text
   renderer = g_object_new (GTK_TYPE_CELL_RENDERER_TEXT, NULL);
   gtk_tree_view_column_pack_start (col, renderer, TRUE);
   gtk_tree_view_column_set_attributes (col, renderer,
                                       "text", BTREEVIEW_COL_TEXT,
                                       NULL);

   gtk_tree_view_append_column(tree, col);

   /* -GtkTreeView signals- */
   gtk_widget_add_events (GTK_WIDGET (tree), GDK_BUTTON_PRESS_MASK
                                             & GDK_BUTTON_RELEASE_MASK
                                             & GDK_KEY_RELEASE_MASK);
   g_signal_connect_after (G_OBJECT (tree),
                           "key_release_event",
                           G_CALLBACK (on_gtk_treeview_KeyReleaseEvent_btree), NULL);
   g_signal_connect_after (G_OBJECT (tree),
                     "button_release_event",
                     G_CALLBACK (on_gtk_treeview_ButtonReleaseEvent_btree),
                     NULL);
   g_signal_connect (G_OBJECT (tree), /* double clicks are detected in button_press_event */
                     "button_press_event",
                     G_CALLBACK (on_gtk_treeview_ButtonPressEvent_btree),
                     NULL);

   return (tree);
}


static void
btree_add_node (gftp_bookmarks_var * entry, int copy, GtkTreeIter * parent_iter, gboolean edit)
{
  // the calling function must provide parent nodes first, otherwise this will not work
  // this does not check for duplicates, everything must be already sorted out
  GtkTreeStore *store = GTK_TREE_STORE (gtk_tree_view_get_model (btree));
  GtkTreeIter  iter;
  GtkTreeIter  *parent;
  GdkPixbuf *pixbuf;
  char *text, *pos;
  gftp_bookmarks_var * newentry;

  if (copy) {
     // ->children = ->parent = ->next = NULL
     newentry = g_malloc0 (sizeof (gftp_bookmarks_var));
     newentry->port          = entry->port;
     newentry->isfolder      = entry->isfolder;
     newentry->save_password = entry->save_password;
     if (entry->path)       newentry->path       = g_strdup (entry->path);
     if (entry->hostname)   newentry->hostname   = g_strdup (entry->hostname);
     if (entry->protocol)   newentry->protocol   = g_strdup (entry->protocol);
     if (entry->remote_dir) newentry->remote_dir = g_strdup (entry->remote_dir);
     if (entry->local_dir)  newentry->local_dir  = g_strdup (entry->local_dir);
     if (entry->user)       newentry->user       = g_strdup (entry->user);
     if (entry->pass)       newentry->pass       = g_strdup (entry->pass);
     if (entry->acct)       newentry->acct       = g_strdup (entry->acct);
     gftp_copy_local_options (&newentry->local_options_vars,
                            &newentry->local_options_hash,
                            &newentry->num_local_options_vars,
                            entry->local_options_vars,
                            entry->num_local_options_vars);
     newentry->num_local_options_vars = entry->num_local_options_vars;

  } else {
     newentry = entry;
  }

  if (!entry->parent) {
    /* empty path - it can only be the root node */
    pos = _("Bookmarks");
  } else {
     if ((pos = strrchr (entry->path, '/')) == NULL)
        pos = entry->path;
     else
        pos++;
  }

  text = pos;

  if (parent_iter) {
     parent = parent_iter;
  } else if (entry->parent) {
     if (entry->parent->parent == NULL) {
        // parent is the root node
        GtkTreeIter iter;
        gtk_tree_model_get_iter_first (gtk_tree_view_get_model (btree), &iter);
        parent = &iter;
     } else {
        // find parent GtkTreeIter
        struct btree_bookmark bm;
        memset (&bm, 0, sizeof(bm));
        gtktreemodel_find_bookmark (entry->parent->path, &bm);
        parent = &(bm.iter);
     }
  } else  {
     // entry->parent = NULL, the root node
     parent = NULL;
  }
  ///fprintf(stderr, "%s\n-- %s\n\n", text, entry->path);

  if (entry->isfolder) {
    pixbuf = closedir_pixbuf;
  } else {
    pixbuf = bookmark_pixbuf;
  }

  gtk_tree_store_insert (store, &iter, parent, -1);
  gtk_tree_store_set (store, &iter,
                      BTREEVIEW_COL_ICON,     pixbuf,
                      BTREEVIEW_COL_TEXT,     text,
                      BTREEVIEW_COL_BOOKMARK, newentry,
                      -1);
  if (edit) {
     /* expand row */
     btree_expand_collapse_selected_row (GDK_KEY_Right);
     // select new row
     GtkTreeSelection * sel = gtk_tree_view_get_selection (btree);
     gtk_tree_selection_select_iter (sel, &iter);
     // no need to edit folder info right now
     if (!entry->isfolder) {
        edit_entry_dlg (newentry);
     }
  }
}


static gftp_bookmarks_var * btree_get_selected_bookmark (GtkTreeIter * out_iter)
{
  gftp_bookmarks_var *bmentry = NULL;
  GtkTreeModel *model;
  GtkTreeSelection *select = gtk_tree_view_get_selection (btree);
  GtkTreeIter iter;
  if (gtk_tree_selection_get_selected (select, &model, &iter))
  {
    gtk_tree_model_get (model, &iter,
                        BTREEVIEW_COL_BOOKMARK, &bmentry,
                        -1);
    if (out_iter) {
        *out_iter = iter;
    }
  }
  return (bmentry);
}


static void btree_remove_selected_node ()
{
  GtkTreeModel *model;
  GtkTreeStore *store;
  GtkTreeSelection *select = gtk_tree_view_get_selection (btree);
  GtkTreeIter iter;
  if (gtk_tree_selection_get_selected (select, &model, &iter))
  {
    store = GTK_TREE_STORE (model);
    gtk_tree_store_remove (store, &iter);
  }
}

static void btree_expand_collapse_selected_row (int key)
{
   GtkTreeModel *model;
   GtkTreeSelection *select = gtk_tree_view_get_selection (btree);
   GtkTreeIter iter;
   GtkTreePath * path;
   if (!gtk_tree_selection_get_selected (select, &model, &iter))
   {
      return;
   }
   path = gtk_tree_model_get_path (model, &iter);
   if (key == GDK_KEY_Left) {
      if (gtk_tree_view_row_expanded (btree, path)) {
         gtk_tree_view_collapse_row (btree, path);
      }
   } else if (key == GDK_KEY_Right) {
      if (!gtk_tree_view_row_expanded (btree, path)) {
         gtk_tree_view_expand_row (btree, path, FALSE);
      }
   }
   gtk_tree_path_free (path);
}

// ---

static gboolean /* GtkTreeModelForeachFunc */
gtktreemodel_to_gftp_foreach (GtkTreeModel *model,
                              GtkTreePath *path,
                              GtkTreeIter *iter,
                              gpointer data)
{
   /* see lib/gftp.h */
   gftp_bookmarks_var * entry, * parent, * next;
   GtkTreeIter iter2;
   GtkTreePath * treepath2;

   gtk_tree_model_get (model, iter, BTREEVIEW_COL_BOOKMARK, &entry, -1);
   if (!entry || !entry->path) {
      return FALSE;
   }

   /* convert GtkTreeModel to a singly linked list (tree) */

   entry->parent = NULL;
   entry->next   = NULL;
   if (!*entry->path) {
      /* empty path - root */
      gftp_bookmarks = entry;
      return FALSE;
   }

   treepath2 = gtk_tree_path_copy (path);
   gtk_tree_path_next (treepath2);
   if (gtk_tree_model_get_iter (model, &iter2, treepath2))
   {
      gtk_tree_model_get (model, &iter2,
                          BTREEVIEW_COL_BOOKMARK, &next, -1);
      entry->next = next;
   }
   gtk_tree_path_free (treepath2);

   treepath2 = gtk_tree_path_copy (path);
   if (gtk_tree_path_up (treepath2))
   {
      gtk_tree_model_get_iter (model, &iter2, treepath2);
      gtk_tree_model_get (model, &iter2,
                          BTREEVIEW_COL_BOOKMARK, &parent, -1);
      entry->parent = parent;
      if (parent && !parent->children) {
         parent->children = entry;
      }
   }
   gtk_tree_path_free (treepath2);

   ///fprintf (stderr, "%s\n", entry->path);
   g_hash_table_insert (gftp_bookmarks_htable, entry->path, entry);

   return FALSE;
}

static void
gtktreemodel_to_gftp (void)
{
   // The GtkTreeModel contains pointers to dynamically allocated bookmark entries
   // We'll use the same entries and only fix the pointers to parent and sibling nodes..
   GtkTreeModel * model = gtk_tree_view_get_model (btree);
   gtk_tree_model_foreach (model,
                           gtktreemodel_to_gftp_foreach,
                           NULL);
}

// ---

static gboolean /* GtkTreeModelForeachFunc */
gtktreemodel_find_bookmark_foreach (GtkTreeModel *model,
                                    GtkTreePath *path,
                                    GtkTreeIter *iter,
                                    gpointer data)
{
   struct btree_bookmark * out_bookmark = (struct btree_bookmark *) data;
   const char * path_to_find = (const char *) out_bookmark->data;
   gftp_bookmarks_var * entry;
   gtk_tree_model_get (model, iter, BTREEVIEW_COL_BOOKMARK, &entry, -1);
   if (!entry || !entry->path || !*entry->path) {
      return FALSE;
   }
   ///fprintf (stderr, "%s - %s\n", entry->path, btree_path_to_find);
   if (strcmp (entry->path, path_to_find) == 0)
   {
      out_bookmark->entry = entry;
      out_bookmark->iter = *iter;
      return TRUE;
   }
   return FALSE; // next
}


static void
gtktreemodel_find_bookmark (const char *path, struct btree_bookmark * out_bookmark)
{
   GtkTreeModel * model = gtk_tree_view_get_model (btree);
   out_bookmark->entry = NULL;
   if (!path || !*path) {
      return;
   }
   out_bookmark->data = (gpointer) path;
   gtk_tree_model_foreach (model,
                           gtktreemodel_find_bookmark_foreach,
                           out_bookmark);
}

