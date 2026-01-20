import ws from 'k6/ws'
import { check } from 'k6'
import { Counter, Trend, Rate } from 'k6/metrics'

// Custom metrics
const transcriptionCount = new Counter('transcriptions_received')
const transcriptionErrors = new Counter('transcription_errors')
const transcriptionLength = new Trend('transcription_length')
const connectionDuration = new Trend('connection_duration_ms')
const messageLatency = new Trend('message_latency_ms')
const successRate = new Rate('transcription_success_rate')

export let options = {
  vus: parseInt(__ENV.CONCURRENCY) || 1,
  duration: __ENV.DURATION || '30s',
}

// Configuration
const wsURL           = __ENV.WS_URL           || 'ws://127.0.0.1:8081'
const audioFile       = __ENV.AUDIO_FILE       || null
const chunkSizeMs     = parseInt(__ENV.CHUNK_SIZE_MS) || 100  // Send 100ms chunks
const sampleRate      = parseInt(__ENV.SAMPLE_RATE)   || 16000
const language        = __ENV.LANGUAGE         || 'en'

// Calculate samples per chunk (100ms of audio at 16kHz = 1600 samples)
const samplesPerChunk = Math.floor((chunkSizeMs / 1000) * sampleRate)

console.log(`WebSocket Benchmark Configuration:`)
console.log(`  URL: ${wsURL}`)
console.log(`  Chunk size: ${chunkSizeMs}ms (${samplesPerChunk} samples)`)
console.log(`  Sample rate: ${sampleRate}Hz`)
console.log(`  Concurrency: ${options.vus}`)
console.log(`  Duration: ${options.duration}`)

// Generate synthetic audio data (silence or sine wave)
function generateAudioChunk(samples, frequency = 440) {
  const buffer = new Float32Array(samples)
  
  if (__ENV.AUDIO_TYPE === 'sine') {
    // Generate sine wave
    for (let i = 0; i < samples; i++) {
      buffer[i] = Math.sin(2 * Math.PI * frequency * i / sampleRate) * 0.3
    }
  } else {
    // Generate silence (for testing infrastructure)
    buffer.fill(0)
  }
  
  return buffer.buffer
}

export default function () {
  const connectionStart = Date.now()
  let messagesReceived = 0
  let lastMessageTime = Date.now()
  
  const res = ws.connect(wsURL, {}, function (socket) {
    
    socket.on('open', () => {
      console.log(`VU ${__VU}: Connected to WebSocket`)
    })
    
    socket.on('message', (data) => {
      const now = Date.now()
      const latency = now - lastMessageTime
      messageLatency.add(latency)
      
      try {
        const msg = JSON.parse(data)
        
        if (msg.type === 'connected') {
          console.log(`VU ${__VU}: Received welcome message`)
          
          // Optionally send config
          if (language !== 'en') {
            socket.send(JSON.stringify({
              type: 'config',
              language: language
            }))
          }
          
          // Start sending audio chunks
          sendAudioStream(socket)
          
        } else if (msg.type === 'transcription') {
          messagesReceived++
          transcriptionCount.add(1)
          successRate.add(1)
          
          if (msg.text) {
            transcriptionLength.add(msg.text.length)
            console.log(`VU ${__VU}: Transcription #${messagesReceived}: "${msg.text.substring(0, 50)}..."`)
          }
          
        } else if (msg.type === 'error') {
          transcriptionErrors.add(1)
          successRate.add(0)
          console.error(`VU ${__VU}: Error: ${msg.message}`)
          
        } else if (msg.type === 'config_updated') {
          console.log(`VU ${__VU}: Config updated`)
        }
        
      } catch (e) {
        transcriptionErrors.add(1)
        console.error(`VU ${__VU}: Failed to parse message: ${e.message}`)
      }
      
      lastMessageTime = now
    })
    
    socket.on('error', (e) => {
      transcriptionErrors.add(1)
      console.error(`VU ${__VU}: WebSocket error: ${e.error()}`)
    })
    
    socket.on('close', () => {
      const duration = Date.now() - connectionStart
      connectionDuration.add(duration)
      console.log(`VU ${__VU}: Connection closed after ${duration}ms, received ${messagesReceived} transcriptions`)
    })
    
    // Keep connection alive for test duration
    socket.setTimeout(() => {
      console.log(`VU ${__VU}: Closing connection`)
      socket.close()
    }, 25000) // Close after 25 seconds
  })
  
  check(res, { 
    'WebSocket connection established': (r) => r && r.status === 101 
  })
}

function sendAudioStream(socket) {
  const chunkIntervalMs = chunkSizeMs
  let chunkCount = 0
  const maxChunks = 200 // Send ~20 seconds of audio (200 * 100ms)
  
  const interval = setInterval(() => {
    if (chunkCount >= maxChunks) {
      clearInterval(interval)
      return
    }
    
    const audioChunk = generateAudioChunk(samplesPerChunk)
    socket.sendBinary(audioChunk)
    chunkCount++
    
    if (chunkCount % 10 === 0) {
      console.log(`VU ${__VU}: Sent ${chunkCount} chunks (${chunkCount * chunkSizeMs}ms of audio)`)
    }
    
  }, chunkIntervalMs)
  
  // Store interval ID so it can be cleared on close
  socket.setInterval(interval, chunkIntervalMs)
}

export function handleSummary(data) {
  return {
    'stdout': textSummary(data),
  }
}

function textSummary(data) {
  let summary = '\n'
  
  summary += ' WebSocket Streaming Benchmark Results\n'
  summary += ' ==================================================\n\n'
  
  // Connection metrics
  summary += ' WebSocket Connections:\n'
  summary += `   Total: ${data.metrics.ws_connecting ? data.metrics.ws_connecting.values.count : 0}\n`
  summary += `   Duration (avg): ${data.metrics.connection_duration_ms ? data.metrics.connection_duration_ms.values.avg.toFixed(2) : 0}ms\n\n`
  
  // Transcription metrics
  summary += ' Transcriptions:\n'
  summary += `   Received: ${data.metrics.transcriptions_received ? data.metrics.transcriptions_received.values.count : 0}\n`
  summary += `   Errors: ${data.metrics.transcription_errors ? data.metrics.transcription_errors.values.count : 0}\n`
  
  if (data.metrics.transcription_success_rate) {
    summary += `   Success Rate: ${(data.metrics.transcription_success_rate.values.rate * 100).toFixed(2)}%\n`
  }
  
  if (data.metrics.transcription_length) {
    summary += `   Length (avg): ${data.metrics.transcription_length.values.avg.toFixed(0)} chars\n`
  }
  
  if (data.metrics.message_latency_ms) {
    summary += `   Latency (avg): ${data.metrics.message_latency_ms.values.avg.toFixed(2)}ms\n`
    summary += `   Latency (p95): ${data.metrics.message_latency_ms.values['p(95)'].toFixed(2)}ms\n`
  }
  
  summary += '\n ==================================================\n'
  
  return summary
}
