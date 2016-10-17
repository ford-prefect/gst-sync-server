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
#include "sync-tcp-control-server.h"

struct _GstSyncTcpControlServer {
  GObject parent;

  gchar *addr;
  gint port;

  GRWLock info_lock;
  GstSyncServerInfo *info;

  GSocketService *server;
};

struct _GstSyncTcpControlServerClass {
  GObjectClass parent;
};

#define gst_sync_tcp_control_server_parent_class parent_class
G_DEFINE_TYPE (GstSyncTcpControlServer, gst_sync_tcp_control_server,
    G_TYPE_OBJECT);

enum {
  PROP_0,
  PROP_ADDRESS,
  PROP_PORT,
  PROP_SYNC_INFO,
};

#define DEFAULT_PORT 0

static void
gst_sync_tcp_control_server_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSyncTcpControlServer *self = GST_SYNC_TCP_CONTROL_SERVER (object);

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
        gst_sync_server_info_free (self->info);

      self->info = g_value_dup_boxed (value);
      g_rw_lock_writer_unlock (&self->info_lock);

      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gst_sync_tcp_control_server_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstSyncTcpControlServer *self = GST_SYNC_TCP_CONTROL_SERVER (object);

  switch (property_id) {
    case PROP_ADDRESS:
      g_value_set_string (value, self->addr);
      break;

    case PROP_PORT:
      g_value_set_int (value, self->port);
      break;

    case PROP_SYNC_INFO:
      g_rw_lock_reader_lock (&self->info_lock);
      g_value_set_boxed (value, self->info);
      g_rw_lock_reader_unlock (&self->info_lock);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gst_sync_tcp_control_server_dispose (GObject * object)
{
  GstSyncTcpControlServer *self = GST_SYNC_TCP_CONTROL_SERVER (object);

  if (self->server) {
    g_socket_service_stop (self->server);
    g_object_unref (self->server);
    self->server = NULL;
  }

  g_free (self->addr);
  self->addr = NULL;

  if (self->info) {
    gst_sync_server_info_free (self->info);
    self->info = NULL;
  }

  g_rw_lock_clear (&self->info_lock);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
send_sync_info (GstSyncTcpControlServer * self, GSocket * socket)
{
  JsonNode *node;
  gchar *out;
  gsize len;
  GError *err;

  g_rw_lock_reader_lock (&self->info_lock);
  if (!self->info) {
    g_rw_lock_reader_unlock (&self->info_lock);
    return;
  }
  node = json_boxed_serialize (GST_TYPE_SYNC_SERVER_INFO, self->info);
  g_rw_lock_reader_unlock (&self->info_lock);

  out = json_to_string (node, TRUE);
  len = strlen (out);
  json_node_unref (node);

  if (g_socket_send (socket, out, len, NULL, &err) != len) {
    g_warning ("Could not write out %lu bytes: %s", len, err->message);
    g_error_free (err);
  }
}

static gboolean
socket_error_cb (GSocket * socket, GIOCondition cond, gpointer user_data)
{
  GMainLoop *loop = (GMainLoop *) user_data;

  g_message ("Got error on a client socket, closing connection");

  g_main_loop_quit (loop);

  return FALSE;
}

static void
sync_info_notify (GObject * object, GParamSpec * pspec, gpointer user_data)
{
  gint fd = GPOINTER_TO_INT (user_data);
  char c = 0;

  if (write (fd, &c, sizeof (c)) < 0)
    g_warning ("Failed to write data to fd");
}

struct SyncInfoData {
  GstSyncTcpControlServer *self;
  GSocket *socket;
};

static gboolean
sync_info_updated (gint fd, GIOCondition cond, gpointer user_data)
{
  struct SyncInfoData *data = (struct SyncInfoData *) user_data;
  char c;

  if (read (fd, &c, sizeof (c) <= 0))
    g_warning ("Failed to read data from fd");

  send_sync_info (data->self, data->socket);

  return TRUE;
}

static gboolean
run_cb (GThreadedSocketService * service, GSocketConnection * connection,
    GObject * source_object G_GNUC_UNUSED, gpointer user_data)
{
  GstSyncTcpControlServer *self = GST_SYNC_TCP_CONTROL_SERVER (user_data);
  GSocket *socket;
  GSource *source;
  GMainLoop *loop;
  gint fds[2] = { -1, };
  gulong sig_id;
  struct SyncInfoData d;
  GError *err;

  socket = g_socket_connection_get_socket (connection);

  send_sync_info (self, socket);

  loop = g_main_loop_new (g_main_context_get_thread_default (), FALSE);

  source = g_socket_create_source (socket, G_IO_ERR, NULL);
  g_source_set_callback (source, (GSourceFunc) socket_error_cb, loop, NULL);
  g_source_attach (source, g_main_context_get_thread_default ());

  if (!g_unix_open_pipe (fds, 0, &err)) {
    g_warning ("Could not create pipe: %s", err->message);
    g_error_free (err);
    goto done;
  }

  /* We get a notification every time sync-info changes, and dispatch that to
   * the client thread */
  source = g_unix_fd_source_new (fds[0], G_IO_IN);
  d.self = self;
  d.socket = socket;
  g_source_set_callback (source, (GSourceFunc) sync_info_updated, &d, NULL);
  g_source_attach (source, g_main_context_get_thread_default ());

  sig_id = g_signal_connect (self, "notify::sync-info",
      G_CALLBACK (sync_info_notify), GINT_TO_POINTER (fds[1]));

  g_main_loop_run (loop);

  g_signal_handler_disconnect (self, sig_id);

done:
  g_close (fds[0], NULL);
  g_close (fds[1], NULL);

  g_main_loop_unref (loop);
  return TRUE;
}

static void
gst_sync_tcp_control_server_constructed (GObject * object)
{
  /* We have address and port set, so we can start the socket service */
  GstSyncTcpControlServer *self = GST_SYNC_TCP_CONTROL_SERVER (object);
  GSocketAddress *sockaddr;
  GError *err = NULL;

  G_OBJECT_CLASS (parent_class)->constructed (object);

  self->server = g_threaded_socket_service_new (-1);

  g_signal_connect (self->server, "run", G_CALLBACK (run_cb), self);

  sockaddr = g_inet_socket_address_new_from_string (self->addr, self->port);

  if (!g_socket_listener_add_address (G_SOCKET_LISTENER (self->server),
        sockaddr, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_TCP, NULL, NULL,
        &err)) {
    g_warning ("Could not set up socket listener: %s", err->message);
    g_error_free (err);
    return;
  }
}

static void
gst_sync_tcp_control_server_class_init (GstSyncTcpControlServerClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = gst_sync_tcp_control_server_dispose;
  object_class->set_property = gst_sync_tcp_control_server_set_property;
  object_class->get_property = gst_sync_tcp_control_server_get_property;
  object_class->constructed = gst_sync_tcp_control_server_constructed;

  g_object_class_install_property (object_class, PROP_ADDRESS,
      g_param_spec_string ("address", "Address", "Address to listen on", NULL,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PORT,
      g_param_spec_int ("port", "Port", "Port to listen on", 0, 65535,
        DEFAULT_PORT,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_SYNC_INFO,
      g_param_spec_boxed ("sync-info", "Sync info",
        "Sync parameters for clients to use", GST_TYPE_SYNC_SERVER_INFO,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
}

static void
gst_sync_tcp_control_server_init (GstSyncTcpControlServer *self)
{
  self->addr = NULL;
  self->port = 0;

  g_rw_lock_init (&self->info_lock);
  self->info = NULL;
}
