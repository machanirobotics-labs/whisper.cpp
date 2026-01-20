# WebSocket Real-time Streaming Server for Whisper.cpp

This is a WebSocket-based real-time audio streaming server that accepts continuous PCM audio data from clients (e.g., microphone input) and returns transcriptions in real-time.

## Features

- **Real-time streaming**: Send PCM audio data continuously via WebSocket
- **Bidirectional communication**: Receive audio, send back transcriptions
- **Sliding window processing**: Maintains context between audio chunks
- **Multiple audio formats**: Supports both float32 and int16 PCM data
- **Client-controlled buffering**: Client decides when to send audio chunks
- **Low latency**: Processes audio as it arrives based on configurable intervals

## Quick Setup (3 Steps)

```bash
# 1. Download dependencies automatically
cd examples/server
./setup-websocket.sh

# 2. Build
cd ../../build
cmake ..
make whisper-websocket-server

# 3. Done! The script downloads everything you need.
```

The `setup-websocket.sh` script automatically downloads [uWebSockets](https://github.com/uNetworking/uWebSockets) and uSockets into `examples/server/deps/`. No manual path configuration needed!

## Usage

### Starting the Server

```bash
# Basic usage
./whisper-websocket-server --model models/ggml-base.en.bin

# Custom port and host
./whisper-websocket-server \
  --model models/ggml-base.en.bin \
  --port 8081 \
  --host 0.0.0.0

# Disable GPU
./whisper-websocket-server \
  --model models/ggml-base.en.bin \
  --no-gpu
```

### Command-line Options

- `--port PORT` - WebSocket server port (default: 8081)
- `--host HOST` - Server hostname (default: 127.0.0.1)
- `--model PATH` - Path to whisper model (default: models/ggml-base.en.bin)
- `--no-gpu` - Disable GPU acceleration
- `--help` - Show help message

## WebSocket Protocol

### Connection

Connect to: `ws://localhost:8081/`

Upon connection, you'll receive a welcome message:
```json
{
  "type": "connected",
  "user_id": 1,
  "message": "Ready to receive PCM audio data",
  "format": "Send binary PCM data: float32 or int16",
  "sample_rate": 16000
}
```

### Sending Audio Data

Send **binary** WebSocket messages containing PCM audio data:

**Format 1: Float32 PCM**
- Send raw float32 samples (values between -1.0 and 1.0)
- Sample rate: 16000 Hz
- Mono channel

**Format 2: Int16 PCM**
- Send raw int16 samples (values between -32768 and 32767)
- Sample rate: 16000 Hz
- Mono channel

The server automatically detects the format based on message size.

### Receiving Transcriptions

When enough audio is buffered and processed, you'll receive:
```json
{
  "type": "transcription",
  "text": "[00:00.000 --> 00:03.000]  Hello, this is a test.",
  "user_id": 1
}
```

### Control Messages (JSON Text Messages)

**Flush remaining audio (when user stops speaking):**
```json
{
  "type": "flush"
}
```

This processes any remaining audio in the buffer immediately and returns the final transcription. Use this when the user stops speaking to get the last bit of transcription.

Response:
```json
{
  "type": "flush_complete",
  "text": "final transcription text",
  "user_id": 1
}
```

**Reset the stream context:**
```json
{
  "type": "reset"
}
```

Response:
```json
{
  "type": "reset",
  "status": "ok"
}
```

**Update configuration (future feature):**
```json
{
  "type": "config",
  "language": "en",
  "translate": false
}
```

## Client Example (JavaScript)

```javascript
const ws = new WebSocket('ws://localhost:8081');

ws.onopen = () => {
  console.log('Connected to Whisper WebSocket server');
};

ws.onmessage = (event) => {
  const data = JSON.parse(event.data);
  
  if (data.type === 'connected') {
    console.log('Server ready, sample rate:', data.sample_rate);
    startAudioCapture();
  } else if (data.type === 'transcription') {
    console.log('Transcription:', data.text);
    displayTranscription(data.text);
  } else if (data.type === 'flush_complete') {
    console.log('Final transcription:', data.text);
    displayFinalTranscription(data.text);
  }
};

// When user stops speaking (e.g., after 2 seconds of silence)
let silenceTimer;
function onAudioActivity() {
  clearTimeout(silenceTimer);
  silenceTimer = setTimeout(() => {
    // User stopped speaking, flush remaining audio
    ws.send(JSON.stringify({ type: 'flush' }));
  }, 2000); // 2 seconds of silence
}

// Capture audio from microphone
async function startAudioCapture() {
  const stream = await navigator.mediaDevices.getUserMedia({ 
    audio: {
      sampleRate: 16000,
      channelCount: 1,
      echoCancellation: true,
      noiseSuppression: true
    } 
  });
  
  const audioContext = new AudioContext({ sampleRate: 16000 });
  const source = audioContext.createMediaStreamSource(stream);
  const processor = audioContext.createScriptProcessor(4096, 1, 1);
  
  processor.onaudioprocess = (e) => {
    const inputData = e.inputBuffer.getChannelData(0);
    
    // Send as Float32Array
    ws.send(inputData.buffer);
  };
  
  source.connect(processor);
  processor.connect(audioContext.destination);
}
```

## Client Example (Python)

```python
import asyncio
import websockets
import numpy as np
import pyaudio
import json

SAMPLE_RATE = 16000
CHUNK_SIZE = 4096

async def stream_audio():
    uri = "ws://localhost:8081"
    
    async with websockets.connect(uri) as websocket:
        # Receive welcome message
        welcome = await websocket.recv()
        print(f"Server: {welcome}")
        
        # Start audio capture
        p = pyaudio.PyAudio()
        stream = p.open(
            format=pyaudio.paFloat32,
            channels=1,
            rate=SAMPLE_RATE,
            input=True,
            frames_per_buffer=CHUNK_SIZE
        )
        
        print("Streaming audio...")
        
        async def send_audio():
            while True:
                data = stream.read(CHUNK_SIZE, exception_on_overflow=False)
                await websocket.send(data)
                await asyncio.sleep(0.01)
        
        async def receive_transcriptions():
            while True:
                message = await websocket.recv()
                data = json.loads(message)
                if data['type'] == 'transcription':
                    print(f"Transcription: {data['text']}")
        
        # Run both tasks concurrently
        await asyncio.gather(
            send_audio(),
            receive_transcriptions()
        )

if __name__ == "__main__":
    asyncio.run(stream_audio())
```

## How It Works

1. **Client connects** via WebSocket
2. **Client sends PCM audio chunks** continuously (e.g., every 100ms from microphone)
3. **Server buffers audio** in a sliding window
4. **Server processes** when enough audio is accumulated (configurable via `step_ms`)
5. **Server sends transcription** back to client as JSON
6. **Process repeats** - maintaining context between chunks for better accuracy

## Configuration

Default parameters (can be modified in code):
- `step_ms`: 3000 - Process audio every 3 seconds
- `length_ms`: 10000 - Use 10 seconds of audio context
- `keep_ms`: 200 - Keep 200ms overlap between chunks
- `language`: "en" - English language
- `translate`: false - Don't translate

## Performance Tips

1. **Chunk size**: Send audio in reasonable chunks (e.g., 100-500ms worth)
2. **Network**: Use local network or low-latency connection
3. **GPU**: Enable GPU acceleration for faster processing
4. **Model**: Use smaller models (tiny, base) for lower latency

## Troubleshooting

**Connection refused:**
- Check if server is running
- Verify port is not blocked by firewall

**No transcriptions:**
- Ensure audio format is correct (16kHz, mono)
- Check if enough audio is being sent
- Verify microphone permissions

**High latency:**
- Reduce `step_ms` parameter
- Use smaller whisper model
- Enable GPU acceleration

## License

Same as whisper.cpp - MIT License
