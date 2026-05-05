#include "ilys/midi/MidiEngine.hpp"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl2.h>
#include <miniaudio.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr unsigned int sampleRate = 48000;
constexpr std::size_t ringSize = 16384;
constexpr std::size_t analysisSize = 4096;
constexpr float a4Frequency = 440.0F;

struct TunerReading {
    float frequency{0.0F};
    int midiNote{-1};
    float cents{0.0F};
    float confidence{0.0F};
    std::string source{"audio input"};
};

struct SharedState {
    std::mutex audioMutex;
    std::array<float, ringSize> ring{};
    std::size_t writeIndex{0};
    std::atomic<float> energy{0.0F};
    std::atomic<float> midiFrequency{0.0F};
    std::atomic<int> midiNote{-1};
    std::atomic<float> midiHit{0.0F};
    std::string audioDeviceName{"no audio input"};
    std::string midiDeviceName{"no MIDI input"};
};

float midiNoteFrequency(int note)
{
    return a4Frequency * std::pow(2.0F, static_cast<float>(note - 69) / 12.0F);
}

int frequencyToMidi(float frequency)
{
    return static_cast<int>(std::round(69.0F + 12.0F * std::log2(frequency / a4Frequency)));
}

std::string noteName(int midiNote)
{
    static constexpr std::array names{
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };

    if (midiNote < 0) {
        return "--";
    }

    const auto octave = (midiNote / 12) - 1;
    return std::string{names[static_cast<std::size_t>(midiNote % 12)]} + std::to_string(octave);
}

TunerReading readingFromFrequency(float frequency, float confidence, std::string source)
{
    if (frequency <= 0.0F) {
        return {};
    }

    const auto midi = frequencyToMidi(frequency);
    const auto target = midiNoteFrequency(midi);
    const auto cents = 1200.0F * std::log2(frequency / target);
    return TunerReading{frequency, midi, cents, confidence, std::move(source)};
}

std::vector<float> snapshotAudio(SharedState& state)
{
    std::vector<float> samples(analysisSize, 0.0F);
    std::lock_guard lock{state.audioMutex};
    const auto start = state.writeIndex >= analysisSize ? state.writeIndex - analysisSize : 0;
    for (std::size_t index = 0; index < analysisSize; ++index) {
        samples[index] = state.ring[(start + index) % ringSize];
    }

    return samples;
}

TunerReading detectPitch(const std::vector<float>& samples)
{
    if (samples.empty()) {
        return {};
    }

    double rms = 0.0;
    for (const auto sample : samples) {
        rms += static_cast<double>(sample * sample);
    }
    rms = std::sqrt(rms / static_cast<double>(samples.size()));
    if (rms < 0.006) {
        return {};
    }

    constexpr float minFrequency = 55.0F;
    constexpr float maxFrequency = 1200.0F;
    const auto minLag = static_cast<int>(sampleRate / maxFrequency);
    const auto maxLag = static_cast<int>(sampleRate / minFrequency);

    int bestLag = 0;
    double bestCorrelation = 0.0;
    for (int lag = minLag; lag <= maxLag; ++lag) {
        double sum = 0.0;
        double leftEnergy = 0.0;
        double rightEnergy = 0.0;
        for (std::size_t index = 0; index + static_cast<std::size_t>(lag) < samples.size(); ++index) {
            const auto left = static_cast<double>(samples[index]);
            const auto right = static_cast<double>(samples[index + static_cast<std::size_t>(lag)]);
            sum += left * right;
            leftEnergy += left * left;
            rightEnergy += right * right;
        }

        const auto correlation = sum / std::sqrt((leftEnergy * rightEnergy) + 1.0e-12);
        if (correlation > bestCorrelation) {
            bestCorrelation = correlation;
            bestLag = lag;
        }
    }

    if (bestLag <= 0 || bestCorrelation < 0.35) {
        return {};
    }

    const auto frequency = static_cast<float>(sampleRate) / static_cast<float>(bestLag);
    return readingFromFrequency(frequency, static_cast<float>(bestCorrelation), "audio input");
}

void audioCallback(ma_device* device, void* /*output*/, const void* input, ma_uint32 frameCount)
{
    auto* state = static_cast<SharedState*>(device->pUserData);
    if (state == nullptr || input == nullptr) {
        return;
    }

    const auto* samples = static_cast<const float*>(input);
    double sum = 0.0;
    {
        std::lock_guard lock{state->audioMutex};
        for (ma_uint32 frame = 0; frame < frameCount; ++frame) {
            const auto sample = samples[frame * device->capture.channels];
            state->ring[state->writeIndex % ringSize] = sample;
            ++state->writeIndex;
            sum += static_cast<double>(sample * sample);
        }
    }

    const auto rms = frameCount == 0 ? 0.0F : static_cast<float>(std::sqrt(sum / static_cast<double>(frameCount)));
    const auto previous = state->energy.load(std::memory_order_relaxed);
    state->energy.store(std::max(previous * 0.94F, std::min(rms * 8.0F, 1.0F)), std::memory_order_relaxed);
}

