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

            v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            v4l2_fmtdesc fmt;
            v4l2_frmsizeenum frmsize;
            v4l2_frmivalenum frmival;

            memset(&fmt, 0, sizeof(fmt));
            memset(&frmsize, 0, sizeof(frmsize));
            memset(&frmival, 0, sizeof(frmival));

            fmt.index = 0;
            fmt.type = type;

            while (ioctl(fd, VIDIOC_ENUM_FMT, &fmt) >= 0) {
                if (fmt.pixelformat == V4L2_PIX_FMT_MJPEG) {
                	info.camera_type = RAW_CAM;
                    break;
                }
                if (fmt.pixelformat == V4L2_PIX_FMT_H264 || fmt.pixelformat == V4L2_PIX_FMT_H264_MVC) {
                    if (check_h264_ioctls(fd)) {
                        info.camera_type = H264_CAM;
                    } else {
                        info.camera_type = UVC_CAM;
                    }
                    break;
                }
                fmt.index++;
            }

            frmsize.pixel_format = fmt.pixelformat;

            cout << "capabilites: " << endl;
            for (frmsize.index = 0; ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) >= 0; frmsize.index++) {
                if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
                    FramePresets preset;
                    if (frmsize.discrete.width == 320 && frmsize.discrete.height == 240) {
                        preset = FRAME_320x240;
                    } else if (frmsize.discrete.width == 640 && frmsize.discrete.height == 480) {
                        preset = FRAME_640x480;
                    } else if (frmsize.discrete.width == 1280 && frmsize.discrete.height == 720) {
                        preset = FRAME_1280x720;
                    } else if (frmsize.discrete.width == 1920 && frmsize.discrete.height == 1080) {
                        preset = FRAME_1920x1080;
                    } else {
                        continue;
                    }
                	cout << " " << frmsize.discrete.width << "x" << frmsize.discrete.height << " @ ";

                    frmival.pixel_format = frmsize.pixel_format;
                    frmival.width = frmsize.discrete.width;
                    frmival.height = frmsize.discrete.height;

                    for (frmival.index = 0; ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &frmival) >= 0; frmival.index++) {
                        int framerate  = frmival.discrete.denominator;
                        if (frmival.index > 0) {
                        	cout << ", ";
                        }
                        cout << framerate;
                        // For simplicity in maintenance, we only support 240p, 480p
                        // and 720p. If a camera supports a resolution we use a bitmask
                        // for saving its capabilities as it greatly simplifies our IPC
                        switch (preset) {
                        case FRAME_320x240:
                            if (framerate == 10) {
                                info.frame_property_bitmask |= (1 << VIDEO_320x240x10);
                            } else if (framerate == 20) {
                                info.frame_property_bitmask |= (1 << VIDEO_320x240x20);
                            } else if (framerate == 30) {
                                info.frame_property_bitmask |= (1 << VIDEO_320x240x30);
                            }
                            break;
                        case FRAME_640x480:
                            if (framerate == 10) {
                                info.frame_property_bitmask |= (1 << VIDEO_640x480x10);
                            } else if (framerate == 20) {
                                info.frame_property_bitmask |= (1 << VIDEO_640x480x20);
                            } else if (framerate == 30) {
                                info.frame_property_bitmask |= (1 << VIDEO_640x480x30);
                            }
                            break;
                        case FRAME_1280x720:
                            if (framerate == 10) {
                                info.frame_property_bitmask |= (1 << VIDEO_1280x720x10);
                            } else if (framerate == 20) {
                                info.frame_property_bitmask |= (1 << VIDEO_1280x720x20);
                            } else if (framerate == 30) {
                                info.frame_property_bitmask |= (1 << VIDEO_1280x720x30);
                            }
                            break;
                        case FRAME_1920x1080:
                            if (framerate == 10) {
                                info.frame_property_bitmask |= (1 << VIDEO_1920x1080x10);
                            } else if (framerate == 20) {
                                info.frame_property_bitmask |= (1 << VIDEO_1920x1080x20);
                            } else if (framerate == 30) {
                                info.frame_property_bitmask |= (1 << VIDEO_1920x1080x30);
                            }
                            break;
                        default:
                            ;
                        }
                    }

                    // The PiCam doesn't list the resolutions explicitly, so we have
                    // to guess its capabilities
                } else if (frmsize.type == V4L2_FRMSIZE_TYPE_STEPWISE) {
                    // How do I get the framerates for stepwise cams?
                    info.frame_property_bitmask |= (1 << VIDEO_320x240x10);
                    info.frame_property_bitmask |= (1 << VIDEO_320x240x20);
                    info.frame_property_bitmask |= (1 << VIDEO_320x240x30);

                    info.frame_property_bitmask |= (1 << VIDEO_640x480x10);
                    info.frame_property_bitmask |= (1 << VIDEO_640x480x20);
                    info.frame_property_bitmask |= (1 << VIDEO_640x480x30);

                    info.frame_property_bitmask |= (1 << VIDEO_1280x720x10);
                    info.frame_property_bitmask |= (1 << VIDEO_1280x720x20);
                    info.frame_property_bitmask |= (1 << VIDEO_1280x720x30);

                    info.frame_property_bitmask |= (1 << VIDEO_1920x1080x10);
                    info.frame_property_bitmask |= (1 << VIDEO_1920x1080x20);
                    info.frame_property_bitmask |= (1 << VIDEO_1920x1080x30);
                    break;
                }

                cout << " fps" << endl;
            }

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
                                            it->second.camera_type,
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
