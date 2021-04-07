/*
 * Copyright 2021 Xilinx Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 i/
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
#include <stdio.h>
#include <gst/video/videooverlay.h>
#include <string>
#include <string.h>
#include <unistd.h>
#include <set>
#include <sstream>
#include <memory>
#include <stdexcept>

using namespace std;

GST_DEBUG_CATEGORY (defectdetect_app);
#define GST_CAT_DEFAULT defectdetect_app

#define PRE_PROCESS_JSON_FILE        "pre-process.json"
#define CANNY_ACC_JSON_FILE          "canny-accelarator.json"
#define EDGE_TRACER_JSON_FILE        "edge-tracer.json"
#define DEFECT_CALC_JSON_FILE        "defect-calculation.json"
#define DRM_BUS_ID                   "B0010000.v_mix"
#define CAPTURE_FORMAT_Y8            "GRAY8"
#define MAX_WIDTH                    1280
#define MAX_HEIGHT                   800
#define MAX_FRAME_RATE_DENOM         1
#define MAX_DEMO_MODE_FRAME_RATE     4
#define BASE_PLANE_ID                34

typedef enum {
    DD_SUCCESS,
    DD_ERROR_FILE_IO = -1,
    DD_ERROR_PIPELINE_CREATE_FAIL = -2,
    DD_ERROR_PIPELINE_LINKING_FAIL = -3,
    DD_ERROR_STATE_CHANGE_FAIL = -4,
    DD_ERROR_RESOLUTION_NOT_SUPPORTED = -5,
    DD_ERROR_INPUT_OPTIONS_INVALID = -6,
    DD_ERROR_OVERLAY_CREATION_FAIL = -7,
    DD_ERROR_OTHER = -99,
} DD_ERROR_LOG;

typedef struct _AppData {
    GstElement *pipeline, *capsfilter, *src;
    GstElement *sink_raw, *sink_preprocess, *sink_display;
    GstElement *tee_raw, *tee_preprocess;
    GstElement *queue_raw, *queue_raw2, *queue_preprocess, *queue_preprocess2;
    GstElement *perf_raw, *perf_preprocess, *perf_display;
    GstElement *videorate_raw, *videorate_preprocess, *videorate_display;
    GstElement *preprocess, *canny, *edge_tracer, *defect_calculator;
    GstElement *capsfilter_raw, *capsfilter_preprocess, *capsfilter_display;
    GstPad *pad_raw, *pad_raw2, *pad_preprocess, *pad_preprocess2;
    GstVideoOverlay  *overlay_raw, *overlay_preprocess, *overlay_display;
} AppData;

GMainLoop *loop;
gboolean fileplayback = FALSE;
gboolean demo_mode = FALSE;
static gchar* in_file = NULL;
static gchar* config_path = "/opt/xilinx/share/ivas/defect-detect/";
static gchar* media_node = "/dev/media0";
static gchar* out_file = NULL;
static gchar* preprocess_file = NULL;
static gchar* raw_file = NULL;
guint width = 1280;
guint height = 800;
guint framerate = 60;

static GOptionEntry entries[] =
{
    { "infile",       'i', 0, G_OPTION_ARG_FILENAME, &in_file, "location of GRAY8 file as input", "file path"},
    { "rawout",       'x', 0, G_OPTION_ARG_FILENAME, &raw_file, "location of GRAY8 file as raw MIPI output", "file path"},
    { "preprocessout",'y', 0, G_OPTION_ARG_FILENAME, &preprocess_file, "location of GRAY8 file as pre-processed output", "file path"},
    { "finalout",     'z', 0, G_OPTION_ARG_FILENAME, &out_file, "location of GRAY8 file as final stage output", "file path"},
    { "width",        'w', 0, G_OPTION_ARG_INT, &width, "resolution width of the input", "1280"},
    { "height",       'h', 0, G_OPTION_ARG_INT, &height, "resolution height of the input", "800"},
    { "framerate",    'r', 0, G_OPTION_ARG_INT, &framerate, "framerate of the input source", "60"},
    { "inputtype",    'f', 0, G_OPTION_ARG_INT, &fileplayback, "For live playback value must be 0 otherwise 1, default is 0", NULL},
    { "demomode",     'd', 0, G_OPTION_ARG_INT, &demo_mode, "For demo mode value must be 1 otherwise 0, default is 0", NULL},
    { "mediatype",    'm', 0, G_OPTION_ARG_STRING, &media_node, "Media node should be provided in live use case, default is /dev/media0", NULL},
    { "cfgpath",      'c', 0, G_OPTION_ARG_STRING, &config_path, "JSON file path", "config path"},
    { NULL }
};

/* Handler for the pad-added signal */
static void pad_added_cb (GstElement *src, GstPad *pad, AppData *data);

DD_ERROR_LOG set_pipeline_config (AppData *data, gboolean fileplayback);

const gchar * error_to_string (gint error_code);

