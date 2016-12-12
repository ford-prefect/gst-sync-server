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

#ifndef __GST_SYNC_SERVER_INFO_H
#define __GST_SYNC_SERVER_INFO_H

#include <glib.h>

G_BEGIN_DECLS

#define GST_TYPE_SYNC_SERVER_INFO (gst_sync_server_info_get_type ())
G_DECLARE_FINAL_TYPE (GstSyncServerInfo, gst_sync_server_info, GST,
    SYNC_SERVER_INFO, GObject);

GstSyncServerInfo * gst_sync_server_info_new ();
guint64  gst_sync_server_info_get_version (GstSyncServerInfo * info);
gchar *  gst_sync_server_info_get_clock_address (GstSyncServerInfo * info);
guint    gst_sync_server_info_get_clock_port (GstSyncServerInfo * info);
gchar *  gst_sync_server_info_get_uri (GstSyncServerInfo * info);
guint64  gst_sync_server_info_get_base_time (GstSyncServerInfo * info);
guint64  gst_sync_server_info_get_latency (GstSyncServerInfo * info);
gboolean gst_sync_server_info_get_stopped (GstSyncServerInfo * info);
gboolean gst_sync_server_info_get_paused (GstSyncServerInfo * info);
guint64  gst_sync_server_info_get_base_time_offset (GstSyncServerInfo * info);

G_END_DECLS

#endif /* __GST_SYNC_SERVER_INFO_H */

