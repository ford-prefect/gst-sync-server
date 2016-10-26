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

#ifndef __GST_SYNC_CLIENT_H
#define __GST_SYNC_CLIENT_H

#include <glib.h>
#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct _GstSyncClient GstSyncClient;
typedef struct _GstSyncClientClass GstSyncClientClass;

#define GST_TYPE_SYNC_CLIENT (gst_sync_client_get_type ())
#define GST_SYNC_CLIENT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_SYNC_CLIENT, GstSyncClient))
#define GST_SYNC_CLIENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_SYNC_CLIENT, GstSyncClientClass))
#define GST_IS_SYNC_CLIENT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_SYNC_CLIENT))
#define GST_IS_SYNC_CLIENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_SYNC_CLIENT))

GType gst_sync_client_get_type ();

GstSyncClient * gst_sync_client_new (const gchar * addr, gint port,
    GstPipeline * pipeline);

G_END_DECLS

#endif /* __GST_SYNC_CLIENT_H */