class AudioCapture {
public:
    explicit AudioCapture(SharedState& state)
        : state_{state}
    {
        if (ma_context_init(nullptr, 0, nullptr, &context_) != MA_SUCCESS) {
            return;
        }
        contextReady_ = true;

        ma_device_info* playbackInfos = nullptr;
        ma_uint32 playbackCount = 0;
        ma_device_info* captureInfos = nullptr;
        ma_uint32 captureCount = 0;
        if (ma_context_get_devices(&context_, &playbackInfos, &playbackCount, &captureInfos, &captureCount) != MA_SUCCESS
            || captureCount == 0) {
            return;
        }

        state_.audioDeviceName = captureInfos[0].name;
        auto config = ma_device_config_init(ma_device_type_capture);
        config.capture.pDeviceID = &captureInfos[0].id;
        config.capture.format = ma_format_f32;
        config.capture.channels = 1;
        config.sampleRate = sampleRate;
        config.dataCallback = audioCallback;
        config.pUserData = &state_;

        if (ma_device_init(&context_, &config, &device_) != MA_SUCCESS) {
            return;
        }
        deviceReady_ = true;
        running_ = ma_device_start(&device_) == MA_SUCCESS;
    }

    ~AudioCapture()
    {
        if (deviceReady_) {
            ma_device_stop(&device_);
            ma_device_uninit(&device_);
        }
        if (contextReady_) {
            ma_context_uninit(&context_);
        }
    }

    [[nodiscard]] bool running() const noexcept
    {
        return running_;
    }

private:
    SharedState& state_;
    ma_context context_{};
    ma_device device_{};
    bool contextReady_{false};
    bool deviceReady_{false};
    bool running_{false};
};

void setupMidi(SharedState& state, ilys::midi::MidiEngine& midi)
{
    midi.setNoteHandlers(
        [&state](unsigned int note, float velocity) {
            state.midiNote.store(static_cast<int>(note), std::memory_order_relaxed);
            state.midiFrequency.store(midiNoteFrequency(static_cast<int>(note)), std::memory_order_relaxed);
            state.midiHit.store(std::clamp(velocity, 0.0F, 1.0F), std::memory_order_relaxed);
        },
        [&state](unsigned int note) {
            if (state.midiNote.load(std::memory_order_relaxed) == static_cast<int>(note)) {
                state.midiHit.store(0.0F, std::memory_order_relaxed);
            }
        }
    );

    const auto devices = midi.listInputDevices();
    if (!devices.empty()) {
        state.midiDeviceName = devices.front().name;
        midi.setInputDevice(devices.front().index);
        midi.start();
    }
}

void drawString(ImDrawList* drawList, ImVec2 topLeft, ImVec2 size, float activity, float frequency, double time)
{
    const auto centerY = topLeft.y + size.y * 0.5F;
    const auto left = topLeft.x + 24.0F;
    const auto right = topLeft.x + size.x - 24.0F;
    const auto amplitude = 8.0F + activity * 54.0F;
    const auto cycles = std::clamp(frequency / 90.0F, 2.0F, 12.0F);

    drawList->AddRectFilled(topLeft, ImVec2{topLeft.x + size.x, topLeft.y + size.y}, IM_COL32(18, 22, 26, 255), 8.0F);
    drawList->AddLine(ImVec2{left, centerY}, ImVec2{right, centerY}, IM_COL32(80, 90, 100, 255), 2.0F);

    std::array<ImVec2, 160> points{};
    for (std::size_t index = 0; index < points.size(); ++index) {
        const auto t = static_cast<float>(index) / static_cast<float>(points.size() - 1);
        const auto x = left + (right - left) * t;
        const auto decay = std::sin(t * 3.14159265F);
        const auto wave = std::sin((t * cycles * 6.2831853F) + static_cast<float>(time * 9.0));
        points[index] = ImVec2{x, centerY + wave * amplitude * decay};
    }
    drawList->AddPolyline(points.data(), static_cast<int>(points.size()), IM_COL32(72, 219, 166, 255), ImDrawFlags_None, 4.0F);
}

} // namespace

