#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>

#include "godot_libav.hpp"

const AVPixelFormat pixelFormat = AV_PIX_FMT_RGBA;
const Image::Format godotPixelFormat = Image::FORMAT_RGBA8;

VideoStreamPlaybackLibAV::VideoStreamPlaybackLibAV() {
    texture = Ref<ImageTexture>(memnew(ImageTexture));
}

void VideoStreamPlaybackLibAV::_bind_methods() { }

void VideoStreamPlaybackLibAV::set_file(String file) {
    this->file = file;
    time = 0;
    last_pts = 0;

    if (formatContext != nullptr) {
        avformat_free_context(formatContext);

        avcodec_free_context(&videoCodecContext);
        avcodec_free_context(&audioCodecContext);
    }

    formatContext = avformat_alloc_context();
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

    videoStream = formatContext->streams[videoIndex];

    if (frame != nullptr) {
        av_frame_free(&frame);
        av_frame_free(&rgb8Frame);
        av_frame_free(&audioFrame);
        av_frame_free(&fltFrame);
        sws_freeContext(sws);
        swr_free(&swr);
        av_packet_free(&packet);
    }

    // Video frame
    frame = av_frame_alloc();
    // Frame for RGB8 conversion
    rgb8Frame = av_frame_alloc();
    rgb8Frame->width = frame->width;
    rgb8Frame->height = frame->height;
    rgb8Frame->format = pixelFormat;

    audioCodecContext = avcodec_alloc_context3(audioCodec);
    avcodec_parameters_to_context(audioCodecContext, audioCodecParameters);
    avcodec_open2(audioCodecContext, audioCodec, nullptr);

    audioStream = formatContext->streams[audioIndex];
    audioFrame = av_frame_alloc();
    // Frame for non-planar float conversion
    fltFrame = av_frame_alloc();
    fltFrame->format = AV_SAMPLE_FMT_FLT;

    channels = audioCodecContext->ch_layout.nb_channels;
    mixRate = audioCodecContext->sample_rate;

    sws = sws_getContext(
        videoCodecContext->width, videoCodecContext->height, videoCodecContext->pix_fmt,
        videoCodecContext->width, videoCodecContext->height, pixelFormat,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    swr = swr_alloc();
    swr_alloc_set_opts2(
        &swr,
        &audioCodecContext->ch_layout, (AVSampleFormat)AV_SAMPLE_FMT_FLT, audioCodecContext->sample_rate,
        &audioCodecContext->ch_layout, audioCodecContext->sample_fmt, audioCodecContext->sample_rate,
        0, nullptr
    );
    swr_init(swr);

    packet = av_packet_alloc();
}

Ref<Texture2D> VideoStreamPlaybackLibAV::_get_texture() const { return texture; }

int32_t VideoStreamPlaybackLibAV::_get_channels() const {
    return channels;
}

int32_t VideoStreamPlaybackLibAV::_get_mix_rate() const {
    return mixRate;
}

void VideoStreamPlaybackLibAV::_play() {
    playing = true;
}

void VideoStreamPlaybackLibAV::_stop() {
    playing = false;
    paused = false;

    set_file(file);
}

double VideoStreamPlaybackLibAV::_get_length() const {
    return videoStream->duration * ((double)videoStream->time_base.num / videoStream->time_base.den);
}

double VideoStreamPlaybackLibAV::_get_playback_position() const {
    return last_pts * ((double)videoStream->time_base.num / videoStream->time_base.den);
}

bool VideoStreamPlaybackLibAV::_is_paused() const {
    return paused;
}
void VideoStreamPlaybackLibAV::_set_paused(bool pause) {
    paused = pause;
}

void VideoStreamPlaybackLibAV::_update(double delta) {
    if (!playing || paused) return;

    auto check = [](int response) {
        if (response < 0) {
            char errbuf[1024];
            av_make_error_string(errbuf, 1024, response);
            ERR_PRINT(errbuf);

            return true;
        }

        return false;
    };
    auto decode_packet = [this, &check](
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
                    int num_bytes = av_image_get_buffer_size(pixelFormat, frame->width, frame->height, 1);
                    PackedByteArray dataArr;
                    dataArr.resize(num_bytes);

                    response =
                        av_image_fill_arrays(rgb8Frame->data, rgb8Frame->linesize, dataArr.ptrw(), pixelFormat, frame->width, frame->height, 1);
                    if (check(response)) {
                        return response;
                    }

                    sws_scale(sws,
                              frame->data, frame->linesize, 0, frame->height,
                              rgb8Frame->data, rgb8Frame->linesize
                    );

                    Ref<Image> img = Image::create_from_data(frame->width, frame->height, false, godotPixelFormat, dataArr);
                    if (texture->get_image() == nullptr)
                        texture->set_image(img);
                    else
                        texture->update(img);
                } else if (codecContext->codec_type == AVMEDIA_TYPE_AUDIO) {
                    int num_bytes = av_samples_get_buffer_size(
                        nullptr, frame->ch_layout.nb_channels, frame->nb_samples, (AVSampleFormat)AV_SAMPLE_FMT_FLT, 1
                    );
                    PackedFloat32Array dataArr;
                    dataArr.resize(num_bytes);

                    response =
                        av_samples_fill_arrays(
                            (uint8_t**)&fltFrame->data, fltFrame->linesize, (const uint8_t *)dataArr.ptrw(),
                            frame->ch_layout.nb_channels, frame->nb_samples, (AVSampleFormat)AV_SAMPLE_FMT_FLT,
                            1
                        );
                    if (check(response)) return response;

                    response = swr_convert(
                        swr,
                        (uint8_t**)&fltFrame->data, frame->nb_samples,
                        (const uint8_t**)&frame->data, frame->nb_samples);
                    if (check(response)) return response;

                    mix_audio(frame->nb_samples, dataArr, 0);
                }
            }
        }

        return 0;
    };

    time += delta;

    int response = 0;
    // Fill the Packet with data from the Stream
    while (av_read_frame(formatContext, packet) >= 0) {
        if (packet->stream_index == videoIndex) {
            response = decode_packet(packet, videoCodecContext, frame);
            if (response < 0)
                break;

            if ((packet->pts * ((double)videoStream->time_base.num / videoStream->time_base.den)) > time)
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
    if (response == AVERROR_EOF) {
        playing = false;
        paused = false;
        texture->update(Image::create(frame->width, frame->height, false, godotPixelFormat));
    }

    av_frame_unref(frame);
    av_frame_unref(audioFrame);
    av_frame_unref(rgb8Frame);
    av_frame_unref(fltFrame);
}

bool VideoStreamPlaybackLibAV::_is_playing() const {
    return playing;
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
