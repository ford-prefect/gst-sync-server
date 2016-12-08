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

#ifndef __GST_SYNC_SERVER_H
#define __GST_SYNC_SERVER_H

#include <glib.h>

G_BEGIN_DECLS

#define GST_TYPE_SYNC_SERVER (gst_sync_server_get_type ())
G_DECLARE_FINAL_TYPE (GstSyncServer, gst_sync_server, GST, SYNC_SERVER,
    GObject);

GstSyncServer *
gst_sync_server_new (const gchar * control_addr, gint control_port);

gboolean gst_sync_server_start (GstSyncServer * server, GError ** error);

void gst_sync_server_set_stopped (GstSyncServer * server, gboolean stopped);
void gst_sync_server_set_paused (GstSyncServer * server, gboolean paused);

void gst_sync_server_stop (GstSyncServer * server);

G_END_DECLS

#endif /* __GST_SYNC_SERVER_H */
