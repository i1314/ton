#!/usr/bin/env python3
"""
TON IPC Debug Client
Enhanced version with more debugging information
"""

import socket
import sys
import os
import time
import select
from datetime import datetime

def format_size(size):
    """Format size in bytes to human readable"""
    for unit in ['B', 'KB', 'MB', 'GB']:
        if size < 1024.0:
            return f"{size:.2f} {unit}"
        size /= 1024.0
    return f"{size:.2f} TB"

def main():
    socket_path = "/tmp/ton-ipc.sock"
    
    if not os.path.exists(socket_path):
        print(f"Socket {socket_path} does not exist. Make sure the node is running with IPC enabled.")
        sys.exit(1)
    
    # Create a Unix socket
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.setblocking(0)  # Non-blocking mode
    
    try:
        # Connect to the socket
        sock.connect(socket_path)
        print(f"✓ Connected to {socket_path}")
        print(f"  Time: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
        print("  Waiting for events...")
        print("  Event format: TYPE|FIELD1|FIELD2|...")
        print("  Types: 1=External Message, 2=New Block, 3=Contract State Change\n")
        
        # Statistics
        stats = {
            'start_time': time.time(),
            'events_received': 0,
            'bytes_received': 0,
            'last_event_time': None,
            'event_counts': {1: 0, 2: 0, 3: 0}
        }
        
        # Buffer for incomplete messages
        buffer = ""
        last_status_time = time.time()
        
        while True:
            # Check if data is available
            ready = select.select([sock], [], [], 1.0)  # 1 second timeout
            
            current_time = time.time()
            
            # Print status every 30 seconds
            if current_time - last_status_time > 30:
                elapsed = int(current_time - stats['start_time'])
                print(f"\n[Status] Running for {elapsed}s | Events: {stats['events_received']} | "
                      f"Data: {format_size(stats['bytes_received'])}")
                if stats['last_event_time']:
                    last_event_ago = int(current_time - stats['last_event_time'])
                    print(f"         Last event: {last_event_ago}s ago")
                else:
                    print("         No events received yet")
                print(f"         Event counts - Ext Msg: {stats['event_counts'][1]} | "
                      f"Blocks: {stats['event_counts'][2]} | "
                      f"State Changes: {stats['event_counts'][3]}\n")
                last_status_time = current_time
            
            if ready[0]:
                try:
                    data = sock.recv(4096)
                    if not data:
                        print("\n✗ Connection closed by server")
                        break
                    
                    stats['bytes_received'] += len(data)
                    
                    # Add to buffer and process complete lines
                    buffer += data.decode('utf-8', errors='ignore')
                    lines = buffer.split('\n')
                    
                    # Keep the last incomplete line in buffer
                    buffer = lines[-1]
                    
                    # Process complete lines
                    for line in lines[:-1]:
                        if line.strip():
                            stats['events_received'] += 1
                            stats['last_event_time'] = time.time()
                            
                            try:
                                fields = line.split('|')
                                event_type = int(fields[0])
                                stats['event_counts'][event_type] = stats['event_counts'].get(event_type, 0) + 1
                                
                                timestamp = datetime.now().strftime('%H:%M:%S')
                                
                                if event_type == 1:  # External Message
                                    print(f"[{timestamp}] [External Message #{stats['event_counts'][1]}]")
                                    print(f"  Hash: {fields[1][:16]}...")
                                    print(f"  Workchain: {fields[3]}, Address: {fields[4][:16]}...")
                                    print(f"  Size: {format_size(int(fields[5]))}")
                                elif event_type == 2:  # New Block
                                    print(f"[{timestamp}] [New Block #{stats['event_counts'][2]}]")
                                    print(f"  Block ID: {fields[1]}")
                                    print(f"  Seqno: {fields[2]}, Workchain: {fields[3]}, Shard: {fields[4]}")
                                elif event_type == 3:  # Contract State Change
                                    print(f"[{timestamp}] [State Change #{stats['event_counts'][3]}]")
                                    print(f"  Address: {fields[1][:32]}...")
                                    print(f"  Workchain: {fields[2]}, Block: {fields[3]}")
                                    print(f"  LT: {fields[4]}")
                                    if len(fields) > 5:
                                        print(f"  Old Hash: {fields[5][:16]}...")
                                        print(f"  New Hash: {fields[6][:16]}...")
                                else:
                                    print(f"[{timestamp}] [Unknown Event Type {event_type}]")
                                    print(f"  Raw: {line[:100]}...")
                                
                            except Exception as e:
                                print(f"[{timestamp}] [Parse Error] {e}")
                                print(f"  Raw: {line[:100]}...")
                            
                            print()  # Empty line for readability
                            
                except socket.error as e:
                    if e.errno != 11:  # Ignore "Resource temporarily unavailable"
                        print(f"\n✗ Socket error: {e}")
                        break
                        
    except KeyboardInterrupt:
        print("\n\nShutting down...")
        print(f"Total events received: {stats['events_received']}")
        print(f"Total data received: {format_size(stats['bytes_received'])}")
        if stats['events_received'] > 0:
            elapsed = time.time() - stats['start_time']
            print(f"Average rate: {stats['events_received']/elapsed:.2f} events/sec")
    except Exception as e:
        print(f"✗ Error: {e}")
    finally:
        sock.close()

if __name__ == "__main__":
    main()