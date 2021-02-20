/*
 * Copyright 2020 Xilinx Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <math.h>
#include <glib.h>
#include <gst/gst.h>
#include <string.h>
#include <stdio.h>

GST_DEBUG_CATEGORY (defectdetection_app);
#define GST_CAT_DEFAULT defectdetection_app
#define MAX_WIDTH                    1280
#define MAX_HEIGHT                   800
#define MAX_SUPPORTED_FRAME_RATE     60
#define MAX_FRAME_RATE_DENOM         1
#define FPS_UPDATE_INTERVAL          1000  // 1sec
#define CAPTURE_FORMAT_Y8            "GRAY8"

typedef enum {
    DD_SUCCESS,
    DD_ERROR_FILE_IO = -1,
    DD_ERROR_PIPELINE_CREATE_FAIL = -2,
    DD_ERROR_PIPELINE_LINKING_FAIL = -3,
    DD_ERROR_STATE_CHANGE_FAIL = -4,
    DD_ERROR_RESOLUTION_NOT_SUPPORTED = -5,
    DD_ERROR_INPUT_OPTIONS_INVALID = -6,
    DD_ERROR_OTHER = -99,
} DD_ERROR_LOG;

typedef enum {
    V4L2_IO_MODE_AUTO,
    V4L2_IO_MODE_RW,
    V4L2_IO_MODE_MMAP,
    V4L2_IO_MODE_USERPTR,
    V4L2_IO_MODE_DMABUF_EXPORT,
    V4L2_IO_MODE_DMABUF_IMPORT,
} V4L2_IO_MODE;

typedef struct _AppData {
    GstElement *pipeline, *capsfilter;
    GstElement *src, *prepros, *canny, *blob, *contour, *text, *sink;
    guint width, height, fr;
    gchar *in_file, *out_file;
} AppData;

GMainLoop *loop;

void set_pipeline_config (AppData *data, gboolean fileplayback);

void cb_message (GstBus *bus, GstMessage *msg, AppData *data);

const gchar * error_to_string (DD_ERROR_LOG error_code);

DD_ERROR_LOG create_pipeline (AppData *data, gboolean fileplayback);

DD_ERROR_LOG link_pipeline (AppData *data, gboolean fileplayback);

void
signal_handler (gint sig) {
     signal(sig, SIG_IGN);
     g_print ("Hit Ctrl-C, Quitting the app now\n");
     if (loop && g_main_is_running (loop)) {
         g_print ("Quitting the loop \n");
         g_main_loop_quit (loop);
     }
     return;
}

void
cb_message (GstBus *bus, GstMessage *msg, AppData *data) {
  //printf("Msgs %s\n",gst_message_type_get_name (GST_MESSAGE_TYPE (msg)));
  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR: {
      GError *err;
      gchar *debug;

      gst_message_parse_error (msg, &err, &debug);
      g_print ("Error: %s\n", err->message);
      g_error_free (err);
      g_free (debug);
      g_main_loop_quit (loop);
      break;
    }
    case GST_MESSAGE_EOS:
      /* end-of-stream */
      g_print ("End Of Stream\n");
      g_main_loop_quit (loop);
      break;
    default:
      /* Unhandled message */
      break;
    }
}

const gchar *
error_to_string (DD_ERROR_LOG error_code) {
    switch (error_code) {
        case DD_SUCCESS :
            return "Success";
        case DD_ERROR_FILE_IO :
            return "File I/O Error";
        case DD_ERROR_PIPELINE_CREATE_FAIL :
            return "pipeline creation failed";
        case DD_ERROR_PIPELINE_LINKING_FAIL :
            return "pipeline linking failed";
        case DD_ERROR_STATE_CHANGE_FAIL :
            return "state change failed";
        case DD_ERROR_RESOLUTION_NOT_SUPPORTED :
            return "Resolution WxH should be 3840x2160";
        case DD_ERROR_INPUT_OPTIONS_INVALID :
            return "Input options are incorrect";
        default :
            return "Unknown Error";
    }
    return "Unknown Error";
}

void
set_pipeline_config (AppData *data, gboolean fileplayback) {
    gint block_size;
    GstCaps *srcCaps;

    if (fileplayback) {
        block_size = data->width * data->height;
        g_object_set(G_OBJECT(data->src), "location",  data->in_file, NULL);
        g_object_set(G_OBJECT(data->src), "blocksize", block_size, NULL);
        g_object_set(G_OBJECT(data->sink),"location",  data->out_file, NULL);
    }
    srcCaps  = gst_caps_new_simple ("video/x-raw",
                                    "width",     G_TYPE_INT,        data->width,
                                    "height",    G_TYPE_INT,        data->height,
                                    "format",    G_TYPE_STRING,     CAPTURE_FORMAT_Y8,
                                    "framerate", GST_TYPE_FRACTION, data->fr, MAX_FRAME_RATE_DENOM,
                                    NULL);
    GST_DEBUG ("new Caps for src capsfilter %" GST_PTR_FORMAT, srcCaps);
    g_object_set (G_OBJECT (data->capsfilter),  "caps",  srcCaps, NULL);
    gst_caps_unref (srcCaps);
    g_object_set (G_OBJECT(data->prepros), "kernels-config", "pre_pros.json", NULL);
    g_object_set (G_OBJECT(data->text),    "kernels-config", "text2overlay.json", NULL);
    if (!fileplayback) {
        g_object_set (G_OBJECT(data->src),     "io-mode", V4L2_IO_MODE_DMABUF_EXPORT, NULL);
        g_object_set (G_OBJECT(data->canny),   "kernels-config", "canny.json", NULL);
        g_object_set (G_OBJECT(data->blob),    "kernels-config", "blob.json", NULL);
        g_object_set (G_OBJECT(data->contour), "kernels-config", "contour.json", NULL);
    }
}

