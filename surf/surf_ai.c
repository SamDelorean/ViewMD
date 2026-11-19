/*
 * Surf AI: GTK3 terminal for the Surface RT / postmarketOS.
 * Reuses ViewMD's editor/markdown renderer; no webview or API keys on device.
 * Copyright (c) 2026 Surf AI contributors. SPDX-License-Identifier: MIT
 */
#include "../src/app.h"
#include "../src/config.h"
#include "../src/editor.h"
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <string.h>

#define BRIDGE_HOST "127.0.0.1"
#define BRIDGE_PORT 8765
#define RENDER_INTERVAL_MS 120

typedef struct {
  GtkWidget *window, *input, *status, *send, *new_chat, *provider;
  GtkWidget *sidebar, *sidebar_restore, *session_list, *scroll, *mode_buttons[4];
  GtkWidget *load_more, *older, *recent, *rename_button, *delete_button;
  gchar *list_cursor;
  guint history_offset;
  gint mode;
  gboolean suppress_selection;
  gint reader_font_pt, pinch_start_pt;
  GtkCssProvider *reader_css;
  MarkydEditor *viewer;
  GString *transcript;
  gchar *session;
  gboolean busy;
  guint render_id;
} SurfUI;

typedef struct {
  SurfUI *ui;
  gchar *prompt, *provider, *session;
} Request;

typedef struct {
  SurfUI *ui;
  gchar *text;
  gboolean finished, error;
} UiEvent;

/* Required by ViewMD's existing renderer for relative local image paths. */
const gchar *markyd_app_get_current_path(MarkydApp *unused) {
  (void)unused;
  return NULL;
}

static gboolean do_render(gpointer ptr) {
  SurfUI *ui = ptr;
  ui->render_id = 0;
  markyd_editor_set_content(ui->viewer, ui->transcript->str);
  return G_SOURCE_REMOVE;
}

static void schedule_render(SurfUI *ui) {
  if (!ui->render_id)
    ui->render_id = g_timeout_add(RENDER_INTERVAL_MS, do_render, ui);
}

static gboolean refresh_after_answer(gpointer ptr);

static gboolean apply_event(gpointer ptr) {
  UiEvent *ev = ptr;
  SurfUI *ui = ev->ui;
  if (ev->text && ev->text[0]) {
    if (ev->error)
      g_string_append(ui->transcript, "\n\n**Error:** ");
    g_string_append(ui->transcript, ev->text);
    schedule_render(ui);
  }
  if (ev->finished) {
    ui->busy = FALSE;
    gtk_widget_set_sensitive(ui->send, TRUE);
    gtk_widget_set_sensitive(ui->new_chat, TRUE);
    for (guint i = 0; i < 4; i++)
      gtk_widget_set_sensitive(ui->mode_buttons[i], TRUE);
    gtk_label_set_text(GTK_LABEL(ui->status),
                       ev->error ? "Error del servidor" : "Listo");
    g_string_append(ui->transcript, "\n\n---\n\n");
    schedule_render(ui);
    if (!ev->error) g_idle_add(refresh_after_answer, ui);
  }
  g_free(ev->text);
  g_free(ev);
  return G_SOURCE_REMOVE;
}

static void post_event(SurfUI *ui, const gchar *text, gboolean finished,
                       gboolean error) {
  UiEvent *ev = g_new0(UiEvent, 1);
  ev->ui = ui;
  ev->text = g_strdup(text);
  ev->finished = finished;
  ev->error = error;
  g_idle_add(apply_event, ev);
}

static gchar *decode_b64(const gchar *encoded) {
  gsize length = 0;
  guchar *bytes = g_base64_decode(encoded, &length);
  gchar *out = g_utf8_make_valid((const gchar *)bytes, (gssize)length);
  g_free(bytes);
  return out;
}

