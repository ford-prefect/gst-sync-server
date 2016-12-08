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
 * SECTION: gst-sync-server
 * @short_description: Provides a server object to publish information that
 *                     clients on a network can use to play a stream in a
 *                     synchronised manner.
 *
 * The #GstSyncServer object provides API to start a server on one device on a
 * network that other devices (using #GstSyncClient) can communicate with to
 * play a stream such that all devices are playing the same stream at the same
 * time.
 *
 * It also provides API to control these clients and perform tasks such as
 * switching the currently playing stream, pausing/unpausing, etc.
 *
 * #GstSyncServer itself does not implement the network transport for
 * controlling the client, but defers that to an object that implements the
 * #GstSyncControlServer interface. A default TCP-based implementation is
 * provided with this library, however.
 */

#include <gst/gst.h>
#include <gst/net/gstnet.h>
#include <glib-unix.h>

#include "sync-server.h"
#include "sync-server-info.h"
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

#define GST_SYNC_SERVER_ERROR \
  (g_quark_from_static_string ("gst-sync-server-error-quark"))

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
    self->started = FALSE;
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

  /**
   * GstSyncServer:control-server:
   *
   * The implementation of the control protocol that should be used to
   * communicate with clients. This object must implement the
   * #GstSyncControlServer interface. If set to NULL, a built-in TCP
   * implementation is used.
   */
  g_object_class_install_property (object_class, PROP_CONTROL_SERVER,
      g_param_spec_object ("control-server", "Control server",
        "Control server object (NULL => use TCP control server)",
        G_TYPE_OBJECT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstSyncServer:control-address:
   *
   * The network address for the control server to listen on.
   */
  g_object_class_install_property (object_class, PROP_CONTROL_ADDRESS,
      g_param_spec_string ("control-address", "Control address",
        "Address for control", NULL,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstSyncServer:control-port:
   *
   * The network port for the control server to listen on.
   */
  g_object_class_install_property (object_class, PROP_CONTROL_PORT,
      g_param_spec_int ("control-port", "Control port", "Port for control", 0,
        65535, DEFAULT_PORT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstSyncServer:uri:
   *
   * The URI that clients should play.
   */
  g_object_class_install_property (object_class, PROP_URI,
      g_param_spec_string ("uri", "URI", "URI to provide clients", NULL,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstSyncServer:latency:
   *
   * The pipeline latency that clients should use. This should be large enough
   * to account for any buffering that is expected (network related for
   * HTTP/RTP/... streams, and worst-case audio device latency).
   */
  g_object_class_install_property (object_class, PROP_LATENCY,
      g_param_spec_uint64 ("latency", "Latency",
        "Pipeline latency for clients", 0, G_MAXUINT64, DEFAULT_LATENCY,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstSyncServer::eos
   *
   * Emitted when the currently playing URI reaches the end of the stream. Can
   * be used, for example, to distribute a new URI to clients via the
   * #GstSyncServer:uri property.
   */
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

/**
 * gst_sync_server_new:
 * @control_addr: The network address that the server should listen on
 * @control_port: The network port that the server should listen on
 *
 * Creates a new #GstSyncServer object that will listen on the given network
 * address/port pair once started.
 *
 * Returns: (transfer full): A new #GstSyncServer object.
 */
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

static GstSyncServerInfo *
get_sync_info (GstSyncServer * self)
{
  GstSyncServerInfo *info;
  guint clock_port;

  info = gst_sync_server_info_new ();

  g_object_get (self->clock_provider, "port", &clock_port, NULL);

  g_object_set (info,
      "clock-address", self->control_addr,
      "clock-port", clock_port,
      "uri", self->uri,
      "base-time", self->base_time,
      "latency", self->latency,
      "paused", self->paused, /* FIXME: Deal with pausing on live streams */
      "paused-time", self->paused_time,
      NULL);

  return info;
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
        GstSyncServerInfo *info;

        /* FIXME: Implement a "ready" signal */
        info = get_sync_info (self);

        gst_sync_control_server_set_sync_info (self->server, info);

        g_object_unref (info);
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

/**
 * gst_sync_server_start:
 * @server: The #GstSyncServer object
 * @error: If non-NULL, will be set to the appropriate #GError if starting the
 *         server fails.
 *
 * Starts the #GstSyncServer so that clients can connect and start synchronised
 * playback.
 *
 * Returns: #TRUE on success, and #FALSE if the server could not be started.
 */
gboolean
gst_sync_server_start (GstSyncServer * server, GError ** error)
{
  GstElement *uridecodebin;
  GstBus *bus;

  server->clock = gst_system_clock_obtain ();

  if (!server->uri) {
    GST_ERROR_OBJECT (server, "Need a URI before we can start");
    if (error) {
      *error = g_error_new (GST_SYNC_SERVER_ERROR, 0,
          "Cannot start server without a URI");
    }
    goto fail;
  }

  if (!server->server)
    server->server = g_object_new (GST_TYPE_SYNC_CONTROL_TCP_SERVER, NULL);
  g_return_val_if_fail (GST_IS_SYNC_CONTROL_SERVER (server->server), FALSE);

  if (server->control_addr) {
    gst_sync_control_server_set_address (server->server, server->control_addr);
    gst_sync_control_server_set_port (server->server, server->control_port);
  }

  if (!gst_sync_control_server_start (server->server, error))
    goto fail;

  server->clock_provider =
    gst_net_time_provider_new (server->clock, server->control_addr, 0);

  if (server->clock_provider == NULL) {
    GST_ERROR_OBJECT (server, "Could not create net time provider");
    if (error) {
      *error = g_error_new (GST_SYNC_SERVER_ERROR, 0,
          "Failed to initialise network time provider");
    }
    goto fail;
  }

  g_object_get (server->clock_provider, "port", &server->clock_port, NULL);

  uridecodebin = gst_element_factory_make ("uridecodebin", "uridecodebin");
  if (!uridecodebin) {
    GST_ERROR_OBJECT (server, "Could not create uridecodebin");
    if (error) {
      *error = g_error_new (GST_SYNC_SERVER_ERROR, 0,
          "Failed to instantiate a uridecodebin element");
    }
    goto fail;
  }

  g_signal_connect (uridecodebin, "pad-added", G_CALLBACK (pad_added_cb),
      server);
  g_signal_connect (uridecodebin, "pad-removed", G_CALLBACK (pad_removed_cb),
      server);
  g_signal_connect (uridecodebin, "autoplug-continue",
      G_CALLBACK (autoplug_continue_cb), NULL);

  server->pipeline = gst_pipeline_new ("sync-server");
  gst_bin_add (GST_BIN (server->pipeline), uridecodebin);

  gst_element_set_start_time (server->pipeline, GST_CLOCK_TIME_NONE);
  gst_pipeline_use_clock (GST_PIPELINE (server->pipeline), server->clock);

  bus = gst_pipeline_get_bus (GST_PIPELINE (server->pipeline));
  gst_bus_add_watch (bus, bus_cb, server);
  gst_object_unref (bus);

  if (!update_pipeline (server)) {
    if (error) {
      *error = g_error_new (GST_SYNC_SERVER_ERROR, 0,
          "Failed to set up local GStreamer pipeline with URI");
    }
    goto fail;
  }

  server->started = TRUE;

  return TRUE;

fail:
  gst_sync_server_cleanup (server);

  return FALSE;
}

/**
 * gst_sync_server_set_paused:
 * @server: The #GstSyncServer object
 * @paused: Whether the stream should be paused or unpaused
 *
 * Pauses or unpauses playback of the current stream on all connected clients.
 */
void
gst_sync_server_set_paused (GstSyncServer * server, gboolean paused)
{
  GstStateChangeReturn ret;

  if (server->paused == paused)
    return;

  server->paused = paused;

  if (server->paused)
    server->last_pause_time = gst_clock_get_time (server->clock);

  if (!paused) {
    server->paused_time +=
      gst_clock_get_time (server->clock) - server->last_pause_time;
    server->last_pause_time = GST_CLOCK_TIME_NONE;
    GST_DEBUG_OBJECT (server, "Total paused time: %lu", server->paused_time);

    GST_DEBUG_OBJECT (server, "Updating base time: %lu",
        server->base_time + server->paused_time);
    gst_element_set_base_time (server->pipeline,
        server->base_time + server->paused_time);
  }

  ret = gst_element_set_state (server->pipeline,
      server->paused ? GST_STATE_PAUSED : GST_STATE_PLAYING);

  if (ret == GST_STATE_CHANGE_FAILURE)
    GST_ERROR_OBJECT (server, "Could not change paused state");
}

/**
 * gst_sync_server_stop:
 * @server: The #GstSyncServer object
 *
 * Disconnects all existing clients and stops listening for new clients.
 */
void
gst_sync_server_stop (GstSyncServer * server)
{
  if (!server->started)
    return;

  gst_sync_server_cleanup (server);
}
