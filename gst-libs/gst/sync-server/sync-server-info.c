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

#include "sync-server.h"
#include "sync-server-info.h"

struct _GstSyncServerInfo {
  GObject parent;

  guint64 version;
  gchar *clock_addr;
  guint clock_port;
  GVariant *playlist;
  guint64 base_time;
  guint64 latency;
  gboolean stopped;
  gboolean paused;
  guint64 base_time_offset;
  guint64 stream_start_delay;
};

struct _GstSyncServerInfoClass {
  GObjectClass parent;
};

static JsonNode *
gst_sync_server_info_serialize_property (JsonSerializable *serializable,
    const gchar *property_name, const GValue *value, GParamSpec *pspec);
static gboolean
gst_sync_server_info_deserialize_property (JsonSerializable *serializable,
    const gchar *property_name, GValue *value, GParamSpec *pspec,
    JsonNode *property_node);

static void
gst_sync_server_info_json_iface_init (JsonSerializableIface *iface)
{
  iface->serialize_property = gst_sync_server_info_serialize_property;
  iface->deserialize_property = gst_sync_server_info_deserialize_property;
}

#define gst_sync_server_info_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstSyncServerInfo, gst_sync_server_info,
    G_TYPE_OBJECT, G_IMPLEMENT_INTERFACE (JSON_TYPE_SERIALIZABLE,
      gst_sync_server_info_json_iface_init));

#define DEFAULT_VERSION 1

enum {
  PROP_0,
  PROP_VERSION,
  PROP_CLOCK_ADDRESS,
  PROP_CLOCK_PORT,
  PROP_PLAYLIST,
  PROP_BASE_TIME,
  PROP_LATENCY,
  PROP_STOPPED,
  PROP_PAUSED,
  PROP_BASE_TIME_OFFSET,
  PROP_STREAM_START_DELAY,
};

static void
gst_sync_server_info_dispose (GObject * object)
{
  GstSyncServerInfo *info = GST_SYNC_SERVER_INFO (object);

  g_free (info->clock_addr);

  if (info->playlist)
    g_variant_unref (info->playlist);
}

static JsonNode *
gst_sync_server_info_serialize_property (JsonSerializable *serializable,
    const gchar *property_name, const GValue *value, GParamSpec *pspec)
{
  if (g_str_equal (property_name, "playlist")) {
    return json_gvariant_serialize (g_value_get_variant (value));
  } else {
    return json_serializable_default_serialize_property (serializable,
        property_name, value, pspec);
  }
}

static gboolean
gst_sync_server_info_deserialize_property (JsonSerializable *serializable,
    const gchar *property_name, GValue *value, GParamSpec *pspec,
    JsonNode *property_node)
{
  if (g_str_equal (property_name, "playlist")) {
    GVariant *playlist;

    playlist = json_gvariant_deserialize (property_node,
        GST_SYNC_SERVER_PLAYLIST_FORMAT_STRING, NULL);

    if (playlist) {
      g_value_set_variant (value, playlist);
      return TRUE;
    } else
      return FALSE;

  } else {
    return json_serializable_default_deserialize_property (serializable,
        property_name, value, pspec, property_node);
  }
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

    case PROP_PLAYLIST:
      if (info->playlist)
        g_variant_unref (info->playlist);

      info->playlist = g_value_dup_variant (value);
      break;

    case PROP_BASE_TIME:
      info->base_time = g_value_get_uint64 (value);
      break;

    case PROP_LATENCY:
      info->latency = g_value_get_uint64 (value);
      break;

    case PROP_STOPPED:
      info->stopped = g_value_get_boolean (value);
      break;

    case PROP_PAUSED:
      info->paused = g_value_get_boolean (value);
      break;

    case PROP_BASE_TIME_OFFSET:
      info->base_time_offset = g_value_get_uint64 (value);
      break;

    case PROP_STREAM_START_DELAY:
      info->stream_start_delay = g_value_get_uint64 (value);
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

    case PROP_PLAYLIST:
      g_value_set_variant (value, info->playlist);
      break;

    case PROP_BASE_TIME:
      g_value_set_uint64 (value, info->base_time);
      break;

    case PROP_LATENCY:
      g_value_set_uint64 (value, info->latency);
      break;

    case PROP_STOPPED:
      g_value_set_boolean (value, info->stopped);
      break;

    case PROP_PAUSED:
      g_value_set_boolean (value, info->paused);
      break;

    case PROP_BASE_TIME_OFFSET:
      g_value_set_uint64 (value, info->base_time_offset);
      break;

    case PROP_STREAM_START_DELAY:
      g_value_set_uint64 (value, info->stream_start_delay);
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

  g_object_class_install_property (object_class, PROP_PLAYLIST,
      g_param_spec_variant ("playlist", "Playlist",
        "Playlist as a current track index, and array of URI and durations",
        GST_TYPE_SYNC_SERVER_PLAYLIST, NULL,
        G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_BASE_TIME,
      g_param_spec_uint64 ("base-time", "Base time",
        "Base time of the GStreamer pipeline (ns)", 0, G_MAXUINT64, 0,
        G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_BASE_TIME_OFFSET,
      g_param_spec_uint64 ("base-time-offset", "Base time offset",
        "How much to offset base time by", 0, G_MAXUINT64, 0,
        G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_LATENCY,
      g_param_spec_uint64 ("latency", "Latency",
        "Latency of the GStreamer pipeline (ns)", 0, G_MAXUINT64, 0,
        G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_STREAM_START_DELAY,
      g_param_spec_uint64 ("stream-start-delay", "Stream start delay",
        "Delay before starting a stream (ns)", 0, G_MAXUINT64, 0,
        G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_STOPPED,
      g_param_spec_boolean ("stopped", "Stopped",
        "Whether playback is currently stopped", FALSE,
        G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PAUSED,
      g_param_spec_boolean ("paused", "Paused",
        "Whether playback is currently paused", FALSE,
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

GVariant *
gst_sync_server_info_get_playlist (GstSyncServerInfo * info)
{
  return g_variant_ref (info->playlist);
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
gst_sync_server_info_get_stopped (GstSyncServerInfo * info)
{
  return info->stopped;
}

gboolean
gst_sync_server_info_get_paused (GstSyncServerInfo * info)
{
  return info->paused;
}

guint64
gst_sync_server_info_get_base_time_offset (GstSyncServerInfo * info)
{
  return info->base_time_offset;
}

guint64
gst_sync_server_info_get_stream_start_delay (GstSyncServerInfo * info)
{
  return info->stream_start_delay;
}
