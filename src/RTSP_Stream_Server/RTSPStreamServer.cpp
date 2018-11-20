#include <sys/ioctl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/videodev2.h>
#include <dirent.h>
#include <string>
#include <string.h>
#include <iostream>
#include <fcntl.h>
#include <algorithm>

#include "RTSPStreamServer.h"

RTSPStreamServer::RTSPStreamServer(string _ip_addr, string _port) : ip_addr(_ip_addr), port(_port)
{
    server = gst_rtsp_server_new();
    gst_rtsp_server_set_address(server, ip_addr.c_str());
    gst_rtsp_server_set_service(server, port.c_str());

    get_v4l2_devices();
    get_v4l2_devices_info();
    setup_streams();
}

RTSPStreamServer::~RTSPStreamServer()
{
    for (auto stream_pair : adaptive_streams_map) {
        delete stream_pair.second;
    }
}

void RTSPStreamServer::get_v4l2_devices()
{
    DIR *dp;
    struct dirent *ep;
    dp = opendir(V4L2_DEVICE_PATH.c_str());
    if (dp == nullptr) {
        cerr << "Could not open directory " << errno << endl;
    }
    while ((ep = readdir(dp))) {
        string s = ep->d_name;
        if (s.find(V4L2_DEVICE_PREFIX) != std::string::npos) {
            s = V4L2_DEVICE_PATH + s;
            cout << "Found V4L2 camera device " << s << endl;
            // Add device path to list
            device_list.push_back(s);
        }
    }
    closedir(dp);
    // To make mount points and the device names in the same order
    reverse(device_list.begin(), device_list.end());
}

bool RTSPStreamServer::check_h264_ioctls(int fd)
{
    v4l2_queryctrl bitrate_ctrl;
    bitrate_ctrl.id = V4L2_CID_MPEG_VIDEO_BITRATE;

    if (ioctl(fd, VIDIOC_S_CTRL, &bitrate_ctrl) == -1) {
        return false;
    }
    return true;
}

void RTSPStreamServer::get_v4l2_devices_info()
{
    int i = 0;
    for (string dev : device_list) {
        cout << dev << endl;
        int fd = open(dev.c_str(), O_RDONLY);
        v4l2_info info;
        if (fd != -1) {
            v4l2_capability caps;
            ioctl(fd, VIDIOC_QUERYCAP, &caps);

            info.camera_name = string(caps.card, caps.card + sizeof caps.card / sizeof caps.card[0]);
            info.mount_point = MOUNT_POINT_PREFIX + to_string(i);
            info.frame_property_bitmask = 0;

            cout << "Name - " << caps.card << " Driver - " << caps.driver << endl;

            device_properties_map.insert(pair<string, v4l2_info>(dev, info));
            close(fd);
        }
        i++;
    }
}

void RTSPStreamServer::setup_streams()
{
    for (auto it = device_properties_map.begin(); it != device_properties_map.end(); it++) {
        adaptive_streams_map.insert(pair<string, RTSPAdaptiveStreaming*>(it->first,
                                    new RTSPAdaptiveStreaming(it->first,
                                            it->second.mount_point,
                                            server)));
    }
}

void RTSPStreamServer::set_service_id(guint id)
{
    service_id = id;
}

map<string, RTSPAdaptiveStreaming*> RTSPStreamServer::get_stream_map()
{
    return adaptive_streams_map;
}

map<string, v4l2_info> RTSPStreamServer::get_device_map()
{
    return device_properties_map;
}

GstRTSPServer* RTSPStreamServer::get_server()
{
    return server;
}

string RTSPStreamServer::get_ip_address()
{
    return ip_addr;
}

string RTSPStreamServer::get_port()
{
    return port;
}
