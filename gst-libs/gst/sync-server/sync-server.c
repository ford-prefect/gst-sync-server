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
 *
 * The stream(s) to play are configured using the #GstSyncServer:playlist
 * property, which takes a #GVariant that can be constructed using
 * gst_sync_server_playlist_new() and manipulated by related functions.
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
  guint64 base_time_offset; /* what to offset base time by */
  guint64 stream_start_delay;
  guint64 last_pause_time;
  guint64 last_duration;

  gchar **uris;
  guint64 *durations;
  guint64 n_tracks;
  guint64 current_track; /* set to -1 at the end of the playlist */
  GHashTable *fakesinks;

  gboolean server_started;
  gboolean stopped;
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
  PROP_PLAYLIST,
  PROP_LATENCY,
  PROP_STREAM_START_DELAY,
};

#define DEFAULT_PORT 0
#define DEFAULT_LATENCY (300 * GST_MSECOND)
#define DEFAULT_STREAM_START_DELAY (500 * GST_MSECOND)

static void
free_playlist (GstSyncServer * self)
{
  gst_sync_server_playlist_free_tracks (self->uris, self->durations,
      self->n_tracks);

  self->n_tracks = 0;
  self->current_track = -1;
  self->uris = NULL;
  self->durations = NULL;
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
    self->server_started = FALSE;
  }
}

static void
gst_sync_server_dispose (GObject * object)
{
  GstSyncServer *self = GST_SYNC_SERVER (object);

  if (self->server_started)
    gst_sync_server_stop (self);

  g_free (self->control_addr);

  free_playlist (self);

  if (self->fakesinks) {
    g_hash_table_unref (self->fakesinks);
    self->fakesinks = NULL;
  }

  if (self->clock)
    gst_object_unref (self->clock);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static gboolean
update_pipeline (GstSyncServer * self, gboolean advance)
{
  GstStateChangeReturn ret;
  GstState new_state;

  if (advance) {
    if (self->current_track == -1 ||
        self->current_track + 1 == self->n_tracks) {
      /* We're done with all the tracks */
      return TRUE;
    }

    if (self->durations[self->current_track] != GST_CLOCK_TIME_NONE)
      self->base_time_offset += self->durations[self->current_track];
    else if (self->last_duration != GST_CLOCK_TIME_NONE)
      self->base_time_offset += self->last_duration;
    else {
      /* If we don't know what the duration to skip forwards by is, reset */
      advance = FALSE;
    }

    self->base_time_offset += self->stream_start_delay;
    self->current_track++;
  }

  gst_child_proxy_set (GST_CHILD_PROXY (self->pipeline), "uridecodebin::uri",
      self->uris[self->current_track], NULL);

  gst_pipeline_set_latency (GST_PIPELINE (self->pipeline),
      self->latency);

  if (!self->stopped && !self->paused) {
    if (!advance) {
      self->base_time = gst_clock_get_time (self->clock);
      self->base_time_offset = 0;
    }

    GST_DEBUG_OBJECT (self, "Setting base time: %lu + %lu", self->base_time,
        self->base_time_offset);
    gst_element_set_base_time (self->pipeline,
        self->base_time + self->base_time_offset);
  }

  if (self->stopped)
    new_state = GST_STATE_NULL;
  else if (self->paused)
    new_state = GST_STATE_PAUSED;
  else
    new_state = GST_STATE_PLAYING;

  ret = gst_element_set_state (self->pipeline, new_state);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    GST_ERROR_OBJECT (self, "Could not play new URI");
    return FALSE;
  }

  return TRUE;
}

static GstSyncServerInfo *
get_sync_info (GstSyncServer * self)
{
  GstSyncServerInfo *info;
  guint clock_port;
  GVariant *playlist;

  info = gst_sync_server_info_new ();

  playlist = gst_sync_server_playlist_new (self->uris, self->durations,
      self->n_tracks, self->current_track);

  g_object_get (self->clock_provider, "port", &clock_port, NULL);

  g_object_set (info,
      "clock-address", self->control_addr,
      "clock-port", clock_port,
      "playlist", playlist, /* Takes ownership of the floating ref */
      "base-time", self->base_time,
      "base-time-offset", self->base_time_offset,
      "latency", self->latency,
      "stream-start-delay", self->stream_start_delay,
      "stopped", self->stopped,
      "paused", self->paused, /* FIXME: Deal with pausing on live streams */
      NULL);

  return info;
}

