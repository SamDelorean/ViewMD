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
#define SIDEBAR_OPEN_PX 260
#define SIDEBAR_CLOSED_PX 52

typedef struct {
  GtkWidget *window, *input, *status, *send, *new_chat, *provider;
  GtkWidget *sidebar, *sidebar_restore, *splitter, *session_list, *scroll, *mode_buttons[4];
  GtkWidget *load_more, *older, *recent;
  gchar *list_cursor;
  guint history_offset;
  gint mode;
  gint reader_font_pt, pinch_start_pt;
  GtkCssProvider *reader_css;
  MarkydEditor *viewer;
  GString *transcript;
  gchar *session, *conversation_title;
  gboolean new_session, mode_initialized;
  gboolean busy;
  guint render_id, status_pulse_id;
  gboolean status_pulse_bright;
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


static const gchar *mode_name(gint mode) {
  static const gchar *names[] = {"GPT", "Codex", "Claude", "Claude Code"};
  return (mode >= 0 && mode < 4) ? names[mode] : "GPT";
}

static void update_window_title(SurfUI *ui) {
  if (!ui->window) return;
  const gchar *name = ui->conversation_title ?
                      ui->conversation_title : "nueva conversación";
  gchar *title = g_strdup_printf("Surf AI - %s - %s",
                                  name, mode_name(ui->mode));
  gtk_window_set_title(GTK_WINDOW(ui->window), title);
  g_free(title);
}

static void set_conversation_title(SurfUI *ui, const gchar *title) {
  g_free(ui->conversation_title);
  ui->conversation_title = g_strdup((title && *title) ?
                                    title : "nueva conversación");
  update_window_title(ui);
}

/* The status row is hidden when idle. Errors and ongoing work still show. */
static void set_status(SurfUI *ui, const gchar *message) {
  if (!ui->status) return;
  gboolean visible = message && *message;
  gtk_label_set_text(GTK_LABEL(ui->status), visible ? message : "");
  if (visible) gtk_widget_show(ui->status);
  else gtk_widget_hide(ui->status);
}

static void reset_conversation_view(SurfUI *ui) {
  g_string_assign(ui->transcript, "");
  ui->history_offset = 0;
  if (ui->older) gtk_widget_set_sensitive(ui->older, FALSE);
  if (ui->recent) gtk_widget_set_sensitive(ui->recent, FALSE);
  if (ui->viewer) schedule_render(ui);
}

/* Color is applied to the same circular "⋯" button: no extra column.
   This is a local GTK animation only while an existing row is working,
   never a network polling or background monitoring process. */
static void install_conversation_css(void) {
  static const gchar css[] =
      "button.surf-chat-menu { min-width:34px; min-height:34px;"
      " padding:0px; border-radius:19px; background-image:none;"
      " box-shadow:none; border:1px solid rgba(0,0,0,0.14);"
      " font-size:16px; font-weight:bold; }"
      "button.surf-chat-menu.surf-state-idle {background-color:#879199;color:white;}"
      "button.surf-chat-menu.surf-state-working {background-color:#e2b632;color:#382600;}"
      "button.surf-chat-menu.surf-state-working.surf-pulse {background-color:#fff0aa;color:#382600;}"
      "button.surf-chat-menu.surf-state-ready {background-color:#418e56;color:white;}"
      "button.surf-chat-menu.surf-state-error {background-color:#c55252;color:white;}";
  GtkCssProvider *provider = gtk_css_provider_new();
  gtk_css_provider_load_from_data(provider, css, -1, NULL);
  gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(provider);
}

static const gchar *valid_row_state(const gchar *status) {
  if (g_strcmp0(status, "working") == 0) return "working";
  if (g_strcmp0(status, "ready") == 0) return "ready";
  if (g_strcmp0(status, "error") == 0) return "error";
  return "idle";
}

static void set_menu_state(GtkWidget *button, const gchar *status) {
  GtkStyleContext *style = gtk_widget_get_style_context(button);
  static const gchar *classes[] = {
      "surf-state-idle", "surf-state-working", "surf-state-ready",
      "surf-state-error", "surf-pulse"
  };
  for (guint i = 0; i < G_N_ELEMENTS(classes); ++i)
    gtk_style_context_remove_class(style, classes[i]);
  const gchar *normalized = valid_row_state(status);
  gchar *klass = g_strconcat("surf-state-", normalized, NULL);
  gtk_style_context_add_class(style, "surf-chat-menu");
  gtk_style_context_add_class(style, klass);
  g_free(klass);
  const gchar *tip =
      g_strcmp0(normalized, "working") == 0 ? "Procesando respuesta · Opciones" :
      g_strcmp0(normalized, "ready") == 0 ? "Respuesta lista · Opciones" :
      g_strcmp0(normalized, "error") == 0 ? "Error de conversación · Opciones" :
                                           "Consultada / sin actividad · Opciones";
  gtk_widget_set_tooltip_text(button, tip);
}

static gboolean pulse_working_buttons(gpointer ptr) {
  SurfUI *ui = ptr;
  gboolean any = FALSE;
  ui->status_pulse_bright = !ui->status_pulse_bright;
  GList *rows = gtk_container_get_children(GTK_CONTAINER(ui->session_list));
  for (GList *node = rows; node; node = node->next) {
    GtkWidget *row = GTK_WIDGET(node->data);
    const gchar *status = g_object_get_data(G_OBJECT(row), "session-status");
    GtkWidget *button = g_object_get_data(G_OBJECT(row), "status-button");
    if (g_strcmp0(status, "working") == 0 && GTK_IS_BUTTON(button)) {
      any = TRUE;
      GtkStyleContext *style = gtk_widget_get_style_context(button);
      if (ui->status_pulse_bright)
        gtk_style_context_add_class(style, "surf-pulse");
      else
        gtk_style_context_remove_class(style, "surf-pulse");
    }
  }
  g_list_free(rows);
  if (!any) {
    ui->status_pulse_id = 0;
    return G_SOURCE_REMOVE;
  }
  return G_SOURCE_CONTINUE;
}

static void ensure_status_pulse(SurfUI *ui) {
  if (!ui->status_pulse_id)
    ui->status_pulse_id = g_timeout_add(650, pulse_working_buttons, ui);
}

static void set_visible_session_state(SurfUI *ui, const gchar *session,
                                      const gchar *status) {
  GList *rows = gtk_container_get_children(GTK_CONTAINER(ui->session_list));
  for (GList *node = rows; node; node = node->next) {
    GtkWidget *row = GTK_WIDGET(node->data);
    const gchar *id = g_object_get_data(G_OBJECT(row), "session-id");
    if (g_strcmp0(id, session) == 0) {
      g_object_set_data_full(G_OBJECT(row), "session-status",
                             g_strdup(status), g_free);
      GtkWidget *button = g_object_get_data(G_OBJECT(row), "status-button");
      if (GTK_IS_BUTTON(button)) set_menu_state(button, status);
      if (g_strcmp0(status, "working") == 0) ensure_status_pulse(ui);
      break;
    }
  }
  g_list_free(rows);
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
    set_status(ui, ev->error ? "Error del servidor" : "");
    set_visible_session_state(ui, ui->session, ev->error ? "error" : "ready");
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
    set_status(ui, "Herramienta de código pendiente; requiere autorización expresa.");
    g_free(prompt);
    return;
  }
  provider = ui->mode == 2 ? "anthropic" : "openai";

  if (ui->new_session) {
    gchar *short_title = g_utf8_substring(
        prompt, 0, MIN((glong)50, g_utf8_strlen(prompt, -1)));
    g_strdelimit(short_title, "\n\r\t", ' ');
    g_strstrip(short_title);
    set_conversation_title(ui, short_title);
    ui->new_session = FALSE;
    g_free(short_title);
  }

  g_string_append_printf(ui->transcript, "### Tú\n\n%s\n\n### %s\n\n",
                         prompt, g_strcmp0(provider, "anthropic") == 0
                                       ? "Claude" : "GPT");
  schedule_render(ui);
  gtk_text_buffer_set_text(buffer, "", -1);
  set_visible_session_state(ui, ui->session, "working");
  set_status(ui, "Consultando T160...");
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
  gboolean protocol_error = FALSE, done = FALSE;
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
      done = TRUE;
      g_free(line);
      break;
    }
    if (g_str_has_prefix(line, "ERROR\t")) {
      gchar *msg = decode_b64(line + 6);
      set_status(ui, msg);
      g_free(msg);
      g_free(line);
      protocol_error = TRUE;
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
  if (protocol_error || !done) {
    if (!protocol_error)
      set_status(ui, "Respuesta incompleta de T160; intenta Recargar");
    g_string_free(response, TRUE);
    return NULL;
  }
  return g_string_free(response, FALSE);
failed:
  result = g_strdup_printf("Sin conexión con T160: %s",
                           error ? error->message : "desconocido");
  set_status(ui, result);
  g_free(result);
  g_clear_error(&error);
  if (conn) g_object_unref(conn);
  g_object_unref(client);
  g_string_free(response, TRUE);
  return NULL;
}