static gpointer bridge_worker(gpointer ptr) {
  Request *req = ptr;
  GError *err = NULL;
  GSocketClient *client = g_socket_client_new();
  GSocketConnection *conn = NULL;
  GDataInputStream *input = NULL;
  GOutputStream *output;
  gchar *line = NULL;
  gchar *encoded, *command;
  gboolean finished = FALSE;

  g_socket_client_set_timeout(client, 180);
  conn = g_socket_client_connect_to_host(client, BRIDGE_HOST, BRIDGE_PORT,
                                        NULL, &err);
  if (!conn)
    goto failure;

  encoded = g_base64_encode((const guchar *)req->prompt, strlen(req->prompt));
  command = g_strdup_printf("ASK\t%s\t%s\t%s\n",
                            req->provider, req->session, encoded);
  g_free(encoded);
  output = g_io_stream_get_output_stream(G_IO_STREAM(conn));
  if (!g_output_stream_write_all(output, command, strlen(command),
                                 NULL, NULL, &err) ||
      !g_output_stream_flush(output, NULL, &err)) {
    g_free(command);
    goto failure;
  }
  g_free(command);

  input = g_data_input_stream_new(
      g_io_stream_get_input_stream(G_IO_STREAM(conn)));
  g_data_input_stream_set_newline_type(input, G_DATA_STREAM_NEWLINE_TYPE_LF);
  while ((line = g_data_input_stream_read_line(input, NULL, NULL, &err))) {
    if (g_str_has_prefix(line, "DELTA\t")) {
      gchar *text = decode_b64(line + 6);
      post_event(req->ui, text, FALSE, FALSE);
      g_free(text);
    } else if (g_str_has_prefix(line, "ERROR\t")) {
      gchar *text = decode_b64(line + 6);
      post_event(req->ui, text, TRUE, TRUE);
      g_free(text);
      finished = TRUE;
    } else if (g_strcmp0(line, "DONE") == 0) {
      post_event(req->ui, NULL, TRUE, FALSE);
      finished = TRUE;
    }
    g_free(line);
    line = NULL;
    if (finished)
      break;
  }

failure:
  if (!finished) {
    post_event(req->ui,
               err ? err->message : "Conexion cerrada sin respuesta completa",
               TRUE, TRUE);
  }
  g_clear_error(&err);
  g_clear_object(&input);
  if (conn) {
    g_io_stream_close(G_IO_STREAM(conn), NULL, NULL);
    g_object_unref(conn);
  }
  g_object_unref(client);
  g_free(req->prompt);
  g_free(req->provider);
  g_free(req->session);
  g_free(req);
  return NULL;
}

static void on_send(GtkWidget *widget, gpointer ptr) {
  SurfUI *ui = ptr;
  GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(ui->input));
  GtkTextIter start, end;
  gchar *prompt;
  Request *req;
  GThread *thread;
  const gchar *provider;
  (void)widget;

  if (ui->busy) return;
  gtk_text_buffer_get_bounds(buffer, &start, &end);
  prompt = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
  g_strstrip(prompt);
  if (!prompt[0]) {
    g_free(prompt);
    return;
  }
  if (ui->mode == 1 || ui->mode == 3) {
    gtk_label_set_text(GTK_LABEL(ui->status),
      "Herramienta de codigo pendiente; requiere autorizacion expresa.");
    g_free(prompt);
    return;
  }
  provider = ui->mode == 2 ? "anthropic" : "openai";

  g_string_append_printf(ui->transcript, "### Tú\n\n%s\n\n### %s\n\n",
                         prompt, g_strcmp0(provider, "anthropic") == 0
                                       ? "Claude" : "GPT");
  schedule_render(ui);
  gtk_text_buffer_set_text(buffer, "", -1);
  gtk_label_set_text(GTK_LABEL(ui->status), "Consultando T160...");
  ui->busy = TRUE;
  gtk_widget_set_sensitive(ui->send, FALSE);
  gtk_widget_set_sensitive(ui->new_chat, FALSE);
  for (guint i = 0; i < 4; i++)
    gtk_widget_set_sensitive(ui->mode_buttons[i], FALSE);

  req = g_new0(Request, 1);
  req->ui = ui;
  req->prompt = prompt;
  req->provider = g_strdup(provider);
  req->session = g_strdup(ui->session);
  thread = g_thread_new("surf-bridge", bridge_worker, req);
  g_thread_unref(thread);
}

static gboolean on_input_key(GtkWidget *widget, GdkEventKey *event,
                              gpointer ptr) {
  if ((event->state & GDK_CONTROL_MASK) &&
      (event->keyval == GDK_KEY_Return ||
       event->keyval == GDK_KEY_KP_Enter)) {
    on_send(widget, ptr);
    return TRUE;
  }
  return FALSE;
}


/* v0.2 catalog: only compact metadata travels to Surf; max 20 rows/page.
   A catalog operation is user-initiated, bounded to 4 seconds. */
static const gchar *selected_provider(SurfUI *ui) {
  return ui->mode == 2 || ui->mode == 3 ? "anthropic" : "openai";
}