static void
client_joined_cb (GstSyncControlServer * server, const gchar * id,
    const GVariant * config, gpointer user_data)
{
  GstSyncServer *self = GST_SYNC_SERVER (user_data);

  g_signal_emit_by_name (self, "client-joined", id, config);
}

static void
client_left_cb (GstSyncControlServer * server, const gchar * id,
    gpointer user_data)
{
  GstSyncServer *self = GST_SYNC_SERVER (user_data);

  g_signal_emit_by_name (self, "client-left", id);
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

      /* We just proxy the signal */
      g_signal_connect (self->server, "client-joined",
          G_CALLBACK (client_joined_cb), self);
      g_signal_connect (self->server, "client-left",
          G_CALLBACK (client_left_cb), self);

      break;

    case PROP_CONTROL_ADDRESS:
      if (self->control_addr)
        g_free (self->control_addr);

      self->control_addr = g_value_dup_string (value);
      break;

    case PROP_CONTROL_PORT:
      self->control_port = g_value_get_int (value);
      break;

    case PROP_PLAYLIST: {
      gchar **old_uris, **new_uris;
      guint64 *old_durations, old_current_track, old_n_tracks;
      guint64 *new_durations, new_current_track, new_n_tracks;
      GVariant *playlist;
      int i;

      old_uris = self->uris;
      old_durations = self->durations;
      old_current_track = self->current_track;
      old_n_tracks = self->n_tracks;

      playlist = g_value_get_variant (value);
      gst_sync_server_playlist_get_tracks (playlist, &self->uris,
          &self->durations, &self->n_tracks);
      self->current_track =
        gst_sync_server_playlist_get_current_track (playlist);

      if (self->server_started) {
        if (old_n_tracks == 0 ||
            old_current_track != self->current_track ||
            !g_str_equal (old_uris[old_current_track],
              self->uris[self->current_track])) {
          /* We need to update things */
          gst_element_set_state (self->pipeline, GST_STATE_NULL);
          update_pipeline (self, FALSE);
        } else {
          GstSyncServerInfo *info;
          info = get_sync_info (self);
          gst_sync_control_server_set_sync_info (self->server,
              get_sync_info (self));
          g_object_unref (info);
        }
      }

      gst_sync_server_playlist_free_tracks (old_uris, old_durations,
          old_n_tracks);

      break;
    }

    case PROP_LATENCY:
      self->latency = g_value_get_uint64 (value);
      /* We don't distribute this immediately as it will cause a glitch */
      break;

    case PROP_STREAM_START_DELAY:
      self->stream_start_delay = g_value_get_uint64 (value);
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

    case PROP_PLAYLIST:
      g_value_set_variant (value, NULL /* FIXME */);
      break;

    case PROP_LATENCY:
      g_value_set_uint64 (value, self->latency);
      break;

    case PROP_STREAM_START_DELAY:
      g_value_set_uint64 (value, self->stream_start_delay);
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
   * GstSyncServer:playlist:
   *
   * A #GVariant tuple of the current track index and an array of playlist
   * entries. Each playlist entry, in turn, is a tuple of a URI and its
   * duration. Unknown durations can be set to 0, which might cause a small
   * (network-dependent) delay in swiching tracks.
   * 
   * See gst_sync_server_playlist_new() and related functions for easy
   * manipulation of these playlists.
   */
  g_object_class_install_property (object_class, PROP_PLAYLIST,
      g_param_spec_variant ("playlist", "Playlist", "Playlist to send clients",
        GST_TYPE_SYNC_SERVER_PLAYLIST, NULL,
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
   * GstSyncServer:stream-start-delay:
   *
   * The amount of time to wait between streams before starting. This allows
   * for devices which take different amounts of time to load the data (either
   * due to network delays or differing storage speeds) to start smoothly at
   * the same time when switching streams.
   */
  g_object_class_install_property (object_class, PROP_STREAM_START_DELAY,
      g_param_spec_uint64 ("stream-start-delay", "Stream start delay",
        "Delay before starting stream playback", 0, G_MAXUINT64,
        DEFAULT_STREAM_START_DELAY,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstSyncServer::end-of-stream
   *
   * Emitted when the currently playing URI reaches the end of the stream. This
   * is called for each stream in the current playlist.
   */
  g_signal_new_class_handler ("end-of-stream", GST_TYPE_SYNC_SERVER,
      G_SIGNAL_RUN_FIRST, NULL, NULL, NULL, NULL, G_TYPE_NONE, 0);

  /**
   * GstSyncServer::end-of-playlist
   *
   * Emitted when the currently all the songs in the playlist have finished
   * playing.
   */
  g_signal_new_class_handler ("end-of-playlist", GST_TYPE_SYNC_SERVER,
      G_SIGNAL_RUN_FIRST, NULL, NULL, NULL, NULL, G_TYPE_NONE, 0);

  GST_DEBUG_CATEGORY_INIT (sync_server_debug, "syncserver", 0, "GstSyncServer");

  /**
   * GstSyncServer::client-joined:
   * @server: the #GstSynclServer
   * @id: (transfer none): the client ID as a string
   * @config: (transfer none): client-specific configuration as a #GVariant
   *          dictionary
   *
   * Emitted whenever a new client connects.
   */
  g_signal_new_class_handler ("client-joined", GST_TYPE_SYNC_SERVER,
      G_SIGNAL_RUN_LAST, NULL, NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_STRING,
      G_TYPE_VARIANT, NULL);

  /**
   * GstSyncServer::client-left:
   * @server: the #GstSyncr
   * @id: (transfer none): the client ID as a string
   *
   * Emitted whenever a client disconnects.
   */
  g_signal_new_class_handler ("client-left", GST_TYPE_SYNC_SERVER,
      G_SIGNAL_RUN_LAST, NULL, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING,
      NULL);
}

static void
gst_sync_server_init (GstSyncServer * self)
{
  self->control_addr = NULL;
  self->control_port = DEFAULT_PORT;

  self->n_tracks = 0;
  self->current_track = 0;
  self->uris = NULL;
  self->durations = NULL;
  self->latency = DEFAULT_LATENCY;
  self->stream_start_delay = DEFAULT_STREAM_START_DELAY;
  self->server_started = FALSE;
  self->paused = FALSE;
  self->base_time_offset = 0;
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
          (self->stopped && new_state == GST_STATE_NULL) ||
          new_state == GST_STATE_PLAYING) {
        GstSyncServerInfo *info;

        /* FIXME: Implement a "ready" signal */
        info = get_sync_info (self);

        gst_sync_control_server_set_sync_info (self->server, info);

        g_object_unref (info);
      }

      if (new_state == GST_STATE_PLAYING) {
        gst_element_query_duration (self->pipeline, GST_FORMAT_TIME,
            &self->last_duration);
      }

      break;
    }

    case GST_MESSAGE_EOS: {
      /* Should we be connecting to about-to-finish instead (and thus forcing
       * clients to give us a playbin) */
      if (GST_MESSAGE_SRC (message) == GST_OBJECT (self->pipeline)) {
        gst_element_set_state (self->pipeline, GST_STATE_NULL);
        g_signal_emit_by_name (self, "end-of-stream");

        if (self->current_track + 1 == self->n_tracks) {
          self->current_track = -1;
          g_signal_emit_by_name (self, "end-of-playlist");
        }

        update_pipeline (self, TRUE);
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

  if (!server->n_tracks) {
    GST_ERROR_OBJECT (server, "Need a playlist before we can start");
    if (error) {
      *error = g_error_new (GST_SYNC_SERVER_ERROR, 0,
          "Cannot start server without a URI");
    }
    goto fail;
  }

  if (!server->server) {
    GstSyncControlTcpServer *tcp_server;

    tcp_server = g_object_new (GST_TYPE_SYNC_CONTROL_TCP_SERVER, NULL);
    g_object_set (server, "control-server", tcp_server, NULL);

    g_object_unref (tcp_server);
  }

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
  gst_pipeline_set_auto_flush_bus (GST_PIPELINE (server->pipeline), FALSE);

  bus = gst_pipeline_get_bus (GST_PIPELINE (server->pipeline));
  gst_bus_add_watch (bus, bus_cb, server);
  gst_object_unref (bus);

  if (!update_pipeline (server, FALSE)) {
    if (error) {
      *error = g_error_new (GST_SYNC_SERVER_ERROR, 0,
          "Failed to set up local GStreamer pipeline with URI");
    }
    goto fail;
  }

  server->server_started = TRUE;

  return TRUE;

fail:
  gst_sync_server_cleanup (server);

  return FALSE;
}

/**
 * gst_sync_server_set_stopped:
 * @server: The #GstSyncServer object
 * @stopped: Whether the stream should be stopped (or restarted)
 *
 * Stops or restarts playback of the current stream on all connected clients.
 */
void
gst_sync_server_set_stopped (GstSyncServer * server, gboolean stopped)
{
  GstStateChangeReturn ret;

  if (server->stopped == stopped)
    return;

  server->stopped = stopped;
  update_pipeline (server, FALSE);
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
    server->base_time_offset +=
      gst_clock_get_time (server->clock) - server->last_pause_time;
    server->last_pause_time = GST_CLOCK_TIME_NONE;

    GST_DEBUG_OBJECT (server, "Updating base time: %lu",
        server->base_time + server->base_time_offset);
    gst_element_set_base_time (server->pipeline,
        server->base_time + server->base_time_offset);
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
  if (!server->server_started)
    return;

  gst_sync_server_cleanup (server);
}

/**
 * gst_sync_server_playlist_get_tracks:
 * @playlist: The playlist
 * @uris: (out callee-allocates) (array length=n_tracks): A list of string URIs
 *        representing the playlist
 * @durations: (out callee-allocates) (array length=n_tracks): A list of
 *        durations corresponding to each URI in @uris (set to
 *        #GST_CLOCK_TIME_NONE if duration for a stream is unknown)
 * @n_tracks: (out caller-allocates): The number of tracks returned in @uris
 *        and @durations.
 *
 * Returns the set of tracks and their corresponding durations, if available.
 * Use gst_sync_server_playlist_free_tracks() to free @uris and @durations.
 */
void
gst_sync_server_playlist_get_tracks (GVariant * playlist, gchar *** uris,
    guint64 ** durations, guint64 * n_tracks)
{
  GVariantIter *iter;
  gchar *uri;
  guint64 duration;
  int i;

  g_variant_get_child (playlist, 1, "a(st)", &iter);

  if (n_tracks)
    *n_tracks = g_variant_iter_n_children (iter);
  if (uris)
    *uris = g_new0 (gchar *, *n_tracks);
  if (durations)
    *durations = g_new0 (guint64, *n_tracks);

  for (i = 0; g_variant_iter_next (iter, "(st)", &uri, &duration); i++) {
    if (uris)
      (*uris)[i] = uri;
    if (durations)
      (*durations)[i] = duration;
  }
}

/**
 * gst_sync_server_playlist_free_tracks:
 * @uris: (array length=n_tracks): A list of string URIs representing the
 *        playlist
 * @durations: (array length=n_tracks): A list of durations corresponding to
 *             each URI
 * @n_tracks: The number of tracks in @uris and @durations.
 *
 * Frees the URIs and durations (allocated by
 * gst_sync_server_playlist_get_tracks().
 */
void
gst_sync_server_playlist_free_tracks (gchar ** uris, guint64 * durations,
  guint64 n_tracks)
{
  int i;

  for (i = 0; i < n_tracks; i++)
    g_free (uris[i]);

  g_free (uris);
  g_free (durations);
}

/**
 * gst_sync_server_playlist_get_current_track:
 * @playlist: The playlist
 *
 * Returns: The currently streaming track
 */
guint64
gst_sync_server_playlist_get_current_track (GVariant * playlist)
{
  guint64 current_track;

  g_variant_get_child (playlist, 0, "t", &current_track);

  return current_track;
}

static GVariant *
make_tracks (gchar ** uris, guint64 * durations,
    guint64 n_tracks)
{
  GVariantBuilder tracks;
  int i;

  g_variant_builder_init (&tracks, G_VARIANT_TYPE ("a(st)"));

  for (i = 0; i < n_tracks; i++)
    g_variant_builder_add (&tracks, "(st)", uris[i], durations[i]);

  return g_variant_builder_end (&tracks);
}

/**
 * gst_sync_server_playlist_new:
 * @uris: (array length=n_tracks): A list of string URIs representing the
 *        playlist
 * @durations: (array length=n_tracks): A list of durations corresponding to
 *             each URI (set to GST_CLOCK_TIME_NONE if unknown)
 * @n_tracks: The number of tracks in @uris and @durations.
 * @current_track: The current track to play
 *
 * Creates a playlist object from the given uris, durations and current track.
 *
 * Returns: The playlist as a #GVariant
 */
GVariant *
gst_sync_server_playlist_new (gchar ** uris,
    guint64 * durations, guint64 n_tracks, guint64 current_track)
{
  GVariantBuilder new_playlist;

  g_variant_builder_init (&new_playlist, GST_TYPE_SYNC_SERVER_PLAYLIST);

  g_variant_builder_add (&new_playlist, "t", current_track);
  g_variant_builder_add_value (&new_playlist,
      make_tracks (uris, durations, n_tracks));

  return g_variant_builder_end (&new_playlist);
}

/**
 * gst_sync_server_playlist_set_tracks:
 * @playlist: (transfer full): The playlist
 * @uris: (array length=n_tracks): A list of string URIs representing the
 *        playlist
 * @durations: (array length=n_tracks): A list of durations corresponding to
 *             each URI (set to GST_CLOCK_TIME_NONE if unknown)
 * @n_tracks: The number of tracks in @uris and @durations.
 *
 * Changes the track list in the given playlist and returns a new playlist.
 *
 * Returns: (transfer full): The new playlist as a #GVariant
 */
GVariant *
gst_sync_server_playlist_set_tracks (GVariant * playlist, gchar ** uris,
    guint64 * durations, guint64 n_tracks)
{
  GVariantBuilder new_playlist;

  g_variant_builder_init (&new_playlist, GST_TYPE_SYNC_SERVER_PLAYLIST);

  g_variant_builder_add (&new_playlist, "t",
      gst_sync_server_playlist_get_current_track (playlist));
  g_variant_builder_add_value (&new_playlist,
      make_tracks (uris, durations, n_tracks));

  g_variant_unref (playlist);

  return g_variant_builder_end (&new_playlist);
}

/**
 * gst_sync_server_playlist_set_current_track:
 * @playlist: (transfer full): The playlist
 * @current_track: The current track to play
 *
 * Changes the currently playing track in the given playlist and returns a new
 * playlist.
 *
 * Returns: (transfer full): The new playlist as a #GVariant
 */
GVariant *
gst_sync_server_playlist_set_current_track (GVariant * playlist,
    guint64 current_track)
{
  GVariantBuilder new_playlist;
  GVariant *tracks;

  g_variant_builder_init (&new_playlist, GST_TYPE_SYNC_SERVER_PLAYLIST);
  tracks = g_variant_get_child_value (playlist, 1);

  g_variant_builder_add (&new_playlist, "t", current_track);
  g_variant_builder_add_value (&new_playlist, tracks);

  g_variant_unref (playlist);

  return g_variant_builder_end (&new_playlist);
}
