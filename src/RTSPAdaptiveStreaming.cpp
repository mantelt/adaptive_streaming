#include <iostream>
#include <iomanip>
#include <ctime>
#include <sstream>

#include "RTSPAdaptiveStreaming.h"

RTSPAdaptiveStreaming::RTSPAdaptiveStreaming(string _device,
        CameraType type,
        string _uri,
        GstRTSPServer* server,
        int quality):
    GenericAdaptiveStreaming(_device, type),
    uri(_uri),
    rtsp_server(server),
    media_prepared(false)
{
    recording_spinlock = false;
    current_quality = quality;
    init_media_factory();
}

RTSPAdaptiveStreaming::~RTSPAdaptiveStreaming()
{
    if (multi_udp_sink) {
        gst_element_set_state(multi_udp_sink, GST_STATE_NULL);
        gst_object_unref(multi_udp_sink);
    }
    gst_element_set_state(rtpbin, GST_STATE_NULL);
    gst_object_unref(rtpbin);
    gst_object_unref(rtsp_server);
}

void RTSPAdaptiveStreaming::init_media_factory()
{
    GstRTSPMediaFactory* media_factory;
    GstRTSPMountPoints* mounts;
    mounts = gst_rtsp_server_get_mount_points(rtsp_server);
    media_factory = gst_rtsp_media_factory_new();

    string launch_string;

    switch (camera_type) {
    case RAW_CAM:
        launch_string = "v4l2src name=src device=" + device +
                        " ! capsfilter name=capsfilter caps=image/jpeg, width=320, height=240, framerate=30/1"
                        " ! jpegdec"
                        " ! videoconvert"
                        " ! textoverlay name=textoverlay"
                        " ! x264enc name=x264enc tune=zerolatency threads=4 bitrate=500"
                        " ! tee name=tee_element tee_element."
                        " ! queue"
                        " ! h264parse"
                        " ! rtph264pay name=pay0";
        break;
    case UVC_CAM:
//gst-launch-1.0 -v -e uvch264src device=/dev/video1 name=src auto-start=true src.vfsrc ! queue ! "video/x-raw,width=320,height=240,framerate=10/1" ! fakesink sync=false src.vidsrc ! queue ! video/x-h264,width=1280,height=720,framerate=30/1 ! h264parse ! avdec_h264 ! xvimagesink sync=false
        launch_string = "uvch264src device=" + device +
                        " name=src auto-start=true src.vfsrc"
                        " ! queue"
                        " ! capsfilter name=vfcaps caps=video/x-raw, width=640, height=480, framerate=10/1"
                        " ! fakesink sync=false src.vidsrc"
                        " ! queue"
                        " ! video/x-h264, width=640, height=480, framerate=30/1"
                        " ! tee name=tee_element tee_element."
                        " ! queue"
                        " ! h264parse"
                        " ! rtph264pay name=pay0";
        // launch_string = "uvch264src device=" + device +
        //                 " name=src auto-start=true src.vidsrc "
        //                 " ! queue"
        //                 " ! video/x-h264, width=320, height=240, framerate=30/1"
        //                 " ! tee name=tee_element tee_element."
        //                 " ! queue"
        //                 " ! h264parse"
        //                 " ! rtph264pay name=pay0";
        // launch_string = "uvch264src device=" + device +
        //                 " name=src auto-start=true src.vdsrc "
        //                 " ! queue"
        //                 " ! video/x-h264, width=320, height=240, framerate=30/1"
        //                 " ! tee name=tee_element tee_element."
        //                 " ! queue"
        //                 " ! h264parse"
        //                 " ! rtph264pay name=pay0";
        break;
    case H264_CAM:
        launch_string = "v4l2src name=src device=" + device +
                        " ! video/x-h264, width=320, height=240, framerate=30/1"
                        " ! tee name=tee_element tee_element."
                        " ! queue"
                        " ! h264parse"
                        " ! rtph264pay name=pay0";
        break;
    };
    gst_rtsp_media_factory_set_launch(media_factory, launch_string.c_str());

    gst_rtsp_mount_points_add_factory(mounts, uri.c_str(), media_factory);
    g_signal_connect(media_factory, "media-constructed", G_CALLBACK(static_media_constructed_callback), this);
    g_object_unref(mounts);
}

void RTSPAdaptiveStreaming::static_media_constructed_callback(GstRTSPMediaFactory *media_factory,
        GstRTSPMedia *media, gpointer data)
{
    g_signal_connect(media, "prepared", G_CALLBACK(static_media_prepared_callback), data);
}