static gchar *catalog_exchange(SurfUI *ui, const gchar *op,
                               const gchar *session, const gchar *payload) {
  GSocketClient *client = g_socket_client_new();
  GSocketConnection *conn;
  GDataInputStream *input;
  GOutputStream *output;
  GError *error = NULL;
  gchar *frame, *line, *result;
  GString *response = g_string_new("");
  g_socket_client_set_timeout(client, 4);
  conn = g_socket_client_connect_to_host(client, BRIDGE_HOST, BRIDGE_PORT,
                                        NULL, &error);
  if (!conn) goto failed;
  frame = g_strdup_printf("%s\t%s\t%s\t%s\n", op, selected_provider(ui),
                           session ? session : ui->session,
                           payload ? payload : "-");
  output = g_io_stream_get_output_stream(G_IO_STREAM(conn));
  if (!g_output_stream_write_all(output, frame, strlen(frame), NULL,
                                  NULL, &error) ||
      !g_output_stream_flush(output, NULL, &error)) {
    g_free(frame);
    goto failed;
  }
  g_free(frame);
  input = g_data_input_stream_new(
      g_io_stream_get_input_stream(G_IO_STREAM(conn)));
  g_data_input_stream_set_newline_type(input, G_DATA_STREAM_NEWLINE_TYPE_LF);
  while ((line = g_data_input_stream_read_line(input,NULL,NULL,&error))) {
    if (g_strcmp0(line, "DONE") == 0) {
      g_free(line);
      break;
    }
    if (g_str_has_prefix(line, "ERROR\t")) {
      gchar *msg = decode_b64(line + 6);
      gtk_label_set_text(GTK_LABEL(ui->status), msg);
      g_free(msg);
      g_free(line);
      g_string_truncate(response, 0);
      break;
    }
    gchar **parts = g_strsplit(line, "\t", 2);
    if (parts[0] && parts[1]) {
      g_string_append_printf(response, "%s\t%s\n", parts[0], parts[1]);
    }
    g_strfreev(parts);
    g_free(line);
  }
  g_object_unref(input);
  g_io_stream_close(G_IO_STREAM(conn), NULL, NULL);
  g_object_unref(conn);
  g_object_unref(client);
  return g_string_free(response, FALSE);
failed:
  result = g_strdup_printf("Sin conexión con T160: %s",
                           error ? error->message : "desconocido");
  gtk_label_set_text(GTK_LABEL(ui->status), result);
  g_free(result);
  g_clear_error(&error);
  if (conn) g_object_unref(conn);
  g_object_unref(client);
  g_string_free(response, TRUE);
  return NULL;
}

static GtkListBoxRow *selected_row(SurfUI *ui) {
  return gtk_list_box_get_selected_row(GTK_LIST_BOX(ui->session_list));
}

static void load_history(SurfUI *ui, guint offset) {
  if (ui->busy) return;
  gchar *number = g_strdup_printf("%u", offset);
  gchar *reply = catalog_exchange(ui, "HISTORY", ui->session, number);
  g_free(number);
  if (!reply) return;
  GString *md = g_string_new("");
  gboolean more = FALSE;
  gchar **lines = g_strsplit(reply, "\n", -1);
  for (guint i = 0; lines[i]; i++) {
    if (g_str_has_prefix(lines[i], "ITEM\t")) {
      gchar *body = decode_b64(lines[i]+5);
      if (g_str_has_prefix(body, "user\t")) {
        g_string_append_printf(md, "### Tú\n\n%s\n\n---\n\n", body+5);
      } else if (g_str_has_prefix(body, "assistant\t")) {
        g_string_append_printf(md, "### Asistente\n\n%s\n\n---\n\n", body+10);
      }
      g_free(body);
    } else if (g_str_has_prefix(lines[i], "MORE\t")) {
      gchar *value = decode_b64(lines[i]+5);
      more = g_strcmp0(value, "1") == 0;
      g_free(value);
    }
  }
  g_strfreev(lines);
  if (md->len == 0 && offset == 0)
    g_string_append(md,"# Conversación nueva\n\nEscribe un mensaje abajo.\n");
  g_string_assign(ui->transcript, md->str);
  g_string_free(md, TRUE);
  ui->history_offset = offset;
  gtk_widget_set_sensitive(ui->older, more);
  gtk_widget_set_sensitive(ui->recent, offset > 0);
  schedule_render(ui);
  g_free(reply);
}

static void on_older(GtkButton *btn, gpointer ptr) {
  SurfUI *ui = ptr; (void)btn;
  load_history(ui, ui->history_offset + 5);
}
static void on_recent(GtkButton *btn, gpointer ptr) {
  SurfUI *ui = ptr; (void)btn;
  load_history(ui, 0);
}

