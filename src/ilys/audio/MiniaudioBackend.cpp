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

bool AudioEngine::Impl::requiresAudioInput() const noexcept
{
    return processor_.requiresAudioInput();
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

core::Result AudioEngine::Impl::applyPreset(const presets::InstrumentPreset& preset)
{
    processor_.applyPreset(preset);
    return core::Result::success("Loaded preset: " + preset.name);
}

void AudioEngine::Impl::noteOn(unsigned int note, float velocity) noexcept
{
    processor_.noteOn(note, velocity);
}

void AudioEngine::Impl::noteOff(unsigned int note) noexcept
{
    processor_.noteOff(note);
}

core::Result AudioEngine::Impl::start()
{
    return startStream(requiresAudioInput(), "Monitoring started.");
}

core::Result AudioEngine::Impl::startStream(bool needsAudioInput, const std::string& successMessage)
{
    if (isRunning()) {
        return core::Result::success("Audio stream is already running.");
    }

    const auto outputs = playbackDevices();
    const auto inputs = needsAudioInput ? captureDevices() : std::vector<NativeDevice>{};
    if (needsAudioInput && inputs.empty()) {
        return core::Result::failure("No audio input devices found.");
    }

    if (outputs.empty()) {
        return core::Result::failure("No audio output devices found.");
    }

    const auto outputIndex = settings_.outputDeviceIndex.value_or(0);
    const auto inputIndex = settings_.inputDeviceIndex.value_or(0);
    if ((needsAudioInput && inputIndex >= inputs.size()) || outputIndex >= outputs.size()) {
        return core::Result::failure("Selected audio device is no longer available.");
    }

    if (needsAudioInput) {
        settings_.inputDeviceIndex = inputIndex;
        settings_.inputChannels = 1;
    }
    settings_.outputDeviceIndex = outputIndex;
    settings_.outputChannels = preferredOutputChannels(outputs[outputIndex].publicInfo.channelCount);

    processor_.prepare(static_cast<float>(settings_.sampleRate), settings_.outputChannels);

    auto config = ma_device_config_init(needsAudioInput ? ma_device_type_duplex : ma_device_type_playback);
    if (needsAudioInput) {
        config.capture.pDeviceID = &inputs[inputIndex].nativeId;
        config.capture.format = ma_format_f32;
        config.capture.channels = settings_.inputChannels;
    }
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
    return core::Result::success(successMessage);
}

core::Result AudioEngine::Impl::beginRecording(double maxSeconds)
{
    if (recording_.load(std::memory_order_relaxed)) {
        return core::Result::failure("Recording is already active.");
    }

    if (isRunning() && device_.capture.channels == 0) {
        stop();
    }

    const auto streamResult = startStream(true, "Recording stream started.");
    if (!streamResult.ok && !isRunning()) {
        return streamResult;
    }

    maxRecordingSamples_ = static_cast<std::size_t>(
        std::max(1.0, maxSeconds) * static_cast<double>(settings_.sampleRate)
    );
    recordingBuffer_.clear();
    recordingBuffer_.reserve(maxRecordingSamples_);
    recording_.store(true, std::memory_order_release);
    return core::Result::success("Recording started. Press space to stop.");
}

AudioClip AudioEngine::Impl::finishRecording()
{
    recording_.store(false, std::memory_order_release);
    if (deviceReady_) {
        ma_device_stop(&device_);
        ma_device_uninit(&device_);
        deviceReady_ = false;
        running_.store(false, std::memory_order_relaxed);
    }

    AudioClip clip;
    clip.sampleRate = settings_.sampleRate;
    clip.samples = std::move(recordingBuffer_);
    recordingBuffer_.clear();
    maxRecordingSamples_ = 0;
    return clip;
}

core::Result AudioEngine::Impl::playClips(std::vector<AudioClip> clips, bool loop, bool monitorInput)
{
    if (recording_.load(std::memory_order_relaxed)) {
        return core::Result::failure("Stop recording before playback.");
    }

    if (clips.empty()) {
        return core::Result::failure("No unmuted regions contain recorded audio.");
    }

    const auto streamResult = startStream(monitorInput, "Playback stream started.");
    if (!streamResult.ok && !isRunning()) {
        return streamResult;
    }

    playback_.store(false, std::memory_order_release);
    playbackClips_ = std::move(clips);
    playbackPosition_ = 0;
    playbackLength_ = 0;
    for (const auto& clip : playbackClips_) {
        playbackLength_ = std::max(playbackLength_, clip.samples.size());
    }
    loopPlayback_.store(loop, std::memory_order_release);
    playback_.store(true, std::memory_order_release);
    return core::Result::success(loop ? "Loop playback started." : "Playback started.");
}

core::Result AudioEngine::Impl::loadClipFromFile(const std::filesystem::path& path, AudioClip& clip)
{
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 1, settings_.sampleRate);
    ma_decoder decoder{};
    const auto initResult = ma_decoder_init_file(path.string().c_str(), &config, &decoder);
    if (initResult != MA_SUCCESS) {
        return core::Result::failure("Could not import audio file.");
    }

    ma_uint64 frameCount = 0;
    const auto lengthResult = ma_decoder_get_length_in_pcm_frames(&decoder, &frameCount);
    if (lengthResult != MA_SUCCESS || frameCount == 0) {
        ma_decoder_uninit(&decoder);
        return core::Result::failure("Audio file is empty or unsupported.");
    }

    clip.sampleRate = settings_.sampleRate;
    clip.samples.assign(static_cast<std::size_t>(frameCount), 0.0F);

    ma_uint64 framesRead = 0;
    const auto readResult = ma_decoder_read_pcm_frames(&decoder, clip.samples.data(), frameCount, &framesRead);
    ma_decoder_uninit(&decoder);
    if (readResult != MA_SUCCESS) {
        clip.samples.clear();
        return core::Result::failure("Could not decode audio file.");
    }

    clip.samples.resize(static_cast<std::size_t>(framesRead));
    return core::Result::success("Imported audio file.");
}

