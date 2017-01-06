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
 * SECTION: gst-sync-client
 * @short_description: Provides a client object to receive information from a
 *                     #GstSyncServer to play a synchronised stream.
 *
 * The #GstSyncClient object provides API to connect to a #GstSyncServer in
 * order to receive and play back a stream synchronised with other clients on a
 * network.
 *
 * #GstSyncClient itself does not implement the network transport for receiving
 * messages from the server, but defers that to an object that implements the
 * #GstSyncControlClient interface. A default TCP-based implementation is
 * provided with this library.
 */

#include <gst/gst.h>
#include <gst/net/gstnet.h>

#include "sync-server-info.h"
#include "sync-client.h"
#include "sync-control-client.h"
#include "sync-control-tcp-client.h"

enum {
  NEED_SEEK,
  IN_SEEK,
  DONE_SEEK,
};

struct _GstSyncClient {
  GObject parent;

  gchar *id;
  GVariant *config;

  gchar *control_addr;
  gint control_port;

  GstSyncServerInfo *info;
  GMutex info_lock;

  GstPipeline *pipeline;
  GstClock *clock;

  GstSyncControlClient *client;
  gboolean synchronised;

  /* See bus_cb() for why this needs to be atomic */
  volatile int seek_state;
  GstClockTime seek_offset;

  guint64 last_duration;
};

struct _GstSyncClientClass {
  GObjectClass parent;
};

#define gst_sync_client_parent_class parent_class
G_DEFINE_TYPE (GstSyncClient, gst_sync_client, G_TYPE_OBJECT);

GST_DEBUG_CATEGORY_STATIC (sync_client_debug);
#define GST_CAT_DEFAULT sync_client_debug

enum {
  PROP_0,
  PROP_ID,
  PROP_CONFIG,
  PROP_CONTROL_CLIENT,
  PROP_CONTROL_ADDRESS,
  PROP_CONTROL_PORT,
  PROP_PIPELINE,
};

#define DEFAULT_PORT 0
#define DEFAULT_SEEK_TOLERANCE (200 * GST_MSECOND)