DD_ERROR_LOG create_pipeline (AppData *data, gboolean fileplayback);

DD_ERROR_LOG link_pipeline (AppData *data, gboolean fileplayback);

void
signal_handler (gint sig) {
     signal(sig, SIG_IGN);
     GST_DEBUG ("Hit Ctrl-C, Quitting the app now");
     if (loop && g_main_is_running (loop)) {
        GST_DEBUG ("Quitting the loop");
        g_main_loop_quit (loop);
     }
     return;
}

static std::string exec(const char* cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

/** @brief
 *  This function is the callback function required to hadnle the
 *  incoming bus messages.
 *
 *  This function will be receiving bus message from different elements
 *  used in the pipeline.
 *
 *  @param bus is an object responsible for delivering GstMessage packets
 *  in a first-in first-out way from the streaming threads
 *  @param msg is the structure which holds the required information
 *  @param data is the application structure.
 *  @return gboolean.
 */
static gboolean
cb_message (GstBus *bus, GstMessage *msg, AppData *data) {
    GError *err;
    gchar *debug;
    switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_INFO:
        gst_message_parse_info (msg, &err, &debug);
        if (debug)
            GST_INFO ("INFO: %s\n", debug);
    break;
    case GST_MESSAGE_ERROR:
        gst_message_parse_error (msg, &err, &debug);
        g_printerr ("Error: %s\n", err->message);
        g_error_free (err);
        g_free (debug);
        if (loop && g_main_is_running (loop)) {
            GST_DEBUG ("Quitting the loop \n");
            g_main_loop_quit (loop);
        }
    break;
    case GST_MESSAGE_EOS:
        /* end-of-stream */
        GST_DEBUG ("End Of Stream");
        if (loop && g_main_is_running (loop)) {
            GST_DEBUG ("Quitting the loop");
            g_main_loop_quit (loop);
        }
    break;
    default:
      /* Unhandled message */
      break;
    }
    return TRUE;
}

/** @brief
 *  This function will be called to convert the error number to
 *  meaningful string.
 *
 *  This function will be called to convert the error number to
 *  meaningful string so that, user can easily understand the issue
 *  and fix the issue.
 *
 *  @param error_code The integer error which has to be translated
 *  into string.
 *  @return string.
 */
const gchar *
error_to_string (gint error_code) {
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
        case DD_ERROR_OVERLAY_CREATION_FAIL :
            return "overlay creation is failed for the display";
        default :
            return "Unknown Error";
    }
    return "Unknown Error";
}

/** @brief
 *  This function is to set the GstElement properties.
 *
 *  All gstreamer element has some properties and to work
 *  need to set few of the properties. This function does the
 *  same.
 *
 *  @param data is the application structure.
 *  @param fileplayback is the boolean param is to set the
 *  pipeline accordingly.
 *  @return Error code.
 */
