#include "core/MediaTools.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <cmath>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

namespace kl::media {

// FFmpeg wants UTF-8 file names on Windows (it converts them to wide strings itself).
static QByteArray ffPath(const QString &p) {
#ifdef _WIN32
    return p.toUtf8();
#else
    return QFile::encodeName(p);
#endif
}

static QString averr(int code) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

struct InCtx {
    AVFormatContext *f = nullptr;
    ~InCtx() { if (f) avformat_close_input(&f); }
    bool open(const QString &path, QString *err) {
        int r = avformat_open_input(&f, ffPath(path).constData(), nullptr, nullptr);
        if (r < 0) { if (err) *err = QObject::tr("Can't open %1 (%2)").arg(QFileInfo(path).fileName(), averr(r)); return false; }
        r = avformat_find_stream_info(f, nullptr);
        if (r < 0) { if (err) *err = QObject::tr("Can't read %1 (%2)").arg(QFileInfo(path).fileName(), averr(r)); return false; }
        return true;
    }
};

static double streamSeconds(const AVStream *st, int64_t ts) { return ts == AV_NOPTS_VALUE ? NAN : double(ts) * av_q2d(st->time_base); }

Info probe(const QString &path) {
    Info info;
    InCtx in;
    if (!in.open(path, &info.error)) return info;
    if (in.f->duration > 0) info.duration = double(in.f->duration) / AV_TIME_BASE;
    for (unsigned i = 0; i < in.f->nb_streams; ++i) {
        const AVStream *st = in.f->streams[i];
        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && !(st->disposition & AV_DISPOSITION_ATTACHED_PIC) && info.width == 0) {
            info.width = st->codecpar->width;
            info.height = st->codecpar->height;
            const AVRational fr = st->avg_frame_rate.num ? st->avg_frame_rate : st->r_frame_rate;
            info.fps = fr.den ? av_q2d(fr) : 0;
            info.videoCodec = QString::fromUtf8(avcodec_get_name(st->codecpar->codec_id));
            if (info.duration <= 0 && st->duration > 0) info.duration = streamSeconds(st, st->duration);
        } else if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            ++info.audioTracks;
        }
    }
    info.ok = info.width > 0 || info.audioTracks > 0;
    if (!info.ok) info.error = QObject::tr("No video in this file.");
    return info;
}

QImage thumbnail(const QString &path, double at, int width) {
    InCtx in;
    if (!in.open(path, nullptr)) return {};
    const int vi = av_find_best_stream(in.f, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vi < 0) return {};
    AVStream *st = in.f->streams[vi];
    const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) return {};
    AVCodecContext *dec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(dec, st->codecpar);
    dec->thread_count = 2;
    QImage out;
    if (avcodec_open2(dec, codec, nullptr) == 0) {
        const double dur = in.f->duration > 0 ? double(in.f->duration) / AV_TIME_BASE : 0;
        if (dur > 0 && at > dur * 0.5) at = dur * 0.25;
        if (at > 0) av_seek_frame(in.f, -1, int64_t(at * AV_TIME_BASE), AVSEEK_FLAG_BACKWARD);
        AVPacket *pkt = av_packet_alloc();
        AVFrame *frame = av_frame_alloc();
        int guard = 0;
        while (out.isNull() && guard++ < 600 && av_read_frame(in.f, pkt) >= 0) {
            if (pkt->stream_index == vi && avcodec_send_packet(dec, pkt) == 0) {
                while (avcodec_receive_frame(dec, frame) == 0) {
                    const int w = width > 0 ? width : frame->width;
                    const int h = qMax(2, int(std::lround(double(frame->height) * w / qMax(1, frame->width))) & ~1);
                    SwsContext *sws = sws_getContext(frame->width, frame->height, AVPixelFormat(frame->format), w, h,
                                                     AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr);
                    if (sws) {
                        QImage img(w, h, QImage::Format_RGB888);
                        uint8_t *dst[4] = {img.bits(), nullptr, nullptr, nullptr};
                        int dstStride[4] = {int(img.bytesPerLine()), 0, 0, 0};
                        sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst, dstStride);
                        sws_freeContext(sws);
                        out = img;
                    }
                    break;
                }
            }
            av_packet_unref(pkt);
        }
        av_frame_free(&frame);
        av_packet_free(&pkt);
    }
    avcodec_free_context(&dec);
    return out;
}

