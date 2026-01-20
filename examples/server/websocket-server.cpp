#include "App.h"
#include "realtime-stream.h"
#include "common.h"
#include "whisper.h"
#include "json.hpp"
#include <iostream>
#include <thread>
#include <atomic>

using json = nlohmann::ordered_json;

struct WebSocketServerConfig {
    std::string hostname = "127.0.0.1";
    int32_t port = 8081;
    std::string model_path = "models/ggml-base.en.bin";
    bool use_gpu = true;
    bool flash_attn = true;
};

std::atomic<int> next_user_id{1};

void startWebSocketServer(whisper_context* ctx, const WebSocketServerConfig& config) {
    
    uWS::App().ws<PerSocketData>("/*", {
        .compression = uWS::CompressOptions::DISABLED,
        .maxPayloadLength = 16 * 1024 * 1024,
        .idleTimeout = 120,
        .maxBackpressure = 16 * 1024 * 1024,
        .closeOnBackpressureLimit = false,
        .resetIdleTimeoutOnSend = false,
        .sendPingsAutomatically = true,
        
        .upgrade = nullptr,
        
        .open = [ctx](auto *ws) {
            auto *data = ws->getUserData();
            data->user_id = next_user_id++;
            
            realtime_stream_params params;
            params.language = "en";
            params.translate = false;
            params.no_timestamps = false;
            params.step_ms = 3000;
            params.length_ms = 10000;
            
            data->stream_ctx = std::make_shared<RealtimeStreamContext>(ctx, params);
            
            std::cout << "WebSocket connection opened for user " << data->user_id << std::endl;
            
            json welcome = {
                {"type", "connected"},
                {"user_id", data->user_id},
                {"message", "Ready to receive PCM audio data"},
                {"format", "Send binary PCM data: float32 or int16"},
                {"sample_rate", WHISPER_SAMPLE_RATE}
            };
            ws->send(welcome.dump(), uWS::OpCode::TEXT);
        },
        
        .message = [](auto *ws, std::string_view message, uWS::OpCode opCode) {
            auto *data = ws->getUserData();
            
            if (opCode == uWS::OpCode::TEXT) {
                try {
                    json msg = json::parse(message);
                    
                    if (msg.contains("type")) {
                        std::string type = msg["type"];
                        
                        if (type == "config") {
                            if (msg.contains("language")) {
                                std::cout << "User " << data->user_id << " config update (language would be set here)" << std::endl;
                            }
                            
                            json response = {
                                {"type", "config_updated"},
                                {"status", "ok"}
                            };
                            ws->send(response.dump(), uWS::OpCode::TEXT);
                        }
                        else if (type == "flush") {
                            std::string transcription = data->stream_ctx->flush();
                            
                            json response = {
                                {"type", "flush_complete"},
                                {"text", transcription},
                                {"user_id", data->user_id}
                            };
                            ws->send(response.dump(), uWS::OpCode::TEXT);
                        }
                        else if (type == "reset") {
                            data->stream_ctx->reset();
                            json response = {
                                {"type", "reset"},
                                {"status", "ok"}
                            };
                            ws->send(response.dump(), uWS::OpCode::TEXT);
                        }
                    }
                } catch (const std::exception& e) {
                    json error = {
                        {"type", "error"},
                        {"message", std::string("Invalid JSON: ") + e.what()}
                    };
                    ws->send(error.dump(), uWS::OpCode::TEXT);
                }
            }
            else if (opCode == uWS::OpCode::BINARY) {
                if (message.size() % sizeof(float) == 0) {
                    const float* samples = reinterpret_cast<const float*>(message.data());
                    size_t count = message.size() / sizeof(float);
                    data->stream_ctx->addPCMAudio(samples, count);
                }
                else if (message.size() % sizeof(int16_t) == 0) {
                    const int16_t* samples = reinterpret_cast<const int16_t*>(message.data());
                    size_t count = message.size() / sizeof(int16_t);
                    data->stream_ctx->addPCMAudio(samples, count);
                } else {
                    json error = {
                        {"type", "error"},
                        {"message", "Invalid audio data size"}
                    };
                    ws->send(error.dump(), uWS::OpCode::TEXT);
                    return;
                }
                
                std::string transcription = data->stream_ctx->processIfReady();
                
                if (!transcription.empty()) {
                    json result = {
                        {"type", "transcription"},
                        {"text", transcription},
                        {"user_id", data->user_id}
                    };
                    ws->send(result.dump(), uWS::OpCode::TEXT);
                }
            }
        },
        
        .drain = [](auto *ws) {
        },
        
        .ping = [](auto */*ws*/, std::string_view) {
        },
        
        .pong = [](auto */*ws*/, std::string_view) {
        },
        
        .close = [](auto *ws, int code, std::string_view message) {
            auto *data = ws->getUserData();
            std::cout << "WebSocket connection closed for user " << data->user_id 
                      << " (code: " << code << ")" << std::endl;
        }
        
    }).listen(config.port, [config](auto *listen_socket) {
        if (listen_socket) {
            std::cout << "\n==================================================" << std::endl;
            std::cout << "WebSocket server listening on ws://" << config.hostname 
                      << ":" << config.port << std::endl;
            std::cout << "Ready to accept real-time PCM audio streams" << std::endl;
            std::cout << "==================================================" << std::endl;
            std::cout << "\nUsage:" << std::endl;
            std::cout << "1. Connect via WebSocket to ws://" << config.hostname 
                      << ":" << config.port << std::endl;
            std::cout << "2. Send binary PCM audio data (float32 or int16)" << std::endl;
            std::cout << "3. Receive transcriptions as JSON text messages" << std::endl;
            std::cout << "\nSample rate: " << WHISPER_SAMPLE_RATE << " Hz" << std::endl;
            std::cout << "==================================================" << std::endl;
        } else {
            std::cerr << "Failed to bind WebSocket server to port " << config.port << std::endl;
        }
    }).run();
}

int main(int argc, char** argv) {
    ggml_backend_load_all();
    
    WebSocketServerConfig config;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            config.port = std::stoi(argv[++i]);
        } else if (arg == "--host" && i + 1 < argc) {
            config.hostname = argv[++i];
        } else if (arg == "--model" && i + 1 < argc) {
            config.model_path = argv[++i];
        } else if (arg == "--no-gpu") {
            config.use_gpu = false;
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n";
            std::cout << "Options:\n";
            std::cout << "  --port PORT       WebSocket server port (default: 8081)\n";
            std::cout << "  --host HOST       Server hostname (default: 127.0.0.1)\n";
            std::cout << "  --model PATH      Path to whisper model (default: models/ggml-base.en.bin)\n";
            std::cout << "  --no-gpu          Disable GPU acceleration\n";
            std::cout << "  --help            Show this help message\n";
            return 0;
        }
    }
    
    struct whisper_context_params cparams = whisper_context_default_params();
    cparams.use_gpu = config.use_gpu;
    cparams.flash_attn = config.flash_attn;
    
    struct whisper_context* ctx = whisper_init_from_file_with_params(config.model_path.c_str(), cparams);
    
    if (ctx == nullptr) {
        std::cerr << "Error: failed to initialize whisper context from model: " 
                  << config.model_path << std::endl;
        return 1;
    }
    
    std::cout << "Whisper model loaded successfully: " << config.model_path << std::endl;
    
    startWebSocketServer(ctx, config);
    
    whisper_free(ctx);
    
    return 0;
}
