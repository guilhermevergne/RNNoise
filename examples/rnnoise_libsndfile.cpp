#include "sndfile.hh"
#include "cxxopts.hpp"

#include "rnnoise.h"
#include <cstdint>
#include <filesystem>
#include <fmt/core.h>
#include <array>
#include <memory>
#include <utility>
#include <optional>

#include "lazy_file_writer.hpp"
#include "profiling/xcorr_impl.h"
#include "profiling/xcorr_offload_kernel.hpp"

#include <mimalloc.h>

template <auto DeleterFunction>
using CustomDeleter = std::integral_constant<decltype(DeleterFunction), DeleterFunction>;

template <typename ManagedType, auto Functor>
using PointerWrapper = std::unique_ptr<ManagedType, CustomDeleter<Functor>>;


inline constexpr std::size_t AUDIO_BUFFER_LENGTH = 480;
inline constexpr std::size_t NUM_CHANNELS = 1;
inline constexpr std::size_t SAMPLERATE = 48000;

inline constexpr float RNNOISE_PCM16_MULTIPLY_FACTOR = 32768.0f;

using RNNoiseDenoiseStatePtr = DenoiseState*;
using RnnModelPtr = RNNModel*;
using TSamplesBufferArray = std::array<float,AUDIO_BUFFER_LENGTH>;

RnnModelPtr rnn_model_ptr;
RNNoiseDenoiseStatePtr rnnoise_denoise_state_ptr;

static void initialize_rnnoise_library(){
    rnnoise_denoise_state_ptr = rnnoise_create(nullptr);
    rnnoise_set_xcorr_kernel_cb(rnnoise_denoise_state_ptr,xcorr_kernel);
}

static void normalize_to_rnnoise_expected_level(TSamplesBufferArray& samples_buffer){
    for(auto& sample : samples_buffer){
            sample *= RNNOISE_PCM16_MULTIPLY_FACTOR;
    }
}

static void denormalize_from_rnnoise_expected_level(TSamplesBufferArray& samples_buffer){
    for(auto& sample : samples_buffer){
            sample /= RNNOISE_PCM16_MULTIPLY_FACTOR;
    }
}

static void dump_vad_prob(LazyFileWriter& lazy_probe_dumper,float vad_probe_value){
    lazy_probe_dumper.write(vad_probe_value);
}
static void process_audio_recording(
    LazyFileWriter& lazy_vad_probe_writer,
    const std::filesystem::path& input_file,
    const std::filesystem::path& output_file
){
    SndfileHandle input_audio_file_handle{SndfileHandle(input_file.c_str())};

    fmt::println(stdout,"Opened input audio file:{}", input_file.generic_string());
    fmt::println(stdout,"Number of channels:{}", input_audio_file_handle.channels());

    auto input_samplerate = input_audio_file_handle.samplerate();
    fmt::println(stdout,"Samplerate:{}", input_samplerate);

    const bool samplerate_mistmatch{input_audio_file_handle.samplerate() != SAMPLERATE };
    if(samplerate_mistmatch){
        fmt::println(stderr,"Audio samplerate mistmatch! Expected 48K, got:{}", input_samplerate);
        std::exit(-1);
    }
    SndfileHandle output_audio_file_handle{SndfileHandle{
        output_file.c_str(),
        SFM_WRITE,
        SF_FORMAT_WAV | SF_FORMAT_PCM_16,
        NUM_CHANNELS,
        SAMPLERATE
        }
    };

    
    static TSamplesBufferArray samples_buffer{};

    fmt::println(stdout,"Processing audio...");
    while (input_audio_file_handle.read (samples_buffer.data(), samples_buffer.size()) != 0) {
        normalize_to_rnnoise_expected_level(samples_buffer);
        float vad_prob = rnnoise_process_frame(rnnoise_denoise_state_ptr, samples_buffer.data(), samples_buffer.data());
        dump_vad_prob(lazy_vad_probe_writer,vad_prob);
        denormalize_from_rnnoise_expected_level(samples_buffer);
        output_audio_file_handle.write(samples_buffer.data(),samples_buffer.size());
    }
    fmt::println(stdout,"Processing done. WAVE file can be found at: {}", output_file.generic_string());
}

#ifdef WINDOWS_SPECIFIC_MACRO
static const std::wstring DEFAULT_VAD_PROBE_FILENAME = L"vad_prob.txt";
#else
static const std::string DEFAULT_VAD_PROBE_FILENAME = "vad_prob.txt";
#endif

int main(int argc, char** argv){


    cxxopts::Options options("rnnoise_libsoundfile denoiser", "Simple runner of rnnoise over WAVe files with 48K samplerate");
    const auto DEFAULT_VAD_PROBE_PATH {(std::filesystem::current_path()/DEFAULT_VAD_PROBE_FILENAME).generic_string()};

    options.add_options()
    ("input", "Input file to process",cxxopts::value<std::filesystem::path>())
    ("output", "Output file", cxxopts::value<std::filesystem::path>())
    ("vad_probe", "Path to store output VAD prob data", cxxopts::value<std::filesystem::path>()->default_value(DEFAULT_VAD_PROBE_PATH))
    ("help", "Print usage");

    auto result = options.parse(argc, argv);

    if (result.count("help"))
    {
        fmt::print(options.help());
        std::exit(0);
    }

    mi_option_enable(mi_option_verbose);
    mi_option_enable(mi_option_show_stats);


    using TOptionalPathHolder = std::optional<std::filesystem::path>;
    TOptionalPathHolder input_file_path_opt = result["input"].as<std::filesystem::path>();
    TOptionalPathHolder output_file_path_opt = result["output"].as<std::filesystem::path>();
    TOptionalPathHolder output_vad_probe = result["vad_probe"].as<std::filesystem::path>();

    try{
        input_file_path_opt = result["input"].as<std::filesystem::path>();
        output_file_path_opt = result["output"].as<std::filesystem::path>();
        output_vad_probe = result["vad_probe"].as<std::filesystem::path>();
    }
    catch(...){
        std::cerr << "Failed to obtain one of the required CMD args. Check help message below and verify passed options:" << std::endl;
        fmt::print(options.help());
        std::exit(-1);
    }

    LazyFileWriter vad_file_probe(output_vad_probe.value());
    initialize_rnnoise_library();
    process_audio_recording(
        vad_file_probe,
        input_file_path_opt.value(),
        output_file_path_opt.value()
    );

    mi_stats_print(nullptr);
    return 0;
}