static void load_history_internal(SurfUI *ui, guint offset,
                                  gboolean preserve_unread) {
  if (ui->busy) return;
  gchar *number = g_strdup_printf("%u%s", offset,
                                   preserve_unread ? "u" : "");
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
  g_string_assign(ui->transcript, md->str);
  g_string_free(md, TRUE);
  ui->history_offset = offset;
  gtk_widget_set_sensitive(ui->older, more);
  gtk_widget_set_sensitive(ui->recent, offset > 0);
  schedule_render(ui);
  if (!preserve_unread && offset == 0)
    set_visible_session_state(ui, ui->session, "idle");
  g_free(reply);
}

static void load_history(SurfUI *ui, guint offset) {
  load_history_internal(ui, offset, FALSE);
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
  if (!row || ui->busy) return;
  const gchar *id = g_object_get_data(G_OBJECT(row), "session-id");
  if (!id) return;
  g_free(ui->session);
  ui->session = g_strdup(id);
  ui->new_session = FALSE;
  set_conversation_title(ui,
      g_object_get_data(G_OBJECT(row), "session-title"));
  set_status(ui, "");
  load_history(ui, 0);
}

static void on_row_long_press(GtkGestureLongPress *gesture,
                              gdouble x, gdouble y, gpointer ptr);
