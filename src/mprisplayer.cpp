/*
 *
 * Copyright (C) 2015-2021 Jolla Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <QObject>
#include "mprisplayeradaptor_p.h"
#include "mprisplayer.h"
#include "mprisplayer_p.h"
#include "mprismetadata_p.h"
#include "mprismetadata.h"
#include "ambermpris_p.h"

using namespace Amber;

namespace {
    const QString PlayerInterface = MprisPlayerAdaptor::staticMetaObject.classInfo(MprisPlayerAdaptor::staticMetaObject.indexOfClassInfo("D-Bus Interface")).value();
    const QString ServiceInterface = MprisServiceAdaptor::staticMetaObject.classInfo(MprisPlayerAdaptor::staticMetaObject.indexOfClassInfo("D-Bus Interface")).value();
    const QString TrackPrefix = QStringLiteral("/org/mpris/MediaPlayer2/TrackList/");
}

MprisPlayerPrivate::MprisPlayerPrivate(MprisPlayer *parent)
    : QObject(parent)
    , m_connection(getDBusConnection())
    , m_serviceAdaptor(this)
    , m_playerAdaptor(this)
    , m_canQuit(false)
    , m_canRaise(false)
    , m_canSetFullscreen(false)
    , m_fullscreen(false)
    , m_hasTrackList(false)
    , m_metaData(this)
    , m_canControl(false)
    , m_canGoNext(false)
    , m_canGoPrevious(false)
    , m_canPause(false)
    , m_canPlay(false)
    , m_canSeek(false)
    , m_loopStatus(Mpris::LoopNone)
    , m_maximumRate(1)
    , m_minimumRate(1)
    , m_playbackStatus(Mpris::Stopped)
    , m_position(0)
    , m_rate(1.0)
    , m_shuffle(false)
    , m_volume(0.0)
{
    m_changedDelay.setSingleShot(true);
    m_changedDelay.setInterval(50);

    qDBusRegisterMetaType<QStringList>();
    connect(&m_metaData, &MprisMetaData::metaDataChanged, this, [this] { propertyChanged(PlayerInterface, QStringLiteral("Metadata"), metaData()); });
    connect(&m_changedDelay, &QTimer::timeout, this, &MprisPlayerPrivate::emitPropertiesChanged);
}

MprisPlayerPrivate::~MprisPlayerPrivate()
{
}

MprisPlayer *MprisPlayerPrivate::parent() const
{
    return static_cast<MprisPlayer *>(QObject::parent());
}

void MprisPlayerPrivate::quit()
{
    Q_EMIT parent()->quitRequested();
}

void MprisPlayerPrivate::raise()
{
    Q_EMIT parent()->raiseRequested();
}

qlonglong MprisPlayerPrivate::position() const
{
    return static_cast<MprisPlayer *>(parent())->position() * 1000;
}

void MprisPlayerPrivate::setLoopStatus(const QString &value)
{
    int enumVal;
    bool ok = true;

    if (value == QLatin1String("None")) {
        enumVal = Mpris::LoopNone;
    } else if (value == QLatin1String("Track")) {
        enumVal = Mpris::LoopTrack;
    } else if (value == QLatin1String("Playlist")) {
        enumVal = Mpris::LoopPlaylist;
    } else {
        ok = false;
    }

    if (ok) {
        Q_EMIT parent()->loopStatusRequested(enumVal);
    } else {
        sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Invalid loop status"));
    }
}

QString MprisPlayerPrivate::loopStatus() const
{
    switch (parent()->loopStatus()) {
    case Mpris::LoopNone:
        return QLatin1String("None");
    case Mpris::LoopTrack:
        return QLatin1String("Track");
    case Mpris::LoopPlaylist:
        return QLatin1String("Playlist");
    default:
        return QString();
    }
}

QString MprisPlayerPrivate::playbackStatus() const
{
    const char *strVal = QMetaEnum::fromType<Mpris::PlaybackStatus>().valueToKey(static_cast<int>(parent()->playbackStatus()));
    return QString::fromLatin1(strVal);
}

void MprisPlayerPrivate::setRate(double rate)
{
    if (!parent()->canControl()) {
        sendErrorReply(QDBusError::NotSupported, QStringLiteral("The operation is not supported"));
    } else if (rate < parent()->minimumRate() || rate > parent()->maximumRate()) {
        sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Rate not in the allowed range"));
    } else {
        Q_EMIT parent()->rateRequested(rate);
    }
}

void MprisPlayerPrivate::setShuffle(bool shuffle)
{
    if (!parent()->canControl()) {
        sendErrorReply(QDBusError::NotSupported, QStringLiteral("The operation is not supported"));
    } else {
        Q_EMIT parent()->shuffleRequested(shuffle);
    }
}

void MprisPlayerPrivate::setVolume(double volume)
{
    if (!parent()->canControl()) {
        sendErrorReply(QDBusError::NotSupported, QStringLiteral("The operation is not supported"));
    } else {
        Q_EMIT parent()->volumeRequested(volume);
    }
}

void MprisPlayerPrivate::Next()
{
    if (!parent()->canControl()) {
        sendErrorReply(QDBusError::NotSupported, QStringLiteral("The operation is not supported"));
    } else if (!parent()->canGoNext()) {
        sendErrorReply(QDBusError::Failed, QStringLiteral("The operation can not be performed"));
    } else {
        Q_EMIT parent()->nextRequested();
    }
}

void MprisPlayerPrivate::OpenUri(const QString &Uri)
{
    if (!parent()->canControl()) {
        sendErrorReply(QDBusError::NotSupported, QStringLiteral("The operation is not supported"));
    } else {
        Q_EMIT parent()->openUriRequested(QUrl::fromUserInput(Uri));
    }
}

void MprisPlayerPrivate::Pause()
{
    if (!parent()->canControl()) {
        sendErrorReply(QDBusError::NotSupported, QStringLiteral("The operation is not supported"));
    } else if (!parent()->canPause()) {
        sendErrorReply(QDBusError::Failed, QStringLiteral("The operation can not be performed"));
    } else {
        Q_EMIT parent()->pauseRequested();
    }
}

void MprisPlayerPrivate::Play()
{
    if (!parent()->canControl()) {
        sendErrorReply(QDBusError::NotSupported, QStringLiteral("The operation is not supported"));
    } else if (!parent()->canPlay()) {
        sendErrorReply(QDBusError::Failed, QStringLiteral("The operation can not be performed"));
    } else {
        Q_EMIT parent()->playRequested();
    }
}

void MprisPlayerPrivate::PlayPause()
{
    if (!parent()->canControl()) {
        sendErrorReply(QDBusError::NotSupported, QStringLiteral("The operation is not supported"));
    } else if (!parent()->canPlay() && !parent()->canPause()) {
        sendErrorReply(QDBusError::Failed, QStringLiteral("The operation can not be performed"));
    } else {
        Q_EMIT parent()->playPauseRequested();
    }
}

void MprisPlayerPrivate::Previous()
{
    if (!parent()->canControl()) {
        sendErrorReply(QDBusError::NotSupported, QStringLiteral("The operation is not supported"));
    } else if (!parent()->canGoPrevious()) {
        sendErrorReply(QDBusError::Failed, QStringLiteral("The operation can not be performed"));
    } else {
        Q_EMIT parent()->previousRequested();
    }
}

void MprisPlayerPrivate::Seek(qlonglong Offset)
{
    if (!parent()->canControl()) {
        sendErrorReply(QDBusError::NotSupported, QStringLiteral("The operation is not supported"));
    } else if (!parent()->canSeek()) {
        sendErrorReply(QDBusError::Failed, QStringLiteral("The operation can not be performed"));
    } else {
        Q_EMIT parent()->seekRequested(Offset / 1000);
    }
}

void MprisPlayerPrivate::SetPosition(const QDBusObjectPath &TrackId, qlonglong position)
{
    if (!parent()->canControl()) {
        sendErrorReply(QDBusError::NotSupported, QStringLiteral("The operation is not supported"));
    } else if (!parent()->canSeek()) {
        sendErrorReply(QDBusError::Failed, QStringLiteral("The operation can not be performed"));
    } else {
        QString tid = TrackId.path();
        if (tid.startsWith(TrackPrefix))
            tid = tid.mid(TrackPrefix.size());
        Q_EMIT parent()->setPositionRequested(tid, position / 1000);
    }
}

void MprisPlayerPrivate::Stop()
{
    if (!parent()->canControl()) {
        sendErrorReply(QDBusError::NotSupported, QStringLiteral("The operation is not supported"));
    } else {
        Q_EMIT parent()->stopRequested();
    }
}

QVariantMap MprisPlayerPrivate::metaData() const
{
    return m_metaData.priv->typedMetaData();
}

void MprisPlayerPrivate::propertyChanged(const QString &iface, const QString &name, const QVariant &value)
{
    if (!value.isValid()) {
        m_changedProperties[iface].first.remove(name);
        m_changedProperties[iface].second << name;
    } else {
        m_changedProperties[iface].first[name] = value;
        m_changedProperties[iface].second.remove(name);
    }

    m_changedDelay.start();
}

void MprisPlayerPrivate::emitPropertiesChanged()
{
    for (auto i = m_changedProperties.cbegin();
         i != m_changedProperties.cend();
         ++i) {
        QDBusMessage msg = QDBusMessage::createSignal(QStringLiteral("/org/mpris/MediaPlayer2"),
                                                      QStringLiteral("org.freedesktop.DBus.Properties"),
                                                      QStringLiteral("PropertiesChanged"));

        msg << i.key();
        msg << QVariant::fromValue(i.value().first);
        msg << QStringList(i.value().second.values());

        m_connection.send(msg);
    }

    m_changedProperties.clear();
}

MprisPlayer::MprisPlayer(QObject *parent)
    : QObject(parent)
    , priv(new MprisPlayerPrivate(this))
{
    connect(this, &MprisPlayer::seeked, priv, [this](qlonglong position) { Q_EMIT priv->m_playerAdaptor.Seeked(position * 1000); });
}

MprisPlayer::~MprisPlayer()
{
    delete priv;
}

bool MprisPlayer::canControl() const
{
    return priv->m_canControl;
}

bool MprisPlayer::canGoNext() const
{
    if (!canControl())
        return false;
    return priv->m_canGoNext;
}

bool MprisPlayer::canGoPrevious() const
{
    if (!canControl())
        return false;
    return priv->m_canGoPrevious;
}

bool MprisPlayer::canPause() const
{
    if (!canControl())
        return false;
    return priv->m_canPause;
}

bool MprisPlayer::canPlay() const
{
    if (!canControl())
        return false;
    return priv->m_canPlay;
}

bool MprisPlayer::canSeek() const
{
    if (!canControl())
        return false;
    return priv->m_canSeek;
}

Mpris::LoopStatus MprisPlayer::loopStatus() const
{
    return priv->m_loopStatus;
}

double MprisPlayer::maximumRate() const
{
    return priv->m_maximumRate;
}

MprisMetaData *MprisPlayer::metaData() const
{
    return &priv->m_metaData;
}

double MprisPlayer::minimumRate() const
{
    return priv->m_minimumRate;
}
Mpris::PlaybackStatus MprisPlayer::playbackStatus() const
{
    return priv->m_playbackStatus;
}
qlonglong MprisPlayer::position() const
{
    return priv->m_position;
}
double MprisPlayer::rate() const
{
    return priv->m_rate;
}
bool MprisPlayer::shuffle() const
{
    return priv->m_shuffle;
}
double MprisPlayer::volume() const
{
    return priv->m_volume;
}

void MprisPlayer::setCanControl(bool canControl)
{
    if (canControl != priv->m_canControl) {
        priv->m_canControl = canControl;
        Q_EMIT canControlChanged();
    }
}
void MprisPlayer::setCanGoNext(bool canGoNext)
{
    if (canGoNext != priv->m_canGoNext) {
        priv->m_canGoNext = canGoNext;
        if (canControl()) {
            Q_EMIT canGoNextChanged();
            priv->propertyChanged(PlayerInterface, QStringLiteral("CanGoNext"), canGoNext);
        }
    }
}
void MprisPlayer::setCanGoPrevious(bool canGoPrevious)
{
    if (canGoPrevious != priv->m_canGoPrevious) {
        priv->m_canGoPrevious = canGoPrevious;
        if (canControl()) {
            Q_EMIT canGoPreviousChanged();
            priv->propertyChanged(PlayerInterface, QStringLiteral("CanGoPrevious"), canGoPrevious);
        }
    }
}
void MprisPlayer::setCanPause(bool canPause)
{
    if (canPause != priv->m_canPause) {
        priv->m_canPause = canPause;
        if (canControl()) {
            Q_EMIT canPauseChanged();
            priv->propertyChanged(PlayerInterface, QStringLiteral("CanPause"), canPause);
        }
    }
}
void MprisPlayer::setCanPlay(bool canPlay)
{
    if (canPlay != priv->m_canPlay) {
        priv->m_canPlay = canPlay;
        if (canControl()) {
            Q_EMIT canPlayChanged();
            priv->propertyChanged(PlayerInterface, QStringLiteral("CanPlay"), canPlay);
        }
    }
}
void MprisPlayer::setCanSeek(bool canSeek)
{
    if (canSeek != priv->m_canSeek) {
        priv->m_canSeek = canSeek;
        if (canControl()) {
            Q_EMIT canSeekChanged();
            priv->propertyChanged(PlayerInterface, QStringLiteral("CanSeek"), canSeek);
        }
    }
}

void MprisPlayer::setLoopStatus(Mpris::LoopStatus loopStatus)
{
    if (loopStatus != priv->m_loopStatus) {
        priv->m_loopStatus = loopStatus;
        Q_EMIT loopStatusChanged();
        priv->propertyChanged(PlayerInterface, QStringLiteral("LoopStatus"), priv->m_playerAdaptor.loopStatus());
    }
}

void MprisPlayer::setMaximumRate(double maximumRate)
{
    if (maximumRate != priv->m_maximumRate) {
        priv->m_maximumRate = maximumRate;
        Q_EMIT maximumRateChanged();
        priv->propertyChanged(PlayerInterface, QStringLiteral("MaximumRate"), maximumRate);
    }
}
void MprisPlayer::setMinimumRate(double minimumRate)
{
    if (minimumRate != priv->m_minimumRate) {
        priv->m_minimumRate = minimumRate;
        Q_EMIT minimumRateChanged();
        priv->propertyChanged(PlayerInterface, QStringLiteral("MinimumRate"), minimumRate);
    }
}
void MprisPlayer::setPlaybackStatus(Mpris::PlaybackStatus playbackStatus)
{
    if (playbackStatus != priv->m_playbackStatus) {
        priv->m_playbackStatus = playbackStatus;
        Q_EMIT playbackStatusChanged();
        priv->propertyChanged(PlayerInterface, QStringLiteral("PlaybackStatus"), priv->m_playerAdaptor.playbackStatus());
    }
}
void MprisPlayer::setPosition(qlonglong position)
{
    if (position != priv->m_position) {
        priv->m_position = position;
        Q_EMIT positionChanged();
    }
}
void MprisPlayer::setRate(double rate)
{
    if (rate != priv->m_rate) {
        priv->m_rate = rate;
        Q_EMIT rateChanged();
        priv->propertyChanged(PlayerInterface, QStringLiteral("Rate"), rate);
    }
}
void MprisPlayer::setShuffle(bool shuffle)
{
    if (shuffle != priv->m_shuffle) {
        priv->m_shuffle = shuffle;
        Q_EMIT shuffleChanged();
        priv->propertyChanged(PlayerInterface, QStringLiteral("Shuffle"), shuffle);
    }
}
void MprisPlayer::setVolume(double volume)
{
    if (volume != priv->m_volume) {
        priv->m_volume = volume;
        Q_EMIT volumeChanged();
        priv->propertyChanged(PlayerInterface, QStringLiteral("Volume"), volume);
    }
}

QString MprisPlayer::serviceName() const
{
    return priv->m_serviceName;
}

bool MprisPlayer::canQuit() const
{
    return priv->m_canQuit;
}

bool MprisPlayer::canRaise() const
{
    return priv->m_canRaise;
}

bool MprisPlayer::canSetFullscreen() const
{
    return priv->m_canSetFullscreen;
}

QString MprisPlayer::desktopEntry() const
{
    return priv->m_desktopEntry;
}

bool MprisPlayer::fullscreen() const
{
    return priv->m_fullscreen;
}

bool MprisPlayer::hasTrackList() const
{
    return priv->m_hasTrackList;
}

QString MprisPlayer::identity() const
{
    return priv->m_identity;
}

QStringList MprisPlayer::supportedUriSchemes() const
{
    return priv->m_supportedUriSchemes;
}

QStringList MprisPlayer::supportedMimeTypes() const
{
    return priv->m_supportedMimeTypes;
}

void MprisPlayer::setServiceName(const QString &serviceName)
{
    if (!priv->m_serviceName.isEmpty()) {
        priv->m_connection.unregisterObject(QStringLiteral("/org/mpris/MediaPlayer2"));
        priv->m_connection.unregisterService(priv->m_serviceName);
    }

    if (!serviceName.isEmpty()) {
        if (!serviceName.startsWith(QLatin1String("org.mpris.MediaPlayer2."))) {
            priv->m_serviceName = QStringLiteral("%1.%2.instance%3").arg(QLatin1String("org.mpris.MediaPlayer2")).arg(serviceName).arg(QCoreApplication::applicationPid());
        } else {
            priv->m_serviceName = QStringLiteral("%1.instance%2").arg(serviceName).arg(QCoreApplication::applicationPid());
        }

        priv->m_connection.registerService(priv->m_serviceName);
        priv->m_connection.registerObject(QStringLiteral("/org/mpris/MediaPlayer2"), priv);
    } else {
        priv->m_serviceName = serviceName;
    }

    Q_EMIT serviceNameChanged();
}

void MprisPlayer::setCanQuit(bool canQuit)
{
    if (priv->m_canQuit != canQuit) {
        priv->m_canQuit = canQuit;
        priv->propertyChanged(ServiceInterface, QStringLiteral("CanQuit"), canQuit);
        Q_EMIT canQuitChanged();
    }
}

void MprisPlayer::setCanRaise(bool canRaise)
{
    if (priv->m_canRaise != canRaise) {
        priv->m_canRaise = canRaise;
        priv->propertyChanged(ServiceInterface, QStringLiteral("CanRaise"), canRaise);
        Q_EMIT canRaiseChanged();
    }
}

void MprisPlayer::setCanSetFullscreen(bool canSetFullscreen)
{
    if (priv->m_canSetFullscreen != canSetFullscreen) {
        priv->m_canSetFullscreen = canSetFullscreen;
        priv->propertyChanged(ServiceInterface, QStringLiteral("CanSetFullscreen"), canSetFullscreen);
        Q_EMIT canSetFullscreenChanged();
    }
}

void MprisPlayer::setDesktopEntry(const QString &desktopEntry)
{
    if (priv->m_desktopEntry != desktopEntry) {
        priv->m_desktopEntry = desktopEntry;
        priv->propertyChanged(ServiceInterface, QStringLiteral("DesktopEntry"), desktopEntry);
        Q_EMIT desktopEntryChanged();
    }
}

void MprisPlayer::setFullscreen(bool fullscreen)
{
    if (priv->m_fullscreen != fullscreen) {
        priv->m_fullscreen = fullscreen;
        priv->propertyChanged(ServiceInterface, QStringLiteral("Fullscreen"), fullscreen);
        Q_EMIT fullscreenChanged();
    }
}

void MprisPlayer::setHasTrackList(bool hasTrackList)
{
    if (priv->m_hasTrackList != hasTrackList) {
        priv->m_hasTrackList = hasTrackList;
        priv->propertyChanged(ServiceInterface, QStringLiteral("HasTrackList"), hasTrackList);
        Q_EMIT hasTrackListChanged();
    }
}

void MprisPlayer::setIdentity(const QString &identity)
{
    if (priv->m_identity != identity) {
        priv->m_identity = identity;
        priv->propertyChanged(ServiceInterface, QStringLiteral("Identity"), identity);
        Q_EMIT identityChanged();
    }
}

void MprisPlayer::setSupportedUriSchemes(const QStringList &supportedUriSchemes)
{
    if (priv->m_supportedUriSchemes != supportedUriSchemes) {
        priv->m_supportedUriSchemes = supportedUriSchemes;
        priv->propertyChanged(ServiceInterface, QStringLiteral("SupportedUriSchemes"), supportedUriSchemes);
        Q_EMIT supportedUriSchemesChanged();
    }
}

void MprisPlayer::setSupportedMimeTypes(const QStringList &supportedMimeTypes)
{
    if (priv->m_supportedMimeTypes != supportedMimeTypes) {
        priv->m_supportedMimeTypes = supportedMimeTypes;
        priv->propertyChanged(ServiceInterface, QStringLiteral("SupportedMimeTypes"), supportedMimeTypes);
        Q_EMIT supportedMimeTypesChanged();
    }
}
