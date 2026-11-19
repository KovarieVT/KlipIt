#pragma once
#include <QImage>
#include <QString>
#include <QStringList>
#include <functional>

namespace kl::media {

// The hard limit for any single saved clip, recording part or exported edit.
constexpr double kMaxClipSeconds = 1200.0;   // 20 minutes
// Long recordings are cut a little under the limit, so a part can never run over
// even when it has to start on the keyframe just before the cut point.
constexpr double kSplitSeconds = 1195.0;

struct Info {
    bool ok = false;
    QString error;
    double duration = 0;
    int width = 0, height = 0;
    double fps = 0;
    int audioTracks = 0;
    QString videoCodec;
};

Info probe(const QString &path);
QImage thumbnail(const QString &path, double atSeconds = 1.0, int width = 480);

// Copies [start, end] of `src` into `dst` without re-encoding (instant, no quality loss).
// The cut starts on the keyframe at or just before `start`. Refuses anything over 20 minutes.
// `progress` gets 0..1 and can return false to cancel.
bool cut(const QString &src, const QString &dst, double start, double end, QString *error = nullptr,
         const std::function<bool(double)> &progress = {});

// Splits a long video into back-to-back parts of at most 20 minutes, losing nothing.
// Returns the part files, named "<base> (part N).<ext>" in `outDir`.
QStringList splitIntoParts(const QString &src, const QString &outDir, const QString &baseName, QString *error = nullptr,
                           const std::function<bool(double)> &progress = {}, double partSeconds = kSplitSeconds);

// Copies a finished MKV recording into MP4 (no re-encode).
bool remux(const QString &src, const QString &dst, QString *error = nullptr);

QString formatDuration(double seconds);   // 0:07, 12:03, 1:02:44

} // namespace kl::media
