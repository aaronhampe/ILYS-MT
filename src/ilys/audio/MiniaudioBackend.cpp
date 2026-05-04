#include "ilys/audio/MiniaudioBackend.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace ilys::audio {
namespace {

std::string backendError(ma_result result)
{
    std::ostringstream stream;
    stream << "miniaudio error " << static_cast<int>(result);
    return stream.str();
}

AudioDevice toPublicDevice(const ma_device_info& info,
                           DeviceDirection direction,
                           unsigned int index)
{
    AudioDevice device;
    device.index = index;
    device.name = info.name;
    device.direction = direction;
    device.channelCount = info.nativeDataFormatCount > 0
        ? info.nativeDataFormats[0].channels
        : 0;
    device.isDefault = info.isDefault == MA_TRUE;
    return device;
}

unsigned int preferredInputChannels(unsigned int reportedChannels)
{
    return reportedChannels == 0 ? 1U : std::max(1U, std::min(2U, reportedChannels));
}

unsigned int preferredOutputChannels(unsigned int reportedChannels)
{
    return reportedChannels == 0 ? 2U : std::max(1U, std::min(2U, reportedChannels));
}

} // namespace

AudioEngine::Impl::Impl()
{
    const auto result = ma_context_init(nullptr, 0, nullptr, &context_);
    if (result != MA_SUCCESS) {
        throw std::runtime_error("Could not initialize audio context: " + backendError(result));
    }

    contextReady_ = true;
}

AudioEngine::Impl::~Impl()
{
    stop();
    if (contextReady_) {
        ma_context_uninit(&context_);
    }
}

std::vector<AudioEngine::Impl::NativeDevice> AudioEngine::Impl::captureDevices() const
{
    ma_device_info* playbackInfos = nullptr;
    ma_uint32 playbackCount = 0;
    ma_device_info* captureInfos = nullptr;
    ma_uint32 captureCount = 0;

    const auto result = ma_context_get_devices(
        const_cast<ma_context*>(&context_),
        &playbackInfos,
        &playbackCount,
        &captureInfos,
        &captureCount
    );

    if (result != MA_SUCCESS) {
        return {};
    }

    std::vector<NativeDevice> devices;
    devices.reserve(captureCount);
    for (ma_uint32 index = 0; index < captureCount; ++index) {
        NativeDevice device;
        device.publicInfo = toPublicDevice(captureInfos[index], DeviceDirection::Input, index);
        device.nativeId = captureInfos[index].id;
        devices.push_back(device);
    }

    return devices;
}

std::vector<AudioEngine::Impl::NativeDevice> AudioEngine::Impl::playbackDevices() const
{
    ma_device_info* playbackInfos = nullptr;
    ma_uint32 playbackCount = 0;
    ma_device_info* captureInfos = nullptr;
    ma_uint32 captureCount = 0;

    const auto result = ma_context_get_devices(
        const_cast<ma_context*>(&context_),
        &playbackInfos,
        &playbackCount,
        &captureInfos,
        &captureCount
    );

    if (result != MA_SUCCESS) {
        return {};
    }

    std::vector<NativeDevice> devices;
    devices.reserve(playbackCount);
    for (ma_uint32 index = 0; index < playbackCount; ++index) {
        NativeDevice device;
        device.publicInfo = toPublicDevice(playbackInfos[index], DeviceDirection::Output, index);
        device.nativeId = playbackInfos[index].id;
        devices.push_back(device);
    }

    return devices;
}

std::vector<AudioDevice> AudioEngine::Impl::listInputDevices() const
{
    const auto nativeDevices = captureDevices();
    std::vector<AudioDevice> devices;
    devices.reserve(nativeDevices.size());
    for (const auto& device : nativeDevices) {
        devices.push_back(device.publicInfo);
    }

    return devices;
}

std::vector<AudioDevice> AudioEngine::Impl::listOutputDevices() const
{
    const auto nativeDevices = playbackDevices();
    std::vector<AudioDevice> devices;
    devices.reserve(nativeDevices.size());
    for (const auto& device : nativeDevices) {
        devices.push_back(device.publicInfo);
    }

    return devices;
}

const AudioSettings& AudioEngine::Impl::settings() const noexcept
{
    return settings_;
}

bool AudioEngine::Impl::isRunning() const noexcept
{
    return running_.load(std::memory_order_relaxed);
}