DD_ERROR_LOG
set_pipeline_config (AppData *data, gboolean fileplayback) {
    gint block_size;
    GstCaps *caps;
    string config_file(config_path);
    gint ret = DD_SUCCESS;
    guint plane_id = BASE_PLANE_ID;
    if (fileplayback) {
        block_size = width * height;
        g_object_set(G_OBJECT(data->src),            "location",  in_file,         NULL);
        g_object_set(G_OBJECT(data->src),            "blocksize", block_size,      NULL);
        g_object_set(G_OBJECT(data->sink_display),   "location",  out_file,        NULL);
        g_object_set(G_OBJECT(data->sink_raw),       "location",  raw_file,        NULL);
        g_object_set(G_OBJECT(data->sink_preprocess),"location",  preprocess_file, NULL);
    } else {
        g_object_set(G_OBJECT(data->queue_raw),         "max-size-buffers", 1,  NULL);
        g_object_set(G_OBJECT(data->queue_raw2),        "max-size-buffers", 1,  NULL);
        g_object_set(G_OBJECT(data->queue_preprocess),  "max-size-buffers", 1,  NULL);
        g_object_set(G_OBJECT(data->queue_preprocess2), "max-size-buffers", 1,  NULL);

        g_object_set(G_OBJECT(data->src),               "media-device", media_node,  NULL);

        g_object_set(G_OBJECT(data->sink_raw),          "bus-id",       DRM_BUS_ID,  NULL);
        g_object_set(G_OBJECT(data->sink_raw),          "plane-id",     plane_id++,  NULL);
        g_object_set(G_OBJECT(data->sink_raw),          "async",        FALSE,       NULL);

        g_object_set(G_OBJECT(data->sink_preprocess),   "bus-id",       DRM_BUS_ID,  NULL);
        g_object_set(G_OBJECT(data->sink_preprocess),   "plane-id",     plane_id++,  NULL);
        g_object_set(G_OBJECT(data->sink_preprocess),   "async",        FALSE,       NULL);

        g_object_set(G_OBJECT(data->sink_display),      "bus-id",       DRM_BUS_ID,  NULL);
        g_object_set(G_OBJECT(data->sink_display),      "plane-id",     plane_id++,  NULL);
        if (demo_mode) {
            caps  = gst_caps_new_simple ("video/x-raw",
                                         "framerate", GST_TYPE_FRACTION, MAX_DEMO_MODE_FRAME_RATE, MAX_FRAME_RATE_DENOM,
                                         NULL);
            GST_DEBUG ("new Caps for src capsfilter %" GST_PTR_FORMAT, caps);
            g_object_set (G_OBJECT (data->capsfilter_raw),  "caps",  caps, NULL);
            gst_caps_unref (caps);

            caps  = gst_caps_new_simple ("video/x-raw",
                                         "framerate", GST_TYPE_FRACTION, MAX_DEMO_MODE_FRAME_RATE, MAX_FRAME_RATE_DENOM,
                                         NULL);
            GST_DEBUG ("new Caps for src capsfilter %" GST_PTR_FORMAT, caps);
            g_object_set (G_OBJECT (data->capsfilter_preprocess),  "caps",  caps, NULL);
            gst_caps_unref (caps);

            caps  = gst_caps_new_simple ("video/x-raw",
                                         "framerate", GST_TYPE_FRACTION, MAX_DEMO_MODE_FRAME_RATE, MAX_FRAME_RATE_DENOM,
                                         NULL);
            GST_DEBUG ("new Caps for src capsfilter %" GST_PTR_FORMAT, caps);
            g_object_set (G_OBJECT (data->capsfilter_display),  "caps",  caps, NULL);
            gst_caps_unref (caps);
        }
        data->overlay_raw = GST_VIDEO_OVERLAY (data->sink_raw);
        if (data->overlay_raw) {
            ret = gst_video_overlay_set_render_rectangle (data->overlay_raw, 0, 680, width, height);
            if (ret) {
                gst_video_overlay_expose (data->overlay_raw);
                ret = DD_SUCCESS;
            }
        } else {
            GST_ERROR ("Failed to create overlay");
            return DD_ERROR_OVERLAY_CREATION_FAIL;
        }

        data->overlay_preprocess = GST_VIDEO_OVERLAY (data->sink_preprocess);
        if (data->overlay_preprocess) {
            ret = gst_video_overlay_set_render_rectangle (data->overlay_preprocess, 1280, 680, width, height);
            if (ret) {
                gst_video_overlay_expose (data->overlay_preprocess);
                ret = DD_SUCCESS;
            }
        } else {
            GST_ERROR ("Failed to create overlay");
            return DD_ERROR_OVERLAY_CREATION_FAIL;
        }

        data->overlay_display = GST_VIDEO_OVERLAY (data->sink_display);
        if (data->overlay_display) {
            ret = gst_video_overlay_set_render_rectangle (data->overlay_display, 2560, 680, width, height);
            if (ret) {
                gst_video_overlay_expose (data->overlay_display);
                ret = DD_SUCCESS;
            }
        } else {
            GST_ERROR ("Failed to create overlay");
            return DD_ERROR_OVERLAY_CREATION_FAIL;
        }
    }
    caps  = gst_caps_new_simple ("video/x-raw",
                                 "width",     G_TYPE_INT,        width,
                                 "height",    G_TYPE_INT,        height,
                                 "format",    G_TYPE_STRING,     CAPTURE_FORMAT_Y8,
                                 "framerate", GST_TYPE_FRACTION, framerate, MAX_FRAME_RATE_DENOM,
                                 NULL);
    GST_DEBUG ("new Caps for src capsfilter %" GST_PTR_FORMAT, caps);
    g_object_set (G_OBJECT (data->capsfilter),  "caps",  caps, NULL);
    gst_caps_unref (caps);

    config_file.append(PRE_PROCESS_JSON_FILE);
    g_object_set (G_OBJECT(data->preprocess), "kernels-config", config_file.c_str(), NULL);
    GST_DEBUG ("Config file path is %s", config_file.c_str());

    config_file.erase (config_file.begin()+ strlen(config_path), config_file.end()-0);
    config_file.append(CANNY_ACC_JSON_FILE);
    g_object_set (G_OBJECT(data->canny),   "kernels-config", config_file.c_str(), NULL);
    GST_DEBUG ("Config file path is %s", config_file.c_str());

    config_file.erase (config_file.begin()+ strlen(config_path), config_file.end()-0);
    config_file.append(EDGE_TRACER_JSON_FILE);
    g_object_set (G_OBJECT(data->edge_tracer),    "kernels-config", config_file.c_str(), NULL);
    GST_DEBUG ("Config file path is %s", config_file.c_str());

    config_file.erase (config_file.begin()+ strlen(config_path), config_file.end()-0);
    config_file.append(DEFECT_CALC_JSON_FILE);
    g_object_set (G_OBJECT(data->defect_calculator), "kernels-config", config_file.c_str(), NULL);
    GST_DEBUG ("Config file path is %s", config_file.c_str());
    return DD_SUCCESS;
}

