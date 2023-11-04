#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>

#include "godot_libav.hpp"

VideoStreamPlaybackLibAV::VideoStreamPlaybackLibAV() {
    texture = Ref<ImageTexture>(memnew(ImageTexture));

    formatContext = avformat_alloc_context();
}

void VideoStreamPlaybackLibAV::_bind_methods() { }

void VideoStreamPlaybackLibAV::set_file(String file) {
    String path = ProjectSettings::get_singleton()->globalize_path(file);
    if (avformat_open_input(&formatContext, path.utf8().get_data(), nullptr, nullptr) != 0) {
        ERR_PRINT("Could not open the file");
        return;
    }
    if (avformat_find_stream_info(formatContext,  NULL) < 0) {
        ERR_PRINT("Could not get the stream info");
        return;
    }

    videoIndex = av_find_best_stream(formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, &videoCodec, 0);
    videoCodecParameters = formatContext->streams[videoIndex]->codecpar;
    audioIndex = av_find_best_stream(formatContext, AVMEDIA_TYPE_AUDIO, -1, videoIndex, &audioCodec, 0);
    audioCodecParameters = formatContext->streams[audioIndex]->codecpar;

    videoCodecContext = avcodec_alloc_context3(videoCodec);
    avcodec_parameters_to_context(videoCodecContext, videoCodecParameters);
    avcodec_open2(videoCodecContext, videoCodec, nullptr);

    audioCodecContext = avcodec_alloc_context3(audioCodec);
    avcodec_parameters_to_context(audioCodecContext, audioCodecParameters);
    avcodec_open2(audioCodecContext, audioCodec, nullptr);

    channels = audioCodecContext->ch_layout.nb_channels;
    mixRate = audioCodecContext->sample_rate;

    sws = sws_getContext(
        videoCodecContext->width, videoCodecContext->height, videoCodecContext->pix_fmt,
        videoCodecContext->width, videoCodecContext->height, AV_PIX_FMT_RGB32,
        SWS_BICUBIC, nullptr, nullptr, nullptr
    );
    // TODO: change from bicubic to a parameter

    swr = swr_alloc();
    swr_alloc_set_opts2(
        &swr,
        &audioCodecContext->ch_layout, (AVSampleFormat)AV_SAMPLE_FMT_FLT, audioCodecContext->sample_rate,
        &audioCodecContext->ch_layout, audioCodecContext->sample_fmt, audioCodecContext->sample_rate,
        0, nullptr
    );
    swr_init(swr);
}

Ref<Texture2D> VideoStreamPlaybackLibAV::_get_texture() const { return texture; }

bool check(int response) {
    if (response < 0) {
        char errbuf[1024];
        av_make_error_string(errbuf, 1024, response);
        ERR_PRINT(errbuf);

        return true;
    }

    return false;
}

int32_t VideoStreamPlaybackLibAV::_get_channels() const {
    return channels;
}

int32_t VideoStreamPlaybackLibAV::_get_mix_rate() const {
    return mixRate;
}