int main()
{
    SharedState state;
    AudioCapture capture{state};
    ilys::midi::MidiEngine midi;
    setupMidi(state, midi);

    if (!glfwInit()) {
        std::cerr << "Could not initialize GLFW.\n";
        return 1;
    }

    auto* window = glfwCreateWindow(860, 540, "ILYS Audiovisualizer", nullptr, nullptr);
    if (window == nullptr) {
        glfwTerminate();
        std::cerr << "Could not open audiovisualizer window.\n";
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();

    auto& style = ImGui::GetStyle();
    style.WindowRounding = 8.0F;
    style.FrameRounding = 6.0F;
    style.Colors[ImGuiCol_WindowBg] = ImVec4{0.08F, 0.09F, 0.10F, 1.0F};
    style.Colors[ImGuiCol_Text] = ImVec4{0.88F, 0.92F, 0.92F, 1.0F};
    style.Colors[ImGuiCol_Button] = ImVec4{0.12F, 0.25F, 0.23F, 1.0F};
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4{0.16F, 0.38F, 0.34F, 1.0F};

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL2_Init();

    TunerReading reading;
    double lastAnalysis = 0.0;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        const auto now = glfwGetTime();
        if (now - lastAnalysis > 0.045) {
            auto audioReading = detectPitch(snapshotAudio(state));
            const auto midiFrequency = state.midiFrequency.load(std::memory_order_relaxed);
            const auto midiHit = state.midiHit.load(std::memory_order_relaxed);
            if (audioReading.frequency > 0.0F) {
                reading = audioReading;
            } else if (midiFrequency > 0.0F && midiHit > 0.02F) {
                reading = readingFromFrequency(midiFrequency, midiHit, "MIDI input");
            }
            lastAnalysis = now;
        }

        const auto decayedMidi = state.midiHit.load(std::memory_order_relaxed) * 0.94F;
        state.midiHit.store(decayedMidi, std::memory_order_relaxed);
        const auto activity = std::max(state.energy.load(std::memory_order_relaxed), decayedMidi);

        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        ImGui::SetNextWindowPos(ImVec2{0, 0});
        ImGui::SetNextWindowSize(ImVec2{static_cast<float>(width), static_cast<float>(height)});
        ImGui::Begin("Audiovisualizer", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

        ImGui::TextUnformatted("ILYS Audiovisualizer");
        ImGui::SameLine();
        ImGui::TextColored(capture.running() ? ImVec4{0.45F, 1.0F, 0.72F, 1.0F} : ImVec4{1.0F, 0.52F, 0.42F, 1.0F},
                           capture.running() ? "audio input active" : "audio input unavailable");
        ImGui::Text("Audio: %s", state.audioDeviceName.c_str());
        ImGui::Text("MIDI:  %s", state.midiDeviceName.c_str());
        ImGui::Separator();

        const auto frequency = reading.frequency;
        const auto cents = reading.cents;
        const auto note = noteName(reading.midiNote);
        ImGui::SetWindowFontScale(1.6F);
        ImGui::Text("Note  %s", note.c_str());
        ImGui::SetWindowFontScale(1.0F);
        ImGui::Text("Frequency: %.2f Hz", frequency);
        ImGui::Text("Detune:    %+.1f cents", cents);
        ImGui::Text("Source:    %s", reading.source.c_str());

        const auto gaugeWidth = ImGui::GetContentRegionAvail().x;
        const auto gaugeStart = ImGui::GetCursorScreenPos();
        const auto gaugeHeight = 54.0F;
        auto* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(gaugeStart, ImVec2{gaugeStart.x + gaugeWidth, gaugeStart.y + gaugeHeight},
                                IM_COL32(24, 27, 31, 255), 6.0F);
        const auto centerX = gaugeStart.x + gaugeWidth * 0.5F;
        drawList->AddLine(ImVec2{centerX, gaugeStart.y + 8.0F}, ImVec2{centerX, gaugeStart.y + gaugeHeight - 8.0F},
                          IM_COL32(230, 230, 220, 255), 2.0F);
        const auto needleX = centerX + std::clamp(cents, -50.0F, 50.0F) / 50.0F * (gaugeWidth * 0.45F);
        const auto needleColor = std::abs(cents) < 5.0F ? IM_COL32(72, 219, 166, 255) : IM_COL32(245, 190, 82, 255);
        drawList->AddTriangleFilled(
            ImVec2{needleX, gaugeStart.y + 10.0F},
            ImVec2{needleX - 9.0F, gaugeStart.y + gaugeHeight - 10.0F},
            ImVec2{needleX + 9.0F, gaugeStart.y + gaugeHeight - 10.0F},
            needleColor
        );
        ImGui::Dummy(ImVec2{gaugeWidth, gaugeHeight + 16.0F});

        const auto stringTop = ImGui::GetCursorScreenPos();
        const auto stringHeight = std::max(160.0F, ImGui::GetContentRegionAvail().y - 18.0F);
        drawString(drawList, stringTop, ImVec2{ImGui::GetContentRegionAvail().x, stringHeight},
                   activity, std::max(frequency, 110.0F), now);
        ImGui::Dummy(ImVec2{ImGui::GetContentRegionAvail().x, stringHeight});

        ImGui::End();
        ImGui::Render();

        glViewport(0, 0, width, height);
        glClearColor(0.07F, 0.08F, 0.09F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    midi.stop();
    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