/** @brief
 *  This function is to link all the elements required to run defect
 *  detect use case.
 *
 *  Link live playback pipeline.
 *
 *  @param data is the application structure pointer.
 *  @param fileplayback is a boolean data to tell whether it's
 *  file playback or live.
 *  @return Error code.
 */
DD_ERROR_LOG
link_pipeline (AppData *data, gboolean fileplayback) {
    if (!fileplayback) {
        gchar *name1, *name2;
        gint ret = DD_SUCCESS;
        data->pad_raw = gst_element_get_request_pad(data->tee_raw, "src_1");
        name1 = gst_pad_get_name(data->pad_raw);
        data->pad_raw2 = gst_element_get_request_pad(data->tee_raw, "src_2");
        name2 = gst_pad_get_name(data->pad_raw2);

        if (!gst_element_link_many(data->capsfilter, data->tee_raw, NULL)) {
            GST_ERROR ("Error linking for capsfilter --> tee");
            return DD_ERROR_PIPELINE_LINKING_FAIL;
        }
        GST_DEBUG ("Linked for capsfilter --> tee successfully");
        if (!gst_element_link_pads(data->tee_raw, name1, data->queue_raw, "sink")) {
            GST_ERROR ("Error linking for tee --> queue_raw");
            return DD_ERROR_PIPELINE_LINKING_FAIL;
        }
        GST_DEBUG ("Linking for tee --> queue_raw successfully");
        if (!gst_element_link_pads(data->tee_raw, name2, data->queue_raw2, "sink")) {
            GST_ERROR ("Error linking for tee --> queue_raw2");
            return DD_ERROR_PIPELINE_LINKING_FAIL;
        }
        GST_DEBUG ("Linking for tee --> queue_raw2 successfully");
        if (name1) g_free (name1);
        if (name2) g_free (name2);
        if (demo_mode) {
            if (!gst_element_link_many(data->queue_raw, data->videorate_raw, data->capsfilter_raw, \
                                       data->perf_raw, data->sink_raw, NULL)) {
                GST_ERROR ("Error linking for queue --> videorate --> capfilter --> perf --> sink");
                return DD_ERROR_PIPELINE_LINKING_FAIL;
            }
            GST_DEBUG ("Linking for queue --> videorate --> capfilter --> perf --> sink successfully");
        } else {
            if (!gst_element_link_many(data->queue_raw, data->perf_raw, data->sink_raw, NULL)) {
                GST_ERROR ("Error linking for queue_raw --> perf_raw --> sink_raw");
                return DD_ERROR_PIPELINE_LINKING_FAIL;
            }
            GST_DEBUG ("Linking for queue_raw --> perf_raw --> sink_raw successfully");
        }
        if (!gst_element_link_many(data->queue_raw2, data->preprocess, data->tee_preprocess, NULL)) {
            GST_ERROR ("Error linking for queue_raw2 --> preprocess --> canny --> edge_tracer --> tee_preprocess");
            return DD_ERROR_PIPELINE_LINKING_FAIL;
        }
        GST_DEBUG ("Linking for queue_raw2 --> preprocess --> canny --> edge_tracer --> tee_preprocess successfully");
        data->pad_preprocess = gst_element_get_request_pad(data->tee_preprocess, "src_1");
        name1 = gst_pad_get_name(data->pad_preprocess);
        data->pad_preprocess2 = gst_element_get_request_pad(data->tee_preprocess, "src_2");
        name2 = gst_pad_get_name(data->pad_preprocess2);

        if (!gst_element_link_pads(data->tee_preprocess, name1, data->queue_preprocess, "sink")) {
            GST_ERROR ("Error linking for tee_preprocess --> queue_preprocess");
            return DD_ERROR_PIPELINE_LINKING_FAIL;
        }
        GST_DEBUG ("Linking for tee_preprocess --> queue_preprocess successfully");
        if (!gst_element_link_pads(data->tee_preprocess, name2, data->queue_preprocess2, "sink")) {
            GST_ERROR ("Error linking for tee_preprocess --> queue_preprocess2");
            return DD_ERROR_PIPELINE_LINKING_FAIL;
        }
        GST_DEBUG ("Linking for tee_preprocess --> queue_preprocess2 successfully");

        if (name1) g_free (name1);
        if (name2) g_free (name2);
        if (demo_mode) {
            if (!gst_element_link_many(data->queue_preprocess, data->videorate_preprocess, data->capsfilter_preprocess, \
                                       data->perf_preprocess, data->sink_preprocess, NULL)) {
                GST_ERROR ("Error linking for queue --> videorate --> capsfilter --> perf --> sink");
                return DD_ERROR_PIPELINE_LINKING_FAIL;
            }
            GST_DEBUG ("Linking for queue --> videorate --> capsfilter --> perf --> sink successfully");
        } else {
            if (!gst_element_link_many(data->queue_preprocess, data->perf_preprocess, data->sink_preprocess, NULL)) {
                GST_ERROR ("Error linking for queue_preprocess --> perf_preprocess --> sink_preprocess");
                return DD_ERROR_PIPELINE_LINKING_FAIL;
            }
            GST_DEBUG ("Linking for queue_preprocess --> perf_preprocess --> sink_preprocess successfully");
        }
        if (demo_mode) {
            if (!gst_element_link_many(data->queue_preprocess2, data->canny, data->edge_tracer, data->defect_calculator, \
                                       data->videorate_display, data->capsfilter_display, data->perf_display, \
                                       data->sink_display, NULL)) {
                GST_ERROR ("Error linking for queue --> canny --> edge_tracer --> defect_calc --> videorate --> capsfilter \
                            --> perf --> sink");
                return DD_ERROR_PIPELINE_LINKING_FAIL;
            }
            GST_DEBUG ("Linking for queue --> canny --> edge_tracer --> defect_calc --> videorate  --> capsfilter --> \
                        perf --> sink  successfully");
        } else {
            if (!gst_element_link_many(data->queue_preprocess2, data->canny, data->edge_tracer, data->defect_calculator, \
                                       data->perf_display, data->sink_display, NULL)) {
                GST_ERROR ("Error linking for queue_preprocess2 --> canny --> edge_tracer --> defect_calculator --> perf --> sink");
                return DD_ERROR_PIPELINE_LINKING_FAIL;
            }
            GST_DEBUG ("Linking for queue_raw2 --> canny --> edge_tracer --> defect_calculator --> perf --> sink successfully");
        }

    } else {
        gchar *name1, *name2;
        gint ret = DD_SUCCESS;
        data->pad_raw = gst_element_get_request_pad(data->tee_raw, "src_1");
        name1 = gst_pad_get_name(data->pad_raw);
        data->pad_raw2 = gst_element_get_request_pad(data->tee_raw, "src_2");
        name2 = gst_pad_get_name(data->pad_raw2);

        if (!gst_element_link_many(data->src, data->capsfilter, data->tee_raw, NULL)) {
            GST_ERROR ("Error linking for src --> capsfilter --> tee");
            return DD_ERROR_PIPELINE_LINKING_FAIL;
        }
        GST_DEBUG ("Linked for src --> capsfilter --> tee successfully");
        if (!gst_element_link_pads(data->tee_raw, name1, data->queue_raw, "sink")) {
            GST_ERROR ("Error linking for tee --> queue_raw");
            return DD_ERROR_PIPELINE_LINKING_FAIL;
        }
        GST_DEBUG ("Linking for tee --> queue_raw successfully");
        if (!gst_element_link_pads(data->tee_raw, name2, data->queue_raw2, "sink")) {
            GST_ERROR ("Error linking for tee --> queue_raw2");
            return DD_ERROR_PIPELINE_LINKING_FAIL;
        }
        GST_DEBUG ("Linking for tee --> queue_raw2 successfully");
        if (name1) g_free (name1);
        if (name2) g_free (name2);
        if (!gst_element_link_many(data->queue_raw, data->sink_raw, NULL)) {
            GST_ERROR ("Error linking for queue_raw --> sink_raw");
            return DD_ERROR_PIPELINE_LINKING_FAIL;
        }
        GST_DEBUG ("Linking for queue_raw --> sink_raw successfully");
        if (!gst_element_link_many(data->queue_raw2, data->preprocess, data->tee_preprocess, NULL)) {
            GST_ERROR ("Error linking for queue_raw2 --> preprocess --> canny --> edge_tracer --> tee_preprocess");
            return DD_ERROR_PIPELINE_LINKING_FAIL;
        }
        GST_DEBUG ("Linking for queue_raw2 --> preprocess --> canny --> edge_tracer --> tee_preprocess successfully");
        data->pad_preprocess = gst_element_get_request_pad(data->tee_preprocess, "src_1");
        name1 = gst_pad_get_name(data->pad_preprocess);
        data->pad_preprocess2 = gst_element_get_request_pad(data->tee_preprocess, "src_2");
        name2 = gst_pad_get_name(data->pad_preprocess2);

        if (!gst_element_link_pads(data->tee_preprocess, name1, data->queue_preprocess, "sink")) {
            GST_ERROR ("Error linking for tee_preprocess --> queue_preprocess");
            return DD_ERROR_PIPELINE_LINKING_FAIL;
        }
        GST_DEBUG ("Linking for tee_preprocess --> queue_preprocess successfully");
        if (!gst_element_link_pads(data->tee_preprocess, name2, data->queue_preprocess2, "sink")) {
            GST_ERROR ("Error linking for tee_preprocess --> queue_preprocess2");
            return DD_ERROR_PIPELINE_LINKING_FAIL;
        }
        GST_DEBUG ("Linking for tee_preprocess --> queue_preprocess2 successfully");

        if (name1) g_free (name1);
        if (name2) g_free (name2);
        if (!gst_element_link_many(data->queue_preprocess, data->sink_preprocess, NULL)) {
            GST_ERROR ("Error linking for queue_preprocess --> sink_preprocess");
            return DD_ERROR_PIPELINE_LINKING_FAIL;
        }
        GST_DEBUG ("Linking for queue_preprocess --> sink_preprocess successfully");
        if (!gst_element_link_many(data->queue_preprocess2, data->canny, data->edge_tracer, data->defect_calculator, \
                                   data->sink_display, NULL)) {
            GST_ERROR ("Error linking for queue_raw2 --> canny --> edge_tracer --> defect_calculator --> sink");
            return DD_ERROR_PIPELINE_LINKING_FAIL;
        }
        GST_DEBUG ("Linking for queue_raw2 --> canny --> edge_tracer --> defect_calculator --> sink successfully");

    }
    return DD_SUCCESS;
}

