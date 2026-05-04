#include "ilys/midi/MidiEngine.hpp"

#include <RtMidi.h>

#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace ilys::midi {

class MidiEngine::Impl {
public:
    Impl();
    ~Impl();

    [[nodiscard]] std::vector<MidiDevice> listInputDevices() const;
    [[nodiscard]] std::optional<unsigned int> selectedInputIndex() const noexcept;
    [[nodiscard]] bool isRunning() const noexcept;

    void setNoteHandlers(NoteOnHandler onNoteOn, NoteOffHandler onNoteOff);
    core::Result setInputDevice(unsigned int index);
    core::Result start();
    void stop();

private:
    static void midiCallback(double deltaTime, std::vector<unsigned char>* message, void* userData);
    void handleMessage(const std::vector<unsigned char>& message);

    mutable std::mutex mutex_;
    std::unique_ptr<RtMidiIn> input_;
    std::optional<unsigned int> selectedInputIndex_;
    bool running_{false};
    NoteOnHandler onNoteOn_;
    NoteOffHandler onNoteOff_;
};

MidiEngine::Impl::Impl()
{
    try {
        input_ = std::make_unique<RtMidiIn>();
        input_->ignoreTypes(false, false, false);
    } catch (const RtMidiError& error) {
        throw std::runtime_error("Could not initialize MIDI input: " + error.getMessage());
    }
}

MidiEngine::Impl::~Impl()
{
    stop();
}

std::vector<MidiDevice> MidiEngine::Impl::listInputDevices() const
{
    std::lock_guard lock{mutex_};

    std::vector<MidiDevice> devices;
    const auto portCount = input_->getPortCount();
    devices.reserve(portCount);

    for (unsigned int index = 0; index < portCount; ++index) {
        devices.push_back(MidiDevice{index, input_->getPortName(index)});
    }

    return devices;
}

std::optional<unsigned int> MidiEngine::Impl::selectedInputIndex() const noexcept
{
    return selectedInputIndex_;
}

bool MidiEngine::Impl::isRunning() const noexcept
{
    return running_;
}

void MidiEngine::Impl::setNoteHandlers(NoteOnHandler onNoteOn, NoteOffHandler onNoteOff)
{
    std::lock_guard lock{mutex_};
    onNoteOn_ = std::move(onNoteOn);
    onNoteOff_ = std::move(onNoteOff);
}

core::Result MidiEngine::Impl::setInputDevice(unsigned int index)
{
    std::lock_guard lock{mutex_};
    if (running_) {
        return core::Result::failure("Stop MIDI before changing MIDI input device.");
    }

    const auto portCount = input_->getPortCount();
    if (index >= portCount) {
        return core::Result::failure("MIDI input device index out of range.");
    }

    selectedInputIndex_ = index;
    return core::Result::success("MIDI input set to " + input_->getPortName(index));
}

core::Result MidiEngine::Impl::start()
{
    std::lock_guard lock{mutex_};
    if (running_) {
        return core::Result::success("MIDI input is already running.");
    }

    const auto portCount = input_->getPortCount();
    if (portCount == 0) {
        return core::Result::failure("No MIDI input devices found.");
    }

    const auto index = selectedInputIndex_.value_or(0);
    if (index >= portCount) {
        return core::Result::failure("Selected MIDI input device is no longer available.");
    }

    try {
        input_->openPort(index);
        input_->setCallback(&MidiEngine::Impl::midiCallback, this);
        selectedInputIndex_ = index;
        running_ = true;
    } catch (const RtMidiError& error) {
        return core::Result::failure("Could not open MIDI input: " + error.getMessage());
    }

    return core::Result::success("MIDI input started: " + input_->getPortName(index));
}

void MidiEngine::Impl::stop()
{
    std::lock_guard lock{mutex_};
    if (input_ != nullptr && input_->isPortOpen()) {
        input_->cancelCallback();
        input_->closePort();
    }

    running_ = false;
}

void MidiEngine::Impl::midiCallback(double /*deltaTime*/, std::vector<unsigned char>* message, void* userData)
{
    if (message == nullptr || userData == nullptr) {
        return;
    }

    auto* engine = static_cast<MidiEngine::Impl*>(userData);
    engine->handleMessage(*message);
}

void MidiEngine::Impl::handleMessage(const std::vector<unsigned char>& message)
{
    if (message.size() < 3) {
        return;
    }

    const auto status = static_cast<unsigned int>(message[0] & 0xF0U);
    const auto note = static_cast<unsigned int>(message[1]);
    const auto velocity = static_cast<unsigned int>(message[2]);

    NoteOnHandler onNoteOn;
    NoteOffHandler onNoteOff;
    {
        std::lock_guard lock{mutex_};
        onNoteOn = onNoteOn_;
        onNoteOff = onNoteOff_;
    }

    if (status == 0x90U && velocity > 0) {
        if (onNoteOn) {
            onNoteOn(note, static_cast<float>(velocity) / 127.0F);
        }
    } else if (status == 0x80U || (status == 0x90U && velocity == 0)) {
        if (onNoteOff) {
            onNoteOff(note);
        }
    }
}

MidiEngine::MidiEngine()
    : impl_{std::make_unique<Impl>()}
{
}

MidiEngine::~MidiEngine() = default;

std::vector<MidiDevice> MidiEngine::listInputDevices() const
{
    return impl_->listInputDevices();
}

std::optional<unsigned int> MidiEngine::selectedInputIndex() const noexcept
{
    return impl_->selectedInputIndex();
}

bool MidiEngine::isRunning() const noexcept
{
    return impl_->isRunning();
}

void MidiEngine::setNoteHandlers(NoteOnHandler onNoteOn, NoteOffHandler onNoteOff)
{
    impl_->setNoteHandlers(std::move(onNoteOn), std::move(onNoteOff));
}

core::Result MidiEngine::setInputDevice(unsigned int index)
{
    return impl_->setInputDevice(index);
}

core::Result MidiEngine::start()
{
    return impl_->start();
}

void MidiEngine::stop()
{
    impl_->stop();
}

} // namespace ilys::midi

