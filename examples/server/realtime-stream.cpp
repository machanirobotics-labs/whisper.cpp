#include "realtime-stream.h"
#include "common-whisper.h"
#include <cstring>
#include <algorithm>
#include <cmath>

RealtimeStreamContext::RealtimeStreamContext(whisper_context* ctx, const realtime_stream_params& params)
    : ctx_(ctx), params_(params), n_iter_(0) {
    
    n_samples_step_ = (1e-3 * params_.step_ms) * WHISPER_SAMPLE_RATE;
    n_samples_len_  = (1e-3 * params_.length_ms) * WHISPER_SAMPLE_RATE;
    n_samples_keep_ = (1e-3 * params_.keep_ms) * WHISPER_SAMPLE_RATE;
    
    last_process_time_ = std::chrono::steady_clock::now();
}

RealtimeStreamContext::~RealtimeStreamContext() {
}

void RealtimeStreamContext::addPCMAudio(const float* samples, size_t count) {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    audio_buffer_.insert(audio_buffer_.end(), samples, samples + count);
    
    const size_t max_buffer_size = n_samples_len_ * 2;
    while (audio_buffer_.size() > max_buffer_size) {
        audio_buffer_.pop_front();
    }
}

void RealtimeStreamContext::addPCMAudio(const int16_t* samples, size_t count) {
    std::vector<float> float_samples(count);
    for (size_t i = 0; i < count; i++) {
        float_samples[i] = static_cast<float>(samples[i]) / 32768.0f;
    }
    addPCMAudio(float_samples.data(), count);
}

bool RealtimeStreamContext::shouldProcess() const {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    
    if (audio_buffer_.size() < static_cast<size_t>(n_samples_step_)) {
        return false;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_process_time_).count();
    
    return elapsed >= params_.step_ms;
}

std::string RealtimeStreamContext::processIfReady() {
    if (!shouldProcess()) {
        return "";
    }
    
    std::vector<float> audio_to_process;
    
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        
        const int n_samples_new = std::min(static_cast<int>(audio_buffer_.size()), n_samples_step_);
        
        const int n_samples_take = std::min(
            static_cast<int>(pcmf32_old_.size()), 
            std::max(0, n_samples_keep_ + n_samples_len_ - n_samples_new)
        );
        
        audio_to_process.resize(n_samples_new + n_samples_take);
        
        for (int i = 0; i < n_samples_take; i++) {
            audio_to_process[i] = pcmf32_old_[pcmf32_old_.size() - n_samples_take + i];
        }
        
        std::copy(audio_buffer_.begin(), audio_buffer_.begin() + n_samples_new, 
                  audio_to_process.begin() + n_samples_take);
        
        pcmf32_old_ = audio_to_process;
        
        audio_buffer_.erase(audio_buffer_.begin(), audio_buffer_.begin() + n_samples_new);
        
        last_process_time_ = std::chrono::steady_clock::now();
    }
    
    std::string full_transcription = runInference(audio_to_process);
    return extractNewText(full_transcription);
}

