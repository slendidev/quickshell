#pragma once

#include <functional>
#include <utility>

#include <qobject.h>
#include <qproperty.h>
#include <qqueue.h>
#include <qstring.h>
#include <qtcpsocket.h>
#include <qdatetime.h>
#include <qtimer.h>
#include <qtypes.h>
#include <qvariant.h>
#include <qqmlintegration.h>

#include "../../core/doc.hpp"

namespace qs::service::mpd {

///! Playback state of an MpdPlayer.
/// See @@MpdPlayer.playbackState.
class MpdPlaybackState: public QObject {
Q_OBJECT;
QML_ELEMENT;
QML_SINGLETON;

public:
enum Enum : quint8 {
Stopped = 0,
Playing = 1,
Paused = 2,
};
Q_ENUM(Enum);

Q_INVOKABLE static QString toString(qs::service::mpd::MpdPlaybackState::Enum status);
};

///! A media player exposed by MPD.
class MpdPlayer: public QObject {
Q_OBJECT;
QML_ELEMENT;
QML_UNCREATABLE("MpdPlayer can only be acquired from Mpd");
Q_PROPERTY(bool ready READ default NOTIFY readyChanged BINDABLE bindableReady);
Q_PROPERTY(bool connected READ default NOTIFY connectedChanged BINDABLE bindableConnected);
Q_PROPERTY(qs::service::mpd::MpdPlaybackState::Enum playbackState READ default NOTIFY playbackStateChanged BINDABLE bindablePlaybackState);
Q_PROPERTY(bool isPlaying READ default NOTIFY isPlayingChanged BINDABLE bindableIsPlaying);
Q_PROPERTY(QString trackTitle READ default NOTIFY trackTitleChanged BINDABLE bindableTrackTitle);
Q_PROPERTY(QString trackArtist READ default NOTIFY trackArtistChanged BINDABLE bindableTrackArtist);
Q_PROPERTY(QString trackAlbum READ default NOTIFY trackAlbumChanged BINDABLE bindableTrackAlbum);
Q_PROPERTY(QString trackArtUrl READ default NOTIFY trackArtUrlChanged BINDABLE bindableTrackArtUrl);
Q_PROPERTY(QString trackFile READ default NOTIFY trackFileChanged BINDABLE bindableTrackFile);
Q_PROPERTY(QVariantMap metadata READ default NOTIFY metadataChanged BINDABLE bindableMetadata);
Q_PROPERTY(qreal position READ default WRITE setPosition NOTIFY positionChanged BINDABLE bindablePosition);
Q_PROPERTY(qreal length READ default NOTIFY lengthChanged BINDABLE bindableLength);
Q_PROPERTY(qreal volume READ default WRITE setVolume NOTIFY volumeChanged BINDABLE bindableVolume);

public:
explicit MpdPlayer(QObject* parent = nullptr): QObject(parent) {}

[[nodiscard]] QBindable<bool> bindableReady() const { return &this->bReady; }
[[nodiscard]] QBindable<bool> bindableConnected() const { return &this->bConnected; }
[[nodiscard]] QBindable<MpdPlaybackState::Enum> bindablePlaybackState() const {
return &this->bPlaybackState;
}
[[nodiscard]] QBindable<bool> bindableIsPlaying() const { return &this->bIsPlaying; }
[[nodiscard]] QBindable<QString> bindableTrackTitle() const { return &this->bTrackTitle; }
[[nodiscard]] QBindable<QString> bindableTrackArtist() const { return &this->bTrackArtist; }
[[nodiscard]] QBindable<QString> bindableTrackAlbum() const { return &this->bTrackAlbum; }
[[nodiscard]] QBindable<QString> bindableTrackArtUrl() const { return &this->bTrackArtUrl; }
[[nodiscard]] QBindable<QString> bindableTrackFile() const { return &this->bTrackFile; }
[[nodiscard]] QBindable<QVariantMap> bindableMetadata() const { return &this->bMetadata; }
[[nodiscard]] QBindable<qreal> bindablePosition() const { return &this->bPosition; }
[[nodiscard]] QBindable<qreal> bindableLength() const { return &this->bLength; }
[[nodiscard]] QBindable<qreal> bindableVolume() const { return &this->bVolume; }

void setPosition(qreal position);
void setVolume(qreal volume);

void setConnected(bool connected);
void setReady(bool ready);
void setPlaybackState(MpdPlaybackState::Enum playbackState);
void setTrackTitle(QString title);
void setTrackArtist(QString artist);
void setTrackAlbum(QString album);
void setTrackArtUrl(QString artUrl);
void setTrackFile(QString file);
void setMetadata(QVariantMap metadata);
void setLength(qreal length);
void setPositionInternal(qreal position);
void setVolumeInternal(qreal volume);

signals:
void readyChanged();
void connectedChanged();
void playbackStateChanged();
void isPlayingChanged();
void trackTitleChanged();
void trackArtistChanged();
void trackAlbumChanged();
void trackArtUrlChanged();
void trackFileChanged();
void metadataChanged();
void positionChanged();
void lengthChanged();
void volumeChanged();

QSDOC_HIDE void positionChangeRequested(qreal position);
QSDOC_HIDE void volumeChangeRequested(qreal volume);

private:
Q_OBJECT_BINDABLE_PROPERTY(MpdPlayer, bool, bReady, &MpdPlayer::readyChanged);
Q_OBJECT_BINDABLE_PROPERTY(MpdPlayer, bool, bConnected, &MpdPlayer::connectedChanged);
Q_OBJECT_BINDABLE_PROPERTY(MpdPlayer, MpdPlaybackState::Enum, bPlaybackState, &MpdPlayer::playbackStateChanged);
Q_OBJECT_BINDABLE_PROPERTY(MpdPlayer, bool, bIsPlaying, &MpdPlayer::isPlayingChanged);
Q_OBJECT_BINDABLE_PROPERTY(MpdPlayer, QString, bTrackTitle, &MpdPlayer::trackTitleChanged);
Q_OBJECT_BINDABLE_PROPERTY(MpdPlayer, QString, bTrackArtist, &MpdPlayer::trackArtistChanged);
Q_OBJECT_BINDABLE_PROPERTY(MpdPlayer, QString, bTrackAlbum, &MpdPlayer::trackAlbumChanged);
Q_OBJECT_BINDABLE_PROPERTY(MpdPlayer, QString, bTrackArtUrl, &MpdPlayer::trackArtUrlChanged);
Q_OBJECT_BINDABLE_PROPERTY(MpdPlayer, QString, bTrackFile, &MpdPlayer::trackFileChanged);
Q_OBJECT_BINDABLE_PROPERTY(MpdPlayer, QVariantMap, bMetadata, &MpdPlayer::metadataChanged);
Q_OBJECT_BINDABLE_PROPERTY(MpdPlayer, qreal, bPosition, &MpdPlayer::positionChanged);
Q_OBJECT_BINDABLE_PROPERTY(MpdPlayer, qreal, bLength, &MpdPlayer::lengthChanged);
Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(MpdPlayer, qreal, bVolume, 1, &MpdPlayer::volumeChanged);
};

///! MPD service access.
class Mpd: public QObject {
Q_OBJECT;
QML_NAMED_ELEMENT(Mpd);
QML_SINGLETON;
Q_PROPERTY(QString host READ default WRITE setHost NOTIFY hostChanged BINDABLE bindableHost);
Q_PROPERTY(quint16 port READ default WRITE setPort NOTIFY portChanged BINDABLE bindablePort);
Q_PROPERTY(QString password READ default WRITE setPassword NOTIFY passwordChanged BINDABLE bindablePassword);
Q_PROPERTY(bool autoReconnect READ default WRITE setAutoReconnect NOTIFY autoReconnectChanged BINDABLE bindableAutoReconnect);
Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged);
Q_PROPERTY(MpdPlayer* player READ player CONSTANT);

public:
explicit Mpd(QObject* parent = nullptr);

[[nodiscard]] QBindable<QString> bindableHost() const { return &this->bHost; }
[[nodiscard]] QBindable<quint16> bindablePort() const { return &this->bPort; }
[[nodiscard]] QBindable<QString> bindablePassword() const { return &this->bPassword; }
[[nodiscard]] QBindable<bool> bindableAutoReconnect() const { return &this->bAutoReconnect; }

[[nodiscard]] bool connected() const { return this->mPlayer.bindableConnected().value(); }
[[nodiscard]] MpdPlayer* player();

void setHost(QString host);
void setPort(quint16 port);
void setPassword(QString password);
void setAutoReconnect(bool autoReconnect);

Q_INVOKABLE void reconnect();
Q_INVOKABLE void play();
Q_INVOKABLE void pause();
Q_INVOKABLE void stop();
Q_INVOKABLE void togglePlaying();
Q_INVOKABLE void next();
Q_INVOKABLE void previous();
Q_INVOKABLE void seek(qreal offset);

signals:
void hostChanged();
void portChanged();
void passwordChanged();
void autoReconnectChanged();
void connectedChanged();

private slots:
void onConnected();
void onDisconnected();
void onReadyRead();
void onSocketError();
void onControlConnected();
void onControlDisconnected();
void onControlReadyRead();
void onControlSocketError();
void onPollTimeout();

private:
struct MpdCommand {
QString command;
std::function<void(bool success)> callback;
};

void sendCommand(
    const QString& command,
    const std::function<void(bool success)>& callback = std::function<void(bool)>()
);
void processLine(const QByteArray& line);
void processPendingBuffer();
void runNextCommand();
void sendControlCommand(
    const QString& command,
    const std::function<void(bool success)>& callback = std::function<void(bool)>()
);
void runNextControlCommand();
void processControlLine(const QByteArray& line);
void processControlPendingBuffer();
void updateState();
void updateStatus();
void updateCurrentSong();
void updateAlbumArt();
void clearTrackData();
void onPositionTimeout();
QString escapeMpdString(const QString& value) const;
qreal clamp01(qreal value) const;
void setSongField(const QString& key, const QString& value);
qreal positionFromSampleNow() const;
void setPositionSample(qreal sampleSeconds);
void refreshPositionFromSample();

Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(Mpd, QString, bHost, "127.0.0.1", &Mpd::hostChanged);
Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(Mpd, quint16, bPort, 6600, &Mpd::portChanged);
Q_OBJECT_BINDABLE_PROPERTY(Mpd, QString, bPassword, &Mpd::passwordChanged);
Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(Mpd, bool, bAutoReconnect, true, &Mpd::autoReconnectChanged);

QTcpSocket mSocket;
QTcpSocket mControlSocket;
QTimer mPollTimer;
QTimer mReconnectTimer;
	QTimer mPositionTimer;
MpdPlayer mPlayer;
QQueue<MpdCommand> mCommandQueue;
QQueue<MpdCommand> mControlCommandQueue;
QVariantMap mResponseMap;
QVariantMap mSongMap;
QByteArray mReadBuffer;
QByteArray mControlReadBuffer;
QByteArray mBinaryData;
qint64 mExpectedBinaryBytes = 0;
	qreal mPositionSampleSeconds = 0;
	QDateTime mPositionSampleTimestamp;
	bool mPositionSampleValid = false;
	bool mRunningCommand = false;
	bool mAcceptedGreeting = false;
	bool mControlRunningCommand = false;
	bool mControlAcceptedGreeting = false;
	bool mControlReady = false;
	bool mPausePending = false;
};

} // namespace qs::service::mpd