static void on_session_selected(GtkListBox *box, GtkListBoxRow *row,
                                gpointer ptr) {
  SurfUI *ui = ptr; (void)box;
  if (!row || ui->busy || ui->suppress_selection) return;
  const gchar *id = g_object_get_data(G_OBJECT(row), "session-id");
  if (!id) return;
  g_free(ui->session);
  ui->session = g_strdup(id);
  load_history(ui, 0);
}

static void on_row_long_press(GtkGestureLongPress *gesture,
                              gdouble x, gdouble y, gpointer ptr);

static void load_catalog(SurfUI *ui, gboolean append) {
  if (ui->busy || !ui->session_list) return;
  if (!append) {
    GList *children = gtk_container_get_children(GTK_CONTAINER(ui->session_list));
    for (GList *it = children; it; it = it->next)
      gtk_widget_destroy(GTK_WIDGET(it->data));
    g_list_free(children);
    g_clear_pointer(&ui->list_cursor, g_free);
  }
  gchar *reply = catalog_exchange(ui, "LIST", ui->session,
                                   ui->list_cursor ? ui->list_cursor : "-");
  if (!reply) return;
  gchar **lines = g_strsplit(reply, "\n", -1);
  gboolean more = FALSE;
  for (guint i = 0; lines[i]; i++) {
    if (g_str_has_prefix(lines[i], "SESSION\t")) {
      gchar *decoded = decode_b64(lines[i]+8);
      gchar **fields = g_strsplit(decoded, "\t", 3);
      if (fields[0] && fields[1] && fields[2]) {
        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *text = gtk_label_new(fields[2]);
        gtk_label_set_xalign(GTK_LABEL(text), 0);
        gtk_label_set_ellipsize(GTK_LABEL(text), PANGO_ELLIPSIZE_END);
        gtk_widget_set_margin_start(text, 5);
        gtk_widget_set_margin_end(text, 5);
        gtk_widget_set_margin_top(text, 6);
        gtk_widget_set_margin_bottom(text, 6);
        gtk_container_add(GTK_CONTAINER(row), text);
        gtk_widget_set_size_request(row, -1, 47);
        GtkGesture *press = gtk_gesture_long_press_new(row);
        gtk_gesture_single_set_touch_only(GTK_GESTURE_SINGLE(press), TRUE);
        g_signal_connect(press, "pressed",
                         G_CALLBACK(on_row_long_press), ui);
        g_object_set_data_full(G_OBJECT(row), "session-id",
                               g_strdup(fields[0]), g_free);
        gtk_list_box_insert(GTK_LIST_BOX(ui->session_list), row, -1);
      }
      g_strfreev(fields);
      g_free(decoded);
    } else if (g_str_has_prefix(lines[i], "NEXT\t")) {
      g_free(ui->list_cursor);
      ui->list_cursor = decode_b64(lines[i]+5);
      more = g_strcmp0(ui->list_cursor, "-") != 0;
    }
  }
  g_strfreev(lines);
  g_free(reply);
  gtk_widget_show_all(ui->session_list);
  gtk_widget_set_sensitive(ui->load_more, more);
}

static void on_load_more(GtkButton *btn, gpointer ptr) {
  (void)btn; load_catalog(ptr, TRUE);
}

static void on_rename(GtkButton *btn, gpointer ptr) {
  SurfUI *ui = ptr; (void)btn;
  GtkListBoxRow *row = selected_row(ui);
  if (!row || ui->busy) return;
  const gchar *id = g_object_get_data(G_OBJECT(row), "session-id");
  GtkWidget *dialog = gtk_dialog_new_with_buttons(
      "Renombrar conversación", GTK_WINDOW(ui->window), GTK_DIALOG_MODAL,
      "_Cancelar", GTK_RESPONSE_CANCEL, "_Guardar", GTK_RESPONSE_OK, NULL);
  GtkWidget *entry = gtk_entry_new();
  gtk_entry_set_max_length(GTK_ENTRY(entry), 100);
  GtkWidget *row_text = gtk_bin_get_child(GTK_BIN(row));
  gtk_entry_set_text(GTK_ENTRY(entry),
    GTK_IS_LABEL(row_text) ? gtk_label_get_text(GTK_LABEL(row_text))
                           : "Conversación");
  gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(
                        GTK_DIALOG(dialog))), entry);
  gtk_widget_show_all(dialog);
  if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
    const gchar *title = gtk_entry_get_text(GTK_ENTRY(entry));
    if (*title) {
      gchar *encoded = g_base64_encode((const guchar *)title, strlen(title));
      gchar *answer = catalog_exchange(ui, "RENAME", id, encoded);
      g_free(answer);
      g_free(encoded);
      load_catalog(ui, FALSE);
    }
  }
  gtk_widget_destroy(dialog);
}