void AudioEngine::Impl::stopPlayback() noexcept
{
    playback_.store(false, std::memory_order_release);
    loopPlayback_.store(false, std::memory_order_release);
}

void AudioEngine::Impl::stop()
{
    recording_.store(false, std::memory_order_release);
    playback_.store(false, std::memory_order_release);
    loopPlayback_.store(false, std::memory_order_release);

    if (deviceReady_) {
        ma_device_stop(&device_);
        ma_device_uninit(&device_);
        deviceReady_ = false;
    }

    running_.store(false, std::memory_order_relaxed);
}

void AudioEngine::Impl::mixPlayback(float* output, unsigned int outputChannels, ma_uint32 frameCount) noexcept
{
    if (!playback_.load(std::memory_order_acquire) || output == nullptr || outputChannels == 0) {
        return;
    }

    if (playbackLength_ == 0) {
        playback_.store(false, std::memory_order_release);
        return;
    }

    bool anyRemaining = false;
    const auto loop = loopPlayback_.load(std::memory_order_acquire);
    for (ma_uint32 frame = 0; frame < frameCount; ++frame) {
        const auto absolutePosition = playbackPosition_ + frame;
        const auto position = loop ? absolutePosition % playbackLength_ : absolutePosition;
        float mixed = 0.0F;
        bool frameHasAudio = false;

        for (const auto& clip : playbackClips_) {
            if (position >= clip.samples.size()) {
                continue;
            }

            mixed += clip.samples[position];
            frameHasAudio = true;
        }

        if (frameHasAudio) {
            anyRemaining = true;
            mixed = std::clamp(mixed, -1.0F, 1.0F);
            for (unsigned int channel = 0; channel < outputChannels; ++channel) {
                output[(frame * outputChannels) + channel] += mixed;
            }
        }
    }

    playbackPosition_ += frameCount;
    if (!loop && !anyRemaining) {
        playback_.store(false, std::memory_order_release);
    }
}

void AudioEngine::Impl::captureInput(const float* input, unsigned int inputChannels, ma_uint32 frameCount) noexcept
{
    if (!recording_.load(std::memory_order_acquire) || input == nullptr || inputChannels == 0) {
        return;
    }

    const auto remaining = maxRecordingSamples_ > recordingBuffer_.size()
        ? maxRecordingSamples_ - recordingBuffer_.size()
        : 0;
    const auto framesToCapture = std::min<std::size_t>(remaining, frameCount);
    for (std::size_t frame = 0; frame < framesToCapture; ++frame) {
        recordingBuffer_.push_back(input[frame * inputChannels]);
    }

    if (framesToCapture < frameCount) {
        recording_.store(false, std::memory_order_release);
    }
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

    auto* outputSamples = static_cast<float*>(output);
    const auto* inputSamples = static_cast<const float*>(input);

    engine->processor_.process(
        inputSamples,
        device->capture.channels,
        outputSamples,
        device->playback.channels,
        frameCount
    );

    engine->captureInput(outputSamples, device->playback.channels, frameCount);
    engine->mixPlayback(outputSamples, device->playback.channels, frameCount);
}

} // namespace ilys::audio
