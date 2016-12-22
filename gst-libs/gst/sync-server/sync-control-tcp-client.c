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

  gchar *id;
  GVariant *config;

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
  PROP_ID,
  PROP_CONFIG,
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
    case PROP_ID:
      if (self->conn) {
        g_warning ("Trying to set client ID after it has started");
        break;
      }

      g_free (self->id);
      self->id = g_value_dup_string (value);

      break;

    case PROP_CONFIG:
      if (self->conn) {
        g_warning ("Trying to set client config after it has started");
        break;
      }

      if (self->config)
        g_variant_unref (self->config);

      self->config = g_value_dup_variant (value);

      break;

    case PROP_ADDRESS:
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
    case PROP_ID:
      g_value_set_string (value, self->id);
      break;

    case PROP_CONFIG:
      g_value_set_variant (value, self->config);
      break;

    case PROP_ADDRESS:
      g_value_set_string (value, self->addr);
      break;

    case PROP_PORT:
      g_value_set_int (value, self->port);
      break;

    case PROP_SYNC_INFO:
      g_value_set_object (value, self->info);
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

  g_free (self->id);
  self->id = NULL;

  if (self->config) {
    g_variant_unref (self->config);
    self->config = NULL;
  }

  g_free (self->addr);
  self->addr = NULL;

  if (self->info) {
    g_object_unref (self->info);
    self->info = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static gchar *
make_client_info (gchar * id, GVariant * config)
{
  JsonBuilder *builder;
  JsonNode *node;
  gboolean free_config = FALSE;
  gchar *ret;

  if (!config) {
    free_config = TRUE;
    config = g_variant_new ("a{sv}", NULL);
  }

  builder = json_builder_new ();

  json_builder_begin_object (builder);

  json_builder_set_member_name (builder, "id");
  json_builder_add_string_value (builder, id);

  json_builder_set_member_name (builder, "config");
  json_builder_add_value (builder, json_gvariant_serialize (config));

  json_builder_end_object (builder);

  node = json_builder_get_root (builder);

  ret = json_to_string (node, FALSE);

  json_builder_reset (builder);
  json_node_free (node);

  if (free_config)
    g_variant_unref (config);

  return ret;
}

static void
send_done_cb (GObject * object, GAsyncResult * res, gpointer user_data)
{
  GstSyncControlTcpClient * self = GST_SYNC_CONTROL_TCP_CLIENT (user_data);
  GOutputStream *ostream = (GOutputStream *) object;
  gssize len;
  GError *err = NULL;

  if (!g_output_stream_write_all_finish (ostream, res, &len, &err)) {
    if (err) {
      g_warning ("Could not send client info: %s", err->message);
      g_error_free (err);
    }

    return;
  }

  /* Now that we've sent client info, we can wait for sync info */
  read_sync_info (self);
}

static void
send_client_info (GstSyncControlTcpClient * self)
{
  gchar *info;
  GOutputStream *ostream;

  info = make_client_info (self->id, self->config);
  ostream = g_io_stream_get_output_stream (G_IO_STREAM (self->conn));

  g_output_stream_write_all_async (ostream, info, strlen (info), 0, NULL,
      send_done_cb, self);
}

static void
read_done_cb (GObject * object, GAsyncResult * res, gpointer user_data)
{
  GstSyncControlTcpClient * self = GST_SYNC_CONTROL_TCP_CLIENT (user_data);
  GInputStream *istream = (GInputStream *) object;
  gssize len;
  GError *err = NULL;

  len = g_input_stream_read_finish (istream, res, &err);
  if (len < 1) {
    if (err) {
      g_warning ("Could not read sync info: %s", err->message);
      g_error_free (err);
    }
    return;
  }

  self->info =
    GST_SYNC_SERVER_INFO (json_gobject_from_data (GST_TYPE_SYNC_SERVER_INFO,
          self->buf, len, &err));

  if (!self->info) {
    g_warning ("Could not parse JSON: %s", err->message);
    g_error_free (err);
    return;
  }

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

  send_client_info (self);

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

  g_object_class_override_property (object_class, PROP_ID, "id");
  g_object_class_override_property (object_class, PROP_CONFIG, "config");
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