static gboolean on_row_right_click(GtkWidget *row,
                                   GdkEventButton *event, gpointer ptr);
static void on_more_clicked(GtkButton *button, gpointer ptr);

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
      gchar **fields = g_strsplit(decoded, "\t", 4);
      if (fields[0] && fields[1] && fields[2]) {
        /* Accept old three-field catalogs until the T160 bridge is upgraded. */
        const gchar *row_status = fields[3] ? fields[2] : "idle";
        const gchar *row_title = fields[3] ? fields[3] : fields[2];
        /* After a first reply or rename, use the canonical T160 title. */
        if (!ui->new_session && g_strcmp0(fields[0], ui->session) == 0)
          set_conversation_title(ui, row_title);
        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *cell = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        GtkWidget *text = gtk_label_new(row_title);
        GtkWidget *more = gtk_button_new_with_label("⋯");
        gtk_label_set_xalign(GTK_LABEL(text), 0);
        gtk_label_set_ellipsize(GTK_LABEL(text), PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(text), 22);
        gtk_label_set_width_chars(GTK_LABEL(text), 1);
        gtk_widget_set_hexpand(text, TRUE);
        gtk_widget_set_margin_start(text, 7);
        gtk_widget_set_margin_end(text, 2);
        gtk_box_pack_start(GTK_BOX(cell), text, TRUE, TRUE, 0);
        gtk_widget_set_size_request(more, 36, 40);
        gtk_button_set_relief(GTK_BUTTON(more), GTK_RELIEF_NONE);
        set_menu_state(more, row_status);
        gtk_box_pack_end(GTK_BOX(cell), more, FALSE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(row), cell);
        gtk_widget_set_size_request(row, -1, 47);
        g_signal_connect(more, "clicked", G_CALLBACK(on_more_clicked), ui);
        gtk_widget_add_events(row, GDK_BUTTON_PRESS_MASK);
        g_signal_connect(row, "button-press-event",
                         G_CALLBACK(on_row_right_click), ui);
        GtkGesture *press = gtk_gesture_long_press_new(row);
        gtk_gesture_single_set_touch_only(GTK_GESTURE_SINGLE(press), TRUE);
        g_signal_connect(press, "pressed",
                         G_CALLBACK(on_row_long_press), ui);
        g_object_set_data_full(G_OBJECT(row), "session-id",
                               g_strdup(fields[0]), g_free);
        g_object_set_data_full(G_OBJECT(row), "session-title",
                               g_strdup(row_title), g_free);
        g_object_set_data_full(G_OBJECT(row), "session-status",
                               g_strdup(valid_row_state(row_status)), g_free);
        g_object_set_data(G_OBJECT(row), "status-button", more);
        if (g_strcmp0(row_status, "working") == 0)
          ensure_status_pulse(ui);
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

/* The menu carries its own target row: no selection change or history load. */
static void on_rename(GtkButton *button, gpointer ptr) {
  SurfUI *ui = ptr;
  if (ui->busy) return;
  GtkWidget *popover = gtk_widget_get_ancestor(GTK_WIDGET(button),
                                               GTK_TYPE_POPOVER);
  if (!popover) return;
  GtkWidget *row = g_object_get_data(G_OBJECT(popover), "surf-target-row");
  if (!GTK_IS_LIST_BOX_ROW(row)) return;
  gchar *id = g_strdup(g_object_get_data(G_OBJECT(row), "session-id"));
  gchar *old_title = g_strdup(g_object_get_data(G_OBJECT(row), "session-title"));
  gtk_widget_destroy(popover);
  if (!id || !old_title) { g_free(id); g_free(old_title); return; }

  GtkWidget *dialog = gtk_dialog_new_with_buttons(
      "Renombrar conversación", GTK_WINDOW(ui->window), GTK_DIALOG_MODAL,
      "_Cancelar", GTK_RESPONSE_CANCEL, "_Guardar", GTK_RESPONSE_OK, NULL);
  GtkWidget *entry = gtk_entry_new();
  gtk_entry_set_max_length(GTK_ENTRY(entry), 100);
  gtk_entry_set_text(GTK_ENTRY(entry), old_title);
  gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(
                        GTK_DIALOG(dialog))), entry);
  gtk_widget_show_all(dialog);
  if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
    const gchar *title = gtk_entry_get_text(GTK_ENTRY(entry));
    if (*title) {
      gchar *encoded = g_base64_encode((const guchar *)title, strlen(title));
      gchar *answer = catalog_exchange(ui, "RENAME", id, encoded);
      if (answer && g_str_has_prefix(answer, "TITLE\t")) {
        if (g_strcmp0(ui->session, id) == 0)
          set_conversation_title(ui, title);
        load_catalog(ui, FALSE);
      }
      g_free(answer);
      g_free(encoded);
    }
  }
  gtk_widget_destroy(dialog);
  g_free(old_title);
  g_free(id);
}

