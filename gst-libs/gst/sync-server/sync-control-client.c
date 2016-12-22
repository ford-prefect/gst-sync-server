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

/**
 * SECTION: gst-sync-control-client
 * @short_description: Interface for the implementation of the network
 *                     transport used by #GstSyncClient.
 *
 * The #GstSyncControlClient interface allows users of this library to provide
 * a custom implementation of the network transport that is used by clients to
 * receive information required to set up synchronised playback.
 *
 * The interface constists of:
 *
 *   * The GstSyncControlClient:address and GstSyncControlClient:port
 *     properties to specify the network address for the client implementation
 *     to connect to.
 *
 *   * The GstSyncControlClient:id and GstSyncControlClient:config for when we
 *     want to send per-client configuration to the server.
 *
 *   * The GstSyncControlServer:sync-info property which is first set up when
 *     the client connects, and then updated when updated information is
 *     received from the server.
 *
 *   * The GstSyncControlClient::start and GstSyncControlClient::stop signals
 *     that are used to have the client connect to and disconnect from the
 *     server.
 *
 * The specifics of how the connection  to the server is established, and how
 * data is received is entirely up to the implementation. It is expected that
 * the server will use a corresponding #GstSyncControlServer implementation.
 */

struct _GstSyncControlClientInterface {
  GTypeInterface parent;
};

G_DEFINE_INTERFACE (GstSyncControlClient, gst_sync_control_client,
    G_TYPE_OBJECT);

static void
gst_sync_control_client_default_init (GstSyncControlClientInterface * iface)
{
  /**
   * GstSyncControlClient:id:
   *
   * Unique client identifier used by the server for client-specific
   * configuration. Automatically generated if set to NULL. Only has an effect
   * if set before the client is started.
   */
  g_object_interface_install_property (iface,
      g_param_spec_string ("id", "ID", "Unique client identifier", NULL,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  /**
   * GstSyncControlClient:config:
   *
   * Client configuration, which can include any data about the client that the
   * server can use. This can include such things as display configuration,
   * position, orientation where transformations need to be applied. The
   * information is provided as a dictionary stored in a #GVariant. Only has an
   * effect if set before the client is started.
   */
  g_object_interface_install_property (iface,
      g_param_spec_variant ("config", "Config", "Client configuration",
        G_VARIANT_TYPE ("a{sv}"), NULL,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  /**
   * GstSyncControlClient:address:
   *
   * The network address for the control client to connect to.
   */
  g_object_interface_install_property (iface,
      g_param_spec_string ("address", "Address", "Address to listen on", NULL,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  /**
   * GstSyncControlClient:port:
   *
   * The network port for the control client to connect to.
   */
  g_object_interface_install_property (iface,
      g_param_spec_int ("port", "Port", "Port to listen on", 0, 65535, 0,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  /**
   * GstSyncControlClient:sync-info:
   *
   * Set when the client finishes connecting, and then whenever updated
   * synchronisation information is received. The data is a #GstSyncServerInfo,
   * which is fairly easy to (de)serialise as JSON.
   */
  g_object_interface_install_property (iface,
      g_param_spec_object ("sync-info", "Sync info",
        "Sync parameters for clients to use", GST_TYPE_SYNC_SERVER_INFO,
        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  /**
   * GstSyncControlClient::start:
   *
   * Connect to the specified server.
   */
  g_signal_new_class_handler ("start", GST_TYPE_SYNC_CONTROL_CLIENT,
      G_SIGNAL_ACTION | G_SIGNAL_RUN_LAST, NULL, NULL, NULL, NULL,
      G_TYPE_BOOLEAN, 1, G_TYPE_POINTER /* GError ** */, NULL);

  /**
   * GstSyncControlClient::stop:
   *
   * Disconnect to the specified server.
   */
  g_signal_new_class_handler ("stop", GST_TYPE_SYNC_CONTROL_CLIENT,
      G_SIGNAL_ACTION | G_SIGNAL_RUN_LAST, NULL, NULL, NULL, NULL, G_TYPE_NONE,
      0, NULL);
}

/**
 * gst_sync_control_client_get_address
 * @client: The #GstSyncControlClient
 *
 * Returns: The configured network address to connect to.
 */
gchar *
gst_sync_control_client_get_address (GstSyncControlClient * client)
{
  gchar *addr;

  g_object_get (client, "address", &addr, NULL);

  return addr;
}

/**
 * gst_sync_control_client_set_address
 * @client: The #GstSyncControlClient
 * @address: (transfer none): A network address
 *
 * Sets the network address to connect to.
 */
void gst_sync_control_client_set_address (GstSyncControlClient * client,
    const gchar * address)
{
  g_object_set (client, "address", address, NULL);
}

/**
 * gst_sync_control_client_get_port
 * @client: The #GstSyncControlClient
 *
 * Returns: The configured network port to connect to.
 */
guint gst_sync_control_client_get_port (GstSyncControlClient * client)
{
  guint port;

  g_object_get (client, "port", &port, NULL);

  return port;
}

/**
 * gst_sync_control_client_set_port
 * @client: The #GstSyncControlClient
 * @port: (transfer none): A network port
 *
 * Sets the network port to connect to.
 */
void
gst_sync_control_client_set_port (GstSyncControlClient * client, guint port)
{
  g_object_set (client, "port", port, NULL);
}

/**
 * gst_sync_control_client_start
 * @client: The #GstSyncControlClient
 * @error: Set to the error that occurred if non-NULL
 *
 * Starts @client and connects to the configured network address/port.
 *
 * Returns: TRUE on success and FALSE otherwise.
 */
gboolean
gst_sync_control_client_start (GstSyncControlClient * client, GError ** error)
{
  gboolean ret;

  g_signal_emit_by_name (client, "start", error, &ret);

  return ret;
}

/**
 * gst_sync_control_client_stop
 * @client: The #GstSyncControlClient
 *
 * Stops @client and disconnects from the server.
 */
void
gst_sync_control_client_stop (GstSyncControlClient * client)
{
  g_signal_emit_by_name (client, "stop");
}

/**
 * gst_sync_control_client_get_sync_info
 * @client: The #GstSyncControlClient
 *
 * Returns: (transfer full): The last received synchronisation information
 */
GstSyncServerInfo *
gst_sync_control_client_get_sync_info (GstSyncControlClient * client)
{
  GstSyncServerInfo *info;

  g_object_get (client, "sync-info", &info, NULL);

  return info;
}
