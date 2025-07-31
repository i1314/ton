package main

import (
	"fmt"
	"log"
	"os"
	"os/signal"
	"syscall"
	"time"
	
	ton_ipc "github.com/ton-blockchain/ton/ipc-service/client/go"
)

func main() {
	// Create client
	client := ton_ipc.NewClient("/tmp/ton-ipc.sock")
	
	// Set handlers
	client.SetExternalMessageHandler(func(msg *ton_ipc.ExternalMessage) {
		fmt.Printf("[EXT_MSG] Time: %s, From: %s, To: %s, Hash: %s, Size: %d bytes\n",
			time.Unix(0, int64(msg.TimestampMs)*1e6).Format("15:04:05.000"),
			msg.SourceAddr,
			msg.DestAddr,
			msg.Hash,
			len(msg.Data))
	})
	
	client.SetNewBlockHandler(func(block *ton_ipc.BlockInfo) {
		fmt.Printf("[NEW_BLOCK] ID: %s, GenTime: %d, Delay: %ds, Accounts: %d, Txs: %d\n",
			block.BlockID,
			block.GenUtime,
			block.DelaySeconds,
			block.AccountCount,
			block.TransactionCount)
		
		for i, hash := range block.TransactionHashes {
			fmt.Printf("  Tx[%d]: %s\n", i+1, hash)
		}
	})
	
	client.SetErrorHandler(func(err error) {
		log.Printf("Error: %v\n", err)
	})
	
	// Connect
	fmt.Println("Connecting to TON IPC service...")
	if err := client.Connect(); err != nil {
		log.Fatalf("Failed to connect: %v", err)
	}
	defer client.Close()
	
	// Subscribe to all events
	fmt.Println("Subscribing to all events...")
	if err := client.Subscribe(ton_ipc.SubscriptionAll); err != nil {
		log.Fatalf("Failed to subscribe: %v", err)
	}
	
	// Stats reporter
	go func() {
		ticker := time.NewTicker(30 * time.Second)
		defer ticker.Stop()
		
		for range ticker.C {
			msgs, blocks, errors := client.GetStats()
			fmt.Printf("\n[STATS] Messages: %d, Blocks: %d, Errors: %d\n\n", 
				msgs, blocks, errors)
		}
	}()
	
	// Wait for interrupt
	fmt.Println("Listening for events... Press Ctrl+C to exit")
	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, os.Interrupt, syscall.SIGTERM)
	<-sigCh
	
	fmt.Println("\nShutting down...")
}