static void on_delete(GtkButton *button, gpointer ptr) {
  SurfUI *ui = ptr;
  if (ui->busy) return;
  GtkWidget *popover = gtk_widget_get_ancestor(GTK_WIDGET(button),
                                               GTK_TYPE_POPOVER);
  if (!popover) return;
  GtkWidget *row = g_object_get_data(G_OBJECT(popover), "surf-target-row");
  if (!GTK_IS_LIST_BOX_ROW(row)) return;
  gchar *id = g_strdup(g_object_get_data(G_OBJECT(row), "session-id"));
  gtk_widget_destroy(popover);
  if (!id) return;

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
    /* A successful DELETE returns only DONE; keep ERROR distinct below. */
    if (answer) {
      if (g_strcmp0(ui->session, id) == 0) {
        g_free(ui->session);
        ui->session = g_uuid_string_random();
        ui->new_session = TRUE;
        set_conversation_title(ui, "nueva conversación");
        reset_conversation_view(ui);
        set_status(ui, "");
      }
      load_catalog(ui, FALSE);
    }
    g_free(answer);
  }
  gtk_widget_destroy(dialog);
  g_free(id);
}

static void show_row_actions(SurfUI *ui, GtkWidget *row,
                             GtkWidget *anchor, gdouble x, gdouble y,
                             gboolean point_to_row) {
  if (!GTK_IS_LIST_BOX_ROW(row) || ui->busy) return;
  GtkWidget *popover = gtk_popover_new(anchor);
  GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  GtkWidget *rename = gtk_button_new_with_label("Renombrar");
  GtkWidget *remove = gtk_button_new_with_label("Eliminar...");
  g_object_set_data(G_OBJECT(popover), "surf-target-row", row);
  if (point_to_row) {
    GdkRectangle point = {(gint)x, (gint)y, 1, 1};
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &point);
  }
  gtk_container_set_border_width(GTK_CONTAINER(actions), 8);
  gtk_widget_set_size_request(rename, 130, 46);
  gtk_widget_set_size_request(remove, 130, 46);
  gtk_container_add(GTK_CONTAINER(popover), actions);
  gtk_box_pack_start(GTK_BOX(actions), rename, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(actions), remove, FALSE, FALSE, 0);
  g_signal_connect(rename, "clicked", G_CALLBACK(on_rename), ui);
  g_signal_connect(remove, "clicked", G_CALLBACK(on_delete), ui);
  gtk_widget_show_all(popover);
  gtk_popover_popup(GTK_POPOVER(popover));
}

