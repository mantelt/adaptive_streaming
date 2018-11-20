#include <iostream>
#include <iomanip>
#include <ctime>
#include <sstream>

#include "RTSPAdaptiveStreaming.h"

RTSPAdaptiveStreaming::RTSPAdaptiveStreaming(string _device,
        string _uri,
        GstRTSPServer* server,
        int quality):
    GenericAdaptiveStreaming(_device),
    uri(_uri),
    rtsp_server((GstRTSPServer*)gst_object_ref(server)),
    media_prepared(false)
{
    current_quality = quality;
    init_media_factory();
}

RTSPAdaptiveStreaming::~RTSPAdaptiveStreaming()
{
    if (media_factory) {
        gst_object_unref(media_factory);
    }
    gst_object_unref(rtsp_server);
}

void RTSPAdaptiveStreaming::init_media_factory()
{
    GstRTSPMountPoints* mounts;
    mounts = gst_rtsp_server_get_mount_points(rtsp_server);
    media_factory = gst_rtsp_media_factory_new();

    string launch_string;

    launch_string = "v4l2src device=" + device + " name=src"
                        " ! queue"
//                        " ! capsfilter name=capsfilter caps=video/x-h264,width=1920,height=1080,framerate=30/1"
                        " ! tee name=tee_element tee_element."
                        " ! queue"
                        " ! h264parse"
                        " ! rtph264pay name=pay0";

    std::cout << launch_string << std::endl;

    gst_rtsp_media_factory_set_launch(media_factory, launch_string.c_str());

    gst_rtsp_mount_points_add_factory(mounts, uri.c_str(), media_factory);
    g_signal_connect(media_factory, "media-constructed", G_CALLBACK(static_media_constructed_callback), this);
    gst_object_unref(mounts);
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

    guint major;
    guint minor;
    guint micro;
    guint nano;
    gst_version(&major, &minor, &micro, &nano);

    if (minor >= 14) {
        g_signal_connect(parent, "deep-element-added", G_CALLBACK(static_deep_callback), this);
    }
    string str;
    list = GST_BIN_CHILDREN(parent);

    for (list_itr = list; list_itr != nullptr; list_itr = list_itr->next) {
        element = (GstElement*)list_itr->data;
        str = gst_element_get_name(element);

        if (str.find("bin") != std::string::npos || str.find("pipeline") != std::string::npos) {
            pipeline = gst_bin_get_by_name(GST_BIN(parent), str.c_str());
        }
        if (str.find("rtpbin") != std::string::npos) {
            rtpbin = gst_bin_get_by_name(GST_BIN(parent), str.c_str());
        }

        // Older gstreamer versions on the ARM boards setup multiudpsink in a different place
        if (minor < 14) {
            if (str.find("multiudpsink") != std::string::npos) {
                multi_udp_sink = gst_bin_get_by_name(GST_BIN(parent), str.c_str());
            }
        }
    }

    if (!get_element_references()) {
        cerr << "Some GStreamer elements not referenced" << endl;
    }

    change_quality_preset(AUTO_PRESET);
    add_rtpbin_probes();
    media_prepared = true;
}

void RTSPAdaptiveStreaming::static_media_constructed_callback(GstRTSPMediaFactory *media_factory,
        GstRTSPMedia *media, gpointer data)
{
    g_signal_connect(media, "prepared", G_CALLBACK(static_media_prepared_callback), data);
    g_signal_connect(media, "unprepared", G_CALLBACK(static_media_unprepared_callback), data);
}

void RTSPAdaptiveStreaming::static_media_prepared_callback(GstRTSPMedia* media, gpointer data)
{
    RTSPAdaptiveStreaming* ptr = (RTSPAdaptiveStreaming*)data;
    ptr->media_prepared_callback(media);
}

void RTSPAdaptiveStreaming::static_media_unprepared_callback(GstRTSPMedia* media, gpointer data)
{
    // use this as a way to clean up variables and refs I guess?!
    RTSPAdaptiveStreaming* ptr = (RTSPAdaptiveStreaming*)data;
    ptr->media_unprepared_callback(media);
}

void RTSPAdaptiveStreaming::static_deep_callback(GstBin* bin,
        GstBin* sub_bin,
        GstElement* element,
        gpointer data)
{
    RTSPAdaptiveStreaming* ptr = (RTSPAdaptiveStreaming*)data;
    ptr->deep_callback(bin, sub_bin, element);
}

void RTSPAdaptiveStreaming::deep_callback(GstBin* bin,
        GstBin* sub_bin,
        GstElement* element)
{
    string element_name;
    element_name = gst_element_get_name(element);
    // One udpsink takes care of RTCP packets and the other RTP
    if (element_name.find("multiudpsink") != std::string::npos && !multi_udp_sink) {
        multi_udp_sink = element;
    }
}

