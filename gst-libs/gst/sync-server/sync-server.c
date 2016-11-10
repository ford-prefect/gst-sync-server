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

#include <gst/gst.h>
#include <gst/net/gstnet.h>
#include <json-glib/json-glib.h>
#include <glib-unix.h>

#include "sync-server.h"
#include "sync-control-server.h"
#include "sync-control-tcp-server.h"

struct _GstSyncServer {
  GObject parent;

  gchar *control_addr;
  gint control_port;
  gint clock_port;
  guint64 latency;
  guint64 base_time; /* time of first transition to PLAYING */
  guint64 paused_time; /* what to offset base time by */
  guint64 last_pause_time;

  gchar *uri;
  GHashTable *fakesinks;

  gboolean started;
  gboolean paused;
  GstElement *pipeline;

  GstNetTimeProvider *clock_provider;
  GstClock *clock;

  GstSyncControlServer *server;
};

struct _GstSyncServerClass {
  GObjectClass parent;
};

#define gst_sync_server_parent_class parent_class
G_DEFINE_TYPE (GstSyncServer, gst_sync_server, G_TYPE_OBJECT);

GST_DEBUG_CATEGORY_STATIC (sync_server_debug);
#define GST_CAT_DEFAULT sync_server_debug

enum {
  PROP_0,
  PROP_CONTROL_SERVER,
  PROP_CONTROL_ADDRESS,
  PROP_CONTROL_PORT,
  PROP_URI,
  PROP_LATENCY,
};

#define DEFAULT_PORT 0
#define DEFAULT_LATENCY 300 * GST_MSECOND

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

void
gst_sync_server_info_free (GstSyncServerInfo *info)
{
  g_boxed_free (GST_TYPE_SYNC_SERVER_INFO, info);
}

static void
gst_sync_server_cleanup (GstSyncServer * self)
{
  if (self->clock_provider) {
    g_object_unref (self->clock_provider);
    self->clock_provider = NULL;
  }

  if (self->pipeline) {
    gst_element_set_state (self->pipeline, GST_STATE_NULL);
    gst_object_unref (self->pipeline);
    self->pipeline = NULL;
  }

  if (self->server) {
    gst_sync_control_server_stop (self->server);
    g_object_unref (self->server);
    self->server = NULL;
  }
}

