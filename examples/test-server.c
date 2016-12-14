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
#include <gio/gio.h>

#include <gst/gst.h>

#include <gst/sync-server/sync-server.h>

#define DEFAULT_ADDR "0.0.0.0"
#define DEFAULT_PORT 3695

#define MAX_TRACKS 1000

static gchar *playlist_path = NULL;
static gchar *uris[MAX_TRACKS];
static guint64 durations[MAX_TRACKS];
static guint64 n_tracks;
static gchar *addr = NULL;
static gint port = DEFAULT_PORT;
static guint64 latency = 0;
static GMainLoop *loop;

static gboolean
read_playlist_file (const gchar * path)
{
  GFile *file = NULL;
  char *contents = NULL, uri[1024];
  gsize length;
  gboolean ret = FALSE;
  int i, read, len;

  file = g_file_new_for_path (path);

  if (!g_file_load_contents (file, NULL, &contents, &length, NULL, NULL)) {
    g_message ("Could not read playlist file");
    goto done;
  }

  for (i = 0, read = 0; read <length; i++) {
    sscanf (&contents[read], "%s %lu\n%n", uri, &durations[i], &len);
    uris[i] = g_strdup (uri);

    read += len;
  }

  n_tracks = i;

  ret = TRUE;

done:
  if (file)
    g_object_unref (file);
  if (contents)
    g_free (contents);

  return ret;
}

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

  g_strstrip (str);
  tok = g_strsplit (str, " ", 2);

  if (g_str_equal (tok[0], "pause")) {
    gst_sync_server_set_paused (server, TRUE);

  } else if (g_str_equal (tok[0], "unpause")) {
    gst_sync_server_set_paused (server, FALSE);

  } else if (g_str_equal (tok[0], "stop")) {
    gst_sync_server_set_stopped (server, TRUE);

  } else if (g_str_equal (tok[0], "unstop")) {
    gst_sync_server_set_stopped (server, FALSE);

  } else if (g_str_equal (tok[0], "playlist")) {
    if (tok[1] == NULL || tok[2] != NULL) {
      g_message ("Invalid input: Use 'playlist <path>'");
      goto done;
    }

    g_free (playlist_path);
    playlist_path = g_strdup (tok[1]);

    if (!read_playlist_file (playlist_path))
      goto done;

    g_object_set (server, "playlist",
        gst_sync_server_playlist_new (uris, durations, n_tracks, 0), NULL);
  }

done:
  g_free (str);
  g_strfreev (tok);

  return TRUE;
}

static void
eos_cb (GstSyncServer * server, gpointer user_data)
{
  g_message ("Got EOS");
}

static void
eop_cb (GstSyncServer * server, gpointer user_data)
{
  /* Restart current playlist in a loop */
  g_message ("Got EOP, looping");
  g_object_set (server, "playlist",
      gst_sync_server_playlist_new (uris, durations, n_tracks, 0), NULL);
}

int main (int argc, char **argv)
{
  GstSyncServer *server;
  GError *err;
  GOptionContext *ctx;
  GIOChannel *input;
  static GOptionEntry entries[] =
  {
    { "playlist", 'f', 0, G_OPTION_ARG_STRING, &playlist_path,
      "Path to playlist file", "PLAYLIST" },
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

  if (!playlist_path) {
    g_print ("You must specify a URI or playlist path\n");
    return -1;
  }

  if (!addr)
    addr = g_strdup (DEFAULT_ADDR);

  server = gst_sync_server_new (addr, port);

  if (!read_playlist_file (playlist_path))
    return -1;

  g_object_set (server, "playlist",
      gst_sync_server_playlist_new (uris, durations, n_tracks, 0), NULL);

  if (latency)
    g_object_set (server, "latency", latency, NULL);

  loop = g_main_loop_new (NULL, FALSE);

  gst_sync_server_start (server, NULL);
  g_signal_connect (server, "end-of-stream", G_CALLBACK (eos_cb), NULL);
  g_signal_connect (server, "end-of-playlist", G_CALLBACK (eop_cb), NULL);

  input = g_io_channel_unix_new (0);
  g_io_channel_set_encoding (input, NULL, NULL);
  g_io_channel_set_buffered (input, TRUE);

  g_io_add_watch (input, G_IO_IN | G_IO_ERR, con_read_cb, server);

  g_main_loop_run (loop);

  g_object_unref (loop);
  gst_sync_server_stop (server);
  g_object_unref (server);

  g_io_channel_unref (input);

  g_free (playlist_path);
  g_free (addr);
}
