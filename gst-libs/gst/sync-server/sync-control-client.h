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

#ifndef __GST_SYNC_CONTROL_CLIENT_H
#define __GST_SYNC_CONTROL_CLIENT_H

#include <glib-object.h>

#include "sync-server.h"

G_BEGIN_DECLS

#define GST_TYPE_SYNC_CONTROL_CLIENT (gst_sync_control_client_get_type ())
G_DECLARE_INTERFACE (GstSyncControlClient, gst_sync_control_client, GST,
    SYNC_CONTROL_CLIENT, GObject);

gchar * gst_sync_control_client_get_address (GstSyncControlClient * client);
void gst_sync_control_client_set_address (GstSyncControlClient * client,
    const gchar * address);

guint gst_sync_control_client_get_port (GstSyncControlClient * client);
void gst_sync_control_client_set_port (GstSyncControlClient * client,
    guint port);

gboolean gst_sync_control_client_start (GstSyncControlClient * client,
    GError ** error);
void gst_sync_control_client_stop (GstSyncControlClient * client);

GstSyncServerInfo *
gst_sync_control_client_get_sync_info (GstSyncControlClient * client);

G_END_DECLS

#endif /* __GST_SYNC_CONTROL_CLIENT_H */