void VideoStreamPlaybackLibAV::_update(double delta) {
    auto decode_packet = [this](
        AVPacket *packet, AVCodecContext *codecContext, AVFrame *frame
    ) {
        // Supply raw packet data as input to a decoder
        // https://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html#ga58bc4bf1e0ac59e27362597e467efff3
        int response = avcodec_send_packet(codecContext, packet);

        if (check(response))
            return response;

        while (response >= 0) {
            // Return decoded output data (into a frame) from a decoder

            response = avcodec_receive_frame(codecContext, frame);
            if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
                break;
            } else if (check(response))
                return response;

            if (response >= 0) {
                if (codecContext->codec_type == AVMEDIA_TYPE_VIDEO) {
                    //declare destination frame
                    AVFrame* frame_rgb8 = av_frame_alloc();
                    frame_rgb8->width = frame->width;
                    frame_rgb8->height = frame->height;
                    frame_rgb8->format = AV_PIX_FMT_RGB32;

                    int num_bytes = av_image_get_buffer_size(AV_PIX_FMT_RGB32, frame->width, frame->height, 1);
                    PackedByteArray dataArr;
                    dataArr.resize(num_bytes);

                    response =
                        av_image_fill_arrays(frame_rgb8->data, frame_rgb8->linesize, dataArr.ptrw(), AV_PIX_FMT_RGB32, frame->width, frame->height, 1);
                    if (check(response))
                        return response;

                    sws_scale(sws,
                              frame->data, frame->linesize, 0, frame->height,
                              frame_rgb8->data, frame_rgb8->linesize
                    );

                    Ref<Image> img = Image::create_from_data(frame->width, frame->height, false, Image::FORMAT_RGBA8, dataArr);
                    if (texture->get_image() == nullptr)
                        texture->set_image(img);
                    else
                        texture->update(img);

                    av_frame_unref(frame_rgb8);
                } else if (codecContext->codec_type == AVMEDIA_TYPE_AUDIO) {
                    int num_bytes = av_samples_get_buffer_size(
                        nullptr, frame->ch_layout.nb_channels, frame->nb_samples, (AVSampleFormat)AV_SAMPLE_FMT_FLT, 1
                    );
                    PackedFloat32Array dataArr;
                    dataArr.resize(num_bytes);

                    AVFrame* frame_flt = av_frame_alloc();
                    frame_flt->format = AV_SAMPLE_FMT_FLT;

                    response =
                        av_samples_fill_arrays(
                            (uint8_t**)&frame_flt->data, frame_flt->linesize, (const uint8_t *)dataArr.ptrw(),
                            frame->ch_layout.nb_channels, frame->nb_samples, (AVSampleFormat)AV_SAMPLE_FMT_FLT,
                            1
                        );
                    if (check(response)) return response;

                    response = swr_convert(
                        swr,
                        (uint8_t**)&frame_flt->data, frame->nb_samples,
                        (const uint8_t**)&frame->data, frame->nb_samples);
                    if (check(response)) return response;

                    mix_audio(frame->nb_samples, dataArr, 0);

                    av_frame_unref(frame_flt);
                }
            }
        }

        return 0;
    };

    time += delta;

    AVStream *stream = formatContext->streams[videoIndex];
    AVFrame *frame = av_frame_alloc();
    AVPacket *packet = av_packet_alloc();

    int response = 0;

    AVStream *audioStream = formatContext->streams[audioIndex];
    AVFrame *audioFrame = av_frame_alloc();

    // Fill the Packet with data from the Stream
    while (av_read_frame(formatContext, packet) >= 0) {
        if (packet->stream_index == videoIndex) {
            response = decode_packet(packet, videoCodecContext, frame);
            if (response < 0)
                break;

            if ((packet->pts * ((double)stream->time_base.num / stream->time_base.den)) > time)
                break;
        } else if (packet->stream_index == audioIndex) {
            response = decode_packet(packet, audioCodecContext, audioFrame);
            if (response < 0)
                break;

            //if ((packet->pts * ((double)audioStream->time_base.num / audioStream->time_base.den)) > time)
            //    break;
        }

        av_packet_unref(packet);
    }

    av_frame_unref(frame);
}

bool VideoStreamPlaybackLibAV::_is_playing() const {
    return true;
}

void VideoStreamLibAV::_bind_methods() {
}

Variant ResourceFormatLoaderLibAV::_load(
    const String &path, const String &original_path,
    bool use_sub_threads, int32_t cache_mode) const {


    Ref<FileAccess> f = FileAccess::open(path, FileAccess::READ);
    ERR_FAIL_NULL_V_MSG(f, Ref<Resource>(), ERR_CANT_OPEN);

    VideoStreamLibAV *stream = memnew(VideoStreamLibAV);
    stream->set_file(path);

    return Ref<VideoStreamLibAV>(stream);
}

PackedStringArray ResourceFormatLoaderLibAV::_get_recognized_extensions() const {
    PackedStringArray result;
    result.push_back("mp4");
    result.push_back("webm");

    return result;
}

bool ResourceFormatLoaderLibAV::_handles_type(const StringName &p_type) const {
    return p_type == StringName("VideoStream");
}

String ResourceFormatLoaderLibAV::_get_resource_type(const String &p_path) const {
    String el = p_path.get_extension().to_lower();
    if (el == "mp4" || el == "webm") {
        return "VideoStreamLibAV";
    }
    return "";
}

void ResourceFormatLoaderLibAV::_bind_methods() { }