void RTSPAdaptiveStreaming::static_media_prepared_callback(GstRTSPMedia* media, gpointer data)
{
    RTSPAdaptiveStreaming* ptr = (RTSPAdaptiveStreaming*)data;
    ptr->media_prepared_callback(media);
}

void RTSPAdaptiveStreaming::media_prepared_callback(GstRTSPMedia* media)
{
    GstElement* element;
    GstElement* parent;
    GList* list;
    GList* list_itr;

    element = gst_rtsp_media_get_element(media);
    parent = (GstElement*)gst_object_get_parent(GST_OBJECT(element));

    multi_udp_sink = nullptr;

    string str;
    list = GST_BIN_CHILDREN(parent);

    for (list_itr = list; list_itr != nullptr; list_itr = list_itr->next) {
        element = (GstElement*)list_itr->data;
        str = gst_element_get_name(element);
        g_warning("element name = %s", str.c_str());
        // FIXME: this is a gstreamer version thing!
        if (str.find("bin") != std::string::npos) {
            pipeline = gst_bin_get_by_name(GST_BIN(parent), str.c_str());
        }
        if (str.find("rtpbin") != std::string::npos) {
            rtpbin = gst_bin_get_by_name(GST_BIN(parent), str.c_str());
        }
        if (str.find("multiudpsink") != std::string::npos) {
            g_warning("Identified %s", str.c_str());
            multi_udp_sink = gst_bin_get_by_name(GST_BIN(parent), str.c_str());
        }
    }

    list = GST_BIN_CHILDREN(pipeline);

    if (get_element_references()) {
        g_warning("Elements referenced!");
    } else {
        g_warning("Some elements not reffed");
    }

    set_resolution(ResolutionPresets::LOW);
    add_rtpbin_probes();
    media_prepared = true;
}

bool RTSPAdaptiveStreaming::get_element_references()
{
    if (pipeline) {
        tee = gst_bin_get_by_name(GST_BIN(pipeline), "tee_element");
        rtph264_payloader = gst_bin_get_by_name(GST_BIN(pipeline), "pay0");
        v4l2_src = gst_bin_get_by_name(GST_BIN(pipeline), "src");
        src_capsfilter = gst_bin_get_by_name(GST_BIN(pipeline), "capsfilter");

        switch (camera_type) {
        case RAW_CAM:
            h264_encoder = gst_bin_get_by_name(GST_BIN(pipeline), "x264enc");
            text_overlay = gst_bin_get_by_name(GST_BIN(pipeline), "textoverlay");
            g_object_set(G_OBJECT(text_overlay), "valignment", 2, NULL); //top
            g_object_set(G_OBJECT(text_overlay), "halignment", 0, NULL); //left
            g_object_set(G_OBJECT(text_overlay), "font-desc", "Sans, 9", NULL);
            if (tee && rtph264_payloader && v4l2_src && src_capsfilter && h264_encoder && text_overlay) {
                return true;
            } else {
                return false;
            }
        case H264_CAM:
        case UVC_CAM:
            if (tee && rtph264_payloader && v4l2_src && src_capsfilter) {
                return true;
            } else {
                return false;
            }
        };
    }
    return false;
}

GstPadProbeReturn RTSPAdaptiveStreaming::static_probe_block_callback(GstPad* pad,
        GstPadProbeInfo* info,
        gpointer data)
{
    RTSPAdaptiveStreaming* ptr = (RTSPAdaptiveStreaming*)data;
    return ptr->probe_block_callback(pad, info);
}

GstPadProbeReturn RTSPAdaptiveStreaming::probe_block_callback(GstPad* pad, GstPadProbeInfo* info)
{
    // pad is blocked, so it's ok to unlink
    gst_pad_unlink(file_recorder.tee_file_pad, file_recorder.queue_pad);
    file_recorder.disable_recorder();
    gst_element_release_request_pad(tee, file_recorder.tee_file_pad);
    g_warning("Pad Removed");
    // cv.notify_one();
    recording_spinlock = false;
    return GST_PAD_PROBE_REMOVE;
}

