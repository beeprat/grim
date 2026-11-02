#include "core/SpotifyMatch.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <limits>

using namespace Qt::StringLiterals;

namespace Spotify {

Link parseLink(const QString &text)
{
    static const QRegularExpression pattern(uR"((playlist|album)[/:]([A-Za-z0-9]{22}))"_s);
    const QRegularExpressionMatch match = pattern.match(text.trimmed());
    if (!match.hasMatch())
        return {};
    return {match.captured(1), match.captured(2)};
}

bool parseEmbedPage(const QByteArray &html, QString *name, QList<RemoteTrack> *tracks)
{
    const QByteArray startTag = "<script id=\"__NEXT_DATA__\" type=\"application/json\">";
    const qsizetype start = html.indexOf(startTag);
    if (start < 0)
        return false;
    const qsizetype from = start + startTag.size();
    const qsizetype end = html.indexOf("</script>", from);
    if (end < 0)
        return false;
    const QJsonObject entity = QJsonDocument::fromJson(html.mid(from, end - from))
                                   .object()
                                   .value(u"props"_s).toObject()
                                   .value(u"pageProps"_s).toObject()
                                   .value(u"state"_s).toObject()
                                   .value(u"data"_s).toObject()
                                   .value(u"entity"_s).toObject();
    const QJsonValue list = entity.value(u"trackList"_s);
    if (!list.isArray())
        return false;
    *name = entity.value(u"name"_s).toString();
    tracks->clear();
    // Albums list no album name per track: it is the page's.
    const bool album = entity.value(u"type"_s).toString() == u"album"_s;
    for (const QJsonValue &value : list.toArray()) {
        const QJsonObject track = value.toObject();
        RemoteTrack remote;
        remote.title = track.value(u"title"_s).toString();
        // "subtitle" is the artists joined with ", " (non-breaking spaces in some pages).
        for (const QString &artist : track.value(u"subtitle"_s).toString().replace(QChar(0xA0), u' ').split(u", "_s, Qt::SkipEmptyParts))
            remote.artists.append(artist.trimmed());
        remote.album = album ? *name : QString();
        remote.durationMs = track.value(u"duration"_s).toInteger();
        if (!remote.title.isEmpty())
            tracks->append(remote);
    }
    return true;
}

QString normalize(const QString &text)
{
    const QString decomposed = text.normalized(QString::NormalizationForm_KD).toLower();
    QString result;
    result.reserve(decomposed.size());
    for (const QChar c : decomposed) {
        if (c.category() == QChar::Mark_NonSpacing)
            continue; // accents left over from the decomposition
        if (c == u'&')
            result += u" and "_s;
        else if (c.isLetterOrNumber())
            result += c;
        else
            result += u' ';
    }
    return result.simplified();
}

QString baseTitle(const QString &title)
{
    static const QRegularExpression brackets(uR"(\s*[\(\[][^\)\]]*[\)\]])"_s);
    // " - Remastered 2009", " - Live at ...", " - Mono": a dash with spaces, then anything.
    static const QRegularExpression suffix(uR"(\s+[-–—]\s+.*$)"_s);
    QString base = title;
    base.remove(brackets);
    base.remove(suffix);
    base = normalize(base);
    return base.isEmpty() ? normalize(title) : base;
}

QStringList artistNames(const QString &artistTag)
{
    static const QRegularExpression separators(
        uR"(\s*(?:,|;|/|&|\+|\band\b|\bfeat\.?|\bft\.?|\bfeaturing\b|\bwith\b|\bx\b|\bvs\.?)\s*)"_s,
        QRegularExpression::CaseInsensitiveOption);
    QStringList names;
    const auto add = [&names](const QString &name) {
        QString n = normalize(name);
        if (n.startsWith(u"the "_s))
            n = n.mid(4);
        if (!n.isEmpty() && !names.contains(n))
            names.append(n);
    };
    add(artistTag); // the whole tag too: "Simon & Garfunkel" is one artist
    for (const QString &part : artistTag.split(separators, Qt::SkipEmptyParts))
        add(part);
    return names;
}

namespace {

QString key(const QString &title, const QString &artist)
{
    return title + u'\x1f' + artist;
}

} // namespace

Matcher::Matcher(const QList<Track> &tracks)
    : m_tracks(tracks)
{
    for (int i = 0; i < m_tracks.size(); ++i) {
        const Track &track = m_tracks.at(i);
        const QString title = baseTitle(track.title);
        for (const QString &artist : artistNames(track.artist))
            m_byKey[key(title, artist)].append(i);
    }
}

QString Matcher::match(const RemoteTrack &remote) const
{
    const QString title = baseTitle(remote.title);
    QList<int> candidates;
    for (const QString &artist : remote.artists) {
        for (const QString &name : artistNames(artist)) {
            for (const int i : m_byKey.value(key(title, name))) {
                if (!candidates.contains(i))
                    candidates.append(i);
            }
        }
    }
    if (candidates.isEmpty())
        return {};

    const QString album = normalize(remote.album);
    int best = -1;
    qint64 bestScore = std::numeric_limits<qint64>::max();
    for (const int i : candidates) {
        const Track &track = m_tracks.at(i);
        // Length difference in ms (unknown lengths count as a small miss); the same album is
        // worth two seconds.
        qint64 score = (remote.durationMs > 0 && track.durationMs > 0) ? qAbs(remote.durationMs - track.durationMs)
                                                                        : 1500;
        if (!album.isEmpty() && normalize(track.album) == album)
            score -= 2000;
        if (score < bestScore) {
            bestScore = score;
            best = i;
        }
    }
    return m_tracks.at(best).path;
}

} // namespace Spotify
