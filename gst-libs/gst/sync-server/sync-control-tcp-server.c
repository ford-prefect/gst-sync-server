/*
 * Copyright (C) 2016 Samsung Electronics
 *   Author: Arun Raghavan <arun@osg.samsung.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <string.h>
#include <unistd.h>

#include <glib-object.h>
#include <gio/gio.h>
#include <json-glib/json-glib.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib-unix.h>

#include "sync-server.h"
#include "sync-control-server.h"
#include "sync-control-tcp-server.h"

struct _GstSyncControlTcpServer {
  GObject parent;

  gchar *addr;
  gint port;

  GRWLock info_lock;
  GstSyncServerInfo *info;

  GSocketService *server;
};

struct _GstSyncControlTcpServerClass {
  GObjectClass parent;
};

#define gst_sync_control_tcp_server_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstSyncControlTcpServer, gst_sync_control_tcp_server,
    G_TYPE_OBJECT, G_IMPLEMENT_INTERFACE (GST_TYPE_SYNC_CONTROL_SERVER, NULL));

enum {
  PROP_0,
  PROP_ADDRESS,
  PROP_PORT,
  PROP_SYNC_INFO,
};

static gboolean
gst_sync_control_tcp_server_start (GstSyncControlTcpServer * self,
    GError ** err);
static gboolean
gst_sync_control_tcp_server_stop (GstSyncControlTcpServer * self);

static void
gst_sync_control_tcp_server_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSyncControlTcpServer *self = GST_SYNC_CONTROL_TCP_SERVER (object);

  switch (property_id) {
    case PROP_ADDRESS:
      if (self->addr)
        g_free (self->addr);

      self->addr = g_value_dup_string (value);
      break;

    case PROP_PORT:
      self->port = g_value_get_int (value);
      break;

    case PROP_SYNC_INFO:
      g_rw_lock_writer_lock (&self->info_lock);
      if (self->info)
        g_object_unref (self->info);

      self->info = g_value_dup_object (value);
      g_rw_lock_writer_unlock (&self->info_lock);

      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gst_sync_control_tcp_server_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstSyncControlTcpServer *self = GST_SYNC_CONTROL_TCP_SERVER (object);

  switch (property_id) {
    case PROP_ADDRESS:
      g_value_set_string (value, self->addr);
      break;

    case PROP_PORT:
      g_value_set_int (value, self->port);
      break;

    case PROP_SYNC_INFO:
      g_rw_lock_reader_lock (&self->info_lock);
      g_value_set_object (value, self->info);
      g_rw_lock_reader_unlock (&self->info_lock);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gst_sync_control_tcp_server_dispose (GObject * object)
{
  GstSyncControlTcpServer *self = GST_SYNC_CONTROL_TCP_SERVER (object);

  gst_sync_control_tcp_server_stop (self);

  g_free (self->addr);
  self->addr = NULL;

  if (self->info) {
    g_object_unref (self->info);
    self->info = NULL;
  }

  g_rw_lock_clear (&self->info_lock);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static gchar *
get_client_info (GstSyncControlTcpServer * self, GSocketConnection * conn)
{
  JsonNode *node = NULL;
  JsonObject *obj;
  gchar *id = NULL;
  GVariant *config = NULL;
  GInputStream *istream;
  gchar buf[16384] = { 0, };
  GError *err = NULL;

  istream = g_io_stream_get_input_stream (G_IO_STREAM (conn));

  if (g_input_stream_read (istream, buf, sizeof (buf) - 1, NULL, &err) < 0) {
    g_message ("Could not read client info: %s", err->message);
    g_error_free (err);
    goto done;
  }

  node = json_from_string (buf, &err);
  if (!node) {
    g_message ("Could not parse client info: %s", err->message);
    g_error_free (err);
    goto done;
  }

  obj = json_node_get_object (node);

  config = json_gvariant_deserialize (json_object_get_member (obj, "config"),
      "a{sv}", &err);
  if (!config) {
    g_message ("Could not parse client config: %s", err->message);
    g_error_free (err);
    goto done;
  }

  /* FIXME: can/should we check the id for uniqueness? */
  id = g_strdup (json_object_get_string_member (obj, "id"));

  g_signal_emit_by_name (self, "client-joined", id,
      g_variant_ref_sink (config));

done:
  if (config)
    g_variant_unref (config);
  if (node)
    json_node_unref (node);

  return id;
}

static gboolean
send_sync_info (GstSyncControlTcpServer * self, GSocket * socket)
{
  gchar *out;
  gsize len;
  GError *err = NULL;
  gboolean ret = TRUE;

  g_rw_lock_reader_lock (&self->info_lock);
  if (!self->info) {
    g_rw_lock_reader_unlock (&self->info_lock);
    return FALSE;
  }
  out = json_gobject_to_data (G_OBJECT (self->info), &len);
  g_rw_lock_reader_unlock (&self->info_lock);

  if (g_socket_send (socket, out, len, NULL, &err) != len) {
    if (err) {
      g_message ("Could not write out %lu bytes: %s", len, err->message);
      g_error_free (err);
    } else
      g_message ("Wrote out less than one full buffer");

    ret = FALSE;
  }

  return ret;
}

static gboolean
socket_error_cb (GSocket * socket, GIOCondition cond, gpointer user_data)
{
  GMainLoop *loop = (GMainLoop *) user_data;

  if (cond & G_IO_ERR)
    g_message ("Got error on a client socket, closing connection");
  else if (g_socket_is_connected (socket))
    ; /* Either we have an EOF or some unexpected data, just quit */

  g_main_loop_quit (loop);
  return FALSE;
}

