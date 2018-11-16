#include "FileRecorder.h"

FileRecorder::FileRecorder()
{
    recording = false;
    tee_file_pad = nullptr;
    queue_pad = nullptr;
    tee = nullptr;
    pipeline = nullptr;
}

bool FileRecorder::init_file_recorder(GstElement* _pipeline, GstElement* _tee)
{
    // In case we are already recording to a file, there's no need to reset the recorder
    if (recording) {
        cerr << "Recording in progress" << endl;
        return false;
    }
    pipeline = _pipeline;
    tee = _tee;
    if (!pipeline || !tee) {
        return false;
    }

    file_queue = gst_element_factory_make("queue", NULL);
    file_h264_parser = gst_element_factory_make("h264parse", NULL);
    mux = gst_element_factory_make("matroskamux", NULL);
    file_sink = gst_element_factory_make("filesink", NULL);

    auto t = std::time(nullptr);
    auto tm = *std::localtime(&t);
    char file_path [40];
    std::strftime(file_path, sizeof(file_path),"%Y%m%d_%H%M%S_recording.mkv",&tm);

    g_object_set(G_OBJECT(file_sink), "location", file_path, NULL);
    gst_bin_add_many(GST_BIN(pipeline), file_queue, file_h264_parser, mux, file_sink, NULL);
    gst_element_link_many(file_queue, file_h264_parser, mux, file_sink, NULL);

    gst_element_sync_state_with_parent(file_queue);
    gst_element_sync_state_with_parent(file_h264_parser);
    gst_element_sync_state_with_parent(mux);
    gst_element_sync_state_with_parent(file_sink);

    tee_file_pad = gst_element_get_request_pad(tee, "src_%u");
    queue_pad    = gst_element_get_static_pad(file_queue, "sink");
    gst_pad_link(tee_file_pad, queue_pad);

    recording = true;
    return true;
}

bool FileRecorder::disable_recorder()
{
    if (!recording) {
        cerr << "Recording not started" << endl;
        return false;
    }

    recording = false;

    gst_element_set_state(file_queue, GST_STATE_NULL);
    gst_element_set_state(file_h264_parser, GST_STATE_NULL);
    gst_element_set_state(mux, GST_STATE_NULL);
    gst_element_set_state(file_sink, GST_STATE_NULL);

    gst_bin_remove(GST_BIN(pipeline), file_queue);
    gst_bin_remove(GST_BIN(pipeline), file_h264_parser);
    gst_bin_remove(GST_BIN(pipeline), mux);
    gst_bin_remove(GST_BIN(pipeline), file_sink);

    gst_pad_unlink(tee_file_pad, queue_pad);
    gst_element_release_request_pad(tee, tee_file_pad);
    gst_object_unref(tee_file_pad);
    gst_object_unref(queue_pad);

    tee_file_pad = nullptr;
    queue_pad = nullptr;

    return true;
}

bool FileRecorder::get_recording()
{
    return recording;
}
