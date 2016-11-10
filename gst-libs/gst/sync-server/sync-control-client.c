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

#include "sync-control-client.h"

struct _GstSyncControlClientInterface {
  GTypeInterface parent;
};

G_DEFINE_INTERFACE (GstSyncControlClient, gst_sync_control_client,
    G_TYPE_OBJECT);

static void
gst_sync_control_client_default_init (GstSyncControlClientInterface * iface)
{
  g_object_interface_install_property (iface,
      g_param_spec_string ("address", "Address", "Address to listen on", NULL,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_interface_install_property (iface,
      g_param_spec_int ("port", "Port", "Port to listen on", 0, 65535, 0,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_interface_install_property (iface,
      g_param_spec_boxed ("sync-info", "Sync info",
        "Sync parameters for clients to use", GST_TYPE_SYNC_SERVER_INFO,
        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_signal_new_class_handler ("start", GST_TYPE_SYNC_CONTROL_CLIENT,
      G_SIGNAL_ACTION | G_SIGNAL_RUN_LAST, NULL, NULL, NULL, NULL,
      G_TYPE_BOOLEAN, 1, G_TYPE_POINTER /* GError ** */, NULL);

  g_signal_new_class_handler ("stop", GST_TYPE_SYNC_CONTROL_CLIENT,
      G_SIGNAL_ACTION | G_SIGNAL_RUN_LAST, NULL, NULL, NULL, NULL, G_TYPE_NONE,
      0, NULL);
}

gchar *
gst_sync_control_client_get_address (GstSyncControlClient * client)
{
  gchar *addr;

  g_object_get (client, "address", &addr, NULL);

  return addr;
}
void gst_sync_control_client_set_address (GstSyncControlClient * client,
    const gchar * address)
{
  g_object_set (client, "address", address, NULL);
}

guint gst_sync_control_client_get_port (GstSyncControlClient * client)
{
  guint port;

  g_object_get (client, "port", &port, NULL);

  return port;
}

void
gst_sync_control_client_set_port (GstSyncControlClient * client, guint port)
{
  g_object_set (client, "port", port, NULL);
}

gboolean
gst_sync_control_client_start (GstSyncControlClient * client, GError ** error)
{
  gboolean ret;

  g_signal_emit_by_name (client, "start", error, &ret);

  return ret;
}

void
gst_sync_control_client_stop (GstSyncControlClient * client)
{
  g_signal_emit_by_name (client, "stop");
}

GstSyncServerInfo *
gst_sync_control_client_get_sync_info (GstSyncControlClient * client)
{
  GstSyncServerInfo *info;

  g_object_get (client, "sync-info", &info, NULL);

  return info;
}
