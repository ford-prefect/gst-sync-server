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

typedef struct _GstSyncServer GstSyncServer;
typedef struct _GstSyncServerClass GstSyncServerClass;

#define GST_TYPE_SYNC_SERVER (gst_sync_server_get_type ())
#define GST_SYNC_SERVER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_SYNC_SERVER, GstSyncServer))
#define GST_SYNC_SERVER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_SYNC_SERVER, GstSyncServerClass))
#define GST_IS_SYNC_SERVER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_SYNC_SERVER))
#define GST_IS_SYNC_SERVER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_SYNC_SERVER))

/* Server messages */

typedef struct _GstSyncServerInfo GstSyncServerInfo;

struct _GstSyncServerInfo {
  gint version;
  gchar *clock_addr;
  guint clock_port;
  gchar *uri;
  guint64 base_time;
  guint64 latency;
  gboolean paused;
  guint64 paused_time;
};

#define GST_TYPE_SYNC_SERVER_INFO (gst_sync_server_info_get_type ())
GType gst_sync_server_info_get_type ();
void gst_sync_server_info_free (GstSyncServerInfo *info);

/* API */
GType gst_sync_server_get_type ();

GstSyncServer * gst_sync_server_new (const gchar * addr, gint port);

gboolean gst_sync_server_start (GstSyncServer * self, GError ** error);

void gst_sync_server_set_paused (GstSyncServer * self, gboolean paused);

void gst_sync_server_stop (GstSyncServer * self);

G_END_DECLS

#endif /* __GST_SYNC_SERVER_H */