static void on_more_clicked(GtkButton *button, gpointer ptr) {
  GtkWidget *row = gtk_widget_get_ancestor(GTK_WIDGET(button),
                                           GTK_TYPE_LIST_BOX_ROW);
  show_row_actions(ptr, row, GTK_WIDGET(button), 0, 0, FALSE);
}

static void on_row_long_press(GtkGestureLongPress *gesture, gdouble x,
                              gdouble y, gpointer ptr) {
  GtkWidget *row = gtk_event_controller_get_widget(
      GTK_EVENT_CONTROLLER(gesture));
  show_row_actions(ptr, row, row, x, y, TRUE);
}

static gboolean on_row_right_click(GtkWidget *row, GdkEventButton *event,
                                   gpointer ptr) {
  if (event->type != GDK_BUTTON_PRESS ||
      event->button != GDK_BUTTON_SECONDARY)
    return FALSE;
  SurfUI *ui = ptr;
  if (ui->busy) return TRUE;
  show_row_actions(ui, row, row, event->x, event->y, TRUE);
  return TRUE;
}

static gboolean refresh_after_answer(gpointer ptr) {
  SurfUI *ui = ptr;
  load_history_internal(ui, 0, TRUE);
  load_catalog(ui, FALSE);
  return G_SOURCE_REMOVE;
}

static void on_toggle_sidebar(GtkButton *button, gpointer ptr) {
  SurfUI *ui = ptr;
  gboolean show_sidebar;
  (void)button;
  show_sidebar = !gtk_revealer_get_reveal_child(GTK_REVEALER(ui->sidebar));
  gtk_revealer_set_reveal_child(GTK_REVEALER(ui->sidebar), show_sidebar);
  gtk_paned_set_position(GTK_PANED(ui->splitter),
                         show_sidebar ? SIDEBAR_OPEN_PX : SIDEBAR_CLOSED_PX);
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
  gboolean changed = ui->mode_initialized && ui->mode != selected;
  ui->mode = selected;
  ui->mode_initialized = TRUE;
  for (gint i = 0; i < 4; i++) {
    if (ui->mode_buttons[i] != GTK_WIDGET(button))
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui->mode_buttons[i]),
                                   FALSE);
  }
  if (changed) {
    /* Each backend provider has a distinct catalog and context. */
    g_free(ui->session);
    ui->session = g_uuid_string_random();
    ui->new_session = TRUE;
    reset_conversation_view(ui);
    set_conversation_title(ui, "nueva conversación");
  } else {
    update_window_title(ui);
  }
  set_status(ui, (selected == 1 || selected == 3)
      ? "Herramienta de código: integración pendiente, sin ejecución" : "");
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