static bool cutImpl(const QString &src, const QString &dst, double start, double end, QString *error,
                    const std::function<bool(double)> &progress, bool enforceLimit) {
    auto fail = [&](const QString &m) { if (error) *error = m; return false; };
    if (start < 0) start = 0;
    if (end <= start) return fail(QObject::tr("The end has to be after the start."));
    if (enforceLimit && end - start > kMaxClipSeconds + 0.001)
        return fail(QObject::tr("A clip can be at most 20 minutes long. This one would be %1.").arg(formatDuration(end - start)));

    InCtx in;
    QString err;
    if (!in.open(src, &err)) return fail(err);
    const QByteArray outName = ffPath(dst);
    AVFormatContext *out = nullptr;
    int r = avformat_alloc_output_context2(&out, nullptr, nullptr, outName.constData());
    if (r < 0 || !out) return fail(QObject::tr("Can't create %1 (%2)").arg(QFileInfo(dst).fileName(), averr(r)));

    std::vector<int> map(in.f->nb_streams, -1);
    int videoIn = -1, nOut = 0;
    for (unsigned i = 0; i < in.f->nb_streams; ++i) {
        AVStream *ist = in.f->streams[i];
        const auto type = ist->codecpar->codec_type;
        if (type != AVMEDIA_TYPE_VIDEO && type != AVMEDIA_TYPE_AUDIO) continue;
        if (type == AVMEDIA_TYPE_VIDEO && (ist->disposition & AV_DISPOSITION_ATTACHED_PIC)) continue;
        AVStream *ost = avformat_new_stream(out, nullptr);
        avcodec_parameters_copy(ost->codecpar, ist->codecpar);
        ost->codecpar->codec_tag = 0;
        ost->time_base = ist->time_base;
        av_dict_copy(&ost->metadata, ist->metadata, 0);
        map[i] = nOut++;
        if (type == AVMEDIA_TYPE_VIDEO && videoIn < 0) videoIn = int(i);
    }
    if (nOut == 0) { avformat_free_context(out); return fail(QObject::tr("No video or audio to copy.")); }
    if (!(out->oformat->flags & AVFMT_NOFILE) && (r = avio_open(&out->pb, outName.constData(), AVIO_FLAG_WRITE)) < 0) {
        avformat_free_context(out);
        return fail(QObject::tr("Can't write %1 (%2)").arg(QFileInfo(dst).fileName(), averr(r)));
    }
    AVDictionary *opts = nullptr;
    if (QFileInfo(dst).suffix().compare("mp4", Qt::CaseInsensitive) == 0 || QFileInfo(dst).suffix().compare("mov", Qt::CaseInsensitive) == 0)
        av_dict_set(&opts, "movflags", "+faststart", 0);
    r = avformat_write_header(out, &opts);
    av_dict_free(&opts);
    if (r < 0) {
        if (!(out->oformat->flags & AVFMT_NOFILE)) avio_closep(&out->pb);
        avformat_free_context(out);
        QFile::remove(dst);
        return fail(QObject::tr("Can't write %1 (%2)").arg(QFileInfo(dst).fileName(), averr(r)));
    }
    if (start > 0) av_seek_frame(in.f, -1, int64_t(start * AV_TIME_BASE), AVSEEK_FLAG_BACKWARD);

    AVPacket *pkt = av_packet_alloc();
    bool started = false, ok = true, cancelled = false;
    double zero = 0;   // input time (seconds) that becomes 0 in the output
    std::vector<int64_t> lastDts(size_t(nOut), INT64_MIN);
    std::vector<bool> done(size_t(nOut), false);
    int finished = 0;
    while (av_read_frame(in.f, pkt) >= 0) {
        const int si = pkt->stream_index;
        const int oi = si >= 0 && si < int(map.size()) ? map[size_t(si)] : -1;
        if (oi < 0 || done[size_t(oi)]) { av_packet_unref(pkt); continue; }
        AVStream *ist = in.f->streams[si];
        const int64_t ts = pkt->pts != AV_NOPTS_VALUE ? pkt->pts : pkt->dts;
        const double t = streamSeconds(ist, ts);
        if (std::isnan(t)) { av_packet_unref(pkt); continue; }
        if (!started) {
            const bool isVideo = si == videoIn;
            if (videoIn >= 0 && !(isVideo && (pkt->flags & AV_PKT_FLAG_KEY))) { av_packet_unref(pkt); continue; }
            if (videoIn < 0 && t < start) { av_packet_unref(pkt); continue; }
            started = true;
            zero = qMin(t, std::isnan(streamSeconds(ist, pkt->dts)) ? t : streamSeconds(ist, pkt->dts));
        }
        if (t < zero - 0.0005) { av_packet_unref(pkt); continue; }   // audio from before the first frame
        if (t > end) {
            done[size_t(oi)] = true;
            av_packet_unref(pkt);
            if (++finished == nOut) break;
            continue;
        }
        AVStream *ost = out->streams[oi];
        const int64_t off = std::llround(zero / av_q2d(ist->time_base));
        if (pkt->pts != AV_NOPTS_VALUE) pkt->pts -= off;
        if (pkt->dts != AV_NOPTS_VALUE) pkt->dts -= off;
        av_packet_rescale_ts(pkt, ist->time_base, ost->time_base);
        if (pkt->dts != AV_NOPTS_VALUE) {
            int64_t &last = lastDts[size_t(oi)];
            if (last != INT64_MIN && pkt->dts <= last) {
                pkt->dts = last + 1;
                if (pkt->pts != AV_NOPTS_VALUE && pkt->pts < pkt->dts) pkt->pts = pkt->dts;
            }
            last = pkt->dts;
        }
        pkt->stream_index = oi;
        pkt->pos = -1;
        r = av_interleaved_write_frame(out, pkt);
        if (r < 0) { ok = false; err = QObject::tr("Writing failed (%1)").arg(averr(r)); break; }
        if (progress && !progress(qBound(0.0, (t - start) / qMax(0.001, end - start), 1.0))) { cancelled = true; break; }
    }
    av_packet_free(&pkt);
    av_write_trailer(out);
    if (!(out->oformat->flags & AVFMT_NOFILE)) avio_closep(&out->pb);
    avformat_free_context(out);
    if (!started) { ok = false; err = QObject::tr("Nothing to copy in that range."); }
    if (cancelled) { ok = false; err = QObject::tr("Cancelled."); }
    if (!ok) { QFile::remove(dst); return fail(err); }
    return true;
}

