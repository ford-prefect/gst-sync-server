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
#include "sync-client.h"

#define DEFAULT_ADDR "127.0.0.1"
#define DEFAULT_PORT 3695

static gchar *addr = NULL;
static gint port = DEFAULT_PORT;

int main (int argc, char **argv)
{
  GstSyncClient *client;
  GMainLoop *loop;
  GstElement *playbin;
  GError *err;
  GOptionContext *ctx;
  static GOptionEntry entries[] =
  {
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

  playbin = gst_element_factory_make ("playbin", NULL);
  client =
    gst_sync_client_new (addr, port, GST_PIPELINE (playbin));

  loop = g_main_loop_new (NULL, FALSE);

  g_main_loop_run (loop);

  g_object_unref (loop);
  g_object_unref (client);

  g_free (addr);
}
