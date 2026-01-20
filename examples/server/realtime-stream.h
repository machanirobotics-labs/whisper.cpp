#pragma once

#include "common.h"
#include "whisper.h"
#include <string>
#include <vector>
#include <mutex>
#include <deque>
#include <memory>
#include <atomic>
#include <chrono>

struct realtime_stream_params {
    int32_t n_threads  = std::min(4, (int32_t) std::thread::hardware_concurrency());
    int32_t step_ms    = 3000;
    int32_t length_ms  = 10000;
    int32_t keep_ms    = 200;
    int32_t max_tokens = 32;
    int32_t audio_ctx  = 0;
    int32_t beam_size  = -1;

    float vad_thold    = 0.6f;
    float freq_thold   = 100.0f;

    bool translate     = false;
    bool no_fallback   = false;
    bool print_special = false;
    bool no_context    = true;
    bool no_timestamps = false;
    bool tinydiarize   = false;

    std::string language  = "en";
};

class RealtimeStreamContext {
public:
    RealtimeStreamContext(whisper_context* ctx, const realtime_stream_params& params);
    ~RealtimeStreamContext();

    void addPCMAudio(const float* samples, size_t count);
    void addPCMAudio(const int16_t* samples, size_t count);
    
    std::string processIfReady();
    std::string flush();
    
    bool shouldProcess() const;
    void reset();

private:
    whisper_context* ctx_;
    realtime_stream_params params_;
    
    std::deque<float> audio_buffer_;
    std::vector<float> pcmf32_old_;
    std::vector<whisper_token> prompt_tokens_;
    
    mutable std::mutex buffer_mutex_;
    
    int n_samples_step_;
    int n_samples_len_;
    int n_samples_keep_;
    int n_iter_;
    
    std::chrono::steady_clock::time_point last_process_time_;
    
    std::string last_transcription_;
    
    std::string runInference(const std::vector<float>& audio_data);
    std::string extractNewText(const std::string& full_text);
};

struct PerSocketData {
    std::shared_ptr<RealtimeStreamContext> stream_ctx;
    int user_id;
};