DD_ERROR_LOG
link_pipeline (AppData *data, gboolean fileplayback) {
    if (fileplayback) {
        if (!gst_element_link_many(data->src, data->capsfilter, data->prepros, data->text, data->sink, NULL)) {
            GST_ERROR ("Error linking for src --> capsfilter --> prepros --> text2overlay --> sink");
            return DD_ERROR_PIPELINE_LINKING_FAIL;
        } else {
            GST_DEBUG ("Linked for src --> capsfilter --> prepros --> text2overlay --> sink successfully");
        }
    } else {
        if (!gst_element_link_many(data->src, data->capsfilter, data->prepros, data->canny, data->blob, \
                              data->contour, data->text, data->sink, NULL)) {
            GST_ERROR ("Error linking for src --> capsfilter --> prepros --> canny --> blob --> contour --> text2overlay --> sink");
            return DD_ERROR_PIPELINE_LINKING_FAIL;
        } else {
            GST_DEBUG ("Linked for src --> capsfilter --> prepros --> canny --> blob --> contour --> text2overlay --> sink successfully");
        }
    }
}

DD_ERROR_LOG
create_pipeline (AppData *data, gboolean fileplayback) {
    data->pipeline =   gst_pipeline_new("defectdetection");
    if (fileplayback) {
        data->src =    gst_element_factory_make("filesrc", "source");
        g_print ("It's a file playback\n");
    } else {
        data->src =   gst_element_factory_make("v4l2src", "source");
        g_print ("It's a live playback\n");
    }
    data->capsfilter =  gst_element_factory_make("capsfilter",   "capsfilter");
    data->prepros    =  gst_element_factory_make("ivas_xfilter", "pre-processing");
    data->canny      =  gst_element_factory_make("ivas_xfilter", "canny-edge");
    data->blob       =  gst_element_factory_make("ivas_xfilter", "blob-detector");
    data->contour    =  gst_element_factory_make("ivas_xfilter", "contour-filling");
    data->text       =  gst_element_factory_make("ivas_xfilter", "text2overlay");
    data->sink       =  gst_element_factory_make("filesink",      "display");

    if(!data->pipeline || !data->src || !data->capsfilter || !data->prepros || \
       !data->canny || !data->blob || !data->contour || !data->text || !data->sink) {
        GST_ERROR ("could not create few elements");
        return DD_ERROR_PIPELINE_CREATE_FAIL;
    } else {
        g_print("All elemnets are created ............\n");
    }
    gst_bin_add_many(GST_BIN(data->pipeline), data->src, data->capsfilter, data->prepros, \
                     data->canny, data->blob, data->contour, data->text, data->sink, NULL);
    return DD_SUCCESS;
}

gint
main (int argc, char **argv) {
    AppData data;
    GstBus *bus;

    gboolean fileplayback = FALSE;
    data.width = MAX_WIDTH;
    data.height = MAX_HEIGHT;
    data.fr = MAX_SUPPORTED_FRAME_RATE;
    gint ret = DD_SUCCESS;
    gst_init(&argc, &argv);
    signal(SIGINT, signal_handler);

    GST_DEBUG_CATEGORY_INIT (defectdetection_app, "defectdetection-app", 0, "defect detection app");

    if (!strcmp (argv[1], "-f")) {
        fileplayback = TRUE;
    }
    if (!strcmp (argv[2], "-w")) {
        data.width = atoi (argv[3]);  
    } else {
        return -1;
    }
    if  (!strcmp (argv[4], "-h")) {
        data.height = atoi (argv[5]);
    } else {
        return -1;
    }
    if  (!strcmp (argv[6], "-fr")) {
        data.fr = atoi (argv[7]);
    } else {
        return -1;
    }
    if (fileplayback) { 
        data.in_file = g_strdup (argv[8]);
        data.out_file = g_strdup (argv[9]);
    }
    if (data.width > MAX_WIDTH || data.height > MAX_HEIGHT) {
        ret = DD_ERROR_RESOLUTION_NOT_SUPPORTED;
        GST_ERROR ("Exiting the app with an error: %s", error_to_string (ret));
        return ret;
    }

    ret = create_pipeline (&data, fileplayback);
    if (ret != DD_SUCCESS) {
        GST_ERROR ("Exiting the app with an error: %s", error_to_string (ret));
        return ret;
    }

    set_pipeline_config (&data, fileplayback);
 
    ret = link_pipeline (&data, fileplayback);
    if (ret != DD_SUCCESS) {
        GST_ERROR ("Exiting the app with an error: %s", error_to_string (ret));
        return ret;
    }

    bus = gst_pipeline_get_bus (GST_PIPELINE (data.pipeline));
    gst_bus_add_watch (bus, (GstBusFunc)(cb_message), &data);

    g_print(" playing ....................\n");
    GST_DEBUG ("Triggering play command");
    if (GST_STATE_CHANGE_FAILURE == gst_element_set_state (data.pipeline, GST_STATE_PLAYING)) {
        GST_ERROR ("state change to Play failed");
        goto CLOSE;
    }
 
    loop = g_main_loop_new (NULL, FALSE);
    g_main_loop_run (loop);
CLOSE:
    gst_element_set_state(data.pipeline, GST_STATE_NULL);
    gst_object_unref(G_OBJECT(data.pipeline));
    g_object_unref(loop);
    g_free (data.in_file);
    g_free (data.out_file);
    return DD_SUCCESS;
}
