// Package ton_ipc provides a Go client for TON IPC subscription service
package ton_ipc

import (
	"bytes"
	"context"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"net"
	"sync"
	"sync/atomic"
	"time"
)

// MessageType represents the type of IPC message
type MessageType uint8

const (
	MessageTypeExternalMessage MessageType = 1
	MessageTypeNewBlock        MessageType = 2
	MessageTypeSubscribe       MessageType = 3
	MessageTypeUnsubscribe     MessageType = 4
	MessageTypeHeartbeat       MessageType = 5
)

// SubscriptionType represents subscription options
type SubscriptionType uint32

const (
	SubscriptionExternalMessages SubscriptionType = 1 << 0
	SubscriptionNewBlocks        SubscriptionType = 1 << 1
	SubscriptionAll              SubscriptionType = SubscriptionExternalMessages | SubscriptionNewBlocks
)

// MessageHeader represents the fixed-size message header
type MessageHeader struct {
	Magic       uint32
	Version     uint32
	Type        MessageType
	Reserved    [3]byte
	PayloadSize uint32
}

const (
	headerSize     = 16
	magicNumber    = 0x544F4E30 // "TON0"
	protocolVersion = 1
)

// ExternalMessage represents an external message event
type ExternalMessage struct {
	TimestampMs uint64
	SourceAddr  string
	DestAddr    string
	Data        []byte
	Hash        string
}

// BlockInfo represents a new block event
type BlockInfo struct {
	BlockID           string
	GenUtime          uint32
	DelaySeconds      uint32
	AccountCount      uint32
	TransactionCount  uint32
	TransactionHashes []string
	RawBlockData      []byte  // Complete raw block data
	HasRawData        bool
}

// Client represents a TON IPC client
type Client struct {
	socketPath string
	conn       net.Conn
	
	handlers struct {
		externalMsg func(*ExternalMessage)
		newBlock    func(*BlockInfo)
		onError     func(error)
	}
	
	mu          sync.RWMutex
	connected   atomic.Bool
	ctx         context.Context
	cancel      context.CancelFunc
	wg          sync.WaitGroup
	
	stats struct {
		messagesReceived atomic.Uint64
		blocksReceived   atomic.Uint64
		errors           atomic.Uint64
	}
}

// NewClient creates a new IPC client
func NewClient(socketPath string) *Client {
	if socketPath == "" {
		socketPath = "/tmp/ton-ipc.sock"
	}
	ctx, cancel := context.WithCancel(context.Background())
	return &Client{
		socketPath: socketPath,
		ctx:        ctx,
		cancel:     cancel,
	}
}

// Connect establishes connection to the IPC service
func (c *Client) Connect() error {
	c.mu.Lock()
	defer c.mu.Unlock()
	
	if c.connected.Load() {
		return errors.New("already connected")
	}
	
	conn, err := net.Dial("unix", c.socketPath)
	if err != nil {
		return fmt.Errorf("failed to connect: %w", err)
	}
	
	c.conn = conn
	c.connected.Store(true)
	
	// Start receiver goroutine
	c.wg.Add(1)
	go c.receiveLoop()
	
	return nil
}

// Close closes the client connection
func (c *Client) Close() error {
	c.cancel()
	c.wg.Wait()
	
	c.mu.Lock()
	defer c.mu.Unlock()
	
	if !c.connected.Load() {
		return nil
	}
	
	c.connected.Store(false)
	if c.conn != nil {
		err := c.conn.Close()
		c.conn = nil
		return err
	}
	return nil
}

// Subscribe subscribes to specified event types
func (c *Client) Subscribe(types SubscriptionType) error {
	if !c.connected.Load() {
		return errors.New("not connected")
	}
	
	header := MessageHeader{
		Magic:       magicNumber,
		Version:     protocolVersion,
		Type:        MessageTypeSubscribe,
		PayloadSize: 4,
	}
	
	var buf bytes.Buffer
	if err := binary.Write(&buf, binary.LittleEndian, header); err != nil {
		return err
	}
	if err := binary.Write(&buf, binary.LittleEndian, types); err != nil {
		return err
	}
	
	c.mu.RLock()
	defer c.mu.RUnlock()
	
	fmt.Printf("[Client] Sending subscribe request, types=%d, size=%d bytes\n", types, buf.Len())
	
	if _, err := c.conn.Write(buf.Bytes()); err != nil {
		return err
	}
	
	fmt.Printf("[Client] Subscribe request sent successfully\n")
	
	return nil
}

