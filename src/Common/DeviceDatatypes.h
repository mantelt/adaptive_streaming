#ifndef DEVICE_DATATYPES_H
#define DEVICE_DATATYPES_H

#include <string>
#include <vector>
#include <gst/gst.h>

using namespace std;

enum QualityPresets {LOW, MED, HIGH, NUM_QUALITY_PRESETS};

enum RTSPMessageType {GET_DEVICE_PROPS, SET_DEVICE_PROPS, ERROR, COUNT};
const static vector<string> RTSPMessageHeader = {
    "GDP", "SDP"
};


// Includes some metadata about each camera
struct v4l2_info {
    string camera_name;
    string mount_point;
    guint64 frame_property_bitmask;
};

// Gives QoS estimates to the Adaptive Streamer for acting upon
struct QoSReport {
    guint8 fraction_lost;
    gfloat estimated_bitrate;
    gfloat encoding_bitrate;
    gfloat rtt;
    gfloat buffer_occ;

    QoSReport()
    {
    }

    QoSReport(guint8 _fl, gfloat _estd_br, gfloat _enc_br, gfloat _rtt, gfloat bo) :
        fraction_lost(_fl), estimated_bitrate(_estd_br), encoding_bitrate(_enc_br),
        rtt(_rtt), buffer_occ(bo)
    {
    }
};


#endif
