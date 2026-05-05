#include "ilys/audio/AudioEngine.hpp"

#include "ilys/audio/MiniaudioBackend.hpp"

#include <utility>

namespace ilys::audio {

AudioEngine::AudioEngine()
    : impl_{std::make_unique<Impl>()}
{
}

AudioEngine::~AudioEngine() = default;

std::vector<AudioDevice> AudioEngine::listInputDevices() const
{
    return impl_->listInputDevices();
}

std::vector<AudioDevice> AudioEngine::listOutputDevices() const
{
    return impl_->listOutputDevices();
}

const AudioSettings& AudioEngine::settings() const noexcept
{
    return impl_->settings();
}

bool AudioEngine::isRunning() const noexcept
{
    return impl_->isRunning();
}

bool AudioEngine::requiresAudioInput() const noexcept
{
    return impl_->requiresAudioInput();
}

core::Result AudioEngine::setInputDevice(unsigned int index)
{
    return impl_->setInputDevice(index);
}

core::Result AudioEngine::setOutputDevice(unsigned int index)
{
    return impl_->setOutputDevice(index);
}

core::Result AudioEngine::applyPreset(const presets::InstrumentPreset& preset)
{
    return impl_->applyPreset(preset);
}

void AudioEngine::noteOn(unsigned int note, float velocity) noexcept
{
    impl_->noteOn(note, velocity);
}

void AudioEngine::noteOff(unsigned int note) noexcept
{
    impl_->noteOff(note);
}

core::Result AudioEngine::start()
{
    return impl_->start();
}

core::Result AudioEngine::beginRecording(double maxSeconds)
{
    return impl_->beginRecording(maxSeconds);
}

AudioClip AudioEngine::finishRecording()
{
    return impl_->finishRecording();
}

core::Result AudioEngine::playClips(std::vector<AudioClip> clips, bool loop, bool monitorInput)
{
    return impl_->playClips(std::move(clips), loop, monitorInput);
}

core::Result AudioEngine::loadClipFromFile(const std::filesystem::path& path, AudioClip& clip)
{
    return impl_->loadClipFromFile(path, clip);
}

void AudioEngine::stopPlayback() noexcept
{
    impl_->stopPlayback();
}

void AudioEngine::stop()
{
    impl_->stop();
}

} // namespace ilys::audio
