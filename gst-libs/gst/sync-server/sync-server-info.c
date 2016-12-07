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
 * SECTION: gst-sync-server-info
 * @short_description: Information object sent by #GstSyncServer to
 *                     #GstSyncClient.
 *
 * The specifics of the contents of this object are not essential to users of
 * the library. This is only exposed so that implementations of
 * #GstSyncControlServer and #GstSyncControlClient have access to the
 * information that needs to be sent across the wire.
 */
#include <json-glib/json-glib.h>

#include "sync-server-info.h"

struct _GstSyncServerInfo {
  GObject parent;

  guint64 version;
  gchar *clock_addr;
  guint clock_port;
  gchar *uri;
  guint64 base_time;
  guint64 latency;
  gboolean paused;
  guint64 paused_time;
};

struct _GstSyncServerInfoClass {
  GObjectClass parent;
};

#define gst_sync_server_info_parent_class parent_class
G_DEFINE_TYPE (GstSyncServerInfo, gst_sync_server_info, G_TYPE_OBJECT);

#define DEFAULT_VERSION 1

enum {
  PROP_0,
  PROP_VERSION,
  PROP_CLOCK_ADDRESS,
  PROP_CLOCK_PORT,
  PROP_URI,
  PROP_BASE_TIME,
  PROP_LATENCY,
  PROP_PAUSED,
  PROP_PAUSED_TIME,
};

static void
gst_sync_server_info_dispose (GObject * object)
{
  GstSyncServerInfo *info = GST_SYNC_SERVER_INFO (object);

  g_free (info->clock_addr);
  g_free (info->uri);
}

static void
gst_sync_server_info_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSyncServerInfo *info = GST_SYNC_SERVER_INFO (object);

  switch (property_id) {
    case PROP_VERSION:
      info->version = g_value_get_uint64 (value);
      break;

    case PROP_CLOCK_ADDRESS:
      g_free (info->clock_addr);
      info->clock_addr = g_value_dup_string (value);
      break;

    case PROP_CLOCK_PORT:
      info->clock_port = g_value_get_uint (value);
      break;

    case PROP_URI:
      g_free (info->uri);
      info->uri = g_value_dup_string (value);
      break;

    case PROP_BASE_TIME:
      info->base_time = g_value_get_uint64 (value);
      break;

    case PROP_LATENCY:
      info->latency = g_value_get_uint64 (value);
      break;

    case PROP_PAUSED:
      info->paused = g_value_get_boolean (value);
      break;

    case PROP_PAUSED_TIME:
      info->paused_time = g_value_get_uint64 (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gst_sync_server_info_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstSyncServerInfo *info = GST_SYNC_SERVER_INFO (object);

  switch (property_id) {
    case PROP_VERSION:
      g_value_set_uint64 (value, info->version);
      break;

    case PROP_CLOCK_ADDRESS:
      g_value_set_string (value, info->clock_addr);
      break;

    case PROP_CLOCK_PORT:
      g_value_set_uint (value, info->clock_port);
      break;

    case PROP_URI:
      g_value_set_string (value, info->uri);
      break;

    case PROP_BASE_TIME:
      g_value_set_uint64 (value, info->base_time);
      break;

    case PROP_LATENCY:
      g_value_set_uint64 (value, info->latency);
      break;

    case PROP_PAUSED:
      g_value_set_boolean (value, info->paused);
      break;

    case PROP_PAUSED_TIME:
      g_value_set_uint64 (value, info->paused_time);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gst_sync_server_info_class_init (GstSyncServerInfoClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = gst_sync_server_info_dispose;
  object_class->set_property = gst_sync_server_info_set_property;
  object_class->get_property = gst_sync_server_info_get_property;

  g_object_class_install_property (object_class, PROP_VERSION,
      g_param_spec_uint64 ("version", "Version",
        "Protocol version of the sync information", 0, G_MAXUINT64,
        DEFAULT_VERSION,
        G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_CLOCK_ADDRESS,
      g_param_spec_string ("clock-address", "Clock address",
        "Network address of the clock provider", NULL,
        G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_CLOCK_PORT,
      g_param_spec_uint ("clock-port", "Clock port",
        "Network port of the clock provider", 0, 65535, 0,
        G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_URI,
      g_param_spec_string ("uri", "URI",
        "The URI to play", NULL,
        G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_BASE_TIME,
      g_param_spec_uint64 ("base-time", "Base time",
        "Base time of the GStreamer pipeline (ns)", 0, G_MAXUINT64, 0,
        G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_LATENCY,
      g_param_spec_uint64 ("latency", "Latency",
        "Latency of the GStreamer pipeline (ns)", 0, G_MAXUINT64, 0,
        G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PAUSED,
      g_param_spec_boolean ("paused", "Paused",
        "Whether playback is currently paused", FALSE,
        G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PAUSED_TIME,
      g_param_spec_uint64 ("paused-time", "Paused time",
        "Time the pipeline has spent in paused", 0, G_MAXUINT64, 0,
        G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}


static void
gst_sync_server_info_init (GstSyncServerInfo * info)
{
  info->version = DEFAULT_VERSION;
}

GstSyncServerInfo *
gst_sync_server_info_new ()
{
  return g_object_new (GST_TYPE_SYNC_SERVER_INFO, NULL);
}

guint64
gst_sync_server_info_get_version (GstSyncServerInfo * info)
{
  return info->version;
}

gchar *
gst_sync_server_info_get_clock_address (GstSyncServerInfo * info)
{
  return g_strdup (info->clock_addr);
}

guint
gst_sync_server_info_get_clock_port (GstSyncServerInfo * info)
{
  return info->clock_port;
}

gchar *
gst_sync_server_info_get_uri (GstSyncServerInfo * info)
{
  return g_strdup (info->uri);
}

guint64
gst_sync_server_info_get_base_time (GstSyncServerInfo * info)
{
  return info->base_time;
}

guint64
gst_sync_server_info_get_latency (GstSyncServerInfo * info)
{
  return info->latency;
}

gboolean
gst_sync_server_info_get_paused (GstSyncServerInfo * info)
{
  return info->paused;
}

guint64
gst_sync_server_info_get_paused_time (GstSyncServerInfo * info)
{
  return info->paused_time;
}
