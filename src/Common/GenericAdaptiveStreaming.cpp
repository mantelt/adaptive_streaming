#include <stdio.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <linux/v4l2-controls.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "GenericAdaptiveStreaming.h"

GenericAdaptiveStreaming::GenericAdaptiveStreaming(string _device) :
    network_state(NetworkState::STEADY),
	successive_transmissions(0),
    device_ctrl(_device),
    device(_device)
{
    bitrate_presets[QualityPresets::LOW] = LOW_QUAL_BITRATE;
    bitrate_presets[QualityPresets::MED] = MED_QUAL_BITRATE;
    bitrate_presets[QualityPresets::HIGH] = HIGH_QUAL_BITRATE;

    pipeline = nullptr;
    v4l2_src = nullptr;
    src_capsfilter = nullptr;
    videoconvert = nullptr;
//    h264_encoder = nullptr;
    h264_parser = nullptr;
    rtph264_payloader = nullptr;
    tee = nullptr;
    multi_udp_sink = nullptr;


    set_state_constants();
}

GenericAdaptiveStreaming::~GenericAdaptiveStreaming()
{
    // g_free(v4l2_src);
    // g_free(src_capsfilter);
    // g_free(videoconvert);
    // g_free(h264_encoder);
    // g_free(h264_parser);
    // g_free(rtph264_payloader);
    // g_free(text_overlay);
    // g_free(tee);
    // g_free(multi_udp_sink);
    // g_free(pipeline);
}

void GenericAdaptiveStreaming::set_state_constants()
{
    if (network_state == NetworkState::STEADY) {
        MAX_BITRATE = MAX_STEADY_BITRATE;
        MIN_BITRATE = MIN_STEADY_BITRATE;
        INC_BITRATE = INC_STEADY_BITRATE;
        DEC_BITRATE = DEC_STEADY_BITRATE;
    } else if (network_state == NetworkState::CONGESTION) {
        MAX_BITRATE = MAX_CONGESTION_BITRATE;
        MIN_BITRATE = MIN_CONGESTION_BITRATE;
        INC_BITRATE = INC_CONGESTION_BITRATE;
        DEC_BITRATE = DEC_CONGESTION_BITRATE;
    }
}

void GenericAdaptiveStreaming::adapt_stream()
{
    QoSReport qos_report;
    qos_report = qos_estimator.get_qos_report();

    if (successive_transmissions >= SUCCESSFUL_TRANSMISSION) {
        network_state = NetworkState::STEADY;
    }

    set_state_constants();

    // If we get a number of transmissions without any packet loss, we can increase bitrate
    if (qos_report.fraction_lost == 0) {
        successive_transmissions++;
        if (multi_udp_sink) {
            if (qos_report.encoding_bitrate < qos_report.estimated_bitrate * 1.5) {
                improve_quality();
            } else {
                cerr << "Buffer overflow possible!" << endl;
                degrade_quality();
            }
        } else {
            improve_quality();
        }
    } else {
        network_state = NetworkState::CONGESTION;
        successive_transmissions = 0;
        degrade_quality();
    }
}

void GenericAdaptiveStreaming::improve_quality()
{
    set_encoding_bitrate(h264_bitrate + INC_BITRATE);
}

void GenericAdaptiveStreaming::degrade_quality()
{
    set_encoding_bitrate(h264_bitrate - DEC_BITRATE);
}

void GenericAdaptiveStreaming::set_encoding_bitrate(uint32_t bitrate)
{
    string currstate = (network_state == NetworkState::STEADY) ? "STEADY" : "CONGESTED";

    if (bitrate >= MIN_BITRATE && bitrate <= MAX_BITRATE) {
        h264_bitrate = bitrate;
    } else if (h264_bitrate > MAX_BITRATE) {
        h264_bitrate = MAX_BITRATE;
    } else if (h264_bitrate < MIN_BITRATE) {
        h264_bitrate = MIN_BITRATE;
    }

    std::cout << "setting bitrate to " << h264_bitrate << std::endl;
    if(device_ctrl.set_bitrate(h264_bitrate*1000)) {
    	std::cout << "current bitrate = " << h264_bitrate << std::endl;
    }
}

void GenericAdaptiveStreaming::change_quality_preset(int quality)
{
    current_quality = quality;
    if (current_quality == AUTO_PRESET) {
        network_state = STEADY;
        set_state_constants();
        h264_bitrate = MIN_BITRATE;
        set_encoding_bitrate(bitrate_presets[QualityPresets::LOW]);
        successive_transmissions = 0;
    } else if (quality >= 0 && quality < NUM_QUALITY_PRESETS) {
		set_encoding_bitrate(bitrate_presets[quality]);
	}

}
