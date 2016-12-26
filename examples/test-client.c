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

#include <glib-object.h>
#include <gst/gst.h>
#include <gst/sync-server/sync-client.h>

#define DEFAULT_ADDR "127.0.0.1"
#define DEFAULT_PORT 3695

static gchar *id = NULL;
static gchar *addr = NULL;
static gint port = DEFAULT_PORT;

int main (int argc, char **argv)
{
  GstSyncClient *client;
  GMainLoop *loop;
  GError *err = NULL;
  GOptionContext *ctx;
  static GOptionEntry entries[] =
  {
    { "id", 'i', 0, G_OPTION_ARG_STRING, &id, "Client ID to send to server",
      "ID" },
    { "address", 'a', 0, G_OPTION_ARG_STRING, &addr, "Address to connect to",
      "ADDR" },
    { "port", 'p', 0, G_OPTION_ARG_INT, &port, "Port to connect to",
      "PORT" },
    { NULL }
  };

  ctx = g_option_context_new ("gst-sync-server example client");
  g_option_context_add_main_entries (ctx, entries, NULL);
  g_option_context_add_group (ctx, gst_init_get_option_group ());

  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_print ("Failed to parse command line arguments: %s\n", err->message);
    return -1;
  }

  g_option_context_free (ctx);

  if (!addr)
    addr = g_strdup (DEFAULT_ADDR);

  gst_init (&argc, &argv);

  client = gst_sync_client_new (addr, port);

  if (id)
    g_object_set (G_OBJECT (client), "id", id, NULL);

  loop = g_main_loop_new (NULL, FALSE);

  if (!gst_sync_client_start (client, &err)) {
    g_warning ("Could not start client: %s", err->message);
    if (err)
      g_error_free (err);
    goto done;
  }

  g_main_loop_run (loop);

done:
  g_main_loop_unref (loop);
  g_object_unref (client);

  g_free (id);
  g_free (addr);
}
