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

#include "sync-control-server.h"

struct _GstSyncControlServerInterface {
  GTypeInterface parent;
};

G_DEFINE_INTERFACE (GstSyncControlServer, gst_sync_control_server,
    G_TYPE_OBJECT);

static void
gst_sync_control_server_default_init (GstSyncControlServerInterface * iface)
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
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_signal_new_class_handler ("start", GST_TYPE_SYNC_CONTROL_SERVER,
      G_SIGNAL_ACTION | G_SIGNAL_RUN_LAST, NULL, NULL, NULL, NULL,
      G_TYPE_BOOLEAN, 1, G_TYPE_POINTER /* GError ** */, NULL);

  g_signal_new_class_handler ("stop", GST_TYPE_SYNC_CONTROL_SERVER,
      G_SIGNAL_ACTION | G_SIGNAL_RUN_LAST, NULL, NULL, NULL, NULL, G_TYPE_NONE,
      0, NULL);
}

gchar *
gst_sync_control_server_get_address (GstSyncControlServer * server)
{
  gchar *addr;

  g_object_get (server, "address", &addr, NULL);

  return addr;
}
void gst_sync_control_server_set_address (GstSyncControlServer * server,
    const gchar * address)
{
  g_object_set (server, "address", address, NULL);
}

guint gst_sync_control_server_get_port (GstSyncControlServer * server)
{
  guint port;

  g_object_get (server, "port", &port, NULL);

  return port;
}

void
gst_sync_control_server_set_port (GstSyncControlServer * server, guint port)
{
  g_object_set (server, "port", port, NULL);
}

gboolean
gst_sync_control_server_start (GstSyncControlServer * server, GError ** error)
{
  gboolean ret;

  g_signal_emit_by_name (server, "start", error, &ret);

  return ret;
}

void
gst_sync_control_server_stop (GstSyncControlServer * server)
{
  g_signal_emit_by_name (server, "stop");
}

void
gst_sync_control_server_set_sync_info (GstSyncControlServer * server,
    const GstSyncServerInfo * info)
{
  g_object_set (server, "sync-info", info, NULL);
}