static void
gst_sync_client_dispose (GObject * object)
{
  GstSyncClient *self = GST_SYNC_CLIENT (object);

  if (self->pipeline) {
    gst_object_unref (self->pipeline);
    self->pipeline = NULL;
  }

  if (self->clock) {
    gst_object_unref (self->clock);
    self->clock = NULL;
  }

  g_free (self->id);
  self->id = NULL;

  if (self->config) {
    g_variant_unref (self->config);
    self->config = NULL;
  }

  g_free (self->control_addr);
  self->control_addr = NULL;

  if (self->info) {
    g_object_unref (self->info);
    self->info = NULL;
  }

  g_mutex_clear (&self->info_lock);

  if (self->client) {
    gst_sync_control_client_stop (self->client);
    g_object_unref (self->client);
    self->client = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
set_base_time (GstSyncClient * self)
{
  gst_element_set_start_time (GST_ELEMENT (self->pipeline),
      GST_CLOCK_TIME_NONE);

  GST_DEBUG_OBJECT (self, "Updating base time to: %lu + %lu + %lu",
      gst_sync_server_info_get_base_time (self->info),
      gst_sync_server_info_get_base_time_offset (self->info),
      self->seek_offset);
  gst_element_set_base_time (GST_ELEMENT (self->pipeline),
      gst_sync_server_info_get_base_time (self->info) +
      gst_sync_server_info_get_base_time_offset (self->info) +
      self->seek_offset);
}

#define LOOKUP_AND_SET(v, e, prop, typ, val)            \
  if (g_variant_lookup (v, prop, typ, &val))            \
    g_object_set (G_OBJECT (e), prop, val, NULL);

#define LOOKUP_AND_SET_NEG(v, e, prop, typ, val)        \
  if (g_variant_lookup (v, prop, typ, &val))            \
    g_object_set (G_OBJECT (e), prop, -val, NULL);

#define MAYBE_ADD(b, e)                                 \
  if (e)                                                \
    gst_bin_add (GST_BIN (b), e);

#define MAYBE_LINK(e, f, l)                             \
  if (e) {                                              \
    if (l)                                              \
      g_assert (gst_element_link (l, e));               \
    else                                                \
      f = e;                                            \
    l = e;                                              \
  }

static void
update_transform (GstSyncClient * self)
{
  GVariant *all = NULL, *transforms = NULL, *transform = NULL;
  guint64 v;
  GstElement *filter = NULL, *crop = NULL, *rotate = NULL, *scale = NULL,
             *scale_caps = NULL, *box = NULL;
  GstElement *first = NULL, *last = NULL;
  GstPad *target;

  /* If we don't have a client ID, we can't look for our transformation */
  if (!self->id)
    goto done;

  /* Get the dict of client -> transformation */
  all =  gst_sync_server_info_get_transform (self->info);
  if (!all)
    goto done;

  /* Look up our transformation */
  transforms = g_variant_lookup_value (all, self->id, G_VARIANT_TYPE_VARDICT);
  if (!transforms)
    goto done;

  /* First look for crop parameters */
  transform =
    g_variant_lookup_value (transforms, "crop", G_VARIANT_TYPE_VARDICT);
  if (transform) {
    crop = gst_element_factory_make ("videocrop", NULL);

    LOOKUP_AND_SET (transform, crop, "left", "x", v);
    LOOKUP_AND_SET (transform, crop, "right", "x", v);
    LOOKUP_AND_SET (transform, crop, "top", "x", v);
    LOOKUP_AND_SET (transform, crop, "bottom", "x", v);

    g_variant_unref (transform);
  }

  /* Now rotate/flip if required */
  if (g_variant_lookup (transforms, "rotate", "x", &v)) {
    rotate = gst_element_factory_make ("videoflip", NULL);
    g_object_set (rotate, "video-direction", v);
  }

  /* Then scale */
  transform =
    g_variant_lookup_value (transforms, "scale", G_VARIANT_TYPE_VARDICT);
  if (transform) {
    GstCaps *caps;

    scale = gst_element_factory_make ("videoscale", NULL);
    scale_caps = gst_element_factory_make ("capsfilter", NULL);

    caps = gst_caps_new_empty_simple ("video/x-raw");
    if (g_variant_lookup (transform, "width", "x", &v))
      gst_caps_set_simple (caps, "width", G_TYPE_INT, v, NULL);
    if (g_variant_lookup (transform, "height", "x", &v))
      gst_caps_set_simple (caps, "height", G_TYPE_INT, v, NULL);

    g_object_set (G_OBJECT (scale_caps), "caps", caps, NULL);

    gst_caps_unref (caps);
    g_variant_unref (transform);
  }

  /* Finally, box it appropriately */
  transform =
    g_variant_lookup_value (transforms, "offset", G_VARIANT_TYPE_VARDICT);
  if (transform) {
    box = gst_element_factory_make ("videobox", NULL);

    /* We apply the offests as negative values to add the box */
    LOOKUP_AND_SET_NEG (transform, box, "left", "x", v);
    LOOKUP_AND_SET_NEG (transform, box, "right", "x", v);
    LOOKUP_AND_SET_NEG (transform, box, "top", "x", v);
    LOOKUP_AND_SET_NEG (transform, box, "bottom", "x", v);

    g_variant_unref (transform);
  }

  filter = gst_bin_new ("video-filter");

  MAYBE_ADD (filter, crop);
  MAYBE_ADD (filter, rotate);
  MAYBE_ADD (filter, scale);
  MAYBE_ADD (filter, scale_caps);
  MAYBE_ADD (filter, box);

  MAYBE_LINK (crop, first, last);
  MAYBE_LINK (rotate, first, last);
  MAYBE_LINK (scale, first, last);
  MAYBE_LINK (scale_caps, first, last);
  MAYBE_LINK (box, first, last);

  /* We didn't find anything to filter, so done */
  if (!first || !last) {
    gst_object_unref (filter);
    goto done;
  }

  target = gst_element_get_static_pad (first, "sink");
  gst_element_add_pad (GST_ELEMENT (filter),
      gst_ghost_pad_new ("sink", target));
  gst_object_unref (target);

  target = gst_element_get_static_pad (last, "src");
  gst_element_add_pad (GST_ELEMENT (filter),
      gst_ghost_pad_new ("src", target));
  gst_object_unref (target);

  g_object_set (G_OBJECT (self->pipeline), "video-filter", filter, NULL);

done:
  if (transforms)
    g_variant_unref (transforms);
  if (all)
    g_variant_unref (all);
}

/* Call with info_lock held */
static void
update_pipeline (GstSyncClient * self, gboolean advance)
{
  gboolean is_live;
  gchar *uri, **uris;
  guint64 current_track, n_tracks, *durations, base_time_offset;
  GVariant *playlist;

  playlist = gst_sync_server_info_get_playlist (self->info);
  gst_sync_server_playlist_get_tracks (playlist, &uris, &durations, &n_tracks);
  current_track = gst_sync_server_playlist_get_current_track (playlist);
  g_variant_unref (playlist);

  if (advance) {
    if (current_track + 1 == n_tracks) {
      /* We're done with all the tracks */
      return;
    }

    base_time_offset = gst_sync_server_info_get_base_time_offset (self->info);

    if (durations[current_track] != GST_CLOCK_TIME_NONE)
      base_time_offset += durations[current_track];
    else if (self->last_duration != GST_CLOCK_TIME_NONE)
      base_time_offset += self->last_duration;
    else {
      /* If we don't know what the duration to skip forwards by is, wait for a
       * reset from the server*/
      return;
    }

    base_time_offset +=
      gst_sync_server_info_get_stream_start_delay (self->info);
    current_track++;

    playlist = gst_sync_server_info_get_playlist (self->info);
    playlist =
      gst_sync_server_playlist_set_current_track (playlist, current_track);

    g_object_set (self->info,
        "playlist", playlist,
        "base-time-offset", base_time_offset,
        NULL);
  }

  uri = uris[current_track];
  g_object_set (GST_OBJECT (self->pipeline), "uri", uri, NULL);

  gst_sync_server_playlist_free_tracks (uris, durations, n_tracks);

  gst_pipeline_set_latency (self->pipeline,
      gst_sync_server_info_get_latency (self->info));

  update_transform (self);

  if (gst_sync_server_info_get_stopped (self->info)) {
    /* Just stop the pipeline and we're done */
    if (gst_element_set_state (GST_ELEMENT (self->pipeline), GST_STATE_NULL) ==
        GST_STATE_CHANGE_FAILURE)
      GST_WARNING_OBJECT (self, "Error while stopping pipeline");
    return;
  }

  switch (gst_element_set_state (GST_ELEMENT (self->pipeline),
        GST_STATE_PAUSED)) {
    case GST_STATE_CHANGE_FAILURE:
      GST_WARNING_OBJECT (self, "Could not play uri: %s", uri);
      break;

    case GST_STATE_CHANGE_NO_PREROLL:
      is_live = TRUE;
      GST_DEBUG_OBJECT (self, "Detected live pipeline");
      break;

    default:
      is_live = FALSE;
      break;
  }

  self->seek_offset = 0;
  g_atomic_int_set (&self->seek_state, is_live ? DONE_SEEK : NEED_SEEK);

  /* We need to do PAUSED and PLAYING in separate steps so we don't have a race
   * between us and reading seek_state in bus_cb() */
  if (!gst_sync_server_info_get_paused (self->info)) {
    set_base_time (self);
    gst_element_set_state (GST_ELEMENT (self->pipeline), GST_STATE_PLAYING);
  }
}

static gboolean
bus_cb (GstBus * bus, GstMessage * message, gpointer user_data)
{
  GstSyncClient *self = GST_SYNC_CLIENT (user_data);

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ELEMENT: {
      const GstStructure *st;

      if (self->synchronised)
        break;

      st = gst_message_get_structure (message);
      if (!gst_structure_has_name (st, "gst-netclock-statistics"))
        break;

      gst_structure_get_boolean (st, "synchronised", &self->synchronised);
      if (!self->synchronised)
        break;

      if (!gst_clock_wait_for_sync (self->clock, 10 * GST_SECOND)) {
        GST_ERROR_OBJECT (self, "Could not synchronise clock");
        self->synchronised = FALSE;
        break;
      }

      GST_INFO_OBJECT (self, "Clock is synchronised, starting playback");

      g_mutex_lock (&self->info_lock);
      update_pipeline (self, FALSE);
      g_mutex_unlock (&self->info_lock);

      break;
    }

    case GST_MESSAGE_STATE_CHANGED: {
      GstState old_state, new_state;
      GstClockTime now;
      gint64 cur_pos;

      if (g_atomic_int_get (&self->seek_state) != NEED_SEEK ||
          GST_MESSAGE_SRC (message) != GST_OBJECT (self->pipeline))
        break;

      gst_message_parse_state_changed (message, &old_state, &new_state, NULL);

      if (old_state == GST_STATE_READY && new_state == GST_STATE_PAUSED) {
        GstElement *audio_sink;

        g_object_get (G_OBJECT (self->pipeline), "audio-sink", &audio_sink,
            NULL);

        g_object_set (G_OBJECT (audio_sink), "drift-tolerance", 10000 /* µs */,
            "alignment-threshold", 10 * GST_MSECOND, NULL);

        gst_object_unref (audio_sink);
      }

      if (old_state != GST_STATE_PAUSED && new_state != GST_STATE_PLAYING)
        break;

      now = gst_clock_get_time (self->clock);
      g_atomic_int_set (&self->seek_state, IN_SEEK);

      g_mutex_lock (&self->info_lock);

      cur_pos = now -
        gst_sync_server_info_get_base_time (self->info) -
        gst_sync_server_info_get_base_time_offset (self->info);

      if (cur_pos > DEFAULT_SEEK_TOLERANCE) {
        /* Let's seek ahead to prevent excessive clipping */
        GST_INFO_OBJECT (self, "Seeking: %lu", cur_pos);

        if (!gst_element_seek_simple (GST_ELEMENT (self->pipeline),
              GST_FORMAT_TIME, GST_SEEK_FLAG_SNAP_AFTER |
              GST_SEEK_FLAG_KEY_UNIT | GST_SEEK_FLAG_FLUSH,
              cur_pos)) {
          GST_WARNING_OBJECT (self, "Could not perform seek");

          g_atomic_int_set (&self->seek_state, DONE_SEEK);
        }
      } else {
        /* For the seek case, the base time will be set after the seek */
        GST_INFO_OBJECT (self, "Not seeking as we're within the threshold");
        g_atomic_int_set (&self->seek_state, DONE_SEEK);
      }

      g_mutex_unlock (&self->info_lock);

      gst_element_query_duration (GST_ELEMENT (self->pipeline),
          GST_FORMAT_TIME, &self->last_duration);

      break;
    }

    case GST_MESSAGE_ASYNC_DONE: {
      /* This message is first examined synchronously in the sync-message
       * signal.
       * The rationale for doing this is that (a) we want the most accurate
       * possible final seek position, and examining position asynchronously
       * will not guarantee that, and (b) setting the base time as early as
       * possible means we'll start rendering correctly synchronised buffers
       * sooner */
      if (g_atomic_int_get (&self->seek_state) != IN_SEEK)
        break;

      if (gst_element_query_position (GST_ELEMENT (self->pipeline),
            GST_FORMAT_TIME, &self->seek_offset)) {
        GST_INFO_OBJECT (self, "Adding offset: %lu", self->seek_offset);

        g_mutex_lock (&self->info_lock);
        set_base_time (self);
        g_mutex_unlock (&self->info_lock);
      }

      g_atomic_int_set (&self->seek_state, DONE_SEEK);

      break;
    }

    case GST_MESSAGE_EOS: {
      guint64 next_track, n_tracks;
      GVariant *playlist;

      if (GST_MESSAGE_SRC (message) != GST_OBJECT (self->pipeline))
        break;

      gst_element_set_state (GST_ELEMENT (self->pipeline), GST_STATE_NULL);

      playlist = gst_sync_server_info_get_playlist (self->info);
      gst_sync_server_playlist_get_tracks (playlist, NULL, NULL, &n_tracks);
      next_track = gst_sync_server_playlist_get_current_track (playlist) + 1;

      /* FIXME: added a stream start delay here */
      update_pipeline (self, TRUE);

      break;
    }

    default:
      break;
  }

  return TRUE;
}

static void
update_sync_info (GstSyncClient * self, GstSyncServerInfo * info)
{
  g_mutex_lock (&self->info_lock);

  if (!self->info) {
    /* First sync info update */
    GstBus *bus;
    gchar *clock_addr;

    self->info = info;

    clock_addr = gst_sync_server_info_get_clock_address (self->info);
    self->clock = gst_net_client_clock_new ("sync-server-clock",
        clock_addr, gst_sync_server_info_get_clock_port (self->info), 0);
    g_free (clock_addr);

    gst_pipeline_use_clock (self->pipeline, self->clock);

    bus = gst_pipeline_get_bus (self->pipeline);
    g_object_set (self->clock, "bus", bus, NULL);

    gst_bus_add_watch (bus, bus_cb, self);
    /* See bus_cb() for why we do this */
    gst_bus_enable_sync_message_emission (bus);
    g_signal_connect (G_OBJECT (bus), "sync-message::async-done",
        G_CALLBACK (bus_cb), self);

    gst_object_unref (bus);
  } else {
    /* Sync info changed, figure out what did. We do not expect the clock
     * parameters or latency to change */
    GstSyncServerInfo *old_info;
    GVariant *old_playlist, *new_playlist;
    guint64 old_track, new_track;

    old_info = self->info;
    self->info = info;

    old_playlist = gst_sync_server_info_get_playlist (old_info);
    new_playlist = gst_sync_server_info_get_playlist (self->info);

    /* We don't really care about changes to the playlist itself. What we want
     * to check is whether the current track has changed. This means that the
     * server can add/remove files from the playlist without affecting the
     * currently playing track. */
    old_track = gst_sync_server_playlist_get_current_track(old_playlist);
    new_track = gst_sync_server_playlist_get_current_track(new_playlist);

    g_variant_unref (old_playlist);
    g_variant_unref (new_playlist);

    if ((gst_sync_server_info_get_stopped (old_info) !=
          gst_sync_server_info_get_stopped (self->info))) {
      GST_INFO_OBJECT (self, "Info change: %sstopped",
          gst_sync_server_info_get_stopped (self->info) ? "" : "un");

      gst_element_set_state (GST_ELEMENT (self->pipeline), GST_STATE_NULL);
      update_pipeline (self, FALSE);

    } else if (old_track != new_track) {
      GST_INFO_OBJECT (self, "Info change: track# %lu -> %lu", old_track,
          new_track);

      gst_element_set_state (GST_ELEMENT (self->pipeline), GST_STATE_NULL);
      update_pipeline (self, FALSE);

    } else if (gst_sync_server_info_get_paused (old_info) !=
        gst_sync_server_info_get_paused (self->info)) {
      GST_INFO_OBJECT (self, "Info change: %spaused",
          gst_sync_server_info_get_paused (self->info) ? "" : "un");

      if (!gst_sync_server_info_get_paused (self->info))
        set_base_time (self);

      gst_element_set_state (GST_ELEMENT (self->pipeline),
          gst_sync_server_info_get_paused (self->info) ?
            GST_STATE_PAUSED :
            GST_STATE_PLAYING);

    } else if (gst_sync_server_info_get_base_time (old_info) !=
        gst_sync_server_info_get_base_time (self->info)) {
      GST_INFO_OBJECT (self, "Info change: base time %lu -> %lu",
          gst_sync_server_info_get_base_time (old_info),
          gst_sync_server_info_get_base_time (self->info));

      gst_element_set_state (GST_ELEMENT (self->pipeline), GST_STATE_NULL);
      update_pipeline (self, FALSE);
    }

    g_object_unref (old_info);
  }

  g_mutex_unlock (&self->info_lock);
}

static void
gst_sync_client_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSyncClient *self = GST_SYNC_CLIENT (object);

  switch (property_id) {
    case PROP_ID:
      g_free (self->id);
      self->id = g_value_dup_string (value);
      break;

    case PROP_CONFIG:
      if (self->config)
        g_variant_unref (self->config);

      self->config = g_value_dup_variant (value);
      break;

    case PROP_CONTROL_CLIENT:
      if (self->client)
        g_object_unref (self->client);

      self->client = g_value_dup_object (value);

      g_object_bind_property (self, "id", self->client, "id",
          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);
      g_object_bind_property (self, "config", self->client, "config",
          G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE);

      break;

    case PROP_CONTROL_ADDRESS:
      if (self->control_addr)
        g_free (self->control_addr);

      self->control_addr = g_value_dup_string (value);
      break;

    case PROP_CONTROL_PORT:
      self->control_port = g_value_get_int (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gst_sync_client_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstSyncClient *self = GST_SYNC_CLIENT (object);

  switch (property_id) {
    case PROP_ID:
      g_value_set_string (value, self->id);
      break;

    case PROP_CONFIG:
      g_value_set_variant (value, self->config);
      break;

    case PROP_CONTROL_CLIENT:
      g_value_set_object (value, self->client);
      break;

    case PROP_CONTROL_ADDRESS:
      g_value_set_string (value, self->control_addr);
      break;

    case PROP_CONTROL_PORT:
      g_value_set_int (value, self->control_port);
      break;

    case PROP_PIPELINE:
      g_value_set_object (value, self->pipeline);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
gst_sync_client_class_init (GstSyncClientClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = GST_DEBUG_FUNCPTR (gst_sync_client_dispose);
  object_class->set_property =
    GST_DEBUG_FUNCPTR (gst_sync_client_set_property);
  object_class->get_property =
    GST_DEBUG_FUNCPTR (gst_sync_client_get_property);

  /**
   * GstSyncControlClient:id:
   *
   * Unique client identifier used by the server for client-specific
   * configuration. Automatically generated if set to NULL. Only has an effect
   * if set before the client is started.
   */
  g_object_class_install_property (object_class, PROP_ID,
      g_param_spec_string ("id", "ID", "Unique client identifier", NULL,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  /**
   * GstSyncControlClient:config:
   *
   * Client configuration, which can include any data about the client that the
   * server can use. This can include such things as display configuration,
   * position, orientation where transformations need to be applied. The
   * information is provided as a dictionary stored in a #GVariant. Only has an
   * effect if set before the client is started.
   */
  g_object_class_install_property (object_class, PROP_CONFIG,
      g_param_spec_variant ("config", "Config", "Client configuration",
        G_VARIANT_TYPE ("a{sv}"), NULL,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  /**
   * GstSyncClient:control-client:
   *
   * The implementation of the control protocol that should be used to
   * communicate with the server. This object must implement the
   * #GstSyncControlClient interface. If set to NULL, a built-in TCP
   * implementation is used.
   */
  g_object_class_install_property (object_class, PROP_CONTROL_CLIENT,
      g_param_spec_object ("control-client", "Control client",
        "Control client object (NULL => use TCP control client)",
        G_TYPE_OBJECT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstSyncClient:control-address:
   *
   * The network address for the client to connect to.
   */
  g_object_class_install_property (object_class, PROP_CONTROL_ADDRESS,
      g_param_spec_string ("control-address", "Control address",
        "Address for control server", NULL,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  /**
   * GstSyncClient:control-port:
   *
   * The network port for the client to connect to.
   */
  g_object_class_install_property (object_class, PROP_CONTROL_PORT,
      g_param_spec_int ("control-port", "Control port",
        "Port for control server", 0, 65535, DEFAULT_PORT,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  /**
   * GstSyncClient:pipeline:
   *
   * A #GstPipeline object that is used for playing the synchronised stream.
   * The object will provide the same interface as #playbin, so that clients
   * can be configured appropriately for the platform (such as selecting the
   * video sink and setting it up, if required).
   */
  g_object_class_install_property (object_class, PROP_PIPELINE,
      g_param_spec_object ("pipeline", "Pipeline",
        "The pipeline for playback (having the URI property)",
        GST_TYPE_PIPELINE,
        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  GST_DEBUG_CATEGORY_INIT (sync_client_debug, "syncclient", 0, "GstSyncClient");
}

static void
sync_info_notify (GObject * object, GParamSpec * pspec, gpointer user_data)
{
  GstSyncClient *self = GST_SYNC_CLIENT (user_data);
  GstSyncServerInfo *info;
  gchar *clock_addr, *playlist_str;
  GVariant *playlist;
  int i;

  info = gst_sync_control_client_get_sync_info (self->client);

  clock_addr = gst_sync_server_info_get_clock_address (info);
  playlist = gst_sync_server_info_get_playlist (info);
  playlist_str = g_variant_print (playlist, FALSE);

  GST_DEBUG_OBJECT (self, "Got sync information:");
  GST_DEBUG_OBJECT (self, "\tClk: %s:%u", clock_addr,
      gst_sync_server_info_get_clock_port (info));
  GST_DEBUG_OBJECT (self, "\tPlaylist: %s", playlist_str);
  GST_DEBUG_OBJECT (self, "\tBase time: %lu",
      gst_sync_server_info_get_base_time (info));
  GST_DEBUG_OBJECT (self, "\tLatency: %lu",
      gst_sync_server_info_get_latency (info));
  GST_DEBUG_OBJECT (self, "\tStopped: %u",
      gst_sync_server_info_get_stopped (info));
  GST_DEBUG_OBJECT (self, "\tPaused: %u",
      gst_sync_server_info_get_paused (info));
  GST_DEBUG_OBJECT (self, "\tBase time offset: %lu",
      gst_sync_server_info_get_base_time_offset (info));

  g_free (playlist_str);
  g_variant_unref (playlist);

  update_sync_info (self, info /* transfers ownership of info */);
}

static void
gst_sync_client_init (GstSyncClient * self)
{
  self->control_addr = NULL;
  self->control_port = DEFAULT_PORT;

  self->info = NULL;
  g_mutex_init (&self->info_lock);

  self->pipeline = GST_PIPELINE (gst_element_factory_make ("playbin", NULL));
  if (!self->pipeline)
    GST_ERROR_OBJECT (self, "Could not instantiate playbin");

  self->synchronised = FALSE;

  self->seek_offset = 0;
  g_atomic_int_set (&self->seek_state, NEED_SEEK);
}

/**
 * gst_sync_client_new:
 * @control_addr: The network address that the client should connect to
 * @control_port: The network port that the client should connect to
 *
 * Creates a new #GstSyncClient object that will connect to a #GstSyncServer on
 * the given network address/port pair once started.
 *
 * Returns: (transfer full): A new #GstSyncServer object.
 */
GstSyncClient *
gst_sync_client_new (const gchar * control_addr, gint control_port)
{
  return
    g_object_new (GST_TYPE_SYNC_CLIENT,
        "control-address", control_addr,
        "control-port", control_port,
        NULL);
}

static gchar *
generate_client_id ()
{
  return g_strdup_printf ("gst-sync-client-%x", g_random_int ());
}

/**
 * gst_sync_client_start:
 * @client: The #GstSyncClient object
 * @error: If non-NULL, will be set to the appropriate #GError if starting the
 *         server fails.
 *
 * Starts the #GstSyncClient, connects to the configured server, and starts
 * playback of the currently configured stream.
 *
 * Returns: #TRUE on success, and #FALSE if the server could not be started.
 */
gboolean
gst_sync_client_start (GstSyncClient * client, GError ** err)
{
  gboolean ret;
  gchar *id;

  if (!client->client) {
    GstSyncControlTcpClient *tcp_client;

    tcp_client = g_object_new (GST_TYPE_SYNC_CONTROL_TCP_CLIENT, NULL);
    g_object_set (client, "control-client", tcp_client, NULL);

    g_object_unref (tcp_client);
  }

  g_return_val_if_fail (GST_IS_SYNC_CONTROL_CLIENT (client->client), FALSE);

  g_object_get (client, "id", &id, NULL);
  if (!id) {
    id = generate_client_id ();
    g_object_set (client, "id", id, NULL);
  }
  g_free (id);

  if (client->control_addr) {
    gst_sync_control_client_set_address (client->client, client->control_addr);
    gst_sync_control_client_set_port (client->client, client->control_port);
  }

  /* FIXME: can this be moved into a convenience method like the rest of the
   * interface? */
  g_signal_connect (client->client, "notify::sync-info",
      G_CALLBACK (sync_info_notify), client);

  ret = gst_sync_control_client_start (client->client, err);

  return ret;
}

/**
 * gst_sync_client_stop:
 * @client: The #GstSyncClient object
 *
 * Disconnects from the server and stops playback.
 */
void
gst_sync_client_stop (GstSyncClient * client)
{
  gst_sync_control_client_stop (client->client);
}
