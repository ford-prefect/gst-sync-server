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

#include <json-glib/json-glib.h>

#include "sync-server.h"

void
gst_sync_server_info_free (GstSyncServerInfo *info)
{
  g_boxed_free (GST_TYPE_SYNC_SERVER_INFO, info);
}

static gpointer
_gst_sync_server_info_copy (gpointer from)
{
  GstSyncServerInfo *info = (GstSyncServerInfo *) from, *ret;

  ret = g_new (GstSyncServerInfo, 1);

  ret->version = info->version;
  ret->clock_addr = g_strdup (info->clock_addr);
  ret->clock_port = info->clock_port;
  ret->uri = g_strdup (info->uri);
  ret->base_time = info->base_time;
  ret->latency = info->latency;
  ret->paused = info->paused;
  ret->paused_time = info->paused_time;

  return ret;
}

static void
_gst_sync_server_info_free (gpointer boxed)
{
  GstSyncServerInfo *info = (GstSyncServerInfo *) boxed;

  g_free (info->clock_addr);
  g_free (info->uri);

  g_free (info);
}

static JsonNode *
gst_sync_server_info_serialize (gconstpointer boxed)
{
  GstSyncServerInfo *info = (GstSyncServerInfo *) boxed;
  JsonObject *object;
  JsonNode *node;

  object = json_object_new ();

  node = json_node_alloc ();
  json_node_init_int (node, info->version);
  json_object_set_member (object, "version", node);

  node = json_node_alloc ();
  json_node_init_string (node, info->clock_addr);
  json_object_set_member (object, "clock-addr", node);

  node = json_node_alloc ();
  json_node_init_int (node, info->clock_port);
  json_object_set_member (object, "clock-port", node);

  node = json_node_alloc ();
  json_node_init_string (node, info->uri);
  json_object_set_member (object, "uri", node);

  node = json_node_alloc ();
  json_node_init_int (node, info->base_time);
  json_object_set_member (object, "base-time", node);

  node = json_node_alloc ();
  json_node_init_int (node, info->latency);
  json_object_set_member (object, "latency", node);

  node = json_node_alloc ();
  json_node_init_boolean (node, info->paused);
  json_object_set_member (object, "paused", node);

  node = json_node_alloc ();
  json_node_init_int (node, info->paused_time);
  json_object_set_member (object, "paused-time", node);

  node = json_node_alloc ();
  json_node_init_object (node, object);
  json_object_unref (object);

  return node;
}

static gpointer
gst_sync_server_info_deserialize (JsonNode * node)
{
  GstSyncServerInfo *info;
  JsonObject *object;

  if (!JSON_NODE_HOLDS_OBJECT (node))
    return NULL;

  info = g_new (GstSyncServerInfo, 1);

  object = json_node_get_object (node);

  /* FIXME: do we need to add validatio here? */

  info->version = json_object_get_int_member (object, "version");
  info->clock_addr =
    g_strdup (json_object_get_string_member (object, "clock-addr"));
  info->clock_port = json_object_get_int_member (object, "clock-port");
  info->uri =
    g_strdup (json_object_get_string_member (object, "uri"));
  info->base_time = json_object_get_int_member (object, "base-time");
  info->latency = json_object_get_int_member (object, "latency");
  info->paused = json_object_get_boolean_member (object, "paused");
  info->paused_time = json_object_get_int_member (object, "paused-time");

  return info;
}

GType
gst_sync_server_info_get_type ()
{
  static GType type = 0;

  if (g_once_init_enter (&type)) {
    GType tmp;

    tmp =
      g_boxed_type_register_static (
        g_intern_static_string ("GstSyncServerInfo"),
        (GBoxedCopyFunc) _gst_sync_server_info_copy,
        (GBoxedFreeFunc) _gst_sync_server_info_free);

    json_boxed_register_serialize_func (tmp, JSON_NODE_OBJECT,
        gst_sync_server_info_serialize);
    json_boxed_register_deserialize_func (tmp, JSON_NODE_OBJECT,
        gst_sync_server_info_deserialize);

    g_once_init_leave (&type, tmp);
  }

  return type;
}