static void on_delete(GtkButton *btn, gpointer ptr) {
  SurfUI *ui = ptr; (void)btn;
  GtkListBoxRow *row = selected_row(ui);
  if (!row || ui->busy) return;
  gchar *id = g_strdup(g_object_get_data(G_OBJECT(row), "session-id"));
  GtkWidget *dialog = gtk_message_dialog_new(
      GTK_WINDOW(ui->window), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING,
      GTK_BUTTONS_NONE, "¿Eliminar esta conversación?");
  gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
      "Se eliminarán de Surf AI esta conversación y sus mensajes. "
      "No se puede deshacer.");
  gtk_dialog_add_buttons(GTK_DIALOG(dialog), "_Cancelar", GTK_RESPONSE_CANCEL,
                         "_Eliminar", GTK_RESPONSE_ACCEPT, NULL);
  if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
    gchar *answer = catalog_exchange(ui, "DELETE", id, "-");
    g_free(answer);
    if (g_strcmp0(ui->session, id) == 0) {
      g_free(ui->session);
      ui->session = g_uuid_string_random();
      g_string_assign(ui->transcript, "# Surf AI\n\nNueva conversación.\n");
      schedule_render(ui);
    }
    load_catalog(ui, FALSE);
  }
  gtk_widget_destroy(dialog);
  g_free(id);
}

/* Long touch opens contextual actions without fetching a conversation. */
static void on_touch_rename(GtkButton *button, gpointer ptr) {
  GtkWidget *popover = gtk_widget_get_ancestor(GTK_WIDGET(button),
                                               GTK_TYPE_POPOVER);
  if (popover) gtk_widget_destroy(popover);
  on_rename(button, ptr);
}

static void on_touch_delete(GtkButton *button, gpointer ptr) {
  GtkWidget *popover = gtk_widget_get_ancestor(GTK_WIDGET(button),
                                               GTK_TYPE_POPOVER);
  if (popover) gtk_widget_destroy(popover);
  on_delete(button, ptr);
}

static void on_row_long_press(GtkGestureLongPress *gesture, gdouble x,
                              gdouble y, gpointer ptr) {
  SurfUI *ui = ptr;
  GtkWidget *row = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
  (void)x; (void)y;
  if (!GTK_IS_LIST_BOX_ROW(row) || ui->busy) return;
  ui->suppress_selection = TRUE;
  gtk_list_box_select_row(GTK_LIST_BOX(ui->session_list),
                          GTK_LIST_BOX_ROW(row));
  ui->suppress_selection = FALSE;

  GtkWidget *popover = gtk_popover_new(row);
  GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  GtkWidget *rename = gtk_button_new_with_label("Renombrar");
  GtkWidget *remove = gtk_button_new_with_label("Eliminar...");
  gtk_container_set_border_width(GTK_CONTAINER(actions), 8);
  gtk_widget_set_size_request(rename, 130, 46);
  gtk_widget_set_size_request(remove, 130, 46);
  gtk_container_add(GTK_CONTAINER(popover), actions);
  gtk_box_pack_start(GTK_BOX(actions), rename, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(actions), remove, FALSE, FALSE, 0);
  g_signal_connect(rename, "clicked", G_CALLBACK(on_touch_rename), ui);
  g_signal_connect(remove, "clicked", G_CALLBACK(on_touch_delete), ui);
  gtk_widget_show_all(popover);
  gtk_popover_popup(GTK_POPOVER(popover));
}

static gboolean refresh_after_answer(gpointer ptr) {
  SurfUI *ui = ptr;
  load_history(ui, 0);
  load_catalog(ui, FALSE);
  return G_SOURCE_REMOVE;
}

static void on_toggle_sidebar(GtkButton *button, gpointer ptr) {
  SurfUI *ui = ptr;
  gboolean show_sidebar;
  (void)button;
  show_sidebar = !gtk_revealer_get_reveal_child(GTK_REVEALER(ui->sidebar));
  gtk_revealer_set_reveal_child(GTK_REVEALER(ui->sidebar), show_sidebar);
  /* A separate minimal menu button remains reachable when the sidebar
     is collapsed; the entire top header has been removed. */
  if (show_sidebar)
    gtk_widget_hide(ui->sidebar_restore);
  else
    gtk_widget_show(ui->sidebar_restore);
}

