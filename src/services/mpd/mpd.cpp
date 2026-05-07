#include "mpd.hpp"

#include <qbytearray.h>
#include <qdatetime.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qqueue.h>
#include <qstringlist.h>
#include <qtimer.h>

#include "../../core/logcat.hpp"

namespace qs::service::mpd {

namespace {
QS_LOGGING_CATEGORY(logMpd, "quickshell.service.mpd", QtWarningMsg);
}

QString MpdPlaybackState::toString(qs::service::mpd::MpdPlaybackState::Enum status) {
switch (status) {
case MpdPlaybackState::Stopped: return "Stopped";
case MpdPlaybackState::Playing: return "Playing";
case MpdPlaybackState::Paused: return "Paused";
default: return "Unknown";
}
}

void MpdPlayer::setPosition(qreal position) { emit this->positionChangeRequested(position); }
void MpdPlayer::setVolume(qreal volume) { emit this->volumeChangeRequested(volume); }

void MpdPlayer::setConnected(bool connected) { this->bConnected = connected; }
void MpdPlayer::setReady(bool ready) { this->bReady = ready; }

void MpdPlayer::setPlaybackState(MpdPlaybackState::Enum playbackState) {
this->bPlaybackState = playbackState;
this->bIsPlaying = playbackState == MpdPlaybackState::Playing;
}

void MpdPlayer::setTrackTitle(QString title) { this->bTrackTitle = std::move(title); }
void MpdPlayer::setTrackArtist(QString artist) { this->bTrackArtist = std::move(artist); }
void MpdPlayer::setTrackAlbum(QString album) { this->bTrackAlbum = std::move(album); }
void MpdPlayer::setTrackArtUrl(QString artUrl) { this->bTrackArtUrl = std::move(artUrl); }
void MpdPlayer::setTrackFile(QString file) { this->bTrackFile = std::move(file); }
void MpdPlayer::setMetadata(QVariantMap metadata) { this->bMetadata = std::move(metadata); }
void MpdPlayer::setLength(qreal length) { this->bLength = length; }
void MpdPlayer::setPositionInternal(qreal position) { this->bPosition = position; }
void MpdPlayer::setVolumeInternal(qreal volume) { this->bVolume = volume; }

Mpd::Mpd(QObject* parent): QObject(parent), mPlayer(this) {
this->mPollTimer.setInterval(1000);
this->mPollTimer.setSingleShot(false);
this->mReconnectTimer.setInterval(2000);
this->mReconnectTimer.setSingleShot(true);
	this->mPositionTimer.setInterval(250);
	this->mPositionTimer.setSingleShot(false);

// clang-format off
QObject::connect(&this->mSocket, &QTcpSocket::connected, this, &Mpd::onConnected);
QObject::connect(&this->mSocket, &QTcpSocket::disconnected, this, &Mpd::onDisconnected);
QObject::connect(&this->mSocket, &QTcpSocket::readyRead, this, &Mpd::onReadyRead);
QObject::connect(&this->mSocket, &QTcpSocket::errorOccurred, this, &Mpd::onSocketError);
QObject::connect(&this->mPollTimer, &QTimer::timeout, this, &Mpd::onPollTimeout);
QObject::connect(&this->mReconnectTimer, &QTimer::timeout, this, &Mpd::reconnect);
	QObject::connect(&this->mPositionTimer, &QTimer::timeout, this, &Mpd::onPositionTimeout);
QObject::connect(&this->mPlayer, &MpdPlayer::positionChangeRequested, this, [this](qreal position) {
auto command = QString("seekcur %1").arg(position, 0, 'f', 3);
this->sendCommand(command);
});
QObject::connect(&this->mPlayer, &MpdPlayer::volumeChangeRequested, this, [this](qreal volume) {
auto targetVolume = static_cast<int>(this->clamp01(volume) * 100.0);
this->sendCommand(QString("setvol %1").arg(targetVolume));
});
// clang-format on

this->reconnect();
}

MpdPlayer* Mpd::player() { return &this->mPlayer; }

void Mpd::setHost(QString host) {
if (host == this->bHost.value()) return;
this->bHost = std::move(host);
this->reconnect();
}

void Mpd::setPort(quint16 port) {
if (port == this->bPort.value()) return;
this->bPort = port;
this->reconnect();
}

void Mpd::setPassword(QString password) {
if (password == this->bPassword.value()) return;
this->bPassword = std::move(password);
this->reconnect();
}

void Mpd::setAutoReconnect(bool autoReconnect) {
if (autoReconnect == this->bAutoReconnect.value()) return;
this->bAutoReconnect = autoReconnect;
}

void Mpd::reconnect() {
this->mReconnectTimer.stop();
this->mPollTimer.stop();
	this->mPositionTimer.stop();
this->mCommandQueue.clear();
this->mReadBuffer.clear();
this->mResponseMap.clear();
this->mSongMap.clear();
this->mBinaryData.clear();
	this->mExpectedBinaryBytes = 0;
	this->mPositionSampleSeconds = 0;
	this->mPositionSampleTimestamp = QDateTime();
	this->mPositionSampleValid = false;
	this->mRunningCommand = false;
	this->mAcceptedGreeting = false;

if (this->mSocket.state() != QAbstractSocket::UnconnectedState) {
this->mSocket.abort();
}

qCDebug(logMpd) << "Connecting to MPD at" << this->bHost << this->bPort;
this->mSocket.connectToHost(this->bHost, this->bPort);
}

void Mpd::play() { this->sendCommand("play"); }
void Mpd::pause() { this->sendCommand("pause 1"); }
void Mpd::stop() { this->sendCommand("stop"); }

void Mpd::togglePlaying() {
if (this->mPlayer.bindablePlaybackState().value() == MpdPlaybackState::Playing) {
this->pause();
} else {
this->play();
}
}

void Mpd::next() { this->sendCommand("next"); }
void Mpd::previous() { this->sendCommand("previous"); }

void Mpd::seek(qreal offset) {
auto command = QString("seekcur %1").arg(offset >= 0 ? QString("+%1").arg(offset, 0, 'f', 3)
                                             : QString::number(offset, 'f', 3));
this->sendCommand(command);
}

void Mpd::onConnected() {
qCDebug(logMpd) << "Connected to MPD";
this->mPlayer.setConnected(true);
emit this->connectedChanged();
}

void Mpd::onDisconnected() {
qCDebug(logMpd) << "Disconnected from MPD";

this->mPlayer.setConnected(false);
this->mPlayer.setReady(false);
this->clearTrackData();
emit this->connectedChanged();

this->mPollTimer.stop();
	this->mPositionTimer.stop();
this->mCommandQueue.clear();
this->mRunningCommand = false;
this->mAcceptedGreeting = false;

if (this->bAutoReconnect.value()) {
this->mReconnectTimer.start();
}
}

void Mpd::onReadyRead() {
this->mReadBuffer.append(this->mSocket.readAll());
this->processPendingBuffer();
}

void Mpd::onSocketError() {
if (this->mSocket.error() == QAbstractSocket::RemoteHostClosedError) return;
qCWarning(logMpd) << "MPD socket error:" << this->mSocket.errorString();
}

void Mpd::onPollTimeout() { this->updateState(); }

void Mpd::onPositionTimeout() { this->refreshPositionFromSample(); }

void Mpd::sendCommand(const QString& command, const std::function<void(bool)>& callback) {
this->mCommandQueue.enqueue({command, callback});
this->runNextCommand();
}

void Mpd::runNextCommand() {
if (this->mRunningCommand || !this->mAcceptedGreeting) return;
if (this->mSocket.state() != QAbstractSocket::ConnectedState) return;
if (this->mCommandQueue.isEmpty()) return;

const auto& command = this->mCommandQueue.head();
this->mResponseMap.clear();
this->mBinaryData.clear();
this->mExpectedBinaryBytes = 0;
this->mRunningCommand = true;
this->mSocket.write(command.command.toUtf8());
this->mSocket.write("\n");
}

void Mpd::processLine(const QByteArray& line) {
if (!this->mAcceptedGreeting) {
if (line.startsWith("OK MPD ")) {
this->mAcceptedGreeting = true;

if (!this->bPassword.value().isEmpty()) {
auto escaped = this->escapeMpdString(this->bPassword.value());
this->sendCommand(QString("password \"%1\"").arg(escaped));
}

this->sendCommand("binarylimit 16777216");
this->updateState();
this->mPollTimer.start();
this->mPlayer.setReady(true);
this->runNextCommand();
} else {
qCWarning(logMpd) << "Invalid MPD greeting:" << line;
this->mSocket.disconnectFromHost();
}

return;
}

if (line == "OK") {
this->mRunningCommand = false;
if (!this->mCommandQueue.isEmpty()) {
auto command = this->mCommandQueue.dequeue();
if (command.callback) command.callback(true);
}
this->runNextCommand();
return;
}

if (line.startsWith("ACK")) {
qCWarning(logMpd) << "MPD command failed:" << line;
this->mRunningCommand = false;
if (!this->mCommandQueue.isEmpty()) {
auto command = this->mCommandQueue.dequeue();
if (command.callback) command.callback(false);
}
this->runNextCommand();
return;
}

	auto separator = line.indexOf(':');
	if (separator <= 0) return;

	auto key = QString::fromUtf8(line.first(separator));
	auto valueBytes = line.sliced(separator + 1);
	if (!valueBytes.isEmpty() && valueBytes.front() == ' ') valueBytes.remove(0, 1);
	auto value = QString::fromUtf8(valueBytes);
	this->mResponseMap.insert(key, value);

if (key == "binary") {
this->mExpectedBinaryBytes = value.toLongLong();
}
}

void Mpd::processPendingBuffer() {
while (true) {
if (this->mExpectedBinaryBytes > 0) {
if (this->mReadBuffer.size() < this->mExpectedBinaryBytes + 1) return;

this->mBinaryData.append(this->mReadBuffer.first(this->mExpectedBinaryBytes));
this->mReadBuffer.remove(0, this->mExpectedBinaryBytes);

if (!this->mReadBuffer.isEmpty() && this->mReadBuffer.front() == '\n') {
this->mReadBuffer.remove(0, 1);
}

this->mExpectedBinaryBytes = 0;
continue;
}

auto index = this->mReadBuffer.indexOf('\n');
if (index < 0) return;

auto line = this->mReadBuffer.first(index);
this->mReadBuffer.remove(0, index + 1);
if (!line.isEmpty() && line.back() == '\r') line.chop(1);

this->processLine(line);
}
}

void Mpd::updateState() {
if (!this->mAcceptedGreeting) return;
this->updateStatus();
this->updateCurrentSong();
}

void Mpd::updateStatus() {
this->sendCommand("status", [this](bool success) {
if (!success) return;

	auto previousState = this->mPlayer.bindablePlaybackState().value();
auto state = this->mResponseMap.value("state").toString();
if (state == "play") {
this->mPlayer.setPlaybackState(MpdPlaybackState::Playing);
} else if (state == "pause") {
this->mPlayer.setPlaybackState(MpdPlaybackState::Paused);
} else {
this->mPlayer.setPlaybackState(MpdPlaybackState::Stopped);
}

auto elapsed = this->mResponseMap.value("elapsed").toDouble();
auto duration = this->mResponseMap.value("duration").toDouble();
		auto volume = this->mResponseMap.value("volume").toDouble() / 100.0;

		if (this->mPlayer.bindablePlaybackState().value() == MpdPlaybackState::Playing) {
			if (!this->mPositionTimer.isActive()) this->mPositionTimer.start();

			if (elapsed >= 0) {
				if (!this->mPositionSampleValid) {
					this->setPositionSample(elapsed);
				} else {
					auto predicted = this->positionFromSampleNow();
					if (elapsed + 0.35 >= predicted) this->setPositionSample(elapsed);
				}
			}

			this->refreshPositionFromSample();
		} else {
			this->mPositionTimer.stop();
			if (elapsed >= 0) this->setPositionSample(elapsed);
			this->refreshPositionFromSample();
		}

		if (previousState != this->mPlayer.bindablePlaybackState().value()) {
			if (this->mPlayer.bindablePlaybackState().value() != MpdPlaybackState::Playing) {
				this->mPositionTimer.stop();
			}
		}

if (duration >= 0) this->mPlayer.setLength(duration);
if (volume >= 0) this->mPlayer.setVolumeInternal(this->clamp01(volume));
});
}

void Mpd::setSongField(const QString& key, const QString& value) {
if (key == "Artist") {
if (this->mSongMap.contains("artist")) {
auto current = this->mSongMap.value("artist").toString();
this->mSongMap.insert("artist", current + ", " + value);
} else {
this->mSongMap.insert("artist", value);
}
return;
}

this->mSongMap.insert(key.toLower(), value);
}

void Mpd::updateCurrentSong() {
this->sendCommand("currentsong", [this](bool success) {
if (!success) return;

this->mSongMap.clear();
for (auto it = this->mResponseMap.cbegin(); it != this->mResponseMap.cend(); ++it) {
this->setSongField(it.key(), it.value().toString());
}

this->mPlayer.setMetadata(this->mSongMap);
this->mPlayer.setTrackTitle(this->mSongMap.value("title").toString());
this->mPlayer.setTrackArtist(this->mSongMap.value("artist").toString());
this->mPlayer.setTrackAlbum(this->mSongMap.value("album").toString());

auto oldFile = this->mPlayer.bindableTrackFile().value();
auto newFile = this->mSongMap.value("file").toString();
this->mPlayer.setTrackFile(newFile);

if (oldFile != newFile) {
	this->mPositionSampleSeconds = 0;
	this->mPositionSampleTimestamp = QDateTime();
	this->mPositionSampleValid = false;
this->mPlayer.setTrackArtUrl(QString());
this->updateAlbumArt();
}

auto duration = this->mSongMap.value("time").toString();
if (!duration.isEmpty()) {
auto split = duration.split(':');
if (split.size() == 2) {
auto total = split.at(1).toDouble();
if (total > 0) this->mPlayer.setLength(total);
}
}
});
}

void Mpd::updateAlbumArt() {
auto file = this->mPlayer.bindableTrackFile().value();
if (file.isEmpty()) return;

auto escaped = this->escapeMpdString(file);

auto applyPicture = [this](bool success) {
if (!success || this->mBinaryData.isEmpty()) return false;

auto mimeType = this->mResponseMap.value("type").toString();
if (mimeType.isEmpty()) mimeType = "image/jpeg";
auto url = QString("data:%1;base64,%2")
               .arg(mimeType, QString::fromUtf8(this->mBinaryData.toBase64()));
this->mPlayer.setTrackArtUrl(url);
return true;
};

this->sendCommand(QString("readpicture \"%1\" 0").arg(escaped), [this, applyPicture, escaped](bool success) {
if (applyPicture(success)) return;

this->sendCommand(QString("albumart \"%1\" 0").arg(escaped), [this, applyPicture](bool fallbackSuccess) {
applyPicture(fallbackSuccess);
});
});
}

void Mpd::clearTrackData() {
	this->mPositionTimer.stop();
	this->mPositionSampleSeconds = 0;
	this->mPositionSampleTimestamp = QDateTime();
	this->mPositionSampleValid = false;
this->mPlayer.setPlaybackState(MpdPlaybackState::Stopped);
this->mPlayer.setTrackTitle(QString());
this->mPlayer.setTrackArtist(QString());
this->mPlayer.setTrackAlbum(QString());
this->mPlayer.setTrackArtUrl(QString());
this->mPlayer.setTrackFile(QString());
this->mPlayer.setMetadata(QVariantMap());
this->mPlayer.setPositionInternal(0);
this->mPlayer.setLength(0);
}

qreal Mpd::positionFromSampleNow() const {
	if (!this->mPositionSampleValid) return this->mPlayer.bindablePosition().value();

	auto position = this->mPositionSampleSeconds;
	if (this->mPlayer.bindablePlaybackState().value() == MpdPlaybackState::Playing
	    && this->mPositionSampleTimestamp.isValid()) {
		position += static_cast<qreal>(
		    this->mPositionSampleTimestamp.msecsTo(QDateTime::currentDateTimeUtc())
		) / 1000.0;
	}

	auto length = this->mPlayer.bindableLength().value();
	if (length > 0 && position > length) position = length;
	if (position < 0) position = 0;
	return position;
}

void Mpd::setPositionSample(qreal sampleSeconds) {
	this->mPositionSampleSeconds = sampleSeconds;
	this->mPositionSampleTimestamp = QDateTime::currentDateTimeUtc();
	this->mPositionSampleValid = true;
}

void Mpd::refreshPositionFromSample() {
	this->mPlayer.setPositionInternal(this->positionFromSampleNow());
}

QString Mpd::escapeMpdString(const QString& value) const {
auto escaped = value;
escaped.replace('\\', "\\\\");
escaped.replace('"', "\\\"");
return escaped;
}

qreal Mpd::clamp01(qreal value) const {
if (value < 0) return 0;
if (value > 1) return 1;
return value;
}

} // namespace qs::service::mpd