void RTSPAdaptiveStreaming::add_rtpbin_probes()
{
    GstPad* rtcp_rr_pad;
    GstPad* rtcp_sr_pad;
    GstPad* payloader_pad;

    rtcp_rr_pad = gst_element_get_static_pad(rtpbin, "recv_rtcp_sink_0");
    gst_pad_add_probe(rtcp_rr_pad, GST_PAD_PROBE_TYPE_BUFFER, static_rtcp_callback, this, NULL);
    g_object_unref(rtcp_rr_pad);

    rtcp_sr_pad = gst_element_get_static_pad(rtpbin, "send_rtcp_src_0");
    gst_pad_add_probe(rtcp_sr_pad, GST_PAD_PROBE_TYPE_BUFFER, static_rtcp_callback, this, NULL);
    g_object_unref(rtcp_sr_pad);
    
    if (multi_udp_sink) {
        payloader_pad = gst_element_get_static_pad(rtph264_payloader, "sink");
        gst_pad_add_probe(payloader_pad, GST_PAD_PROBE_TYPE_BUFFER, static_payloader_callback, this, NULL);
        g_object_unref(payloader_pad);
    }
}

bool RTSPAdaptiveStreaming::get_media_prepared()
{
    return media_prepared;
}

GstPadProbeReturn RTSPAdaptiveStreaming::static_rtcp_callback(GstPad* pad,
        GstPadProbeInfo* info,
        gpointer data)
{
    RTSPAdaptiveStreaming* ptr = (RTSPAdaptiveStreaming*)data;
    return ptr->rtcp_callback(pad, info);
}

GstPadProbeReturn RTSPAdaptiveStreaming::rtcp_callback(GstPad* pad, GstPadProbeInfo* info)
{
    // g_warning("H264 rate - %d", h264_bitrate);
    GstBuffer* buf = GST_PAD_PROBE_INFO_BUFFER(info);
    if (buf != nullptr) {
        GstRTCPBuffer *rtcp_buffer = (GstRTCPBuffer*)malloc(sizeof(GstRTCPBuffer));
        rtcp_buffer->buffer = nullptr;
        gst_rtcp_buffer_map(buf, GST_MAP_READ, rtcp_buffer);
        GstRTCPPacket *packet = (GstRTCPPacket*)malloc(sizeof(GstRTCPPacket));
        gboolean more = gst_rtcp_buffer_get_first_packet(rtcp_buffer, packet);
        //same buffer can have an SDES and an RTCP pkt
        while (more) {
            qos_estimator.handle_rtcp_packet(packet);
            if (current_quality == AUTO_PRESET) {
                adapt_stream();
            }
            more = gst_rtcp_packet_move_to_next(packet);
        }
        free(rtcp_buffer);
        free(packet);
    }
    return GST_PAD_PROBE_OK;
}

GstPadProbeReturn RTSPAdaptiveStreaming::static_payloader_callback(GstPad* pad,
        GstPadProbeInfo* info,
        gpointer data)
{
    RTSPAdaptiveStreaming* ptr = (RTSPAdaptiveStreaming*)data;
    return ptr->payloader_callback(pad, info);
}

// FIXME: UDP SINK NO LONGER EXISTS!!!
GstPadProbeReturn RTSPAdaptiveStreaming::payloader_callback(GstPad* pad, GstPadProbeInfo* info)
{
    guint32 buffer_size;
    guint64 bytes_sent;
    GstBuffer* buf = GST_PAD_PROBE_INFO_BUFFER(info);
    if (buf != nullptr) {
        buffer_size = gst_buffer_get_size(buf);
        if (multi_udp_sink) {
            g_object_get(multi_udp_sink, "bytes-served", &bytes_sent, NULL);
            qos_estimator.calculate_bitrates(bytes_sent, buffer_size);
        }
    }
    return GST_PAD_PROBE_OK;
}

void RTSPAdaptiveStreaming::record_stream(bool _record_stream)
{
    while(recording_spinlock) {

    }
    g_warning("RecordStream %u", _record_stream);
    if (_record_stream) {
        file_recorder.init_file_recorder(pipeline, tee);
    } else {
        if (file_recorder.tee_file_pad) {
            recording_spinlock = true;
            gst_pad_add_probe(file_recorder.tee_file_pad, GST_PAD_PROBE_TYPE_BLOCK,
                                static_probe_block_callback, this, NULL);
        } else {
            g_warning("File pad missing");
        }
    }
}

void RTSPAdaptiveStreaming::set_device_properties(int quality, bool _record_stream)
{
    // we can't have the capsfilter changing when recording from the CC, so we disable it for AUTO mode
    if (quality == AUTO_PRESET) {
        record_stream(false);
        change_quality_preset(quality);
        return;
    }
    if (quality == current_quality) {
        record_stream(_record_stream);
    } else {
        record_stream(false);
        change_quality_preset(quality);
        record_stream(_record_stream);
    }
}