static void on_mode_selected(GtkToggleButton *button, gpointer ptr) {
  SurfUI *ui = ptr;
  gint selected = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button),
                                                   "surf-mode"));
  if (!gtk_toggle_button_get_active(button)) {
    if (ui->mode == selected) gtk_toggle_button_set_active(button, TRUE);
    return;
  }
  ui->mode = selected;
  for (gint i = 0; i < 4; i++) {
    if (ui->mode_buttons[i] != GTK_WIDGET(button))
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui->mode_buttons[i]),
                                   FALSE);
  }
  if (ui->status)
    gtk_label_set_text(GTK_LABEL(ui->status),
      (selected == 1 || selected == 3)
      ? "Herramienta de codigo: integracion pendiente, sin ejecucion"
      : "Proveedor de chat seleccionado");
  if (ui->session_list && !ui->busy) load_catalog(ui, FALSE);
}

/* Multitouch pinch adjusts text size without reloading conversation history. */
static void apply_reader_font(SurfUI *ui, gint size) {
  ui->reader_font_pt = CLAMP(size, 11, 24);
  gchar css[90];
  g_snprintf(css, sizeof(css), "textview { font-size: %dpt; }",
             ui->reader_font_pt);
  gtk_css_provider_load_from_data(ui->reader_css, css, -1, NULL);
}

static void on_zoom_begin(GtkGesture *gesture, GdkEventSequence *sequence,
                          gpointer ptr) {
  SurfUI *ui = ptr;
  (void)gesture; (void)sequence;
  ui->pinch_start_pt = ui->reader_font_pt;
}

static void on_zoom_changed(GtkGestureZoom *gesture, gdouble scale,
                            gpointer ptr) {
  SurfUI *ui = ptr;
  (void)gesture;
  gint pt = (gint)(ui->pinch_start_pt * scale + 0.5);
  if (CLAMP(pt, 11, 24) != ui->reader_font_pt)
    apply_reader_font(ui, pt);
}

static gboolean on_reader_key(GtkWidget *widget, GdkEventKey *event,
                              gpointer ptr) {
  SurfUI *ui = ptr;
  GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(
      GTK_SCROLLED_WINDOW(ui->scroll));
  gdouble amount = 0;
  (void)widget;
  if (event->keyval == GDK_KEY_Up) amount = -38;
  else if (event->keyval == GDK_KEY_Down) amount = 38;
  else if (event->keyval == GDK_KEY_Page_Up)
    amount = -gtk_adjustment_get_page_increment(adj);
  else if (event->keyval == GDK_KEY_Page_Down)
    amount = gtk_adjustment_get_page_increment(adj);
  else return FALSE;
  gtk_adjustment_set_value(adj,
    CLAMP(gtk_adjustment_get_value(adj) + amount,
      gtk_adjustment_get_lower(adj),
      MAX(gtk_adjustment_get_lower(adj),
          gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj))));
  return TRUE;
}

static void on_new_chat(GtkWidget *widget, gpointer ptr) {
  SurfUI *ui = ptr;
  (void)widget;
  if (ui->busy) return;
  g_free(ui->session);
  ui->session = g_uuid_string_random();
  g_string_assign(ui->transcript,
      "# Surf AI\n\nNueva conversación. Escribe abajo y pulsa **Ctrl+Enter**.\n\n"
      "La Surf solo dibuja la interfaz. GPT y Claude se consultan desde la T160.\n\n---\n\n");
  gtk_label_set_text(GTK_LABEL(ui->status), "Nueva conversación");
  schedule_render(ui);
}

