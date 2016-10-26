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

#include <glib-object.h>
#include <gio/gio.h>
#include <json-glib/json-glib.h>

#include "sync-server.h"
#include "sync-client.h"
#include "sync-tcp-control-client.h"

struct _GstSyncTcpControlClient {
  GObject parent;

  gchar *addr;
  gint port;
  GstSyncServerInfo *info;

  GSocketConnection *conn;
  gchar buf[4096];
};

struct _GstSyncTcpControlClientClass {
  GObjectClass parent;
};

#define gst_sync_tcp_control_client_parent_class parent_class
G_DEFINE_TYPE (GstSyncTcpControlClient, gst_sync_tcp_control_client,
    G_TYPE_OBJECT);

enum {
  PROP_0,
  PROP_ADDRESS,
  PROP_PORT,
  PROP_SYNC_INFO,
};

#define DEFAULT_PORT 0

static void read_sync_info (GstSyncTcpControlClient * self);
static void gst_sync_tcp_control_client_stop (GstSyncTcpControlClient * self);

static void
gst_sync_tcp_control_client_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSyncTcpControlClient *self = GST_SYNC_TCP_CONTROL_CLIENT (object);

  switch (property_id) {
    case PROP_ADDRESS:
      if (self->addr)
        g_free (self->addr);

      self->addr = g_value_dup_string (value);
      break;

    case PROP_PORT:
      self->port = g_value_get_int (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gst_sync_tcp_control_client_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstSyncTcpControlClient *self = GST_SYNC_TCP_CONTROL_CLIENT (object);

  switch (property_id) {
    case PROP_ADDRESS:
      g_value_set_string (value, self->addr);
      break;

    case PROP_PORT:
      g_value_set_int (value, self->port);
      break;

    case PROP_SYNC_INFO:
      g_value_set_boxed (value, self->info);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gst_sync_tcp_control_client_dispose (GObject * object)
{
  GstSyncTcpControlClient *self = GST_SYNC_TCP_CONTROL_CLIENT (object);

  gst_sync_tcp_control_client_stop (self);

  g_free (self->addr);
  self->addr = NULL;

  if (self->info) {
    gst_sync_server_info_free (self->info);
    self->info = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
read_done_cb (GObject * object, GAsyncResult * res, gpointer user_data)
{
  GstSyncTcpControlClient * self = GST_SYNC_TCP_CONTROL_CLIENT (user_data);
  GInputStream *istream = (GInputStream *) object;
  JsonNode *node;
  GError *err = NULL;

  if (g_input_stream_read_finish (istream, res, &err) < 1) {
    if (err) {
      g_message ("Could not read sync info: %s", err->message);
      g_error_free (err);
    }
    return;
  }

  node = json_from_string (self->buf, &err);
  if (!node) {
    g_warning ("Could not parse JSON: %s", err->message);
    g_error_free (err);
    return;
  }

  self->info = json_boxed_deserialize (GST_TYPE_SYNC_SERVER_INFO, node);

  json_node_unref (node);

  g_object_notify (G_OBJECT (self), "sync-info");

  read_sync_info (self);
}

static void
read_sync_info (GstSyncTcpControlClient * self)
{
  GInputStream *istream;

  istream = g_io_stream_get_input_stream (G_IO_STREAM (self->conn));

  memset (self->buf, 0, sizeof (self->buf));
  g_input_stream_read_async (istream, self->buf, sizeof (self->buf) - 1, 0,
      NULL, read_done_cb, self);
}

static gboolean
gst_sync_tcp_control_client_start (GstSyncTcpControlClient * self,
    GError ** err)
{
  GSocketClient *client;
  gboolean ret = TRUE;

  client = g_socket_client_new ();

  self->conn =  g_socket_client_connect_to_host (client, self->addr,
      self->port, NULL, err);

  if (!self->conn) {
    ret = FALSE;
    goto done;
  }

  read_sync_info (self);

done:
  g_object_unref (client);
  return ret;
}

static void
gst_sync_tcp_control_client_stop (GstSyncTcpControlClient * self)
{
  if (self->conn) {
    g_io_stream_close (G_IO_STREAM (self->conn), NULL, NULL);
    g_object_unref (self->conn);
    self->conn = NULL;
  }
}

static void
gst_sync_tcp_control_client_class_init (GstSyncTcpControlClientClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = gst_sync_tcp_control_client_dispose;
  object_class->set_property = gst_sync_tcp_control_client_set_property;
  object_class->get_property = gst_sync_tcp_control_client_get_property;

  g_object_class_install_property (object_class, PROP_ADDRESS,
      g_param_spec_string ("address", "Address", "Address to listen on", NULL,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PORT,
      g_param_spec_int ("port", "Port", "Port to listen on", 0, 65535,
        DEFAULT_PORT,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_SYNC_INFO,
      g_param_spec_boxed ("sync-info", "Sync info",
        "Sync parameters for clients to use", GST_TYPE_SYNC_SERVER_INFO,
        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_signal_new_class_handler ("start", GST_TYPE_SYNC_TCP_CONTROL_CLIENT,
      G_SIGNAL_ACTION | G_SIGNAL_RUN_LAST, G_CALLBACK
      (gst_sync_tcp_control_client_start), NULL, NULL, NULL, G_TYPE_BOOLEAN, 1,
      G_TYPE_POINTER /* GError ** */, NULL);

  g_signal_new_class_handler ("stop", GST_TYPE_SYNC_TCP_CONTROL_CLIENT,
      G_SIGNAL_ACTION | G_SIGNAL_RUN_LAST, G_CALLBACK
      (gst_sync_tcp_control_client_stop), NULL, NULL, NULL, G_TYPE_NONE, 0,
      NULL);
}

static void
gst_sync_tcp_control_client_init (GstSyncTcpControlClient *self)
{
  self->addr = NULL;
  self->port = 0;

  self->info = NULL;

  self->conn = NULL;
}