/* Single-shot geometry audit, opt-in with SURF_AI_LAYOUT_AUDIT=1.
   Does not watch, sample repeatedly, or modify the user session. */
static gboolean audit_layout_once(gpointer ptr) {
  SurfUI *ui = ptr;
  GtkPaned *split = GTK_PANED(ui->splitter);
  GtkWidget *nav = gtk_paned_get_child1(split);
  GtkWidget *body = gtk_paned_get_child2(split);
  g_print("SURF_LAYOUT split=%d navigation=%d sidebar=%d list=%d content=%d divider=%d\n",
      gtk_widget_get_allocated_width(ui->splitter),
      gtk_widget_get_allocated_width(nav),
      gtk_widget_get_allocated_width(ui->sidebar),
      gtk_widget_get_allocated_width(ui->session_list),
      gtk_widget_get_allocated_width(body),
      gtk_paned_get_position(split));
  return G_SOURCE_REMOVE;
}

static void on_new_chat(GtkWidget *widget, gpointer ptr) {
  SurfUI *ui = ptr;
  (void)widget;
  if (ui->busy) return;
  g_free(ui->session);
  ui->session = g_uuid_string_random();
  ui->new_session = TRUE;
  set_conversation_title(ui, "nueva conversación");
  reset_conversation_view(ui);
  set_status(ui, "");
}