static void
sync_info_notify (GObject * object, GParamSpec * pspec, gpointer user_data)
{
  gint fd = GPOINTER_TO_INT (user_data);
  char c = 0;

  if (write (fd, &c, sizeof (c)) != sizeof (c))
    g_message ("Failed to write data to fd");
}

struct ClientData {
  GstSyncControlTcpServer *self;
  GSocket *socket;
  GMainLoop *loop;
  gchar *id;
};

static gboolean
sync_info_updated (gint fd, GIOCondition cond, gpointer user_data)
{
  struct ClientData *data = (struct ClientData *) user_data;
  char c;

  if (cond & G_IO_ERR) {
    g_message ("Error reading pipe fd");
    goto err;
  }

  if (read (fd, &c, sizeof (c)) != sizeof (c)) {
    g_message ("Failed to read data from fd");
    goto err;
  }

  send_sync_info (data->self, data->socket);

  return TRUE;

err:
  g_main_loop_quit (data->loop);
  return FALSE;
}

static gboolean
run_cb (GThreadedSocketService * service, GSocketConnection * connection,
    GObject * source_object G_GNUC_UNUSED, gpointer user_data)
{
  GstSyncControlTcpServer *self = GST_SYNC_CONTROL_TCP_SERVER (user_data);
  GSocket *socket;
  GSource *err_source, *pipe_source;
  GMainLoop *loop;
  gint fds[2] = { -1, };
  gulong sig_id;
  struct ClientData d = { 0, };
  GError *err;

  socket = g_socket_connection_get_socket (connection);
  loop = g_main_loop_new (g_main_context_get_thread_default (), FALSE);

  d.self = self;
  d.socket = socket;
  d.loop = loop;

  /* Get the ID And config from the client */
  d.id = get_client_info (self, connection);
  if (!d.id)
    goto done;

  /* Now get the sync info from the server */
  send_sync_info (self, socket);

  /* Catch errors (at this point, any input after the client info is also
   * unexpected) on the socket and exit cleanly */
  err_source = g_socket_create_source (socket, G_IO_IN | G_IO_ERR, NULL);
  g_source_set_callback (err_source, (GSourceFunc) socket_error_cb, loop, NULL);
  g_source_attach (err_source, g_main_context_get_thread_default ());

  /* We get a notification every time sync-info changes, and dispatch that to
   * the client thread */
  if (!g_unix_open_pipe (fds, 0, &err)) {
    g_warning ("Could not create pipe: %s", err->message);
    g_error_free (err);
    goto done;
  }

  pipe_source = g_unix_fd_source_new (fds[0], G_IO_IN | G_IO_ERR);
  g_source_set_callback (pipe_source, (GSourceFunc) sync_info_updated, &d,
      NULL);
  g_source_attach (pipe_source, g_main_context_get_thread_default ());

  sig_id = g_signal_connect (self, "notify::sync-info",
      G_CALLBACK (sync_info_notify), GINT_TO_POINTER (fds[1]));

  /* Now loop until we're done */
  g_main_loop_run (loop);

  g_signal_handler_disconnect (self, sig_id);

  g_source_destroy (err_source);
  g_source_destroy (pipe_source);

  g_signal_emit_by_name (self, "client-left", d.id);

done:
  g_close (fds[0], NULL);
  g_close (fds[1], NULL);

  g_free (d.id);

  g_main_loop_unref (loop);
  return TRUE;
}

static void
gst_sync_control_tcp_server_class_init (GstSyncControlTcpServerClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = gst_sync_control_tcp_server_dispose;
  object_class->set_property = gst_sync_control_tcp_server_set_property;
  object_class->get_property = gst_sync_control_tcp_server_get_property;

  g_object_class_override_property (object_class, PROP_ADDRESS, "address");
  g_object_class_override_property (object_class, PROP_PORT, "port");
  g_object_class_override_property (object_class, PROP_SYNC_INFO, "sync-info");

  g_signal_override_class_handler ("start", GST_TYPE_SYNC_CONTROL_TCP_SERVER,
      G_CALLBACK (gst_sync_control_tcp_server_start));
  g_signal_override_class_handler ("stop", GST_TYPE_SYNC_CONTROL_TCP_SERVER,
      G_CALLBACK (gst_sync_control_tcp_server_stop));
}

static void
gst_sync_control_tcp_server_init (GstSyncControlTcpServer *self)
{
  self->addr = NULL;
  self->port = 0;

  g_rw_lock_init (&self->info_lock);
  self->info = NULL;
}

static gboolean
gst_sync_control_tcp_server_start (GstSyncControlTcpServer * self,
    GError ** err)
{
  /* We have address and port set, so we can start the socket service */
  GSocketAddress *sockaddr;

  self->server = g_threaded_socket_service_new (-1);

  g_signal_connect (self->server, "run", G_CALLBACK (run_cb), self);

  sockaddr = g_inet_socket_address_new_from_string (self->addr, self->port);

  if (!g_socket_listener_add_address (G_SOCKET_LISTENER (self->server),
        sockaddr, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_TCP, NULL, NULL,
        err))
    return FALSE;

  return TRUE;
}

static gboolean
gst_sync_control_tcp_server_stop (GstSyncControlTcpServer * self)
{
  if (self->server) {
    g_socket_service_stop (self->server);
    g_object_unref (self->server);
    self->server = NULL;
  }
}
