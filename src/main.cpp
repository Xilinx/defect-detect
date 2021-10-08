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

#include <gst/gst.h>
#include <gst/video/videooverlay.h>
#include <gst/video/video.h>
#include <string.h>
#include <unistd.h>
#include <memory>
#include <stdexcept>
#include <glob.h>
#include <sstream>

using namespace std;

GST_DEBUG_CATEGORY (defectdetect_app);
#define GST_CAT_DEFAULT defectdetect_app

#define PRE_PROCESS_JSON_FILE        "preprocess-accelarator.json"
#define OTSU_ACC_JSON_FILE           "otsu-accelarator.json"
#define CCA_ACC_JSON_FILE            "cca-accelarator.json"
#define TEXT_2_OVERLAY_JSON_FILE     "text2overlay.json"
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
    GstElement *pipeline, *capsfilter, *src, *rawvideoparse;
    GstElement *sink_raw, *sink_preprocess, *sink_display;
    GstElement *tee_raw, *tee_preprocess;
    GstElement *queue_raw, *queue_raw2, *queue_preprocess, *queue_preprocess2;
    GstElement *perf_raw, *perf_preprocess, *perf_display;
    GstElement *videorate_raw, *videorate_preprocess, *videorate_display;
    GstElement *preprocess, *otsu, *cca, *text2overlay;
    GstElement *capsfilter_raw, *capsfilter_preprocess, *capsfilter_display;
    GstPad *pad_raw, *pad_raw2, *pad_preprocess, *pad_preprocess2;
    GstVideoOverlay  *overlay_raw, *overlay_preprocess, *overlay_display;
} AppData;

GMainLoop *loop;
gboolean file_playback = FALSE;
gboolean file_dump = FALSE;
gboolean demo_mode = FALSE;
static gchar* in_file = NULL;
static gchar* config_path  = (gchar *)"/opt/xilinx/share/ivas/defect-detect/";
static gchar *msg_firmware = (gchar *)"Load the HW accelerator firmware first. Use command: xmutil loadapp kv260-defect-detect\n";
static gchar* final_out = NULL;
static gchar* preprocess_out = NULL;
static gchar* raw_out = NULL;
guint width = 1280;
guint height = 800;
guint framerate = 60;
static std::string dev_node("");

static GOptionEntry entries[] =
{
    { "infile",       'i', 0, G_OPTION_ARG_FILENAME, &in_file, "Location of input file", "file path"},
    { "rawout",       'x', 0, G_OPTION_ARG_FILENAME, &raw_out, "Location of capture raw output file", "file path"},
    { "preprocessout",'y', 0, G_OPTION_ARG_FILENAME, &preprocess_out, "Location of pre-processed output file", "file path"},
    { "finalout",     'z', 0, G_OPTION_ARG_FILENAME, &final_out, "Location of final output file", "file path"},
    { "width",        'w', 0, G_OPTION_ARG_INT, &width, "Resolution width of the input", "1280"},
    { "height",       'h', 0, G_OPTION_ARG_INT, &height, "Resolution height of the input", "800"},
    { "framerate",    'r', 0, G_OPTION_ARG_INT, &framerate, "Framerate of the input source", "60"},
    { "demomode",     'd', 0, G_OPTION_ARG_INT, &demo_mode, "For Demo mode value must be 1", "0"},
    { "cfgpath",      'c', 0, G_OPTION_ARG_STRING, &config_path, "JSON config file path", "/opt/xilinx/share/ivas/defect-detect/"},
    { NULL }
};

/* Handler for the pad-added signal */
static void pad_added_cb (GstElement *src, GstPad *pad, AppData *data);

DD_ERROR_LOG set_pipeline_config (AppData *data);

const gchar * error_to_string (gint error_code);

DD_ERROR_LOG create_pipeline (AppData *data);

DD_ERROR_LOG link_pipeline (AppData *data);