std::string RealtimeStreamContext::runInference(const std::vector<float>& audio_data) {
    if (audio_data.empty()) {
        return "";
    }
    
    whisper_full_params wparams = whisper_full_default_params(
        params_.beam_size > 1 ? WHISPER_SAMPLING_BEAM_SEARCH : WHISPER_SAMPLING_GREEDY
    );

    wparams.print_progress   = false;
    wparams.print_special    = params_.print_special;
    wparams.print_realtime   = false;
    wparams.print_timestamps = !params_.no_timestamps;
    wparams.translate        = params_.translate;
    wparams.single_segment   = true;
    wparams.max_tokens       = params_.max_tokens;
    wparams.language         = params_.language.c_str();
    wparams.n_threads        = params_.n_threads;
    wparams.beam_search.beam_size = params_.beam_size;
    wparams.audio_ctx        = params_.audio_ctx;
    wparams.tdrz_enable      = params_.tinydiarize;
    wparams.temperature_inc  = params_.no_fallback ? 0.0f : wparams.temperature_inc;
    wparams.prompt_tokens    = params_.no_context ? nullptr : prompt_tokens_.data();
    wparams.prompt_n_tokens  = params_.no_context ? 0 : prompt_tokens_.size();

    if (whisper_full(ctx_, wparams, audio_data.data(), audio_data.size()) != 0) {
        return "";
    }

    std::string result;
    const int n_segments = whisper_full_n_segments(ctx_);
    
    for (int i = 0; i < n_segments; ++i) {
        const char* text = whisper_full_get_segment_text(ctx_, i);
        
        if (!params_.no_timestamps) {
            const int64_t t0 = whisper_full_get_segment_t0(ctx_, i);
            const int64_t t1 = whisper_full_get_segment_t1(ctx_, i);
            result += "[" + to_timestamp(t0, false) + " --> " + to_timestamp(t1, false) + "]  ";
        }
        
        result += text;
        
        if (params_.tinydiarize) {
            if (whisper_full_get_segment_speaker_turn_next(ctx_, i)) {
                result += " [SPEAKER_TURN]";
            }
        }
    }

    n_iter_++;

    if (!params_.no_context && n_segments > 0) {
        prompt_tokens_.clear();
        for (int i = 0; i < n_segments; ++i) {
            const int token_count = whisper_full_n_tokens(ctx_, i);
            for (int j = 0; j < token_count; ++j) {
                prompt_tokens_.push_back(whisper_full_get_token_id(ctx_, i, j));
            }
        }
    }

    return result;
}

std::string RealtimeStreamContext::extractNewText(const std::string& full_text) {
    if (full_text.empty()) {
        return "";
    }
    
    // Remove timestamps for comparison
    auto removeTimestamps = [](const std::string& text) -> std::string {
        std::string result;
        bool in_timestamp = false;
        for (char c : text) {
            if (c == '[') {
                in_timestamp = true;
            } else if (c == ']' && in_timestamp) {
                in_timestamp = false;
                continue;
            }
            if (!in_timestamp) {
                result += c;
            }
        }
        return result;
    };
    
    std::string current_clean = removeTimestamps(full_text);
    std::string last_clean = removeTimestamps(last_transcription_);
    
    // Trim whitespace
    auto trim = [](std::string s) {
        s.erase(0, s.find_first_not_of(" \t\n\r"));
        s.erase(s.find_last_not_of(" \t\n\r") + 1);
        return s;
    };
    
    current_clean = trim(current_clean);
    last_clean = trim(last_clean);
    
    // Find new text by checking if current starts with last
    std::string new_text;
    if (current_clean.size() > last_clean.size() && 
        current_clean.substr(0, last_clean.size()) == last_clean) {
        // Extract only the new part
        new_text = current_clean.substr(last_clean.size());
        new_text = trim(new_text);
    } else if (current_clean != last_clean) {
        // Text changed completely, return full text
        new_text = current_clean;
    }
    
    last_transcription_ = full_text;
    
    return new_text.empty() ? "" : new_text;
}

std::string RealtimeStreamContext::flush() {
    std::vector<float> audio_to_process;
    
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        
        if (audio_buffer_.empty()) {
            return "";
        }
        
        const int n_samples_new = audio_buffer_.size();
        const int n_samples_take = std::min(
            static_cast<int>(pcmf32_old_.size()), 
            std::max(0, n_samples_keep_ + n_samples_len_ - n_samples_new)
        );
        
        audio_to_process.resize(n_samples_new + n_samples_take);
        
        for (int i = 0; i < n_samples_take; i++) {
            audio_to_process[i] = pcmf32_old_[pcmf32_old_.size() - n_samples_take + i];
        }
        
        std::copy(audio_buffer_.begin(), audio_buffer_.end(), 
                  audio_to_process.begin() + n_samples_take);
        
        audio_buffer_.clear();
        pcmf32_old_.clear();
        
        last_process_time_ = std::chrono::steady_clock::now();
    }
    
    std::string full_transcription = runInference(audio_to_process);
    return extractNewText(full_transcription);
}

void RealtimeStreamContext::reset() {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    audio_buffer_.clear();
    pcmf32_old_.clear();
    prompt_tokens_.clear();
    last_transcription_.clear();
    n_iter_ = 0;
    last_process_time_ = std::chrono::steady_clock::now();
}
