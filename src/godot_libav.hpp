#include <godot_cpp/classes/video_stream.hpp>
#include <godot_cpp/classes/video_stream_playback.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_format_loader.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>

#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

using namespace godot;

class VideoStreamPlaybackLibAV : public VideoStreamPlayback {
    GDCLASS(VideoStreamPlaybackLibAV, VideoStreamPlayback)

public:
    VideoStreamPlaybackLibAV();

    void set_file(String file);

    virtual bool _is_playing() const override;
    virtual void _update(double delta) override;
    virtual Ref<Texture2D> _get_texture() const override;

    virtual int32_t _get_channels() const override;
    virtual int32_t _get_mix_rate() const override;
protected:
    static void _bind_methods();

private:
    double time = 0;
    int64_t last_pts = 0;

    int32_t channels = 0;
    int32_t mixRate = 0;

    AVFormatContext *formatContext = nullptr;

    const AVCodec *videoCodec = nullptr, *audioCodec = nullptr;
    const AVCodecParameters *videoCodecParameters = nullptr,
        *audioCodecParameters = nullptr;
    int videoIndex = -1, audioIndex = -1;
    AVCodecContext *videoCodecContext = nullptr, *audioCodecContext = nullptr;

    SwsContext *sws;
    SwrContext *swr;

    Ref<ImageTexture> texture;
};

class VideoStreamLibAV : public VideoStream {
    GDCLASS(VideoStreamLibAV, VideoStream)

protected:
    static void _bind_methods();

public:
    Ref<VideoStreamPlayback> _instantiate_playback() override {
        Ref<VideoStreamPlaybackLibAV> pb = memnew(VideoStreamPlaybackLibAV);
        pb->set_file(get_file());

        return pb;
    }
};

class ResourceFormatLoaderLibAV : public ResourceFormatLoader {
    GDCLASS(ResourceFormatLoaderLibAV, ResourceFormatLoader)

public:
    virtual Variant _load(const String &path, const String &original_path, bool use_sub_threads, int32_t cache_mode) const override;
    virtual PackedStringArray _get_recognized_extensions() const override;
    virtual bool _handles_type(const StringName &type) const override;
    virtual String _get_resource_type(const String &p_path) const override;

protected:
    static void _bind_methods();
};
