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

#include <glib.h>
#include <glib-object.h>
#include <glib/gprintf.h>

#include <gst/gst.h>

#include "sync-server.h"

#define DEFAULT_ADDR "0.0.0.0"
#define DEFAULT_PORT 3695

static gchar *uri = NULL;
static gchar *addr = NULL;
static gint port = DEFAULT_PORT;
static guint64 latency = 0;
static GMainLoop *loop;

static gboolean
con_read_cb (GIOChannel * input, GIOCondition cond, gpointer user_data)
{
  GstSyncServer *server = GST_SYNC_SERVER (user_data);
  gchar *str = NULL;
  gchar **tok = NULL;

  if (cond & G_IO_ERR) {
    g_message ("Error while reading from console");
    g_main_loop_quit (loop);
    return FALSE;
  }

  if (g_io_channel_read_line (input, &str, NULL, NULL, NULL) !=
      G_IO_STATUS_NORMAL) {
    g_message ("Error reading from console");
    goto done;
  }

  tok = g_strsplit (str, " ", 2);

  if (g_str_equal (tok[0], "uri")) {
    if (tok[1] == NULL || tok[2] != NULL) {
      g_message ("Invalid input: Use 'uri scheme:///path'");
      goto done;
    }

    g_free (uri);
    uri = g_strdup (g_strstrip (tok[1]));

    g_object_set (server, "uri", uri, NULL);
  }

done:
  g_free (str);
  g_strfreev (tok);

  return TRUE;
}

static void
eos_cb (GstSyncServer * server, gpointer user_data)
{
  /* Restart current media in a loop */
  g_object_set (server, "uri", uri, NULL);
}

int main (int argc, char **argv)
{
  GstSyncServer *server;
  GError *err;
  GOptionContext *ctx;
  GIOChannel *input;
  static GOptionEntry entries[] =
  {
    { "uri", 'u', 0, G_OPTION_ARG_STRING, &uri, "URI to send to clients",
      "URI" },
    { "address", 'a', 0, G_OPTION_ARG_STRING, &addr, "Address to listen on",
      "ADDR" },
    { "port", 'p', 0, G_OPTION_ARG_INT, &port, "Port to listen on",
      "PORT" },
    { "latency", 'l', 0, G_OPTION_ARG_INT64, &latency, "Pipeline latency",
      "LATENCY" },
    { NULL }
  };

  ctx = g_option_context_new ("gst-sync-server example server");
  g_option_context_add_main_entries (ctx, entries, NULL);
  g_option_context_add_group (ctx, gst_init_get_option_group ());

  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_print ("Failed to parse command line arguments: %s\n", err->message);
    return -1;
  }

  g_option_context_free (ctx);

  if (!uri) {
    g_print ("You must specify a URI\n");
    return -1;
  }

  if (!addr)
    addr = g_strdup (DEFAULT_ADDR);

  server = gst_sync_server_new (addr, port);

  g_object_set (server, "uri", uri, NULL);
  if (latency)
    g_object_set (server, "latency", latency, NULL);

  loop = g_main_loop_new (NULL, FALSE);

  gst_sync_server_start (server, NULL);
  g_signal_connect (server, "eos", G_CALLBACK (eos_cb), NULL);

  input = g_io_channel_unix_new (0);
  g_io_channel_set_encoding (input, NULL, NULL);
  g_io_channel_set_buffered (input, TRUE);

  g_io_add_watch (input, G_IO_IN | G_IO_ERR, con_read_cb, server);

  g_main_loop_run (loop);

  g_object_unref (loop);
  gst_sync_server_stop (server);
  g_object_unref (server);

  g_io_channel_unref (input);

  g_free (uri);
  g_free (addr);
}
