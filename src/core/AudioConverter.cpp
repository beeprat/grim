#include "core/AudioConverter.h"

#include <fileref.h>
#include <tpropertymap.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

// AAC in MP4 tops out at 96 kHz in ffmpeg's encoder and MP3 at 48 kHz; resample hi-res sources
// to 48 kHz for both. FLAC keeps any rate.
constexpr int kMaxSampleRate = 48'000;

// Copies every tag (including freeform ones like REPLAYGAIN_*) and embedded pictures.
bool copyTags(const QString &sourcePath, const QString &targetPath)
{
    const std::filesystem::path source = QFileInfo(sourcePath).filesystemFilePath();
    const std::filesystem::path target = QFileInfo(targetPath).filesystemFilePath();
    const TagLib::FileRef from(source.c_str(), false);
    TagLib::FileRef to(target.c_str(), false);
    if (from.isNull() || to.isNull())
        return false;

    to.setProperties(from.properties());
    const auto pictures = from.complexProperties("PICTURE");
    if (!pictures.isEmpty())
        to.setComplexProperties("PICTURE", pictures);
    return to.save();
}

int sampleRateOf(const QString &path)
{
    const std::filesystem::path native = QFileInfo(path).filesystemFilePath();
    const TagLib::FileRef file(native.c_str(), true, TagLib::AudioProperties::Fast);
    return !file.isNull() && file.audioProperties() ? file.audioProperties()->sampleRate() : 0;
}

} // namespace

AudioConverter::AudioConverter(QObject *parent)
    : QObject(parent)
    , m_process(new QProcess(this))
{
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        if (status != QProcess::NormalExit || exitCode != 0) {
            // ffmpeg's last line names the actual problem.
            const QString log = QString::fromLocal8Bit(m_process->readAll()).trimmed();
            finishCurrent(false, tr("ffmpeg failed: %1").arg(log.section(u'\n', -1).trimmed()));
            return;
        }
        if (!copyTags(m_current, m_partialPath)) {
            finishCurrent(false, tr("Could not copy tags"));
            return;
        }
        const QString target = targetPathFor(m_current, m_currentFormat);
        if (!QFile::rename(m_partialPath, target)) {
            finishCurrent(false, tr("Could not rename %1").arg(QFileInfo(m_partialPath).fileName()));
            return;
        }
        finishCurrent(true, {});
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finishCurrent(false, tr("Could not start ffmpeg"));
    });
}

AudioConverter::~AudioConverter()
{
    if (m_current.isEmpty())
        return;
    // Quitting mid-conversion: no signals to a half-destroyed owner, and no half-written .m4a left behind.
    m_process->disconnect(this);
    m_process->kill();
    m_process->waitForFinished(3000);
    QFile::remove(m_partialPath);
}

QString AudioConverter::ffmpegPath()
{
    return QStandardPaths::findExecutable(u"ffmpeg"_s);
}

QStringList AudioConverter::formats()
{
    return {u"m4a"_s, u"mp3"_s, u"flac"_s};
}

QString AudioConverter::targetPathFor(const QString &sourcePath, const QString &format)
{
    const QFileInfo info(sourcePath);
    return info.dir().filePath(info.completeBaseName() + u'.' + format);
}

void AudioConverter::setFormat(const QString &format)
{
    Q_ASSERT(formats().contains(format));
    m_format = format;
}

QString AudioConverter::format() const
{
    return m_format;
}

void AudioConverter::convert(const QString &sourcePath)
{
    const QString target = targetPathFor(sourcePath, m_format);
    if (QFileInfo::exists(target)) {
        emit finished(sourcePath, target, true, {});
        return;
    }
    if (sourcePath == m_current
        || std::any_of(m_queue.cbegin(), m_queue.cend(), [&](const Job &job) { return job.source == sourcePath; }))
        return;
    m_queue.append({sourcePath, m_format});
    if (m_current.isEmpty())
        startNext();
}

void AudioConverter::startNext()
{
    if (m_queue.isEmpty())
        return;
    const Job job = m_queue.takeFirst();
    m_current = job.source;
    m_currentFormat = job.format;

    const QString ffmpeg = ffmpegPath();
    if (ffmpeg.isEmpty()) {
        finishCurrent(false, tr("ffmpeg not found on PATH"));
        return;
    }

    // ".converting.<format>" keeps the extension (ffmpeg picks the muxer from it)
    // while making clear the file is not finished.
    const QFileInfo info(m_current);
    m_partialPath = info.dir().filePath(info.completeBaseName() + u".converting."_s + m_currentFormat);

    QStringList args{u"-hide_banner"_s, u"-nostdin"_s, u"-loglevel"_s, u"error"_s, u"-y"_s,
                     u"-i"_s, QDir::toNativeSeparators(m_current),
                     u"-map"_s, u"0:a:0"_s, // audio only; cover art is copied by TagLib
                     u"-map_metadata"_s, u"-1"_s};
    if (m_currentFormat == "flac"_L1) {
        args << u"-c:a"_s << u"flac"_s;
    } else {
        args << u"-c:a"_s << (m_currentFormat == "mp3"_L1 ? u"libmp3lame"_s : u"aac"_s) << u"-b:a"_s << u"320k"_s;
        if (sampleRateOf(m_current) > kMaxSampleRate)
            args << u"-ar"_s << QString::number(kMaxSampleRate);
    }
    if (m_currentFormat == "m4a"_L1)
        args << u"-movflags"_s << u"+faststart"_s;
    args << QDir::toNativeSeparators(m_partialPath);

    emit started(m_current, m_currentFormat);
    m_process->start(ffmpeg, args);
}

void AudioConverter::finishCurrent(bool ok, const QString &error)
{
    const QString source = m_current;
    if (!ok)
        QFile::remove(m_partialPath);
    m_current.clear();
    m_partialPath.clear();
    emit finished(source, ok ? targetPathFor(source, m_currentFormat) : QString(), ok, error);
    startNext();
}