core::Result AudioEngine::Impl::setInputDevice(unsigned int index)
{
    if (isRunning()) {
        return core::Result::failure("Stop monitoring before changing input device.");
    }

    const auto devices = captureDevices();
    if (index >= devices.size()) {
        return core::Result::failure("Input device index out of range.");
    }

    settings_.inputDeviceIndex = index;
    settings_.inputChannels = preferredInputChannels(devices[index].publicInfo.channelCount);
    return core::Result::success("Input device set to " + devices[index].publicInfo.name);
}

core::Result AudioEngine::Impl::setOutputDevice(unsigned int index)
{
    if (isRunning()) {
        return core::Result::failure("Stop monitoring before changing output device.");
    }

    const auto devices = playbackDevices();
    if (index >= devices.size()) {
        return core::Result::failure("Output device index out of range.");
    }

    settings_.outputDeviceIndex = index;
    settings_.outputChannels = preferredOutputChannels(devices[index].publicInfo.channelCount);
    return core::Result::success("Output device set to " + devices[index].publicInfo.name);
}

core::Result AudioEngine::Impl::applyPreset(const presets::GuitarPreset& preset)
{
    processor_.applyPreset(preset);
    return core::Result::success("Loaded preset: " + preset.name);
}

core::Result AudioEngine::Impl::start()
{
    if (isRunning()) {
        return core::Result::success("Monitoring is already running.");
    }

    const auto inputs = captureDevices();
    const auto outputs = playbackDevices();
    if (inputs.empty()) {
        return core::Result::failure("No audio input devices found.");
    }

    if (outputs.empty()) {
        return core::Result::failure("No audio output devices found.");
    }

    const auto inputIndex = settings_.inputDeviceIndex.value_or(0);
    const auto outputIndex = settings_.outputDeviceIndex.value_or(0);
    if (inputIndex >= inputs.size() || outputIndex >= outputs.size()) {
        return core::Result::failure("Selected audio device is no longer available.");
    }

    settings_.inputDeviceIndex = inputIndex;
    settings_.outputDeviceIndex = outputIndex;
    settings_.inputChannels = 1;
    settings_.outputChannels = preferredOutputChannels(outputs[outputIndex].publicInfo.channelCount);

    processor_.prepare(static_cast<float>(settings_.sampleRate), settings_.outputChannels);

    auto config = ma_device_config_init(ma_device_type_duplex);
    config.capture.pDeviceID = &inputs[inputIndex].nativeId;
    config.capture.format = ma_format_f32;
    config.capture.channels = settings_.inputChannels;
    config.playback.pDeviceID = &outputs[outputIndex].nativeId;
    config.playback.format = ma_format_f32;
    config.playback.channels = settings_.outputChannels;
    config.sampleRate = settings_.sampleRate;
    config.periodSizeInFrames = settings_.framesPerBuffer;
    config.dataCallback = &AudioEngine::Impl::dataCallback;
    config.pUserData = this;

    const auto initResult = ma_device_init(&context_, &config, &device_);
    if (initResult != MA_SUCCESS) {
        return core::Result::failure("Could not open audio device: " + backendError(initResult));
    }
    deviceReady_ = true;

    const auto startResult = ma_device_start(&device_);
    if (startResult != MA_SUCCESS) {
        ma_device_uninit(&device_);
        deviceReady_ = false;
        return core::Result::failure("Could not start audio device: " + backendError(startResult));
    }

    running_.store(true, std::memory_order_relaxed);
    return core::Result::success("Monitoring started.");
}

void AudioEngine::Impl::stop()
{
    if (deviceReady_) {
        ma_device_stop(&device_);
        ma_device_uninit(&device_);
        deviceReady_ = false;
    }

    running_.store(false, std::memory_order_relaxed);
}

void AudioEngine::Impl::dataCallback(ma_device* device,
                                     void* output,
                                     const void* input,
                                     ma_uint32 frameCount)
{
    auto* engine = static_cast<AudioEngine::Impl*>(device->pUserData);
    if (engine == nullptr) {
        return;
    }

    engine->processor_.process(
        static_cast<const float*>(input),
        device->capture.channels,
        static_cast<float*>(output),
        device->playback.channels,
        frameCount
    );
}

} // namespace ilys::audio