bool cut(const QString &src, const QString &dst, double start, double end, QString *error,
         const std::function<bool(double)> &progress) {
    return cutImpl(src, dst, start, end, error, progress, true);
}

QStringList splitIntoParts(const QString &src, const QString &outDir, const QString &baseName, QString *error,
                           const std::function<bool(double)> &progress, double partSeconds) {
    QStringList parts;
    const Info info = probe(src);
    if (!info.ok) { if (error) *error = info.error; return parts; }
    partSeconds = qBound(1.0, partSeconds, kSplitSeconds);
    const QString ext = QFileInfo(src).suffix().isEmpty() ? QStringLiteral("mp4") : QFileInfo(src).suffix();
    const int n = qMax(1, int(std::ceil(info.duration / partSeconds)));
    QDir().mkpath(outDir);
    for (int k = 0; k < n; ++k) {
        const double a = k * partSeconds, b = qMin(info.duration + 1.0, (k + 1) * partSeconds);
        const QString dst = QDir(outDir).filePath(QStringLiteral("%1 (part %2).%3").arg(baseName).arg(k + 1).arg(ext));
        QString err;
        if (!cut(src, dst, a, b, &err, [&](double f) { return progress ? progress((k + f) / n) : true; })) {
            if (error) *error = err;
            for (const auto &p : parts) QFile::remove(p);
            return {};
        }
        parts << dst;
    }
    return parts;
}

bool remux(const QString &src, const QString &dst, QString *error) {
    const Info info = probe(src);
    if (!info.ok) { if (error) *error = info.error; return false; }
    // a copy of the whole file: never shortened (recordings are already split to 20 minutes by the recorder)
    return cutImpl(src, dst, 0, info.duration + 60.0, error, {}, false);
}

QString formatDuration(double s) {
    const int t = int(std::lround(qMax(0.0, s)));
    if (t >= 3600) return QStringLiteral("%1:%2:%3").arg(t / 3600).arg(t / 60 % 60, 2, 10, QChar('0')).arg(t % 60, 2, 10, QChar('0'));
    return QStringLiteral("%1:%2").arg(t / 60).arg(t % 60, 2, 10, QChar('0'));
}

} // namespace kl::media
