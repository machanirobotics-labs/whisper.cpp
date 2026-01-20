import http from 'k6/http'
import { check, sleep } from 'k6'
import { Counter, Trend } from 'k6/metrics'

// Custom metrics
const transcriptionErrors = new Counter('transcription_errors')
const transcriptionLength = new Trend('transcription_length')
const processingTime = new Trend('processing_time_ms')

export let options = {
  vus: parseInt(__ENV.CONCURRENCY) || 4,
  iterations: parseInt(__ENV.CONCURRENCY) || 4,
  thresholds: {
    'http_req_duration': ['p(95)<30000'], // 95% of requests should complete within 30s
    'http_req_failed': ['rate<0.1'],      // Error rate should be less than 10%
  },
}

// Configuration
const filePath        = __ENV.FILE_PATH
const baseURL         = __ENV.BASE_URL        || 'http://127.0.0.1:8080'
const endpoint        = __ENV.ENDPOINT        || '/inference'
const temperature     = __ENV.TEMPERATURE     || '0.0'
const temperatureInc  = __ENV.TEMPERATURE_INC || '0.2'
const responseFormat  = __ENV.RESPONSE_FORMAT || 'json'
const delayMs         = parseInt(__ENV.DELAY_MS) || 0

// Validate required parameters
if (!filePath) {
  throw new Error('FILE_PATH environment variable is required')
}

// Read the file ONCE at init time
const fileBin = open(filePath, 'b')

if (!fileBin) {
  throw new Error(`Failed to read file: ${filePath}`)
}

console.log(`Benchmarking ${baseURL}${endpoint}`)
console.log(`File: ${filePath} (${fileBin.length} bytes)`)
console.log(`Concurrency: ${options.vus}, Iterations: ${options.iterations}`)
console.log(`Response format: ${responseFormat}`)

export default function () {
  const startTime = Date.now()
  
  const payload = {
    file:            http.file(fileBin, filePath),
    temperature:     temperature,
    temperature_inc: temperatureInc,
    response_format: responseFormat,
  }

  const res = http.post(`${baseURL}${endpoint}`, payload, {
    timeout: '60s',
  })
  
  const duration = Date.now() - startTime
  processingTime.add(duration)
  
  // Check response status
  const statusOk = check(res, {
    'status is 200': (r) => r.status === 200,
    'response has body': (r) => r.body && r.body.length > 0,
  })
  
  if (!statusOk) {
    transcriptionErrors.add(1)
    console.error(`Request failed: ${res.status} - ${res.body}`)
  } else {
    // Validate response based on format
    try {
      if (responseFormat === 'json' || responseFormat === 'verbose_json') {
        const data = JSON.parse(res.body)
        const hasText = check(data, {
          'has text field': (d) => d.text !== undefined,
        })
        
        if (hasText && data.text) {
          transcriptionLength.add(data.text.length)
        }
      } else {
        // For text/srt/vtt formats, just measure length
        transcriptionLength.add(res.body.length)
      }
    } catch (e) {
      transcriptionErrors.add(1)
      console.error(`Failed to parse response: ${e.message}`)
    }
  }
  
  // Optional delay between requests
  if (delayMs > 0) {
    sleep(delayMs / 1000)
  }
}

export function handleSummary(data) {
  return {
    'stdout': textSummary(data, { indent: ' ', enableColors: true }),
  }
}

function textSummary(data, options) {
  const indent = options.indent || ''
  let summary = '\n'
  
  summary += `${indent}Whisper Server Benchmark Results\n`
  summary += `${indent}${'='.repeat(50)}\n\n`
  
  // Request metrics
  summary += `${indent}HTTP Requests:\n`
  summary += `${indent}  Total: ${data.metrics.http_reqs.values.count}\n`
  summary += `${indent}  Failed: ${(data.metrics.http_req_failed.values.rate * 100).toFixed(2)}%\n`
  summary += `${indent}  Duration (avg): ${data.metrics.http_req_duration.values.avg.toFixed(2)}ms\n`
  summary += `${indent}  Duration (p95): ${data.metrics.http_req_duration.values['p(95)'].toFixed(2)}ms\n`
  summary += `${indent}  Duration (max): ${data.metrics.http_req_duration.values.max.toFixed(2)}ms\n\n`
  
  // Custom metrics
  if (data.metrics.transcription_errors) {
    summary += `${indent}Transcription Errors: ${data.metrics.transcription_errors.values.count}\n`
  }
  
  if (data.metrics.transcription_length) {
    summary += `${indent}Transcription Length (avg): ${data.metrics.transcription_length.values.avg.toFixed(0)} chars\n`
  }
  
  if (data.metrics.processing_time_ms) {
    summary += `${indent}Processing Time (avg): ${data.metrics.processing_time_ms.values.avg.toFixed(2)}ms\n`
  }
  
  summary += `\n${indent}${'='.repeat(50)}\n`
  
  return summary
}