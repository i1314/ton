package main

import (
	"encoding/hex"
	"fmt"
	"log"
	"os"
	"os/signal"
	"sync/atomic"
	"syscall"
	"time"

	"github.com/ton-blockchain/ton/ipc-service/clients/go/fastipc"
)

func main() {
	fmt.Println("Fast IPC Go Client Test")
	fmt.Println("=======================")

	// Counters
	var extMsgCount atomic.Uint64
	var blockCount atomic.Uint64
	var totalBytes atomic.Uint64

	// Create multi-client
	client := fastipc.NewMultiClient(
		// External message handler
		func(timestamp uint64, data []byte) {
			extMsgCount.Add(1)
			totalBytes.Add(uint64(len(data)))
			
			// Print first message details
			if extMsgCount.Load() == 1 {
				fmt.Printf("First external message received:\n")
				fmt.Printf("  Timestamp: %d ns\n", timestamp)
				fmt.Printf("  Size: %d bytes\n", len(data))
				// Timestamp is from high_resolution_clock, not Unix epoch
				// Just show current latency check
				fmt.Printf("  Latency check: message received\n")
				if len(data) > 32 {
					fmt.Printf("  Data preview: %s...\n", hex.EncodeToString(data[:32]))
				} else {
					fmt.Printf("  Data: %s\n", hex.EncodeToString(data))
				}
			}
		},
		// Block handler
		func(timestamp uint64, data []byte) {
			blockCount.Add(1)
			totalBytes.Add(uint64(len(data)))
			
			// Print first block details
			if blockCount.Load() == 1 {
				fmt.Printf("\nFirst block received:\n")
				fmt.Printf("  Timestamp: %d ns\n", timestamp)
				fmt.Printf("  Size: %d bytes\n", len(data))
				// Timestamp is from high_resolution_clock, not Unix epoch
				// Just show current latency check
				fmt.Printf("  Latency check: message received\n")
			}
		},
	)

	// Connect
	if err := client.Connect(); err != nil {
		log.Fatalf("Failed to connect: %v", err)
	}
	defer client.Close()

	fmt.Println("\nConnected to IPC channels. Press Ctrl+C to stop...")

	// Setup signal handler
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, os.Interrupt, syscall.SIGTERM)

	// Stats ticker
	ticker := time.NewTicker(time.Second)
	defer ticker.Stop()

	startTime := time.Now()

	for {
		select {
		case <-ticker.C:
			// Print statistics
			runtime := time.Since(startTime).Seconds()
			extMsg := extMsgCount.Load()
			blocks := blockCount.Load()
			bytes := totalBytes.Load()

			fmt.Printf("\n--- Statistics (%.0fs) ---\n", runtime)
			fmt.Printf("External messages: %d (%.0f/s)\n", extMsg, float64(extMsg)/runtime)
			fmt.Printf("Blocks: %d (%.1f/s)\n", blocks, float64(blocks)/runtime)
			fmt.Printf("Total bytes: %d (%.1f MB/s)\n", bytes, float64(bytes)/runtime/1024/1024)

			// Get per-channel stats
			extStats, extBytes, _ := client.GetExternalMessageClient().GetStats()
			blockStats, blockBytes, _ := client.GetBlockClient().GetStats()

			// Latency tracking removed due to non-Unix epoch timestamps

			// Verify stats match
			if extStats != extMsg || blockStats != blocks {
				fmt.Printf("WARNING: Stats mismatch! Handler: %d/%d, Client: %d/%d\n", 
					extMsg, blocks, extStats, blockStats)
			}
			if extBytes+blockBytes != bytes {
				fmt.Printf("WARNING: Bytes mismatch! Handler: %d, Client: %d\n", 
					bytes, extBytes+blockBytes)
			}

		case <-sigChan:
			fmt.Println("\n\nShutting down...")
			return
		}
	}
}