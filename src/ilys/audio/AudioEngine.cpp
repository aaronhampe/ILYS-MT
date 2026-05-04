#include "ilys/audio/AudioEngine.hpp"

#include "ilys/audio/MiniaudioBackend.hpp"

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

core::Result AudioEngine::setInputDevice(unsigned int index)
{
    return impl_->setInputDevice(index);
}

core::Result AudioEngine::setOutputDevice(unsigned int index)
{
    return impl_->setOutputDevice(index);
}

core::Result AudioEngine::applyPreset(const presets::GuitarPreset& preset)
{
    return impl_->applyPreset(preset);
}

core::Result AudioEngine::start()
{
    return impl_->start();
}

void AudioEngine::stop()
{
    impl_->stop();
}

} // namespace ilys::audio