/** @brief
 *  This function is to create a pipeline required to run defect
 *  detect use case.
 *
 *  All GStreamer elements instance has to be created and add
 *  into the pipeline bin. Also link file based pipeline.
 *
 *  @param data is the application structure pointer.
 *  @param fileplayback is a boolean data to tell whether it's
 *  file playback or live.
 *  @return Error code.
 */
DD_ERROR_LOG
create_pipeline (AppData *data, gboolean fileplayback) {
    data->pipeline =   gst_pipeline_new("defectdetection");
    if (fileplayback) {
        GST_DEBUG ("It's a file playback");
        data->src               =  gst_element_factory_make("filesrc",      NULL);
        data->capsfilter        =  gst_element_factory_make("capsfilter",   NULL);
        data->preprocess        =  gst_element_factory_make("ivas_xfilter", "pre-process");
        data->canny             =  gst_element_factory_make("ivas_xfilter", "canny-edge");
        data->edge_tracer       =  gst_element_factory_make("ivas_xfilter", "edge-tracer");
        data->defect_calculator =  gst_element_factory_make("ivas_xfilter", "defect-calculator");
        data->sink_raw          =  gst_element_factory_make("filesink",     NULL);
        data->sink_preprocess   =  gst_element_factory_make("filesink",     NULL);
        data->sink_display      =  gst_element_factory_make("filesink",     NULL);
        data->tee_raw           =  gst_element_factory_make("tee",          "tee-raw");
        data->tee_preprocess    =  gst_element_factory_make("tee",          "tee-edge");
        data->queue_raw         =  gst_element_factory_make("queue",        "queue-raw");
        data->queue_raw2        =  gst_element_factory_make("queue",        "queue-raw-2");
        data->queue_preprocess  =  gst_element_factory_make("queue",        "queue-edge");
        data->queue_preprocess2 =  gst_element_factory_make("queue",        "queue-edge-2");
        if (!data->pipeline || !data->src || !data->capsfilter || !data->preprocess || !data->canny \
            || !data->edge_tracer || !data->defect_calculator || !data->sink_display || !data->sink_raw \
            || !data->sink_preprocess || !data->tee_raw || !data->tee_preprocess || !data->queue_raw \
            || !data->queue_raw || !data->queue_raw2 || !data->queue_preprocess || !data->queue_preprocess2) {
            GST_ERROR ("could not create few elements");
            return DD_ERROR_PIPELINE_CREATE_FAIL;
        }
        GST_DEBUG ("All elements are created");
        gst_bin_add_many(GST_BIN(data->pipeline), data->src, data->capsfilter, data->preprocess, \
                         data->canny, data->edge_tracer, data->defect_calculator, data->sink_display, \
                         data->sink_raw, data->queue_raw, data->queue_raw2, data->queue_preprocess, data->queue_preprocess2, \
                         data->sink_preprocess, data->tee_raw, data->tee_preprocess, NULL);
        return DD_SUCCESS;
    } else {
        GST_DEBUG ("It's a live playback");
        data->src                   =  gst_element_factory_make("mediasrcbin",  "source");
        data->capsfilter            =  gst_element_factory_make("capsfilter",   "capsfilter");
        data->preprocess            =  gst_element_factory_make("ivas_xfilter", "pre-processing");
        data->canny                 =  gst_element_factory_make("ivas_xfilter", "canny-edge");
        data->edge_tracer           =  gst_element_factory_make("ivas_xfilter", "edge-tracer");
        data->defect_calculator     =  gst_element_factory_make("ivas_xfilter", "defect-calculator");
        data->sink_raw              =  gst_element_factory_make("kmssink",      "display-raw");
        data->sink_preprocess       =  gst_element_factory_make("kmssink",      "display-edge");
        data->sink_display          =  gst_element_factory_make("kmssink",      "display");
        data->tee_raw               =  gst_element_factory_make("tee",          "tee-raw");
        data->tee_preprocess        =  gst_element_factory_make("tee",          "tee-edge");
        data->queue_raw             =  gst_element_factory_make("queue",        "queue-raw");
        data->queue_raw2            =  gst_element_factory_make("queue",        "queue-raw-2");
        data->queue_preprocess      =  gst_element_factory_make("queue",        "queue-edge");
        data->queue_preprocess2     =  gst_element_factory_make("queue",        "queue-edge-2");
        data->perf_raw              =  gst_element_factory_make("perf",         "perf-raw");
        data->perf_preprocess       =  gst_element_factory_make("perf",         "perf-edge");
        data->perf_display          =  gst_element_factory_make("perf",         "perf-display");
        data->videorate_raw         =  gst_element_factory_make("videorate",    "rate-raw");
        data->videorate_preprocess  =  gst_element_factory_make("videorate",    "rate-edge");
        data->videorate_display     =  gst_element_factory_make("videorate",    "rate");
        data->capsfilter_raw        =  gst_element_factory_make("capsfilter",   "caps-raw");
        data->capsfilter_preprocess =  gst_element_factory_make("capsfilter",   "caps-edge");
        data->capsfilter_display    =  gst_element_factory_make("capsfilter",   "caps");
        if (!data->pipeline || !data->src || !data->capsfilter || !data->preprocess || !data->canny \
            || !data->edge_tracer || !data->defect_calculator || !data->sink_display || !data->sink_raw \
            || !data->sink_preprocess || !data->tee_raw || !data->tee_preprocess || !data->queue_raw \
            || !data->queue_raw || !data->queue_raw2 || !data->queue_preprocess || !data->queue_preprocess2 \
            || !data->perf_raw || !data->perf_preprocess || !data->perf_display \
            || !data->videorate_raw || !data->videorate_preprocess || !data->videorate_display \
            || !data->capsfilter_raw || !data->capsfilter_preprocess || !data->capsfilter_display) {
            GST_ERROR ("could not create few elements");
            return DD_ERROR_PIPELINE_CREATE_FAIL;
        }
        GST_DEBUG ("All elements are created");
        gst_bin_add_many(GST_BIN(data->pipeline), data->src, data->capsfilter, data->preprocess, \
                         data->canny, data->edge_tracer, data->defect_calculator, data->sink_display, \
                         data->sink_raw, data->queue_raw, data->queue_raw2, data->queue_preprocess, data->queue_preprocess2, \
                         data->sink_preprocess, data->tee_raw, data->tee_preprocess, \
                         data->perf_raw, data->perf_preprocess, data->perf_display, \
                         data->videorate_raw, data->videorate_preprocess, data->videorate_display, \
                         data->capsfilter_raw, data->capsfilter_preprocess, data->capsfilter_display, NULL);
    }
    return DD_SUCCESS;
}

