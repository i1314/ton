// Package fastipc provides a high-performance IPC client for TON MEV
package fastipc

import (
	"encoding/binary"
	"errors"
	"fmt"
	"net"
	"sync"
	"sync/atomic"
	"time"
)

// MessageHeader represents the 16-byte message header
type MessageHeader struct {
	TimestampNs uint64 // Nanosecond timestamp
	DataLength  uint32 // Payload size
	Reserved    [4]byte
}

const (
	HeaderSize = 16 // sizeof(MessageHeader)
)

// ChannelType represents the type of IPC channel
type ChannelType int

const (
	ExternalMessageChannel ChannelType = iota
	NewBlockChannel
)

// MessageHandler is called for each received message
type MessageHandler func(timestamp uint64, data []byte)

// Client represents a connection to one IPC channel
type Client struct {
	socketPath string
	conn       net.Conn
	handler    MessageHandler
	
	// Statistics
	messagesReceived atomic.Uint64
	bytesReceived    atomic.Uint64
	lastMessageNs    atomic.Uint64
	
	// Control
	mu       sync.Mutex
	running  atomic.Bool
	wg       sync.WaitGroup
}

// NewClient creates a new IPC client
func NewClient(channelType ChannelType, handler MessageHandler) *Client {
	var socketPath string
	switch channelType {
	case ExternalMessageChannel:
		socketPath = "/tmp/ton-extmsg.ipc"
	case NewBlockChannel:
		socketPath = "/tmp/ton-blocks.ipc"
	default:
		panic("invalid channel type")
	}
	
	return &Client{
		socketPath: socketPath,
		handler:    handler,
	}
}

// Connect establishes connection to the IPC server
func (c *Client) Connect() error {
	c.mu.Lock()
	defer c.mu.Unlock()
	
	if c.running.Load() {
		return errors.New("already connected")
	}
	
	// Connect to Unix domain socket (use "unix" on macOS)
	conn, err := net.Dial("unix", c.socketPath)
	if err != nil {
		return fmt.Errorf("failed to connect to %s: %w", c.socketPath, err)
	}
	
	// Set read buffer size
	if uc, ok := conn.(*net.UnixConn); ok {
		uc.SetReadBuffer(32 * 1024 * 1024) // 32MB
	}
	
	c.conn = conn
	c.running.Store(true)
	
	// Start receiver
	c.wg.Add(1)
	go c.receiveLoop()
	
	return nil
}

// Close closes the connection
func (c *Client) Close() error {
	c.mu.Lock()
	defer c.mu.Unlock()
	
	if !c.running.Load() {
		return nil
	}
	
	c.running.Store(false)
	
	// Close connection
	if c.conn != nil {
		c.conn.Close()
	}
	
	// Wait for receiver to stop
	c.wg.Wait()
	
	return nil
}

// receiveLoop reads messages from the socket
func (c *Client) receiveLoop() {
	defer c.wg.Done()
	
	// Larger buffers for blocks
	const maxMessageSize = 1024 * 1024 // 1MB max
	buffer := make([]byte, 0, maxMessageSize)
	tmpBuffer := make([]byte, 65536)
	
	for c.running.Load() {
		// Read available data
		n, err := c.conn.Read(tmpBuffer)
		if err != nil {
			if c.running.Load() {
				fmt.Printf("Read error: %v\n", err)
			}
			break
		}
		
		// Append to buffer
		buffer = append(buffer, tmpBuffer[:n]...)
		
		// Prevent buffer overflow
		if len(buffer) > maxMessageSize {
			fmt.Printf("Buffer overflow, resetting\n")
			buffer = buffer[:0]
			continue
		}
		
		// Process complete messages in buffer
		for len(buffer) >= HeaderSize {
			// Parse header
			header := MessageHeader{
				TimestampNs: binary.LittleEndian.Uint64(buffer[0:8]),
				DataLength:  binary.LittleEndian.Uint32(buffer[8:12]),
			}
			
			// Check if we have complete message
			expectedSize := HeaderSize + int(header.DataLength)
			if len(buffer) < expectedSize {
				// Wait for more data
				break
			}
			
			// Validate data length
			if header.DataLength > 10*1024*1024 { // 10MB sanity check
				fmt.Printf("Invalid data length: %d\n", header.DataLength)
				// Skip this message and try to recover
				buffer = buffer[HeaderSize:]
				continue
			}
			
			// Extract complete message
			data := make([]byte, header.DataLength)
			copy(data, buffer[HeaderSize:expectedSize])
			
			// Remove processed message from buffer
			buffer = buffer[expectedSize:]
		
			// Update statistics
			c.messagesReceived.Add(1)
			c.bytesReceived.Add(uint64(header.DataLength)) // Count only payload bytes
			c.lastMessageNs.Store(header.TimestampNs)
			
			// Call handler
			if c.handler != nil {
				c.handler(header.TimestampNs, data)
			}
		}
	}
}

// GetStats returns client statistics
func (c *Client) GetStats() (messages, bytes, lastMessageNs uint64) {
	return c.messagesReceived.Load(),
		c.bytesReceived.Load(),
		c.lastMessageNs.Load()
}

// GetLatencyNs returns the latency of the last message in nanoseconds
func (c *Client) GetLatencyNs() int64 {
	lastNs := c.lastMessageNs.Load()
	if lastNs == 0 {
		return -1
	}
	nowNs := time.Now().UnixNano()
	return nowNs - int64(lastNs)
}

// MultiClient manages connections to multiple channels
type MultiClient struct {
	extMsgClient *Client
	blockClient  *Client
}

// NewMultiClient creates a client for both channels
func NewMultiClient(extMsgHandler, blockHandler MessageHandler) *MultiClient {
	return &MultiClient{
		extMsgClient: NewClient(ExternalMessageChannel, extMsgHandler),
		blockClient:  NewClient(NewBlockChannel, blockHandler),
	}
}

// Connect connects to both channels
func (mc *MultiClient) Connect() error {
	if err := mc.extMsgClient.Connect(); err != nil {
		return fmt.Errorf("external message channel: %w", err)
	}
	
	if err := mc.blockClient.Connect(); err != nil {
		mc.extMsgClient.Close()
		return fmt.Errorf("block channel: %w", err)
	}
	
	return nil
}

// Close closes both connections
func (mc *MultiClient) Close() {
	mc.extMsgClient.Close()
	mc.blockClient.Close()
}

// GetExternalMessageClient returns the external message client
func (mc *MultiClient) GetExternalMessageClient() *Client {
	return mc.extMsgClient
}

// GetBlockClient returns the block client
func (mc *MultiClient) GetBlockClient() *Client {
	return mc.blockClient
}