static void
gst_sync_server_dispose (GObject * object)
{
  GstSyncServer *self = GST_SYNC_SERVER (object);

  if (self->started)
    gst_sync_server_stop (self);

  g_free (self->control_addr);
  g_free (self->uri);

  if (self->fakesinks) {
    g_hash_table_unref (self->fakesinks);
    self->fakesinks = NULL;
  }

  if (self->clock)
    gst_object_unref (self->clock);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static gboolean
update_pipeline (GstSyncServer * self)
{
  GstStateChangeReturn ret;

  gst_child_proxy_set (GST_CHILD_PROXY (self->pipeline),
      "uridecodebin::uri", self->uri, NULL);

  gst_pipeline_set_latency (GST_PIPELINE (self->pipeline),
      self->latency);

  if (!self->paused) {
    self->base_time = gst_clock_get_time (self->clock);
    self->paused_time = 0;

    GST_DEBUG_OBJECT (self, "Setting base time: %lu", self->base_time);
    gst_element_set_base_time (self->pipeline, self->base_time);
  }

  ret = gst_element_set_state (self->pipeline,
      self->paused ? GST_STATE_PAUSED : GST_STATE_PLAYING);

  if (ret == GST_STATE_CHANGE_FAILURE) {
    GST_ERROR_OBJECT (self, "Could not play new URI");
    return FALSE;
  }

  return TRUE;
}

static void
gst_sync_server_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSyncServer *self = GST_SYNC_SERVER (object);

  switch (property_id) {
    case PROP_CONTROL_SERVER:
      if (self->server)
        g_object_unref (self->server);

      self->server = g_value_dup_object (value);
      break;

    case PROP_CONTROL_ADDRESS:
      if (self->control_addr)
        g_free (self->control_addr);

      self->control_addr = g_value_dup_string (value);
      break;

    case PROP_CONTROL_PORT:
      self->control_port = g_value_get_int (value);
      break;

    case PROP_URI:
      if (self->uri)
        g_free (self->uri);

      self->uri = g_value_dup_string (value);

      if (self->pipeline) {
        /* We need to update things */
        gst_element_set_state (self->pipeline, GST_STATE_NULL);
        update_pipeline (self);
      }

      break;

    case PROP_LATENCY:
      self->latency = g_value_get_uint64 (value);
      /* We don't distribute this immediately as it will cause a glitch */
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gst_sync_server_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstSyncServer *self = GST_SYNC_SERVER (object);

  switch (property_id) {
    case PROP_CONTROL_SERVER:
      g_value_set_object (value, self->server);
      break;

    case PROP_CONTROL_ADDRESS:
      g_value_set_string (value, self->control_addr);
      break;

    case PROP_CONTROL_PORT:
      g_value_set_int (value, self->control_port);
      break;

    case PROP_URI:
      g_value_set_string (value, self->uri);
      break;

    case PROP_LATENCY:
      g_value_set_uint64 (value, self->latency);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gst_sync_server_class_init (GstSyncServerClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = GST_DEBUG_FUNCPTR (gst_sync_server_dispose);
  object_class->set_property =
    GST_DEBUG_FUNCPTR (gst_sync_server_set_property);
  object_class->get_property =
    GST_DEBUG_FUNCPTR (gst_sync_server_get_property);

  g_object_class_install_property (object_class, PROP_CONTROL_SERVER,
      g_param_spec_object ("control-server", "Control server",
        "Control server object (NULL => use TCP control server)",
        G_TYPE_OBJECT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_CONTROL_ADDRESS,
      g_param_spec_string ("control-address", "Control address",
        "Address for control", NULL,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_CONTROL_PORT,
      g_param_spec_int ("control-port", "Control port", "Port for control", 0,
        65535, DEFAULT_PORT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_URI,
      g_param_spec_string ("uri", "URI", "URI to provide clients", NULL,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_LATENCY,
      g_param_spec_uint64 ("latency", "Latency",
        "Pipeline latency for clients", 0, G_MAXUINT64, DEFAULT_LATENCY,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_signal_new_class_handler ("eos", GST_TYPE_SYNC_SERVER, G_SIGNAL_RUN_FIRST,
      NULL, NULL, NULL, NULL, G_TYPE_NONE, 0);

  GST_DEBUG_CATEGORY_INIT (sync_server_debug, "syncserver", 0, "GstSyncServer");
}

static void
gst_sync_server_init (GstSyncServer * self)
{
  self->control_addr = NULL;
  self->control_port = DEFAULT_PORT;

  self->uri = NULL;
  self->latency = DEFAULT_LATENCY;
  self->started = FALSE;
  self->paused = FALSE;
  self->paused_time = 0;
  self->last_pause_time = GST_CLOCK_TIME_NONE;

  self->server = NULL;

  self->fakesinks = g_hash_table_new (g_direct_hash, g_direct_equal);
}

GstSyncServer *
gst_sync_server_new (const gchar * control_addr, gint control_port)
{
  return
    g_object_new (GST_TYPE_SYNC_SERVER,
        "control-address", control_addr,
        "control-port", control_port,
        NULL);
}

static void
pad_added_cb (GstElement * bin, GstPad * pad, gpointer user_data)
{
  GstSyncServer *self = GST_SYNC_SERVER (user_data);
  GstElement *fakesink;
  GstPad *sinkpad;

  fakesink = gst_element_factory_make ("fakesink", NULL);
  g_assert (fakesink != NULL);
  sinkpad = gst_element_get_static_pad (fakesink, "sink");
  g_assert (sinkpad != NULL);

  g_object_set (fakesink, "sync", TRUE, "enable-last-sample", FALSE, NULL);

  gst_bin_add (GST_BIN (self->pipeline), fakesink);

  if (gst_pad_link (pad, sinkpad) != GST_PAD_LINK_OK)
    GST_ERROR_OBJECT (self, "Could not link pad");

  if (!gst_element_sync_state_with_parent (fakesink))
    GST_ERROR_OBJECT (self, "Could not sync state with parent");

  g_hash_table_insert (self->fakesinks, pad, fakesink);
}

static void
pad_removed_cb (GstElement * bin, GstPad * pad, gpointer user_data)
{
  GstSyncServer *self = GST_SYNC_SERVER (user_data);
  GstElement *sink;

  sink = g_hash_table_lookup (self->fakesinks, pad);
  g_return_if_fail (sink != NULL);

  gst_element_set_state (sink, GST_STATE_NULL);
  gst_bin_remove (GST_BIN (self->pipeline), sink);
}

static void
fill_sync_info (GstSyncServer * self, GstSyncServerInfo * info)
{
  /* FIXME: Deal with pausing on live streams */
  info->version = 1;

  info->clock_addr = g_strdup (self->control_addr);
  g_object_get (self->clock_provider, "port", &info->clock_port, NULL);

  info->uri = g_strdup (self->uri);

  info->base_time = self->base_time;

  info->latency = self->latency;

  info->paused = self->paused;
  info->paused_time = self->paused_time;
}

static gboolean
bus_cb (GstBus * bus, GstMessage * message, gpointer user_data)
{
  GstSyncServer *self = GST_SYNC_SERVER (user_data);

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR: {
      GError *err;
      gchar *debug;

      gst_message_parse_error (message, &err, &debug);
      GST_ERROR_OBJECT (self, "Got error: %s (%s)", err->message, debug);

      g_error_free (err);
      g_free (debug);
      break;
    }

    case GST_MESSAGE_STATE_CHANGED: {
      GstState new_state;

      gst_message_parse_state_changed (message, NULL, &new_state, NULL);

      if (GST_MESSAGE_SRC (message) != GST_OBJECT (self->pipeline))
       break;

      if ((self->paused && new_state == GST_STATE_PAUSED) ||
          new_state == GST_STATE_PLAYING) {
        GstSyncServerInfo info;

        /* FIXME: Implement a "ready" signal */
        fill_sync_info (self, &info);

        gst_sync_control_server_set_sync_info (self->server, &info);
      }

      break;
    }

    case GST_MESSAGE_EOS: {
      /* Should we be connecting to about-to-finish instead (and thus forcing
       * clients to give us a playbin) */
      if (GST_MESSAGE_SRC (message) == GST_OBJECT (self->pipeline)) {
        gst_element_set_state (self->pipeline, GST_STATE_NULL);
        g_signal_emit_by_name (self, "eos", NULL);
      }

      break;
    }

    default:
      break;
  }

  return TRUE;
}

static gboolean
autoplug_continue_cb (GstElement * uridecodebin, GstPad * pad, GstCaps * caps,
    gpointer user_data)
{
  /* We're done once a parser is plugged in */
  const GstStructure *st;
  gboolean parsed = FALSE, framed = FALSE;

  st = gst_caps_get_structure (caps, 0);

  if ((gst_structure_get_boolean (st, "parsed", &parsed) && parsed) ||
      (gst_structure_get_boolean (st, "framed", &framed) && framed))
    return FALSE;

  return TRUE;
}

gboolean
gst_sync_server_start (GstSyncServer * self, GError ** error)
{
  GstElement *uridecodebin;
  GstBus *bus;

  self->clock = gst_system_clock_obtain ();

  if (!self->uri) {
    GST_ERROR_OBJECT (self, "Need a URI before we can start");
    /* FIXME: Set error */
    goto fail;
  }

  if (!self->server)
    self->server = g_object_new (GST_TYPE_SYNC_CONTROL_TCP_SERVER, NULL);
  g_return_val_if_fail (GST_IS_SYNC_CONTROL_SERVER (self->server), FALSE);

  if (self->control_addr) {
    gst_sync_control_server_set_address (self->server, self->control_addr);
    gst_sync_control_server_set_port (self->server, self->control_port);
  }

  if (!gst_sync_control_server_start (self->server, error))
    goto fail;

  self->clock_provider =
    gst_net_time_provider_new (self->clock, self->control_addr, 0);

  if (self->clock_provider == NULL) {
    GST_ERROR_OBJECT (self, "Could not create net time provider");
    /* FIXME: Set error */
    goto fail;
  }

  g_object_get (self->clock_provider, "port", &self->clock_port, NULL);

  uridecodebin = gst_element_factory_make ("uridecodebin", "uridecodebin");
  if (!uridecodebin) {
    GST_ERROR_OBJECT (self, "Could not create uridecodebin");
    /* FIXME: Set error */
    goto fail;
  }

  g_signal_connect (uridecodebin, "pad-added", G_CALLBACK (pad_added_cb), self);
  g_signal_connect (uridecodebin, "pad-removed", G_CALLBACK (pad_removed_cb),
      self);
  g_signal_connect (uridecodebin, "autoplug-continue",
      G_CALLBACK (autoplug_continue_cb), NULL);

  self->pipeline = gst_pipeline_new ("sync-server");
  gst_bin_add (GST_BIN (self->pipeline), uridecodebin);

  gst_element_set_start_time (self->pipeline, GST_CLOCK_TIME_NONE);
  gst_pipeline_use_clock (GST_PIPELINE (self->pipeline), self->clock);

  bus = gst_pipeline_get_bus (GST_PIPELINE (self->pipeline));
  gst_bus_add_watch (bus, bus_cb, self);
  gst_object_unref (bus);

  if (!update_pipeline (self)) {
    /* FIXME: Set error */
    goto fail;
  }

  return TRUE;

fail:
  gst_sync_server_cleanup (self);

  return FALSE;
}

void
gst_sync_server_set_paused (GstSyncServer * self, gboolean paused)
{
  GstStateChangeReturn ret;

  if (self->paused == paused)
    return;

  self->paused = paused;

  if (self->paused)
    self->last_pause_time = gst_clock_get_time (self->clock);

  if (!paused) {
    self->paused_time +=
      gst_clock_get_time (self->clock) - self->last_pause_time;
    self->last_pause_time = GST_CLOCK_TIME_NONE;
    GST_DEBUG_OBJECT (self, "Total paused time: %lu", self->paused_time);

    GST_DEBUG_OBJECT (self, "Updating base time: %lu",
        self->base_time + self->paused_time);
    gst_element_set_base_time (self->pipeline,
        self->base_time + self->paused_time);
  }

  ret = gst_element_set_state (self->pipeline,
      self->paused ? GST_STATE_PAUSED : GST_STATE_PLAYING);

  if (ret == GST_STATE_CHANGE_FAILURE)
    GST_ERROR_OBJECT (self, "Could not change paused state");
}

void
gst_sync_server_stop (GstSyncServer * self)
{
  if (!self->started)
    return;

  gst_sync_server_cleanup (self);
}