// SetExternalMessageHandler sets the handler for external messages
func (c *Client) SetExternalMessageHandler(handler func(*ExternalMessage)) {
	c.mu.Lock()
	c.handlers.externalMsg = handler
	c.mu.Unlock()
}

// SetNewBlockHandler sets the handler for new blocks
func (c *Client) SetNewBlockHandler(handler func(*BlockInfo)) {
	c.mu.Lock()
	c.handlers.newBlock = handler
	c.mu.Unlock()
}

// SetErrorHandler sets the error handler
func (c *Client) SetErrorHandler(handler func(error)) {
	c.mu.Lock()
	c.handlers.onError = handler
	c.mu.Unlock()
}

// GetStats returns client statistics
func (c *Client) GetStats() (messagesReceived, blocksReceived, errors uint64) {
	return c.stats.messagesReceived.Load(),
		c.stats.blocksReceived.Load(),
		c.stats.errors.Load()
}

func (c *Client) receiveLoop() {
	defer c.wg.Done()
	
	for {
		select {
		case <-c.ctx.Done():
			return
		default:
		}
		
		// Set read deadline to allow periodic context checks
		// Use 5 seconds timeout to avoid frequent timeouts
		c.conn.SetReadDeadline(time.Now().Add(5 * time.Second))
		
		header := MessageHeader{}
		if err := binary.Read(c.conn, binary.LittleEndian, &header); err != nil {
			if netErr, ok := err.(net.Error); ok && netErr.Timeout() {
				continue
			}
			if err != io.EOF {
				c.handleError(fmt.Errorf("failed to read header: %w", err))
			}
			return
		}
		
		// Validate header
		if header.Magic != magicNumber {
			c.handleError(fmt.Errorf("invalid magic number: %x", header.Magic))
			return
		}
		
		if header.Version != protocolVersion {
			c.handleError(fmt.Errorf("unsupported protocol version: %d", header.Version))
			return
		}
		
		// Validate payload size to prevent excessive memory allocation
		if header.PayloadSize > 10*1024*1024 { // 10MB limit
			c.handleError(fmt.Errorf("payload too large: %d bytes", header.PayloadSize))
			return
		}
		
		// Read payload
		payload := make([]byte, header.PayloadSize)
		if _, err := io.ReadFull(c.conn, payload); err != nil {
			// Handle timeout specially - it might occur between header and payload
			if netErr, ok := err.(net.Error); ok && netErr.Timeout() {
				// This shouldn't happen often, but if it does, we need to handle it
				// Try to read the remaining payload without deadline
				c.conn.SetReadDeadline(time.Time{})
				if _, err := io.ReadFull(c.conn, payload); err != nil {
					c.handleError(fmt.Errorf("failed to read payload after timeout: %w", err))
					return
				}
				// Restore deadline for next iteration
				c.conn.SetReadDeadline(time.Now().Add(1 * time.Second))
			} else {
				c.handleError(fmt.Errorf("failed to read payload: %w", err))
				return
			}
		}
		
		// Process message
		switch header.Type {
		case MessageTypeExternalMessage:
			if msg, err := c.parseExternalMessage(payload); err == nil {
				c.stats.messagesReceived.Add(1)
				c.handleExternalMessage(msg)
			} else {
				c.handleError(fmt.Errorf("failed to parse external message: %w", err))
			}
			
		case MessageTypeNewBlock:
			if block, err := c.parseBlockInfo(payload); err == nil {
				c.stats.blocksReceived.Add(1)
				c.handleNewBlock(block)
			} else {
				c.handleError(fmt.Errorf("failed to parse block info: %w", err))
			}
			
		case MessageTypeSubscribe:
			// Handle subscription confirmation
			if header.PayloadSize == 4 {
				var subscriptionType uint32
				if err := binary.Read(bytes.NewReader(payload), binary.LittleEndian, &subscriptionType); err == nil {
					fmt.Printf("[Client] Received subscription confirmation for types=%d\n", subscriptionType)
				}
			}
			
		case MessageTypeHeartbeat:
			// Ignore heartbeats
			
		default:
			c.handleError(fmt.Errorf("unknown message type: %d", header.Type))
		}
	}
}