int main(int argc, char **argv) {
  SurfUI ui = {0};
  GtkWidget *root, *scroller, *input_scroller, *bottom;
  GtkWidget *workspace, *nav_shell, *sidebar_box, *main_column, *toggle;
  GtkWidget *side_scroll, *history_controls;
  GtkWidget *mode_row1, *mode_row2, *sidebar_restore;
  const gchar *modes[4] = {"GPT", "Codex", "Claude", "Claude Code"};
  gtk_init(&argc, &argv);
  install_conversation_css();
  config = config_new();
  /* Do not read or alter the original ViewMD settings. */
  g_free(config->theme);
  config->theme = g_strdup("system");
  config->font_size = 13;

  ui.transcript = g_string_new("");
  ui.reader_font_pt = 13;
  ui.pinch_start_pt = 13;
  ui.session = g_uuid_string_random();
  ui.conversation_title = g_strdup("nueva conversación");
  ui.new_session = TRUE;
  ui.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_icon_name(GTK_WINDOW(ui.window), "surf-ai");
  update_window_title(&ui);
  gtk_window_set_default_size(GTK_WINDOW(ui.window), 1024, 680);
  gtk_window_maximize(GTK_WINDOW(ui.window));
  g_signal_connect(ui.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

  root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_container_set_border_width(GTK_CONTAINER(root), 8);
  gtk_container_add(GTK_CONTAINER(ui.window), root);

  /* The workspace occupies the full window height: no separate top bar. */
  /* A paned container gives the sidebar a bounded initial allocation
     (260 px on a 1366 px display) instead of letting nested list widgets
     claim hundreds of unused horizontal pixels. The divider is adjustable. */
  workspace = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
  ui.splitter = workspace;
  gtk_paned_set_wide_handle(GTK_PANED(workspace), TRUE);
  gtk_box_pack_start(GTK_BOX(root), workspace, TRUE, TRUE, 0);
  nav_shell = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_paned_pack1(GTK_PANED(workspace), nav_shell, FALSE, TRUE);
  gtk_paned_set_position(GTK_PANED(workspace), SIDEBAR_OPEN_PX);

  /* Only visible when the left sidebar is hidden, so it can be reopened. */
  sidebar_restore = gtk_button_new_with_label("☰");
  ui.sidebar_restore = sidebar_restore;
  gtk_widget_set_tooltip_text(sidebar_restore, "Mostrar conversaciones");
  gtk_widget_set_size_request(sidebar_restore, 48, 48);
  gtk_widget_set_valign(sidebar_restore, GTK_ALIGN_START);
  gtk_widget_set_no_show_all(sidebar_restore, TRUE);
  gtk_box_pack_start(GTK_BOX(nav_shell), sidebar_restore, FALSE, FALSE, 0);
  g_signal_connect(sidebar_restore, "clicked",
                   G_CALLBACK(on_toggle_sidebar), &ui);

  ui.sidebar = gtk_revealer_new();
  gtk_revealer_set_transition_type(GTK_REVEALER(ui.sidebar),
                                   GTK_REVEALER_TRANSITION_TYPE_SLIDE_RIGHT);
  gtk_revealer_set_reveal_child(GTK_REVEALER(ui.sidebar), TRUE);
  gtk_box_pack_start(GTK_BOX(nav_shell), ui.sidebar, TRUE, TRUE, 0);

  sidebar_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  gtk_widget_set_size_request(sidebar_box, 252, -1);
  gtk_widget_set_hexpand(sidebar_box, FALSE);
  gtk_widget_set_halign(sidebar_box, GTK_ALIGN_FILL);
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
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_propagate_natural_width(
      GTK_SCROLLED_WINDOW(side_scroll), FALSE);
  gtk_box_pack_start(GTK_BOX(sidebar_box), side_scroll, TRUE, TRUE, 0);
  ui.session_list = gtk_list_box_new();
  gtk_container_add(GTK_CONTAINER(side_scroll), ui.session_list);
  g_signal_connect(ui.session_list, "row-selected",
                   G_CALLBACK(on_session_selected), &ui);
  ui.load_more = gtk_button_new_with_label("Cargar 20 más");
  gtk_widget_set_size_request(ui.load_more, -1, 46);
  gtk_box_pack_start(GTK_BOX(sidebar_box), ui.load_more, FALSE, FALSE, 0);
  g_signal_connect(ui.load_more,"clicked",G_CALLBACK(on_load_more),&ui);
  main_column = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_set_hexpand(main_column, TRUE);
  gtk_paned_pack2(GTK_PANED(workspace), main_column, TRUE, FALSE);
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
  /* Two stacked history controls replace the old touch-keyboard button.
     This shares the composer row and frees one complete message-view row. */
  history_controls = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_widget_set_valign(history_controls, GTK_ALIGN_CENTER);
  gtk_box_pack_start(GTK_BOX(bottom), history_controls, FALSE, FALSE, 0);
  ui.older = gtk_button_new_with_label("Mensajes anteriores");
  ui.recent = gtk_button_new_with_label("Volver a recientes");
  gtk_widget_set_size_request(ui.older, 158, 42);
  gtk_widget_set_size_request(ui.recent, 158, 42);
  gtk_widget_set_tooltip_text(ui.older, "Cargar cinco mensajes anteriores");
  gtk_widget_set_tooltip_text(ui.recent, "Volver a los últimos cinco mensajes");
  gtk_box_pack_start(GTK_BOX(history_controls), ui.older, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(history_controls), ui.recent, FALSE, FALSE, 0);
  gtk_widget_set_sensitive(ui.older, FALSE);
  gtk_widget_set_sensitive(ui.recent, FALSE);
  g_signal_connect(ui.older, "clicked", G_CALLBACK(on_older), &ui);
  g_signal_connect(ui.recent, "clicked", G_CALLBACK(on_recent), &ui);
  ui.send = gtk_button_new_with_label("Enviar");
  gtk_widget_set_size_request(ui.send, 96, 48);
  gtk_box_pack_end(GTK_BOX(bottom), ui.send, FALSE, FALSE, 0);
  g_signal_connect(ui.send, "clicked", G_CALLBACK(on_send), &ui);

  ui.status = gtk_label_new(NULL);
  gtk_label_set_xalign(GTK_LABEL(ui.status), 0);
  gtk_widget_set_no_show_all(ui.status, TRUE);
  gtk_box_pack_start(GTK_BOX(main_column), ui.status, FALSE, FALSE, 0);
  on_new_chat(NULL, &ui);
  gtk_widget_show_all(ui.window);
  load_catalog(&ui, FALSE);
  if (g_getenv("SURF_AI_LAYOUT_AUDIT"))
    g_timeout_add(800, audit_layout_once, &ui);
  gtk_widget_grab_focus(ui.input);
  gtk_main();
  /* Background worker is intentionally owned by the process until exit. */
  return 0;
}
