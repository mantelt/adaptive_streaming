#ifndef ADAPTIVE_STREAMING_H
#define ADAPTIVE_STREAMING_H

#include <gst/gst.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <gst/rtp/gstrtcpbuffer.h>
#include <time.h>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cstdint>
#include "QoSEstimator.h"
#include "DeviceDatatypes.h"
#include "Constants.h"
#include "FileRecorder.h"
#include "USBFHD06H.h"

using namespace std;

// Generic implementation of an Adaptive Video Streamer. Contains all the necessary
// GstElements which need to be manually referenced by subclassing it.
// All the adapatation logic is handled here

class GenericAdaptiveStreaming
{
private:
	uint32_t MIN_BITRATE;
	uint32_t MAX_BITRATE;
	uint32_t DEC_BITRATE;
    uint32_t INC_BITRATE;

    enum NetworkState {STEADY, CONGESTION} network_state;
    uint32_t successive_transmissions;

    USBFHD06H device_ctrl;

    uint32_t bitrate_presets[NUM_QUALITY_PRESETS];

    void set_encoding_bitrate(uint32_t bitrate);
    void set_state_constants();

    void improve_quality();
    void degrade_quality();

public:
    string device;

    int current_quality;
    QualityPresets current_res;
    uint32_t h264_bitrate;
    FileRecorder file_recorder;

    GstElement* pipeline;
    GstElement* v4l2_src;
    GstElement* src_capsfilter;
    GstElement* videoconvert;
//    GstElement* h264_encoder;
    GstElement* h264_parser;
    GstElement* rtph264_payloader;
    GstElement* tee;
    GstElement* multi_udp_sink;

    QoSEstimator qos_estimator;

    GenericAdaptiveStreaming(string _device = "/dev/video0");

    virtual ~GenericAdaptiveStreaming();
    void change_quality_preset(int quality);
    bool record_stream(bool _record_stream);
    void adapt_stream();
};

#endif