void
signal_handler (gint sig) {
     signal(sig, SIG_IGN);
     GST_DEBUG ("Hit Ctrl-C, Quitting the app now");
     if (loop && g_main_loop_is_running (loop)) {
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
message_cb (GstBus *bus, GstMessage *msg, AppData *data) {
    GError *err;
    gchar *debug;
    switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_INFO:
        gst_message_parse_info (msg, &err, &debug);
        if (debug)
            GST_INFO ("INFO: %s", debug);
    break;
    case GST_MESSAGE_ERROR:
        gst_message_parse_error (msg, &err, &debug);
        g_printerr ("Error: %s\n", err->message);
        g_error_free (err);
        g_free (debug);
        if (loop && g_main_loop_is_running (loop)) {
            GST_DEBUG ("Quitting the loop");
            g_main_loop_quit (loop);
        }
    break;
    case GST_MESSAGE_EOS:
        /* end-of-stream */
        GST_DEBUG ("End Of Stream");
        if (loop && g_main_loop_is_running (loop)) {
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
            return "Resolution WxH should be 1280x800";
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
 *  @return Error code.
 */
DD_ERROR_LOG
set_pipeline_config (AppData *data) {
    gint block_size;
    GstCaps *caps;
    string config_file(config_path);
    gint ret = DD_SUCCESS;
    guint plane_id = BASE_PLANE_ID;
    if (file_playback) {
        block_size = width * height;
        g_object_set(G_OBJECT(data->src),            "location",  in_file,         NULL);
        g_object_set(G_OBJECT(data->src),            "blocksize", block_size,      NULL);
    } else {
        g_object_set(G_OBJECT(data->src),            "media-device", dev_node.c_str(), NULL);
    }
    if (file_dump) {
        g_object_set(G_OBJECT(data->sink_raw),       "location",  raw_out,        NULL);
        g_object_set(G_OBJECT(data->sink_preprocess),"location",  preprocess_out, NULL);
        g_object_set(G_OBJECT(data->sink_display),   "location",  final_out,      NULL);
    } else {
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
            GST_DEBUG ("new Caps for raw capsfilter %" GST_PTR_FORMAT, caps);
            g_object_set (G_OBJECT (data->capsfilter_raw),  "caps",  caps, NULL);
            gst_caps_unref (caps);

            caps  = gst_caps_new_simple ("video/x-raw",
                                         "framerate", GST_TYPE_FRACTION, MAX_DEMO_MODE_FRAME_RATE, MAX_FRAME_RATE_DENOM,
                                         NULL);
            GST_DEBUG ("new Caps for pre-process capsfilter %" GST_PTR_FORMAT, caps);
            g_object_set (G_OBJECT (data->capsfilter_preprocess),  "caps",  caps, NULL);
            gst_caps_unref (caps);

            caps  = gst_caps_new_simple ("video/x-raw",
                                         "framerate", GST_TYPE_FRACTION, MAX_DEMO_MODE_FRAME_RATE, MAX_FRAME_RATE_DENOM,
                                         NULL);
            GST_DEBUG ("new Caps for final capsfilter %" GST_PTR_FORMAT, caps);
            g_object_set (G_OBJECT (data->capsfilter_display),  "caps",  caps, NULL);
            gst_caps_unref (caps);
            if (file_playback) {
                g_object_set (G_OBJECT (data->rawvideoparse),  "use-sink-caps", FALSE,                    NULL);
                g_object_set (G_OBJECT (data->rawvideoparse),  "width",         width,                    NULL);
                g_object_set (G_OBJECT (data->rawvideoparse),  "height",        height,                   NULL);
                g_object_set (G_OBJECT (data->rawvideoparse),  "format",        GST_VIDEO_FORMAT_GRAY8,   NULL);
                g_object_set (G_OBJECT (data->rawvideoparse),  "framerate",     MAX_DEMO_MODE_FRAME_RATE, MAX_FRAME_RATE_DENOM, NULL);
            }
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
    config_file.append(OTSU_ACC_JSON_FILE);
    g_object_set (G_OBJECT(data->otsu),   "kernels-config", config_file.c_str(), NULL);
    GST_DEBUG ("Config file path is %s", config_file.c_str());

    config_file.erase (config_file.begin()+ strlen(config_path), config_file.end()-0);
    config_file.append(CCA_ACC_JSON_FILE);
    g_object_set (G_OBJECT(data->cca),    "kernels-config", config_file.c_str(), NULL);
    GST_DEBUG ("Config file path is %s", config_file.c_str());

    config_file.erase (config_file.begin()+ strlen(config_path), config_file.end()-0);
    config_file.append(TEXT_2_OVERLAY_JSON_FILE);
    g_object_set (G_OBJECT(data->text2overlay), "kernels-config", config_file.c_str(), NULL);
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
 *  @return Error code.
 */
DD_ERROR_LOG
link_pipeline (AppData *data) {
    gchar *name1, *name2;
    gint ret = DD_SUCCESS;
    data->pad_raw = gst_element_get_request_pad(data->tee_raw, "src_1");
    name1 = gst_pad_get_name(data->pad_raw);
    data->pad_raw2 = gst_element_get_request_pad(data->tee_raw, "src_2");
    name2 = gst_pad_get_name(data->pad_raw2);
    if (!file_playback) {
        if (!gst_element_link_many(data->capsfilter, data->tee_raw, NULL)) {
            GST_ERROR ("Error linking for capsfilter --> tee");
            return DD_ERROR_PIPELINE_LINKING_FAIL;
        }
        GST_DEBUG ("Linked for capsfilter --> tee successfully");
    } else {
        if (!demo_mode) {
            if (!gst_element_link_many(data->src, data->capsfilter, data->tee_raw, NULL)) {
                GST_ERROR ("Error linking for src --> capsfilter --> tee");
                return DD_ERROR_PIPELINE_LINKING_FAIL;
            }
            GST_DEBUG ("Linked for src --> capsfilter --> tee successfully");
        } else {
            if (!gst_element_link_many(data->src, data->rawvideoparse, data->tee_raw, NULL)) {
                GST_ERROR ("Error linking for src --> rawvideoparse --> tee");
                return DD_ERROR_PIPELINE_LINKING_FAIL;
            }
            GST_DEBUG ("Linked for src --> rawvideoparse --> tee successfully");
        }
    }
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
    if (!gst_element_link_many(data->queue_raw2, data->otsu, data->preprocess, data->tee_preprocess, NULL)) {
        GST_ERROR ("Error linking for queue_raw2 --> otsu --> preprocess --> tee_preprocess");
        return DD_ERROR_PIPELINE_LINKING_FAIL;
    }
    GST_DEBUG ("Linking for queue_raw2 --> otsu --> preprocess --> tee_preprocess successfully");
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
        if (!gst_element_link_many(data->queue_preprocess2, data->cca, data->text2overlay, \
                                   data->videorate_display, data->capsfilter_display, \
                                   data->perf_display, data->sink_display, NULL)) {
            GST_ERROR ("Error linking for queue --> cca --> text2overlay --> videorate --> capsfilter \
                        --> perf --> sink");
             return DD_ERROR_PIPELINE_LINKING_FAIL;
        }
        GST_DEBUG ("Linking for queue --> cca --> text2overlay --> videorate  --> capsfilter --> \
                    perf --> sink  successfully");
    } else {
        if (!gst_element_link_many(data->queue_preprocess2, data->cca, data->text2overlay, \
                                   data->perf_display, data->sink_display, NULL)) {
            GST_ERROR ("Error linking for queue_preprocess2 --> cca --> text2overlay --> perf --> sink");
            return DD_ERROR_PIPELINE_LINKING_FAIL;
        }
        GST_DEBUG ("Linking for queue_raw2 --> cca --> text2overlay --> perf --> sink successfully");
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
 *  @return Error code.
 */
DD_ERROR_LOG
create_pipeline (AppData *data) {
    data->pipeline =   gst_pipeline_new("defectdetection");
    if (file_playback) {
        data->src               =  gst_element_factory_make("filesrc",      NULL);
    } else {
        data->src               =  gst_element_factory_make("mediasrcbin",  NULL);
    }
    if (file_dump) {
        data->sink_raw          =  gst_element_factory_make("filesink",     NULL);
        data->sink_preprocess   =  gst_element_factory_make("filesink",     NULL);
        data->sink_display      =  gst_element_factory_make("filesink",     NULL);
    } else {
        data->sink_raw          =  gst_element_factory_make("kmssink",      "display-raw");
        data->sink_preprocess   =  gst_element_factory_make("kmssink",      "display-preprocess");
        data->sink_display      =  gst_element_factory_make("kmssink",      "display-final");
    }
    data->capsfilter            =  gst_element_factory_make("capsfilter",   NULL);
    data->rawvideoparse         =  gst_element_factory_make("rawvideoparse",NULL);
    data->preprocess            =  gst_element_factory_make("ivas_xfilter", "pre-process");
    data->otsu                  =  gst_element_factory_make("ivas_xfilter", "otsu");
    data->cca                   =  gst_element_factory_make("ivas_xfilter", "cca");
    data->text2overlay          =  gst_element_factory_make("ivas_xfilter", "text2overlay");
    data->tee_raw               =  gst_element_factory_make("tee",          NULL);
    data->tee_preprocess        =  gst_element_factory_make("tee",          NULL);
    data->queue_raw             =  gst_element_factory_make("queue",        NULL);
    data->queue_raw2            =  gst_element_factory_make("queue",        NULL);
    data->queue_preprocess      =  gst_element_factory_make("queue",        NULL);
    data->queue_preprocess2     =  gst_element_factory_make("queue",        NULL);
    data->perf_raw              =  gst_element_factory_make("perf",         "perf-raw");
    data->perf_preprocess       =  gst_element_factory_make("perf",         "perf-preprocess");
    data->perf_display          =  gst_element_factory_make("perf",         "perf-final");
    data->videorate_raw         =  gst_element_factory_make("videorate",    NULL);
    data->videorate_preprocess  =  gst_element_factory_make("videorate",    NULL);
    data->videorate_display     =  gst_element_factory_make("videorate",    NULL);
    data->capsfilter_raw        =  gst_element_factory_make("capsfilter",   NULL);
    data->capsfilter_preprocess =  gst_element_factory_make("capsfilter",   NULL);
    data->capsfilter_display    =  gst_element_factory_make("capsfilter",   NULL);

    if (!data->pipeline || !data->src || !data->capsfilter || ! data->rawvideoparse \
        || !data->preprocess || !data->otsu || !data->cca || !data->text2overlay \
        || !data->sink_display || !data->sink_raw || !data->sink_preprocess \
        || !data->tee_raw || !data->tee_preprocess \
        || !data->queue_raw || !data->queue_raw || !data->queue_raw2 || !data->queue_preprocess \
        || !data->queue_preprocess2 || !data->perf_raw || !data->perf_preprocess || !data->perf_display \
        || !data->videorate_raw || !data->videorate_preprocess || !data->videorate_display \
        || !data->capsfilter_raw || !data->capsfilter_preprocess || !data->capsfilter_display) {
           GST_ERROR ("could not create few elements");
           return DD_ERROR_PIPELINE_CREATE_FAIL;
    }
    GST_DEBUG ("All elements are created");
    gst_bin_add_many(GST_BIN(data->pipeline), data->src, data->rawvideoparse, data->capsfilter, \
                     data->preprocess, data->otsu, data->cca, data->text2overlay, \
                     data->sink_display, data->sink_raw, data->queue_raw, data->queue_raw2, \
                     data->queue_preprocess, data->queue_preprocess2, data->sink_preprocess, \
                     data->tee_raw, data->tee_preprocess, data->perf_raw, data->perf_preprocess, \
                     data->perf_display, data->videorate_raw, data->videorate_preprocess, \
                     data->videorate_display, data->capsfilter_raw, data->capsfilter_preprocess, \
                     data->capsfilter_display, NULL);
    return DD_SUCCESS;
}

static std::string
find_mipi_dev() {
    glob_t globbuf;

    glob("/dev/media*", 0, NULL, &globbuf);
    for (int i = 0; i < globbuf.gl_pathc; i++) {
        std::ostringstream cmd;
        cmd << "media-ctl -d " << globbuf.gl_pathv[i] << " -p | grep driver | grep xilinx-video | wc -l";

        std::string a = exec(cmd.str().c_str());
        a=a.substr(0, a.find("\n"));
        if ( a == std::string("1")) {
            dev_node = globbuf.gl_pathv[i];
            break;
        }
    }
    globfree(&globbuf);
    return dev_node;
}

static gint
check_mipi_src() {
    std::string mipidev("");
    mipidev = find_mipi_dev();
    if (mipidev == "") {
        g_printerr("ERROR: MIPI device is not ready.\n%s", msg_firmware);
        return 1;
    }

    if ( access( mipidev.c_str(), F_OK ) != 0) {
        g_printerr("ERROR: Device %s is not ready.\n%s", mipidev.c_str(), msg_firmware);
        return 1;
    }
    return 0;
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
    optctx = g_option_context_new ("- Application to detect the defect of Mango on Xilinx board");
    g_option_context_add_main_entries (optctx, entries, NULL);
    g_option_context_add_group (optctx, gst_init_get_option_group ());
    if (!g_option_context_parse (optctx, &argc, &argv, &error)) {
        g_printerr ("Error parsing options: %s\n", error->message);
        g_option_context_free (optctx);
        g_clear_error (&error);
        return -1;
    }
    g_option_context_free (optctx);

    if (in_file) {
        file_playback = TRUE;
    }

    if (final_out && raw_out && preprocess_out) {
        file_dump = true;
    }

    if (in_file) {
        GST_DEBUG ("In file is %s", in_file);
    }

    GST_DEBUG ("Width is %d", width);
    GST_DEBUG ("height is %d", height);
    GST_DEBUG ("framerate is %d", framerate);
    GST_DEBUG ("file playback mode is %s", file_playback ? "TRUE" : "FALSE");
    GST_DEBUG ("file dump is %s", file_dump ? "TRUE" : "FALSE");
    GST_DEBUG ("demo mode is %s", demo_mode ? "On" : "Off");

    if (config_path)
        GST_DEBUG ("config path is %s", config_path);

    if (dev_node.c_str())
        GST_DEBUG ("media node is %s", dev_node.c_str());

    if (width > MAX_WIDTH || height > MAX_HEIGHT) {
        ret = DD_ERROR_RESOLUTION_NOT_SUPPORTED;
        g_printerr ("Exiting the app with an error: %s\n", error_to_string (ret));
        return ret;
    }

    if (access("/dev/dri/by-path/platform-b0010000.v_mix-card", F_OK) != 0) {
        g_printerr("ERROR: Mixer device is not ready.\n%s", msg_firmware);
        return -1;
    } else {
        exec("echo | modetest -D B0010000.v_mix -s 52@40:3840x2160@NV16");
    }

    if (!file_playback && (check_mipi_src() != 0)) {
        g_printerr ("MIPI media node not found, please check the connection of camera\n");
        return -1;
    }
    if (!file_playback ) {
        std::string script_caller;
        GST_DEBUG ("Calling default sensor calibration script");
        script_caller = "echo | ar0144-sensor-calib.sh " + dev_node;
        exec(script_caller.c_str());
    }
    ret = create_pipeline (&data);
    if (ret != DD_SUCCESS) {
        g_printerr ("Exiting the app with an error: %s\n", error_to_string (ret));
        return ret;
    }

    ret = link_pipeline (&data);
    if (ret != DD_SUCCESS) {
        g_printerr ("Exiting the app with an error: %s\n", error_to_string (ret));
        return ret;
    }

    ret = set_pipeline_config (&data);
    if (ret != DD_SUCCESS) {
        g_printerr ("Exiting the app with an error: %s\n", error_to_string (ret));
        return ret;
    }

    /* we add a message handler */
    bus = gst_pipeline_get_bus (GST_PIPELINE (data.pipeline));
    bus_watch_id = gst_bus_add_watch (bus, (GstBusFunc)(message_cb), &data);
    gst_object_unref (bus);

    if (!file_playback) {
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
    if (final_out)
        g_free (final_out);
    if (raw_out)
        g_free (raw_out);
    if (preprocess_out)
        g_free (preprocess_out);
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
        GST_ERROR ("Linking failed");
    }
exit:
    /* Unreference the sink pad */
    gst_object_unref (sink_pad);
}
