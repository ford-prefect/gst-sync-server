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
#include "sync-control-client.h"
#include "sync-control-tcp-client.h"

struct _GstSyncControlTcpClient {
  GObject parent;

  gchar *addr;
  gint port;
  GstSyncServerInfo *info;

  GSocketConnection *conn;
  gchar buf[4096];
};

struct _GstSyncControlTcpClientClass {
  GObjectClass parent;
};

#define gst_sync_control_tcp_client_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstSyncControlTcpClient, gst_sync_control_tcp_client,
    G_TYPE_OBJECT, G_IMPLEMENT_INTERFACE (GST_TYPE_SYNC_CONTROL_CLIENT, NULL));

enum {
  PROP_0,
  PROP_ADDRESS,
  PROP_PORT,
  PROP_SYNC_INFO,
};

static void read_sync_info (GstSyncControlTcpClient * self);
static void gst_sync_control_tcp_client_stop (GstSyncControlTcpClient * self);

static void
gst_sync_control_tcp_client_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSyncControlTcpClient *self = GST_SYNC_CONTROL_TCP_CLIENT (object);

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
gst_sync_control_tcp_client_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstSyncControlTcpClient *self = GST_SYNC_CONTROL_TCP_CLIENT (object);

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
gst_sync_control_tcp_client_dispose (GObject * object)
{
  GstSyncControlTcpClient *self = GST_SYNC_CONTROL_TCP_CLIENT (object);

  gst_sync_control_tcp_client_stop (self);

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
  GstSyncControlTcpClient * self = GST_SYNC_CONTROL_TCP_CLIENT (user_data);
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
read_sync_info (GstSyncControlTcpClient * self)
{
  GInputStream *istream;

  istream = g_io_stream_get_input_stream (G_IO_STREAM (self->conn));

  memset (self->buf, 0, sizeof (self->buf));
  g_input_stream_read_async (istream, self->buf, sizeof (self->buf) - 1, 0,
      NULL, read_done_cb, self);
}

static gboolean
gst_sync_control_tcp_client_start (GstSyncControlTcpClient * self,
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
gst_sync_control_tcp_client_stop (GstSyncControlTcpClient * self)
{
  if (self->conn) {
    g_io_stream_close (G_IO_STREAM (self->conn), NULL, NULL);
    g_object_unref (self->conn);
    self->conn = NULL;
  }
}

static void
gst_sync_control_tcp_client_class_init (GstSyncControlTcpClientClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = gst_sync_control_tcp_client_dispose;
  object_class->set_property = gst_sync_control_tcp_client_set_property;
  object_class->get_property = gst_sync_control_tcp_client_get_property;

  g_object_class_override_property (object_class, PROP_ADDRESS, "address");
  g_object_class_override_property (object_class, PROP_PORT, "port");
  g_object_class_override_property (object_class, PROP_SYNC_INFO, "sync-info");

  g_signal_override_class_handler ("start", GST_TYPE_SYNC_CONTROL_TCP_CLIENT,
      G_CALLBACK (gst_sync_control_tcp_client_start));
  g_signal_override_class_handler ("stop", GST_TYPE_SYNC_CONTROL_TCP_CLIENT,
      G_CALLBACK (gst_sync_control_tcp_client_stop));
}

static void
gst_sync_control_tcp_client_init (GstSyncControlTcpClient *self)
{
  self->addr = NULL;
  self->port = 0;

  self->info = NULL;

  self->conn = NULL;
}