gint
main (int argc, char **argv) {
    AppData data;
    GstBus *bus;
    gint ret = DD_SUCCESS;
    guint bus_watch_id;
    GOptionContext *optctx;
    GError *error = NULL;

    memset (&data, 0, sizeof(AppData));

    gst_init(&argc, &argv);
    signal(SIGINT, signal_handler);

    GST_DEBUG_CATEGORY_INIT (defectdetect_app, "defectdetect-app", 0, "defect detection app");
    optctx = g_option_context_new ("- Application to detect the defect of Mango on SoM board of Xilinx.");
    g_option_context_add_main_entries (optctx, entries, NULL);
    g_option_context_add_group (optctx, gst_init_get_option_group ());
    if (!g_option_context_parse (optctx, &argc, &argv, &error)) {
        g_printerr ("Error parsing options: %s\n", error->message);
        g_option_context_free (optctx);
        g_clear_error (&error);
        return -1;
    }
    g_option_context_free (optctx);

    if (fileplayback) {
        if (!in_file || !out_file || !raw_file || !preprocess_file) {
            g_printerr ("In case of file playback, input file and output files MUST be given\n");
            return -1;
        }
        if (demo_mode) {
            g_printerr ("In case of file playback, demo mode should be disabled\n");
            return -1;
        }
    } else {
        if (in_file || out_file || raw_file || preprocess_file) {
            g_printerr ("In case of live playback, input file and output file/s option should NOT be given\n");
            return -1;
        }
    }
    if (in_file) {
        GST_DEBUG ("In file is %s", in_file);
    }
    if (out_file) {
        GST_DEBUG ("In file is %s", out_file);
    }
    GST_DEBUG ("Width is %d", width);
    GST_DEBUG ("height is %d", height);
    GST_DEBUG ("framerate is %d", framerate);
    GST_DEBUG ("file playback mode is %s", fileplayback ? "On" : "Off");
    GST_DEBUG ("demo mode is %s", demo_mode ? "On" : "Off");
    if (config_path)
        GST_DEBUG ("config path is %s", config_path);
    if (media_node)
        GST_DEBUG ("media node is %s", media_node);

    if (width > MAX_WIDTH || height > MAX_HEIGHT) {
        ret = DD_ERROR_RESOLUTION_NOT_SUPPORTED;
        g_printerr ("Exiting the app with an error: %s\n", error_to_string (ret));
        return ret;
    }
    if (!fileplayback) {
        if (access("/dev/dri/by-path/platform-b0010000.v_mix-card", F_OK) != 0) {
            g_printerr("ERROR: Mixer device is not ready.\n");
            return 1;
        } else {
            exec("echo | modetest -D B0010000.v_mix -s 52@40:3840x2160@NV16");
        }
    }
    ret = create_pipeline (&data, fileplayback);
    if (ret != DD_SUCCESS) {
        g_printerr ("Exiting the app with an error: %s\n", error_to_string (ret));
        return ret;
    }

    ret = link_pipeline (&data, fileplayback);
    if (ret != DD_SUCCESS) {
        g_printerr ("Exiting the app with an error: %s\n", error_to_string (ret));
        return ret;
    }

    ret = set_pipeline_config (&data, fileplayback);
    if (ret != DD_SUCCESS) {
        g_printerr ("Exiting the app with an error: %s\n", error_to_string (ret));
        return ret;
    }
    /* we add a message handler */
    bus = gst_pipeline_get_bus (GST_PIPELINE (data.pipeline));
    bus_watch_id = gst_bus_add_watch (bus, (GstBusFunc)(cb_message), &data);
    gst_object_unref (bus);

    if (!fileplayback) {
        g_signal_connect (data.src, "pad-added", G_CALLBACK (pad_added_cb), &data);
    }
    GST_DEBUG ("Triggering play command");
    if (GST_STATE_CHANGE_FAILURE == gst_element_set_state (data.pipeline, GST_STATE_PLAYING)) {
        g_printerr ("state change to Play failed\n");
        goto CLOSE;
    }
    GST_DEBUG ("waiting for the loop");
    loop = g_main_loop_new (NULL, FALSE);
    g_main_loop_run (loop);
CLOSE:
    gst_element_set_state(data.pipeline, GST_STATE_NULL);
    if (data.pipeline) {
        if (data.pad_raw) {
            GST_DEBUG ("releasing pad");
            gst_element_release_request_pad (data.tee_raw, data.pad_raw);
            gst_object_unref (data.pad_raw);
        }
        if (data.pad_raw2) {
            GST_DEBUG ("releasing pad");
            gst_element_release_request_pad (data.tee_raw, data.pad_raw2);
            gst_object_unref (data.pad_raw2);
        }
        if (data.pad_preprocess) {
            GST_DEBUG ("releasing pad");
            gst_element_release_request_pad (data.tee_preprocess, data.pad_preprocess);
            gst_object_unref (data.pad_preprocess);
        }
        if (data.pad_preprocess2) {
            GST_DEBUG ("releasing pad");
            gst_element_release_request_pad (data.tee_preprocess, data.pad_preprocess2);
            gst_object_unref (data.pad_preprocess2);
        }
        gst_object_unref (GST_OBJECT (data.pipeline));
        data.pipeline = NULL;
    }
    GST_DEBUG ("Removing bus");
    g_source_remove (bus_watch_id);

    if (in_file)
        g_free (in_file);
    if (out_file)
        g_free (out_file);
    return ret;
}

