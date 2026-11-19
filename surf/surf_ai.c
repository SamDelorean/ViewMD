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
    gtk_widget_set_sensitive(ui->provider, TRUE);
    gtk_label_set_text(GTK_LABEL(ui->status),
                       ev->error ? "Error del servidor" : "Listo");
    g_string_append(ui->transcript, "\n\n---\n\n");
    schedule_render(ui);
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
  provider = gtk_combo_box_get_active_id(GTK_COMBO_BOX(ui->provider));
  if (!provider) provider = "openai";

  g_string_append_printf(ui->transcript, "### Tú\n\n%s\n\n### %s\n\n",
                         prompt, g_strcmp0(provider, "anthropic") == 0
                                       ? "Claude" : "GPT");
  schedule_render(ui);
  gtk_text_buffer_set_text(buffer, "", -1);
  gtk_label_set_text(GTK_LABEL(ui->status), "Consultando T160...");
  ui->busy = TRUE;
  gtk_widget_set_sensitive(ui->send, FALSE);
  gtk_widget_set_sensitive(ui->new_chat, FALSE);
  gtk_widget_set_sensitive(ui->provider, FALSE);

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
  GtkWidget *root, *header, *scroller, *input_scroller, *bottom, *label;
  gtk_init(&argc, &argv);
  config = config_new();
  /* Do not read or alter the original ViewMD settings. */
  g_free(config->theme);
  config->theme = g_strdup("system");
  config->font_size = 13;

  ui.transcript = g_string_new("");
  ui.session = g_uuid_string_random();
  ui.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(ui.window), "Surf AI");
  gtk_window_set_default_size(GTK_WINDOW(ui.window), 1024, 680);
  gtk_window_maximize(GTK_WINDOW(ui.window));
  g_signal_connect(ui.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

  root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_container_set_border_width(GTK_CONTAINER(root), 8);
  gtk_container_add(GTK_CONTAINER(ui.window), root);

  header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_pack_start(GTK_BOX(root), header, FALSE, FALSE, 0);
  label = gtk_label_new("Surf AI  ·  T160");
  gtk_box_pack_start(GTK_BOX(header), label, FALSE, FALSE, 0);
  ui.provider = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ui.provider),
                            "openai", "GPT");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(ui.provider),
                            "anthropic", "Claude");
  gtk_combo_box_set_active(GTK_COMBO_BOX(ui.provider), 0);
  gtk_box_pack_start(GTK_BOX(header), ui.provider, FALSE, FALSE, 0);
  ui.new_chat = gtk_button_new_with_label("Nueva conversación");
  gtk_box_pack_end(GTK_BOX(header), ui.new_chat, FALSE, FALSE, 0);
  g_signal_connect(ui.new_chat, "clicked", G_CALLBACK(on_new_chat), &ui);

  scroller = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_box_pack_start(GTK_BOX(root), scroller, TRUE, TRUE, 0);
  ui.viewer = markyd_editor_new(NULL);
  gtk_container_add(GTK_CONTAINER(scroller),
                    markyd_editor_get_widget(ui.viewer));

  bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_pack_start(GTK_BOX(root), bottom, FALSE, FALSE, 0);
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
  gtk_box_pack_end(GTK_BOX(bottom), ui.send, FALSE, FALSE, 0);
  g_signal_connect(ui.send, "clicked", G_CALLBACK(on_send), &ui);

  ui.status = gtk_label_new("T160 via tunel SSH local (127.0.0.1:8765)");
  gtk_label_set_xalign(GTK_LABEL(ui.status), 0);
  gtk_box_pack_start(GTK_BOX(root), ui.status, FALSE, FALSE, 0);
  on_new_chat(NULL, &ui);
  gtk_widget_show_all(ui.window);
  gtk_widget_grab_focus(ui.input);
  gtk_main();
  /* Background worker is intentionally owned by the process until exit. */
  return 0;
}
