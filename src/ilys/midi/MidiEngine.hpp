#pragma once

#include "ilys/core/Result.hpp"
#include "ilys/midi/MidiDevice.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace ilys::midi {

class MidiEngine {
public:
    using NoteOnHandler = std::function<void(unsigned int note, float velocity)>;
    using NoteOffHandler = std::function<void(unsigned int note)>;

    MidiEngine();
    ~MidiEngine();

    MidiEngine(const MidiEngine&) = delete;
    MidiEngine& operator=(const MidiEngine&) = delete;

    [[nodiscard]] std::vector<MidiDevice> listInputDevices() const;
    [[nodiscard]] std::optional<unsigned int> selectedInputIndex() const noexcept;
    [[nodiscard]] bool isRunning() const noexcept;

    void setNoteHandlers(NoteOnHandler onNoteOn, NoteOffHandler onNoteOff);
    core::Result setInputDevice(unsigned int index);
    core::Result start();
    void stop();

private:
    class Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace ilys::midi