/** @brief
 *  This function will be called by the pad-added signal
 *
 *  mediasrcbin has sometimes pad for which pad-added signal
 *  is required to be attached with source element.
 *  This function will be called when pad is created and is
 *  ready to link with peer element.
 *
 *  @param src The GstElement to be connected with peer element.
 *  @param new_pad is the pad of source element which needs to be
 *  linked.
 *  @param data is the application structure pointer.
 *  @return Void.
 */
static void
pad_added_cb (GstElement *src, GstPad *new_pad, AppData *data) {
    GstPadLinkReturn ret;
    GstPad *sink_pad = gst_element_get_static_pad (data->capsfilter, "sink");
    GST_DEBUG ("Received new pad '%s' from '%s':", GST_PAD_NAME (new_pad), GST_ELEMENT_NAME (src));

    /* If our capsfilter is already linked, we have nothing to do here */
    if (gst_pad_is_linked (sink_pad)) {
        GST_DEBUG ("Pad is already linked. Ignoring");
        goto exit;
    }

    /* Attempt the link */
    ret = gst_pad_link (new_pad, sink_pad);
    if (GST_PAD_LINK_FAILED (ret)) {
        GST_ERROR ("Linking failed.");
    }
exit:
    /* Unreference the sink pad */
    gst_object_unref (sink_pad);
}
