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

#include "sync-server.h"
#include "sync-client.h"
#include "sync-tcp-control-client.h"

enum {
  NEED_SEEK,
  IN_SEEK,
  DONE_SEEK,
};

struct _GstSyncClient {
  GObject parent;

  gchar *control_addr;
  gint control_port;

  GstSyncServerInfo *info;

  GstPipeline *pipeline;
  GstClock *clock;

  GstSyncTcpControlClient *client;
  gboolean synchronised;

  int seek_state;
};

struct _GstSyncClientClass {
  GObjectClass parent;
};

#define gst_sync_client_parent_class parent_class
G_DEFINE_TYPE (GstSyncClient, gst_sync_client, G_TYPE_OBJECT);

enum {
  PROP_0,
  PROP_CONTROL_ADDRESS,
  PROP_CONTROL_PORT,
  PROP_PIPELINE,
};

#define DEFAULT_PORT 0
#define DEFAULT_SEEK_TOLERANCE (50 * GST_MSECOND)

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

  g_free (self->control_addr);
  self->control_addr = NULL;

  if (self->info) {
    gst_sync_server_info_free (self->info);
    self->info = NULL;
  }

  if (self->client) {
    g_object_unref (self->client);
    self->client = NULL;
  }
  
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
set_base_time (GstSyncClient * self, GstClockTime base_time)
{
  gst_element_set_start_time (GST_ELEMENT (self->pipeline),
      GST_CLOCK_TIME_NONE);
  gst_element_set_base_time (GST_ELEMENT (self->pipeline), base_time);
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

      /* FIXME: should have a timeout? */
      gst_clock_wait_for_sync (self->clock, GST_CLOCK_TIME_NONE);

      GST_INFO_OBJECT (self, "Clock is synchronised, starting playback");

      g_object_set (GST_OBJECT (self->pipeline), "uri", self->info->uri, NULL);

      gst_element_set_state (GST_ELEMENT (self->pipeline), GST_STATE_PLAYING);

      break;
    }

    case GST_MESSAGE_STATE_CHANGED: {
      GstState old_state, new_state;
      GstClockTime now;

      gst_message_parse_state_changed (message, &old_state, &new_state, NULL);

      if (self->seek_state == NEED_SEEK &&
          (old_state == GST_STATE_PAUSED && new_state == GST_STATE_PLAYING) &&
          GST_MESSAGE_SRC (message) == GST_OBJECT (self->pipeline)) {

        now = gst_clock_get_time (self->clock);
        self->seek_state = IN_SEEK;

        if (now - self->info->base_time > DEFAULT_SEEK_TOLERANCE) {
          /* Let's seek ahead to prevent excessive clipping */
          /* FIXME: test with live pipelines */
          if (!gst_element_seek_simple (GST_ELEMENT (self->pipeline),
                GST_FORMAT_TIME,
                GST_SEEK_FLAG_ACCURATE | GST_SEEK_FLAG_FLUSH,
                now - self->info->base_time + DEFAULT_SEEK_TOLERANCE)) {
            GST_WARNING_OBJECT (self, "Could not perform seek");

            set_base_time (self, self->info->base_time);
          }
        } else {
          /* For the seek case, the base time will be set after the seek */
          set_base_time (self, self->info->base_time);

          self->seek_state = DONE_SEEK;
        }
      }

      break;
    }

    case GST_MESSAGE_ASYNC_DONE: {
        GstClockTime pos, start;

        if (self->seek_state != IN_SEEK)
          break;

        if (gst_element_query_position (GST_ELEMENT (self->pipeline),
              GST_FORMAT_TIME, &pos)) {
          set_base_time (self, self->info->base_time + pos);
        }

        self->seek_state = DONE_SEEK;

        break;
    }

    default:
      break;
  }

  return TRUE;
}

static void
gst_sync_client_constructed (GObject * object)
{
  GstSyncClient *self = GST_SYNC_CLIENT (object);
  GstBus *bus;

  G_OBJECT_CLASS (parent_class)->constructed (object);

  /* FIXME: This should become async */
  self->client = g_object_new (GST_TYPE_SYNC_TCP_CONTROL_CLIENT, "address",
      self->control_addr, "port", self->control_port, NULL);

  g_object_get (self->client, "sync-info", &self->info, NULL);
  
  self->clock = gst_net_client_clock_new ("sync-server-clock",
      self->info->clock_addr, self->info->clock_port, 0);
  gst_pipeline_use_clock (self->pipeline, self->clock);

  bus = gst_pipeline_get_bus (self->pipeline);
  g_object_set (self->clock, "bus", bus, NULL);
  gst_bus_add_watch (bus, bus_cb, self);

  gst_object_unref (bus);
}

static void
gst_sync_client_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSyncClient *self = GST_SYNC_CLIENT (object);

  switch (property_id) {
    case PROP_CONTROL_ADDRESS:
      if (self->control_addr)
        g_free (self->control_addr);

      self->control_addr = g_value_dup_string (value);
      break;

    case PROP_CONTROL_PORT:
      self->control_port = g_value_get_int (value);
      break;

    case PROP_PIPELINE:
      if (self->pipeline)
        gst_object_unref (self->pipeline);

      self->pipeline = GST_PIPELINE (g_value_dup_object (value));
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
  object_class->constructed = GST_DEBUG_FUNCPTR (gst_sync_client_constructed);
  object_class->set_property =
    GST_DEBUG_FUNCPTR (gst_sync_client_set_property);
  object_class->get_property =
    GST_DEBUG_FUNCPTR (gst_sync_client_get_property);

  g_object_class_install_property (object_class, PROP_CONTROL_ADDRESS,
      g_param_spec_string ("control-address", "Control address",
        "Address for control server", NULL,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_CONTROL_PORT,
      g_param_spec_int ("control-port", "Control port",
        "Port for control server", 0, 65535, DEFAULT_PORT,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  /* FIXME: allow a pipeline with a child with the "uri" property too */
  g_object_class_install_property (object_class, PROP_PIPELINE,
      g_param_spec_object ("pipeline", "Pipeline",
        "The pipeline for playback (having the URI property)",
        GST_TYPE_PIPELINE,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
}

static void
gst_sync_client_init (GstSyncClient * self)
{
  self->control_addr = NULL;
  self->control_port = DEFAULT_PORT;
  self->pipeline = NULL;
  self->synchronised = FALSE;
  self->seek_state = NEED_SEEK;
}

/* FIXME: Add a mechanism to specify transport rather than hard-coded TCP */
GstSyncClient *
gst_sync_client_new (const gchar * control_addr, gint control_port,
    GstPipeline * pipeline)
{
  return
    g_object_new (GST_TYPE_SYNC_CLIENT,
        "control-address", control_addr,
        "control-port", control_port,
        "pipeline", pipeline,
        NULL);
}

