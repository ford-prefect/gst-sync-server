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

/**
 * SECTION: gst-sync-control-server
 * @short_description: Interface for the implementation of the network
 *                     transport used by #GstSyncServer.
 *
 * The #GstSyncControlServer interface allows users of this library to provide
 * a custom implementation of the network transport that is used to send
 * clients information required to set up synchronised playback.
 *
 * The interface constists of:
 *
 *   * The GstSyncControlServer:address and GstSyncControlServer:port
 *     properties to specify the network address for the server implementation
 *     to listen on.
 *
 *   * The GstSyncControlServer:sync-info property which is set by
 *     #GstSyncServer every time new information needs to be sent to clients
 *     (both existing, and ones that join later).
 *
 *   * The GstSyncControlServer::start and GstSyncControlServer::stop signals
 *     that are used to have the server start/stop listening for connections
 *     and sending information.
 *
 * The specifics of how connections from clients are received, and how data is
 * sent is entirely up to the implementation. It is expected that clients will
 * use a corresponding #GstSyncControlClient implementation.
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
  /**
   * GstSyncControlServer:address:
   *
   * The network address for the control server to listen on.
   */
  g_object_interface_install_property (iface,
      g_param_spec_string ("address", "Address", "Address to listen on", NULL,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  /**
   * GstSyncControlServer:port:
   *
   * The network port for the control server to listen on.
   */
  g_object_interface_install_property (iface,
      g_param_spec_int ("port", "Port", "Port to listen on", 0, 65535, 0,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  /**
   * GstSyncControlServer:sync-info:
   *
   * Set whenever updated synchronisation information should be sent. The
   * data is a #GstSyncServerInfo, which is fairly easy to serialise as JSON.
   */
  g_object_interface_install_property (iface,
      g_param_spec_object ("sync-info", "Sync info",
        "Sync parameters for clients to use", GST_TYPE_SYNC_SERVER_INFO,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  /**
   * GstSyncControlServer::start:
   *
   * Start listening for connections from clients.
   */
  g_signal_new_class_handler ("start", GST_TYPE_SYNC_CONTROL_SERVER,
      G_SIGNAL_ACTION | G_SIGNAL_RUN_LAST, NULL, NULL, NULL, NULL,
      G_TYPE_BOOLEAN, 1, G_TYPE_POINTER /* GError ** */, NULL);

  /**
   * GstSyncControlServer::stop:
   *
   * Disconnect all clients and stop listening for new connections.
   */
  g_signal_new_class_handler ("stop", GST_TYPE_SYNC_CONTROL_SERVER,
      G_SIGNAL_ACTION | G_SIGNAL_RUN_LAST, NULL, NULL, NULL, NULL, G_TYPE_NONE,
      0, NULL);
}

/**
 * gst_sync_control_server_get_address
 * @server: The #GstSyncControlServer
 *
 * Returns: The configured network address to listen on.
 */
gchar *
gst_sync_control_server_get_address (GstSyncControlServer * server)
{
  gchar *addr;

  g_object_get (server, "address", &addr, NULL);

  return addr;
}

/**
 * gst_sync_control_server_set_address
 * @server: The #GstSyncControlServer
 * @address: (transfer none): A network address for the server
 *
 * Sets the network address for @server to listen on.
 */
void gst_sync_control_server_set_address (GstSyncControlServer * server,
    const gchar * address)
{
  g_object_set (server, "address", address, NULL);
}

/**
 * gst_sync_control_server_get_port
 * @server: The #GstSyncControlServer
 *
 * Returns: The configured network port to listen on.
 */
guint gst_sync_control_server_get_port (GstSyncControlServer * server)
{
  guint port;

  g_object_get (server, "port", &port, NULL);

  return port;
}

/**
 * gst_sync_control_server_set_port
 * @server: The #GstSyncControlServer
 * @port: (transfer none): A network port for the server
 *
 * Sets the network port for @server to listen on.
 */
void
gst_sync_control_server_set_port (GstSyncControlServer * server, guint port)
{
  g_object_set (server, "port", port, NULL);
}

/**
 * gst_sync_control_server_start
 * @server: The #GstSyncControlServer
 * @error: Set to the error that occurred if non-NULL
 *
 * Starts @server listening on the configured network address/port.
 *
 * Returns: TRUE on success and FALSE otherwise.
 */
gboolean
gst_sync_control_server_start (GstSyncControlServer * server, GError ** error)
{
  gboolean ret;

  g_signal_emit_by_name (server, "start", error, &ret);

  return ret;
}

/**
 * gst_sync_control_server_stop
 * @server: The #GstSyncControlServer
 *
 * Stops @server from listening for new connection and, disconnects existing
 * clients.
 */
void
gst_sync_control_server_stop (GstSyncControlServer * server)
{
  g_signal_emit_by_name (server, "stop");
}

/**
 * gst_sync_control_server_set_sync_info
 * @server: The #GstSyncControlServer
 * @info: (transfer none) The #GstSyncServerInfo to set
 *
 * Provides an updated #GstSyncServerInfo to @server to distribute to all
 * connected clients (and new clients when they connect).
 */
void
gst_sync_control_server_set_sync_info (GstSyncControlServer * server,
    const GstSyncServerInfo * info)
{
  g_object_set (server, "sync-info", info, NULL);
}
