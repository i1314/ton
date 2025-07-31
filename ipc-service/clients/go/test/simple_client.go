package main

import (
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
	fmt.Println("Simple IPC Test")
	fmt.Println("===============")

	var msgCount atomic.Uint64
	var blockCount atomic.Uint64
	var lastMsgTime atomic.Int64
	var lastBlockTime atomic.Int64

	// Create client for external messages only
	extClient := fastipc.NewClient(fastipc.ExternalMessageChannel, 
		func(timestamp uint64, data []byte) {
			count := msgCount.Add(1)
			now := time.Now().UnixNano()
			lastMsgTime.Store(now)
			
			// Calculate real-time latency
			latencyNs := now - int64(timestamp)
			latencyUs := float64(latencyNs) / 1000.0
			
			fmt.Printf("[%d] ExtMsg: size=%d, latency=%.1f µs\n", 
				count, len(data), latencyUs)
		})

	// Create client for blocks
	blockClient := fastipc.NewClient(fastipc.NewBlockChannel,
		func(timestamp uint64, data []byte) {
			count := blockCount.Add(1)
			now := time.Now().UnixNano()
			lastBlockTime.Store(now)
			
			// Calculate real-time latency
			latencyNs := now - int64(timestamp)
			latencyUs := float64(latencyNs) / 1000.0
			
			fmt.Printf("[%d] Block: size=%d, latency=%.1f µs\n", 
				count, len(data), latencyUs)
		})

	// Connect
	if err := extClient.Connect(); err != nil {
		log.Fatalf("Failed to connect ext msg: %v", err)
	}
	defer extClient.Close()

	if err := blockClient.Connect(); err != nil {
		log.Fatalf("Failed to connect blocks: %v", err)
	}
	defer blockClient.Close()

	fmt.Println("\nListening for messages...")

	// Signal handler
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, os.Interrupt, syscall.SIGTERM)

	// Status ticker
	ticker := time.NewTicker(5 * time.Second)
	defer ticker.Stop()

	startTime := time.Now()

	for {
		select {
		case <-ticker.C:
			runtime := time.Since(startTime).Seconds()
			msgs := msgCount.Load()
			blocks := blockCount.Load()
			
			fmt.Printf("\n--- Status after %.0fs ---\n", runtime)
			fmt.Printf("Messages: %d (%.1f/s)\n", msgs, float64(msgs)/runtime)
			fmt.Printf("Blocks: %d (%.1f/s)\n", blocks, float64(blocks)/runtime)
			
			// Check if still receiving
			lastMsg := lastMsgTime.Load()
			if lastMsg > 0 {
				timeSinceLastMsg := float64(time.Now().UnixNano() - lastMsg) / 1e9
				if timeSinceLastMsg > 2.0 {
					fmt.Printf("WARNING: No messages for %.1f seconds\n", timeSinceLastMsg)
				}
			}

		case <-sigChan:
			fmt.Println("\nShutting down...")
			return
		}
	}
}