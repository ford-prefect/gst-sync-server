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
#include "sync-server.h"

#define DEFAULT_ADDR "127.0.0.1"
#define DEFAULT_PORT 3695

int main (int argc, char **argv)
{
  GstSyncServer *server;
  GMainLoop *loop;

  gst_init (&argc, &argv);

  server = gst_sync_server_new (DEFAULT_ADDR, DEFAULT_PORT);

  g_object_set (server, "uri", argv[1], NULL);

  loop = g_main_loop_new (NULL, FALSE);

  gst_sync_server_start (server, NULL);

  g_main_loop_run (loop);

  g_object_unref (loop);
  gst_sync_server_stop (server);
  g_object_unref (server);
}
