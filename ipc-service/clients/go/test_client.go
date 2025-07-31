package main

import (
  "fmt"
  "log"
  "os"
  "os/signal"
  "sync/atomic"
  "syscall"
  "time"

  fastipc "github.com/ton-blockchain/ton/ipc-service/clients/go/fastipc"
)

// Statistics
type Stats struct {
  extMsgCount  atomic.Uint64
  blockCount   atomic.Uint64
  totalBytes   atomic.Uint64
  maxLatencyNs atomic.Int64
  minLatencyNs atomic.Int64
}

var stats Stats

func init() {
  stats.minLatencyNs.Store(int64(^uint64(0) >> 1)) // Max int64
}

// Handle external messages
func handleExternalMessage(timestamp uint64, data []byte) {
  // Calculate latency
  nowNs := time.Now().UnixNano()
  latencyNs := nowNs - int64(timestamp)

  // Update stats
  stats.extMsgCount.Add(1)
  stats.totalBytes.Add(uint64(len(data)))

  // Update max/min latency
  for {
    oldMax := stats.maxLatencyNs.Load()
    if latencyNs <= oldMax || stats.maxLatencyNs.CompareAndSwap(oldMax, latencyNs) {
      break
    }
  }

  for {
    oldMin := stats.minLatencyNs.Load()
    if latencyNs >= oldMin || stats.minLatencyNs.CompareAndSwap(oldMin, latencyNs) {
      break
    }
  }

  // Print first few bytes
  if len(data) >= 4 {
    fmt.Printf("ExtMsg: latency=%dμs size=%d magic=%02x%02x%02x%02x\n",
      latencyNs/1000, len(data), data[0], data[1], data[2], data[3])
  }
}

// Handle new blocks
func handleNewBlock(timestamp uint64, data []byte) {
  // Calculate latency
  nowNs := time.Now().UnixNano()
  latencyNs := nowNs - int64(timestamp)

  // Update stats
  stats.blockCount.Add(1)
  stats.totalBytes.Add(uint64(len(data)))

  // Print info
  if len(data) >= 4 {
    fmt.Printf("Block: latency=%dμs size=%d magic=%02x%02x%02x%02x\n",
      latencyNs/1000, len(data), data[0], data[1], data[2], data[3])
  }
}

// Print statistics
func printStats() {
  ticker := time.NewTicker(5 * time.Second)
  defer ticker.Stop()

  startTime := time.Now()

  for range ticker.C {
    elapsed := time.Since(startTime).Seconds()
    extCount := stats.extMsgCount.Load()
    blockCount := stats.blockCount.Load()
    totalBytes := stats.totalBytes.Load()
    maxLatency := stats.maxLatencyNs.Load()
    minLatency := stats.minLatencyNs.Load()

    if minLatency == int64(^uint64(0)>>1) {
      minLatency = 0
    }

    fmt.Printf("\n=== Statistics (%.0fs) ===\n", elapsed)
    fmt.Printf("External messages: %d (%.0f/s)\n", extCount, float64(extCount)/elapsed)
    fmt.Printf("Blocks: %d (%.0f/s)\n", blockCount, float64(blockCount)/elapsed)
    fmt.Printf("Total bytes: %d (%.0f KB/s)\n", totalBytes, float64(totalBytes)/elapsed/1024)
    fmt.Printf("Latency: min=%dμs max=%dμs\n", minLatency/1000, maxLatency/1000)
    fmt.Println()
  }
}

func main() {
  log.Println("Fast IPC Test Client")
  log.Println("====================")

  // Create multi-client
  client := fastipc.NewMultiClient(handleExternalMessage, handleNewBlock)

  // Connect to both channels
  if err := client.Connect(); err != nil {
    log.Fatalf("Failed to connect: %v", err)
  }
  defer client.Close()

  log.Println("Connected to IPC channels")

  // Start stats printer
  go printStats()

  // Handle signals
  sigChan := make(chan os.Signal, 1)
  signal.Notify(sigChan, os.Interrupt, syscall.SIGTERM)

  // Test latency measurement
  go func() {
    ticker := time.NewTicker(10 * time.Second)
    defer ticker.Stop()

    for range ticker.C {
      // Get detailed stats from clients
      extClient := client.GetExternalMessageClient()
      blockClient := client.GetBlockClient()

      extMsgs, extBytes, _ := extClient.GetStats()
      blockMsgs, blockBytes, _ := blockClient.GetStats()

      log.Printf("Client stats - ExtMsg: %d msgs, %d bytes | Blocks: %d msgs, %d bytes",
        extMsgs, extBytes, blockMsgs, blockBytes)
    }
  }()

  // Wait for signal
  <-sigChan
  log.Println("\nShutting down...")
}
