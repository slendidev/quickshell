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
QObject::connect(&this->mControlSocket, &QTcpSocket::connected, this, &Mpd::onControlConnected);
QObject::connect(&this->mControlSocket, &QTcpSocket::disconnected, this, &Mpd::onControlDisconnected);
QObject::connect(&this->mControlSocket, &QTcpSocket::readyRead, this, &Mpd::onControlReadyRead);
QObject::connect(&this->mControlSocket, &QTcpSocket::errorOccurred, this, &Mpd::onControlSocketError);
QObject::connect(&this->mPollTimer, &QTimer::timeout, this, &Mpd::onPollTimeout);
QObject::connect(&this->mReconnectTimer, &QTimer::timeout, this, &Mpd::reconnect);
	QObject::connect(&this->mPositionTimer, &QTimer::timeout, this, &Mpd::onPositionTimeout);
QObject::connect(&this->mPlayer, &MpdPlayer::positionChangeRequested, this, [this](qreal position) {
this->setPositionSample(position);
this->refreshPositionFromSample();
auto command = QString("seekcur %1").arg(position, 0, 'f', 3);
this->sendControlCommand(command, [this](bool success) {
if (!success) this->updateState();
this->updateState();
QTimer::singleShot(100, this, [this]() { this->updateState(); });
});
});
QObject::connect(&this->mPlayer, &MpdPlayer::volumeChangeRequested, this, [this](qreal volume) {
auto targetVolume = static_cast<int>(this->clamp01(volume) * 100.0);
this->sendControlCommand(QString("setvol %1").arg(targetVolume));
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
	this->mControlCommandQueue.clear();
	this->mReadBuffer.clear();
	this->mControlReadBuffer.clear();
this->mResponseMap.clear();
	this->mControlResponseMap.clear();
this->mSongMap.clear();
this->mBinaryData.clear();
	this->mExpectedBinaryBytes = 0;
	this->mPositionSampleSeconds = 0;
	this->mPositionSampleTimestamp = QDateTime();
	this->mPositionSampleValid = false;
	this->mRunningCommand = false;
	this->mAcceptedGreeting = false;
	this->mControlRunningCommand = false;
	this->mControlAcceptedGreeting = false;
	this->mControlReady = false;
	this->mPausePending = false;
	this->clearSongCache();

if (this->mSocket.state() != QAbstractSocket::UnconnectedState) {
this->mSocket.abort();
}

if (this->mControlSocket.state() != QAbstractSocket::UnconnectedState) {
	this->mControlSocket.abort();
}

qCDebug(logMpd) << "Connecting to MPD at" << this->bHost << this->bPort;
this->mSocket.connectToHost(this->bHost, this->bPort);
this->mControlSocket.connectToHost(this->bHost, this->bPort);
}

void Mpd::play() {
	this->mPlayer.setPlaybackState(MpdPlaybackState::Playing);
	if (!this->mPositionTimer.isActive()) this->mPositionTimer.start();
	this->sendControlCommand("play", [this](bool success) {
		if (!success) this->updateState();
		this->updateState();
		QTimer::singleShot(100, this, [this]() { this->updateState(); });
	});
}

void Mpd::pause() {
	this->mPlayer.setPlaybackState(MpdPlaybackState::Paused);
	this->mPausePending = true;
	if (!this->mPositionTimer.isActive()) this->mPositionTimer.start();
	this->sendControlCommand("pause 1", [this](bool success) {
		if (success) this->setPositionSample(this->positionFromSampleNow());
		if (!success) this->mPausePending = false;
		if (!success) this->updateState();
		this->updateState();
		QTimer::singleShot(100, this, [this]() { this->updateState(); });
	});
}

void Mpd::stop() {
	this->mPlayer.setPlaybackState(MpdPlaybackState::Stopped);
	this->mPausePending = false;
	this->mPositionTimer.stop();
	this->setPositionSample(0);
	this->refreshPositionFromSample();
	this->sendControlCommand("stop", [this](bool success) {
		if (!success) this->updateState();
		this->updateState();
		QTimer::singleShot(100, this, [this]() { this->updateState(); });
	});
}

void Mpd::togglePlaying() {
if (this->mPlayer.bindablePlaybackState().value() == MpdPlaybackState::Playing) {
this->pause();
} else {
this->play();
}
}

void Mpd::next() {
	this->setPositionSample(0);
	this->refreshPositionFromSample();
	this->sendControlCommand("next", [this](bool success) {
		if (!success) this->updateState();
		this->updateState();
		QTimer::singleShot(100, this, [this]() { this->updateState(); });
	});
}

void Mpd::previous() {
	this->setPositionSample(0);
	this->refreshPositionFromSample();
	this->sendControlCommand("previous", [this](bool success) {
		if (!success) this->updateState();
		this->updateState();
		QTimer::singleShot(100, this, [this]() { this->updateState(); });
	});
}

void Mpd::seek(qreal offset) {
	auto target = this->positionFromSampleNow() + offset;
	auto length = this->mPlayer.bindableLength().value();
	if (length > 0 && target > length) target = length;
	if (target < 0) target = 0;
	this->setPositionSample(target);
	this->refreshPositionFromSample();

auto command = QString("seekcur %1").arg(offset >= 0 ? QString("+%1").arg(offset, 0, 'f', 3)
                                             : QString::number(offset, 'f', 3));
this->sendControlCommand(command, [this](bool success) {
	if (!success) this->updateState();
	this->updateState();
	QTimer::singleShot(100, this, [this]() { this->updateState(); });
});
}

void Mpd::onConnected() {
qCDebug(logMpd) << "Connected to MPD";
this->mPlayer.setConnected(true);
emit this->connectedChanged();
}

void Mpd::onControlConnected() { qCDebug(logMpd) << "Connected control socket to MPD"; }

void Mpd::onDisconnected() {
qCDebug(logMpd) << "Disconnected from MPD";

this->mPlayer.setConnected(false);
this->mPlayer.setReady(false);
this->clearTrackData();
emit this->connectedChanged();

this->mPollTimer.stop();
	this->mPositionTimer.stop();
this->mCommandQueue.clear();
this->mControlCommandQueue.clear();
this->mRunningCommand = false;
this->mAcceptedGreeting = false;
	this->mControlRunningCommand = false;
	this->mControlAcceptedGreeting = false;
	this->mControlReady = false;
	this->mPausePending = false;
	this->clearSongCache();

if (this->bAutoReconnect.value()) {
this->mReconnectTimer.start();
}
}

void Mpd::onControlDisconnected() {
	qCDebug(logMpd) << "Disconnected control socket from MPD";
	this->mControlCommandQueue.clear();
	this->mControlRunningCommand = false;
	this->mControlAcceptedGreeting = false;
	this->mControlReady = false;
	this->mPausePending = false;

	if (this->bAutoReconnect.value()) this->mReconnectTimer.start();
}

void Mpd::onReadyRead() {
this->mReadBuffer.append(this->mSocket.readAll());
this->processPendingBuffer();
}

void Mpd::onControlReadyRead() {
	this->mControlReadBuffer.append(this->mControlSocket.readAll());
	this->processControlPendingBuffer();
}

void Mpd::onSocketError() {
if (this->mSocket.error() == QAbstractSocket::RemoteHostClosedError) return;
qCWarning(logMpd) << "MPD socket error:" << this->mSocket.errorString();
}

void Mpd::onControlSocketError() {
	if (this->mControlSocket.error() == QAbstractSocket::RemoteHostClosedError) return;
	qCWarning(logMpd) << "MPD control socket error:" << this->mControlSocket.errorString();
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

void Mpd::sendControlCommand(const QString& command, const std::function<void(bool)>& callback) {
	this->mControlCommandQueue.enqueue({command, callback});
	this->runNextControlCommand();
}

void Mpd::runNextControlCommand() {
	if (this->mControlRunningCommand || !this->mControlReady) return;
	if (this->mControlSocket.state() != QAbstractSocket::ConnectedState) return;
	if (this->mControlCommandQueue.isEmpty()) return;

	const auto& command = this->mControlCommandQueue.head();
	this->mControlRunningCommand = true;
	this->mControlResponseMap.clear();
	this->mControlSocket.write(command.command.toUtf8());
	this->mControlSocket.write("\n");
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

void Mpd::processControlLine(const QByteArray& line) {
	if (!this->mControlAcceptedGreeting) {
		if (line.startsWith("OK MPD ")) {
			this->mControlAcceptedGreeting = true;
			if (this->bPassword.value().isEmpty()) {
				this->mControlReady = true;
				this->runNextControlCommand();
			} else {
				auto escaped = this->escapeMpdString(this->bPassword.value());
				this->mControlRunningCommand = true;
				this->mControlSocket.write(QString("password \"%1\"\n").arg(escaped).toUtf8());
			}
		} else {
			qCWarning(logMpd) << "Invalid MPD control greeting:" << line;
			this->mControlSocket.disconnectFromHost();
		}

		return;
	}

	if (!this->mControlReady) {
		if (line == "OK") {
			this->mControlRunningCommand = false;
			this->mControlReady = true;
			this->runNextControlCommand();
		} else if (line.startsWith("ACK")) {
			qCWarning(logMpd) << "MPD control auth failed:" << line;
			this->mControlRunningCommand = false;
			this->mControlSocket.disconnectFromHost();
		}
		return;
	}

	if (line == "OK") {
		this->mControlRunningCommand = false;
		if (!this->mControlCommandQueue.isEmpty()) {
			auto command = this->mControlCommandQueue.dequeue();
			if (command.callback) command.callback(true);
		}
		this->runNextControlCommand();
		return;
	}

	if (line.startsWith("ACK")) {
		qCWarning(logMpd) << "MPD control command failed:" << line;
		this->mControlRunningCommand = false;
		if (!this->mControlCommandQueue.isEmpty()) {
			auto command = this->mControlCommandQueue.dequeue();
			if (command.callback) command.callback(false);
		}
		this->runNextControlCommand();
		return;
	}

	auto separator = line.indexOf(':');
	if (separator <= 0) return;

	auto key = QString::fromUtf8(line.first(separator));
	auto valueBytes = line.sliced(separator + 1);
	if (!valueBytes.isEmpty() && valueBytes.front() == ' ') valueBytes.remove(0, 1);
	auto value = QString::fromUtf8(valueBytes);
	this->mControlResponseMap.insert(key, value);
}

void Mpd::processControlPendingBuffer() {
	while (true) {
		auto index = this->mControlReadBuffer.indexOf('\n');
		if (index < 0) return;

		auto line = this->mControlReadBuffer.first(index);
		this->mControlReadBuffer.remove(0, index + 1);
		if (!line.isEmpty() && line.back() == '\r') line.chop(1);

		this->processControlLine(line);
	}
}

void Mpd::updateState() {
if (!this->mAcceptedGreeting) return;
this->updateStatus();
this->updateCurrentSong();
}

void Mpd::updateStatus() {
this->sendControlCommand("status", [this](bool success) {
if (!success) return;

	auto previousState = this->mPlayer.bindablePlaybackState().value();
	auto state = this->mControlResponseMap.value("state").toString();
if (state == "play") {
this->mPlayer.setPlaybackState(MpdPlaybackState::Playing);
} else if (state == "pause") {
this->mPlayer.setPlaybackState(MpdPlaybackState::Paused);
} else {
this->mPlayer.setPlaybackState(MpdPlaybackState::Stopped);
}

	auto elapsed = this->mControlResponseMap.value("elapsed").toDouble();
	auto duration = this->mControlResponseMap.value("duration").toDouble();
		auto volume = this->mControlResponseMap.value("volume").toDouble() / 100.0;

		if (this->mPlayer.bindablePlaybackState().value() == MpdPlaybackState::Playing) {
			this->mPausePending = false;
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
			if (this->mPlayer.bindablePlaybackState().value() == MpdPlaybackState::Paused
			    && this->mPausePending) {
				if (!this->mPositionTimer.isActive()) this->mPositionTimer.start();
				if (elapsed >= 0) this->setPositionSample(elapsed);
				this->refreshPositionFromSample();
			} else {
				this->mPausePending = false;
				this->setPositionSample(this->positionFromSampleNow());
				this->mPositionTimer.stop();
				if (elapsed >= 0) this->setPositionSample(elapsed);
				this->refreshPositionFromSample();
			}
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

auto oldFile = this->mPlayer.bindableTrackFile().value();
auto newFile = this->mSongMap.value("file").toString();

	if (!newFile.isEmpty()) this->applyCachedSongData(newFile);
	if (!newFile.isEmpty()) this->cacheSongData(newFile, this->mSongMap);

	this->mPlayer.setMetadata(this->mSongMap);
	this->mPlayer.setTrackTitle(this->mSongMap.value("title").toString());
	this->mPlayer.setTrackArtist(this->mSongMap.value("artist").toString());
	this->mPlayer.setTrackAlbum(this->mSongMap.value("album").toString());
this->mPlayer.setTrackFile(newFile);

if (oldFile != newFile) {
	this->mPositionSampleSeconds = 0;
	this->mPositionSampleTimestamp = QDateTime();
	this->mPositionSampleValid = false;
	this->refreshPositionFromSample();
	if (newFile.isEmpty()) {
		this->mPlayer.setTrackArtUrl(QString());
	} else if (this->shouldFetchAlbumArt(newFile)) {
		this->mPlayer.setTrackArtUrl(QString());
		this->updateAlbumArt();
	}
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

auto applyPicture = [this, file](bool success) {
if (!success || this->mBinaryData.isEmpty()) return false;

auto mimeType = this->mResponseMap.value("type").toString();
if (mimeType.isEmpty()) mimeType = "image/jpeg";
auto url = QString("data:%1;base64,%2")
               .arg(mimeType, QString::fromUtf8(this->mBinaryData.toBase64()));
	if (this->mPlayer.bindableTrackFile().value() == file) this->mPlayer.setTrackArtUrl(url);
	this->cacheAlbumArt(file, url, true);
return true;
};

this->sendCommand(QString("readpicture \"%1\" 0").arg(escaped), [this, applyPicture, escaped, file](bool success) {
if (applyPicture(success)) return;

this->sendCommand(QString("albumart \"%1\" 0").arg(escaped), [this, applyPicture, file](bool fallbackSuccess) {
		if (!applyPicture(fallbackSuccess)) {
			this->cacheAlbumArt(file, QString(), true);
		}
});
});
}

void Mpd::cacheSongData(const QString& file, const QVariantMap& songMap) {
	if (file.isEmpty()) return;

	auto it = this->mSongCache.find(file);
	if (it == this->mSongCache.end()) {
		SongCacheEntry entry;
		entry.title = songMap.value("title").toString();
		entry.artist = songMap.value("artist").toString();
		entry.album = songMap.value("album").toString();
		entry.metadata = songMap;
		this->mSongCache.insert(file, std::move(entry));
		this->mSongCacheOrder.enqueue(file);
		this->evictSongCacheEntriesIfNeeded();
		return;
	}

	it->title = songMap.value("title").toString();
	it->artist = songMap.value("artist").toString();
	it->album = songMap.value("album").toString();
	it->metadata = songMap;
}

bool Mpd::applyCachedSongData(const QString& file) {
	if (file.isEmpty()) return false;
	auto it = this->mSongCache.constFind(file);
	if (it == this->mSongCache.cend()) return false;

	this->mPlayer.setMetadata(it->metadata);
	this->mPlayer.setTrackTitle(it->title);
	this->mPlayer.setTrackArtist(it->artist);
	this->mPlayer.setTrackAlbum(it->album);
	this->mPlayer.setTrackArtUrl(it->artUrl);
	return true;
}

bool Mpd::shouldFetchAlbumArt(const QString& file) const {
	if (file.isEmpty()) return false;
	auto it = this->mSongCache.constFind(file);
	if (it == this->mSongCache.cend()) return true;
	return !it->artFetched;
}

void Mpd::cacheAlbumArt(const QString& file, const QString& artUrl, bool fetched) {
	if (file.isEmpty()) return;

	auto it = this->mSongCache.find(file);
	if (it == this->mSongCache.end()) {
		SongCacheEntry entry;
		entry.artUrl = artUrl;
		entry.artFetched = fetched;
		this->mSongCache.insert(file, std::move(entry));
		this->mSongCacheOrder.enqueue(file);
		this->evictSongCacheEntriesIfNeeded();
		return;
	}

	it->artUrl = artUrl;
	it->artFetched = fetched;
}

void Mpd::clearSongCache() {
	this->mSongCache.clear();
	this->mSongCacheOrder.clear();
}

void Mpd::evictSongCacheEntriesIfNeeded() {
	while (this->mSongCache.size() > SongCacheCapacity && !this->mSongCacheOrder.isEmpty()) {
		auto file = this->mSongCacheOrder.dequeue();
		this->mSongCache.remove(file);
	}
}

void Mpd::clearTrackData() {
	this->mPositionTimer.stop();
	this->mPositionSampleSeconds = 0;
	this->mPositionSampleTimestamp = QDateTime();
	this->mPositionSampleValid = false;
	this->mPausePending = false;
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
	if ((this->mPlayer.bindablePlaybackState().value() == MpdPlaybackState::Playing
	     || this->mPausePending)
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
