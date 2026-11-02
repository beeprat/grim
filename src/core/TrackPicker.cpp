#include "core/TrackPicker.h"

#include <QSet>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace TrackPicker {

namespace {

const QSet<QString> kAudio = {u"flac"_s, u"mp3"_s, u"m4a"_s, u"aac"_s, u"ogg"_s, u"opus"_s, u"wav"_s, u"alac"_s};
const QSet<QString> kLossless = {u"flac"_s, u"wav"_s, u"alac"_s};
// Versions that are not the studio song unless the title says so.
const QStringList kOtherVersions = {u"live"_s,  u"karaoke"_s, u"instrumental"_s, u"remix"_s,
                                    u"cover"_s, u"acoustic"_s, u"demo"_s,        u"tribute"_s};

QStringList words(const QString &normalized)
{
    return normalized.split(u' ', Qt::SkipEmptyParts);
}

} // namespace

QString searchText(const Spotify::RemoteTrack &track)
{
    // Soulseek wants every word in the path: leave out "and" (normalize() makes it of "&"), so files
    // spelled either way match.
    // Words with an apostrophe are left out: files spell "Don't" as "Don't", "Dont" or "Don t", and
    // Soulseek wants every search word to appear.
    const auto dropApostrophes = [](const QString &text) {
        QStringList kept;
        for (const QString &word : text.split(u' ', Qt::SkipEmptyParts)) {
            if (!word.contains(u'\'') && !word.contains(QChar(0x2019)))
                kept.append(word);
        }
        return kept.join(u' ');
    };
    const QString artist = track.artists.isEmpty() ? QString() : Spotify::normalize(dropApostrophes(track.artists.first()));
    QStringList kept;
    for (const QString &word : words(artist + u' ' + Spotify::baseTitle(dropApostrophes(track.title)))) {
        if (word != u"and"_s)
            kept.append(word);
    }
    return kept.join(u' ');
}

namespace {

// "*eftones": a word Soulseek matches by its end, so the banned whole word is not in the search.
QString wildcardFirstWord(const QString &text)
{
    QStringList w = words(text);
    if (w.isEmpty() || w.first().size() < 4)
        return {};
    w.first() = u'*' + w.first().mid(1);
    return w.join(u' ');
}

void addUnique(QStringList *list, const QString &text)
{
    const QString trimmed = text.simplified();
    if (!trimmed.isEmpty() && !list->contains(trimmed))
        list->append(trimmed);
}

} // namespace

QStringList searchTexts(const Spotify::RemoteTrack &track)
{
    QStringList result;
    const QString plain = searchText(track);
    addUnique(&result, plain);
    const QString artist = track.artists.isEmpty() ? QString() : Spotify::normalize(track.artists.first());
    const QString title = plain.mid(artist.size()).trimmed(); // searchText() is "artist title"
    const QString wildArtist = wildcardFirstWord(artist);
    if (!wildArtist.isEmpty())
        addUnique(&result, wildArtist + u' ' + title);
    addUnique(&result, title + u' ' + Spotify::baseTitle(track.album));
    return result;
}

QStringList albumSearchTexts(const QString &artist, const QString &album)
{
    QStringList result;
    const QString a = Spotify::normalize(artist);
    const QString b = Spotify::baseTitle(album);
    addUnique(&result, a + u' ' + b);
    const QString wildArtist = wildcardFirstWord(a);
    if (!wildArtist.isEmpty())
        addUnique(&result, wildArtist + u' ' + b);
    addUnique(&result, b);
    return result;
}

QList<int> rank(const QList<SlskdSearchFile> &files, const Spotify::RemoteTrack &track, const QString &format)
{
    const QStringList titleWords = words(Spotify::baseTitle(track.title));
    const QString fullTitle = u' ' + Spotify::normalize(track.title) + u' ';
    QStringList artists;
    for (const QString &artist : track.artists) {
        for (const QString &name : Spotify::artistNames(artist))
            artists.append(name);
    }
    const int lengthSec = int(track.durationMs / 1000);

    struct Scored
    {
        int index;
        double score;
    };
    QList<Scored> accepted;
    for (int i = 0; i < files.size(); ++i) {
        const SlskdSearchFile &file = files.at(i);
        if (!kAudio.contains(file.extension))
            continue;
        if (!kLossless.contains(file.extension) && file.bitRate > 0 && file.bitRate < 192)
            continue;

        // "@@user\Music\Artist\Album\01 - Title.flac": name without the extension, and the whole path.
        const QString base = file.filename.section(u'\\', -1);
        const QString name = u' ' + Spotify::normalize(base.left(base.lastIndexOf(u'.'))) + u' ';
        const QString path = u' ' + Spotify::normalize(file.filename) + u' ';
        if (!std::all_of(titleWords.cbegin(), titleWords.cend(),
                         [&name](const QString &w) { return name.contains(u' ' + w + u' '); }))
            continue;
        const bool artistNamed = std::any_of(artists.cbegin(), artists.cend(), [&path](const QString &a) {
            return path.contains(u' ' + a + u' ');
        });
        if (!artistNamed)
            continue;
        bool otherVersion = false;
        for (const QString &word : kOtherVersions) {
            const QString w = u' ' + word + u' ';
            if (name.contains(w) && !fullTitle.contains(w))
                otherVersion = true;
        }
        if (otherVersion)
            continue;
        if (file.lengthSec > 0 && lengthSec > 0 && qAbs(file.lengthSec - lengthSec) > 5)
            continue;

        double score = 0;
        if (!format.isEmpty() && file.extension == format)
            score += 1000;
        if (file.hasFreeSlot)
            score += 300;
        if (file.lengthSec > 0 && lengthSec > 0)
            score += 100 - 10 * qAbs(file.lengthSec - lengthSec);
        score -= qMin(file.queueLength, 50) * 4;
        score += qMin(double(file.uploadSpeed) / 100000.0, 100.0); // up to 10 MB/s
        // Fewer extra words in the name: closer to just "NN Title".
        score -= qMax(0, int(words(name).size()) - int(titleWords.size()) - 2) * 3;
        accepted.append({i, score});
    }
    std::stable_sort(accepted.begin(), accepted.end(), [](const Scored &a, const Scored &b) { return a.score > b.score; });
    QList<int> result;
    result.reserve(accepted.size());
    for (const Scored &s : accepted)
        result.append(s.index);
    return result;
}

} // namespace TrackPicker