// Called once the client has disconnected, allowing us to clean up the stream
void RTSPAdaptiveStreaming::media_unprepared_callback(GstRTSPMedia* media)
{
    file_recorder.disable_recorder();
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);

    gst_element_set_state(rtpbin, GST_STATE_NULL);
    gst_object_unref(rtpbin);

    current_quality = AUTO_PRESET;
    cout << "Stream disconnected!" << endl;
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
    file_recorder.disable_recorder();
    return GST_PAD_PROBE_REMOVE;
}

GstPadProbeReturn RTSPAdaptiveStreaming::static_rtcp_callback(GstPad* pad,
        GstPadProbeInfo* info,
        gpointer data)
{
    RTSPAdaptiveStreaming* ptr = (RTSPAdaptiveStreaming*)data;
    return ptr->rtcp_callback(pad, info);
}

// On receiving an RTCP packet, we forward it to the QoSEstimator for extracting
// the data and generating the QoSReport
GstPadProbeReturn RTSPAdaptiveStreaming::rtcp_callback(GstPad* pad, GstPadProbeInfo* info)
{
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
            if (current_quality == AUTO_PRESET && gst_rtcp_packet_get_type(packet) == GST_RTCP_TYPE_RR) {
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

// We measure the size of every buffer on the rtph264payloader to get the encoding
// bitrate estimate
GstPadProbeReturn RTSPAdaptiveStreaming::payloader_callback(GstPad* pad, GstPadProbeInfo* info)
{
    guint32 buffer_size;
    guint64 bytes_sent;
    GstBuffer* buf = GST_PAD_PROBE_INFO_BUFFER(info);
    if (buf != nullptr) {
        buffer_size = gst_buffer_get_size(buf);
        if (multi_udp_sink) {
            g_object_get(multi_udp_sink, "bytes-served", &bytes_sent, NULL);
        }
        qos_estimator.calculate_bitrates(bytes_sent, buffer_size);
    }
    return GST_PAD_PROBE_OK;
}

bool RTSPAdaptiveStreaming::get_element_references()
{
    if (pipeline) {
        tee = gst_bin_get_by_name(GST_BIN(pipeline), "tee_element");
        rtph264_payloader = gst_bin_get_by_name(GST_BIN(pipeline), "pay0");
        v4l2_src = gst_bin_get_by_name(GST_BIN(pipeline), "src");
        src_capsfilter = gst_bin_get_by_name(GST_BIN(pipeline), "capsfilter");

		if (tee && rtph264_payloader && v4l2_src && src_capsfilter) {
			return true;
		} else {
			return false;
		}
    }
    return false;
}

void RTSPAdaptiveStreaming::add_rtpbin_probes()
{
    GstPad* rtcp_rr_pad;
    GstPad* rtcp_sr_pad;
    GstPad* payloader_pad;

    rtcp_rr_pad = gst_element_get_static_pad(rtpbin, "recv_rtcp_sink_0");
    gst_pad_add_probe(rtcp_rr_pad, GST_PAD_PROBE_TYPE_BUFFER, static_rtcp_callback, this, NULL);
    gst_object_unref(rtcp_rr_pad);

    rtcp_sr_pad = gst_element_get_static_pad(rtpbin, "send_rtcp_src_0");
    gst_pad_add_probe(rtcp_sr_pad, GST_PAD_PROBE_TYPE_BUFFER, static_rtcp_callback, this, NULL);
    gst_object_unref(rtcp_sr_pad);

    payloader_pad = gst_element_get_static_pad(rtph264_payloader, "sink");
    gst_pad_add_probe(payloader_pad, GST_PAD_PROBE_TYPE_BUFFER, static_payloader_callback, this, NULL);
    gst_object_unref(payloader_pad);
}

bool RTSPAdaptiveStreaming::get_media_prepared()
{
    return media_prepared;
}

void RTSPAdaptiveStreaming::record_stream(bool _record_stream)
{
    if (_record_stream) {
        file_recorder.init_file_recorder(pipeline, tee);
    } else {
        if (file_recorder.tee_file_pad) {
            gst_pad_add_probe(file_recorder.tee_file_pad, GST_PAD_PROBE_TYPE_BLOCK,
                              static_probe_block_callback, this, NULL);
        }
    }
}

// On changing the resolution while the CC is recording, we stop the recording,
// to avoid any serious concurrency issues which can otherwise occur
void RTSPAdaptiveStreaming::set_device_properties(int quality, bool _record_stream)
{
	record_stream(_record_stream);
	change_quality_preset(quality);
}