int main(int argc, char **argv) {
  SurfUI ui = {0};
  GtkWidget *root, *scroller, *input_scroller, *bottom;
  GtkWidget *workspace, *sidebar_box, *main_column, *toggle;
  GtkWidget *side_scroll, *side_controls, *history_controls;
  GtkWidget *mode_row1, *mode_row2, *sidebar_restore;
  const gchar *modes[4] = {"GPT", "Codex", "Claude", "Claude Code"};
  gtk_init(&argc, &argv);
  config = config_new();
  /* Do not read or alter the original ViewMD settings. */
  g_free(config->theme);
  config->theme = g_strdup("system");
  config->font_size = 13;

  ui.transcript = g_string_new("");
  ui.reader_font_pt = 13;
  ui.pinch_start_pt = 13;
  ui.session = g_uuid_string_random();
  ui.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(ui.window), "Surf AI");
  gtk_window_set_default_size(GTK_WINDOW(ui.window), 1024, 680);
  gtk_window_maximize(GTK_WINDOW(ui.window));
  g_signal_connect(ui.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

  root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_container_set_border_width(GTK_CONTAINER(root), 8);
  gtk_container_add(GTK_CONTAINER(ui.window), root);

  /* The workspace occupies the full window height: no separate top bar. */
  workspace = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start(GTK_BOX(root), workspace, TRUE, TRUE, 0);

  /* Only visible when the left sidebar is hidden, so it can be reopened. */
  sidebar_restore = gtk_button_new_with_label("☰");
  ui.sidebar_restore = sidebar_restore;
  gtk_widget_set_tooltip_text(sidebar_restore, "Mostrar conversaciones");
  gtk_widget_set_size_request(sidebar_restore, 48, 48);
  gtk_widget_set_valign(sidebar_restore, GTK_ALIGN_START);
  gtk_widget_set_no_show_all(sidebar_restore, TRUE);
  gtk_box_pack_start(GTK_BOX(workspace), sidebar_restore, FALSE, FALSE, 0);
  g_signal_connect(sidebar_restore, "clicked",
                   G_CALLBACK(on_toggle_sidebar), &ui);

  ui.sidebar = gtk_revealer_new();
  gtk_revealer_set_transition_type(GTK_REVEALER(ui.sidebar),
                                   GTK_REVEALER_TRANSITION_TYPE_SLIDE_RIGHT);
  gtk_revealer_set_reveal_child(GTK_REVEALER(ui.sidebar), TRUE);
  gtk_box_pack_start(GTK_BOX(workspace), ui.sidebar, FALSE, FALSE, 0);

  sidebar_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  gtk_widget_set_size_request(sidebar_box, 252, -1);
  gtk_container_add(GTK_CONTAINER(ui.sidebar), sidebar_box);

  /* Row 1, left-to-right: menu, GPT, Codex. */
  mode_row1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_box_pack_start(GTK_BOX(sidebar_box), mode_row1, FALSE, FALSE, 0);
  toggle = gtk_button_new_with_label("☰");
  gtk_widget_set_tooltip_text(toggle, "Ocultar conversaciones");
  gtk_widget_set_size_request(toggle, 48, 46);
  gtk_box_pack_start(GTK_BOX(mode_row1), toggle, FALSE, FALSE, 0);
  g_signal_connect(toggle, "clicked", G_CALLBACK(on_toggle_sidebar), &ui);

  /* Row 2: Claude, Claude Code. No duplicated provider heading. */
  mode_row2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_box_pack_start(GTK_BOX(sidebar_box), mode_row2, FALSE, FALSE, 0);
  for (gint i = 0; i < 4; i++) {
    GtkWidget *row = i < 2 ? mode_row1 : mode_row2;
    ui.mode_buttons[i] = gtk_toggle_button_new_with_label(modes[i]);
    gtk_widget_set_size_request(ui.mode_buttons[i], -1, 46);
    g_object_set_data(G_OBJECT(ui.mode_buttons[i]), "surf-mode",
                      GINT_TO_POINTER(i));
    gtk_box_pack_start(GTK_BOX(row), ui.mode_buttons[i], TRUE, TRUE, 0);
    g_signal_connect(ui.mode_buttons[i], "toggled",
                     G_CALLBACK(on_mode_selected), &ui);
  }
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui.mode_buttons[0]), TRUE);

  /* Row 3: new conversation. The list begins directly below it. */
  ui.new_chat = gtk_button_new_with_label("+ Nueva conversación");
  gtk_widget_set_size_request(ui.new_chat, -1, 46);
  gtk_box_pack_start(GTK_BOX(sidebar_box), ui.new_chat, FALSE, FALSE, 0);
  g_signal_connect(ui.new_chat, "clicked", G_CALLBACK(on_new_chat), &ui);

  side_scroll = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_kinetic_scrolling(
      GTK_SCROLLED_WINDOW(side_scroll), TRUE);
  gtk_scrolled_window_set_overlay_scrolling(
      GTK_SCROLLED_WINDOW(side_scroll), FALSE);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(side_scroll),
                                 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_box_pack_start(GTK_BOX(sidebar_box), side_scroll, TRUE, TRUE, 0);
  ui.session_list = gtk_list_box_new();
  gtk_container_add(GTK_CONTAINER(side_scroll), ui.session_list);
  g_signal_connect(ui.session_list, "row-selected",
                   G_CALLBACK(on_session_selected), &ui);
  ui.load_more = gtk_button_new_with_label("Cargar 20 más");
  gtk_widget_set_size_request(ui.load_more, -1, 46);
  gtk_box_pack_start(GTK_BOX(sidebar_box), ui.load_more, FALSE, FALSE, 0);
  g_signal_connect(ui.load_more,"clicked",G_CALLBACK(on_load_more),&ui);
  side_controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_box_pack_start(GTK_BOX(sidebar_box), side_controls, FALSE, FALSE, 2);
  ui.rename_button = gtk_button_new_with_label("Renombrar");
  ui.delete_button = gtk_button_new_with_label("Eliminar");
  gtk_widget_set_size_request(ui.rename_button, -1, 46);
  gtk_widget_set_size_request(ui.delete_button, -1, 46);
  gtk_box_pack_start(GTK_BOX(side_controls),ui.rename_button,TRUE,TRUE,0);
  gtk_box_pack_start(GTK_BOX(side_controls),ui.delete_button,TRUE,TRUE,0);
  g_signal_connect(ui.rename_button,"clicked",G_CALLBACK(on_rename),&ui);
  g_signal_connect(ui.delete_button,"clicked",G_CALLBACK(on_delete),&ui);
  main_column = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_set_hexpand(main_column, TRUE);
  gtk_box_pack_start(GTK_BOX(workspace), main_column, TRUE, TRUE, 6);
  scroller = gtk_scrolled_window_new(NULL, NULL);
  ui.scroll = scroller;
  gtk_scrolled_window_set_kinetic_scrolling(
      GTK_SCROLLED_WINDOW(scroller), TRUE);
  gtk_scrolled_window_set_overlay_scrolling(
      GTK_SCROLLED_WINDOW(scroller), FALSE);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
  gtk_box_pack_start(GTK_BOX(main_column), scroller, TRUE, TRUE, 0);
  ui.viewer = markyd_editor_new(NULL);
  gtk_container_add(GTK_CONTAINER(scroller),
                    markyd_editor_get_widget(ui.viewer));
  ui.reader_css = gtk_css_provider_new();
  gtk_style_context_add_provider(
      gtk_widget_get_style_context(ui.viewer->text_view),
      GTK_STYLE_PROVIDER(ui.reader_css),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  apply_reader_font(&ui, ui.reader_font_pt);
  GtkGesture *zoom = gtk_gesture_zoom_new(ui.viewer->text_view);
  g_signal_connect(zoom, "begin", G_CALLBACK(on_zoom_begin), &ui);
  g_signal_connect(zoom, "scale-changed",
                   G_CALLBACK(on_zoom_changed), &ui);
  g_signal_connect(ui.viewer->text_view, "key-press-event",
                   G_CALLBACK(on_reader_key), &ui);
  history_controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_box_pack_start(GTK_BOX(main_column),history_controls,FALSE,FALSE,0);
  ui.older = gtk_button_new_with_label("Mensajes anteriores");
  ui.recent = gtk_button_new_with_label("Volver a recientes");
  gtk_box_pack_start(GTK_BOX(history_controls),ui.older,FALSE,FALSE,0);
  gtk_box_pack_start(GTK_BOX(history_controls),ui.recent,FALSE,FALSE,0);
  gtk_widget_set_sensitive(ui.older,FALSE);
  gtk_widget_set_sensitive(ui.recent,FALSE);
  g_signal_connect(ui.older,"clicked",G_CALLBACK(on_older),&ui);
  g_signal_connect(ui.recent,"clicked",G_CALLBACK(on_recent),&ui);
  bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_pack_start(GTK_BOX(main_column), bottom, FALSE, FALSE, 0);
  input_scroller = gtk_scrolled_window_new(NULL, NULL);
  gtk_widget_set_size_request(input_scroller, -1, 92);
  gtk_widget_set_hexpand(input_scroller, TRUE);
  gtk_box_pack_start(GTK_BOX(bottom), input_scroller, TRUE, TRUE, 0);
  ui.input = gtk_text_view_new();
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(ui.input), GTK_WRAP_WORD_CHAR);
  gtk_container_add(GTK_CONTAINER(input_scroller), ui.input);
  g_signal_connect(ui.input, "key-press-event",
                   G_CALLBACK(on_input_key), &ui);
  ui.send = gtk_button_new_with_label("Enviar");
  gtk_widget_set_size_request(ui.send, 96, 48);
  gtk_box_pack_end(GTK_BOX(bottom), ui.send, FALSE, FALSE, 0);
  g_signal_connect(ui.send, "clicked", G_CALLBACK(on_send), &ui);

  ui.status = gtk_label_new("T160 via tunel SSH local (127.0.0.1:8765)");
  gtk_label_set_xalign(GTK_LABEL(ui.status), 0);
  gtk_box_pack_start(GTK_BOX(main_column), ui.status, FALSE, FALSE, 0);
  on_new_chat(NULL, &ui);
  gtk_widget_show_all(ui.window);
  load_catalog(&ui, FALSE);
  gtk_widget_grab_focus(ui.input);
  gtk_main();
  /* Background worker is intentionally owned by the process until exit. */
  return 0;
}