func (c *Client) parseExternalMessage(data []byte) (*ExternalMessage, error) {
	buf := bytes.NewReader(data)
	msg := &ExternalMessage{}
	
	// Read timestamp
	if err := binary.Read(buf, binary.LittleEndian, &msg.TimestampMs); err != nil {
		return nil, err
	}
	
	// Read strings and data
	var err error
	msg.SourceAddr, err = readString(buf)
	if err != nil {
		return nil, err
	}
	
	msg.DestAddr, err = readString(buf)
	if err != nil {
		return nil, err
	}
	
	msg.Data, err = readBytes(buf)
	if err != nil {
		return nil, err
	}
	
	msg.Hash, err = readString(buf)
	if err != nil {
		return nil, err
	}
	
	return msg, nil
}

func (c *Client) parseBlockInfo(data []byte) (*BlockInfo, error) {
	buf := bytes.NewReader(data)
	block := &BlockInfo{}
	
	// Read block ID
	var err error
	block.BlockID, err = readString(buf)
	if err != nil {
		return nil, err
	}
	
	// Read numeric fields
	if err := binary.Read(buf, binary.LittleEndian, &block.GenUtime); err != nil {
		return nil, err
	}
	if err := binary.Read(buf, binary.LittleEndian, &block.DelaySeconds); err != nil {
		return nil, err
	}
	if err := binary.Read(buf, binary.LittleEndian, &block.AccountCount); err != nil {
		return nil, err
	}
	if err := binary.Read(buf, binary.LittleEndian, &block.TransactionCount); err != nil {
		return nil, err
	}
	
	// Read has_raw_data flag
	var hasRaw uint8
	if err := binary.Read(buf, binary.LittleEndian, &hasRaw); err != nil {
		return nil, err
	}
	block.HasRawData = hasRaw != 0
	
	// Read transaction hashes
	var txCount uint32
	if err := binary.Read(buf, binary.LittleEndian, &txCount); err != nil {
		return nil, err
	}
	
	block.TransactionHashes = make([]string, 0, txCount)
	for i := uint32(0); i < txCount; i++ {
		hash, err := readString(buf)
		if err != nil {
			return nil, err
		}
		block.TransactionHashes = append(block.TransactionHashes, hash)
	}
	
	// Read raw block data if available
	if block.HasRawData {
		block.RawBlockData, err = readBytes(buf)
		if err != nil {
			return nil, err
		}
	}
	
	return block, nil
}

func readString(r io.Reader) (string, error) {
	var length uint32
	if err := binary.Read(r, binary.LittleEndian, &length); err != nil {
		return "", err
	}
	
	buf := make([]byte, length)
	if _, err := io.ReadFull(r, buf); err != nil {
		return "", err
	}
	
	return string(buf), nil
}

func readBytes(r io.Reader) ([]byte, error) {
	var length uint32
	if err := binary.Read(r, binary.LittleEndian, &length); err != nil {
		return nil, err
	}
	
	buf := make([]byte, length)
	if _, err := io.ReadFull(r, buf); err != nil {
		return nil, err
	}
	
	return buf, nil
}

func (c *Client) handleExternalMessage(msg *ExternalMessage) {
	c.mu.RLock()
	handler := c.handlers.externalMsg
	c.mu.RUnlock()
	
	if handler != nil {
		handler(msg)
	}
}

func (c *Client) handleNewBlock(block *BlockInfo) {
	c.mu.RLock()
	handler := c.handlers.newBlock
	c.mu.RUnlock()
	
	if handler != nil {
		handler(block)
	}
}

func (c *Client) handleError(err error) {
	c.stats.errors.Add(1)
	
	c.mu.RLock()
	handler := c.handlers.onError
	c.mu.RUnlock()
	
	if handler != nil {
		handler(err)
	}
}