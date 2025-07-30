#!/usr/bin/env python3
"""
TON IPC Client Example
Connects to the IPC socket and receives events
"""

import socket
import sys
import os

def main():
    socket_path = "/tmp/ton-ipc.sock"
    
    if not os.path.exists(socket_path):
        print(f"Socket {socket_path} does not exist. Make sure the node is running with IPC enabled.")
        sys.exit(1)
    
    # Create a Unix socket
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    
    try:
        # Connect to the socket
        sock.connect(socket_path)
        print(f"Connected to {socket_path}")
        print("Waiting for events...")
        print("Event format: TYPE|FIELD1|FIELD2|...")
        print("Types: 1=External Message, 2=New Block, 3=Contract State Change\n")
        
        # Receive data
        while True:
            data = sock.recv(4096)
            if not data:
                print("Connection closed by server")
                break
                
            # Process each line (event)
            for line in data.decode('utf-8').strip().split('\n'):
                if line:
                    fields = line.split('|')
                    event_type = int(fields[0])
                    
                    if event_type == 1:  # External Message
                        print(f"[External Message] Hash={fields[1]}, WC={fields[3]}, Addr={fields[4]}, Size={fields[5]}")
                    elif event_type == 2:  # New Block
                        print(f"[New Block] ID={fields[1]}, Seqno={fields[2]}, WC={fields[3]}, Shard={fields[4]}")
                    elif event_type == 3:  # Contract State Change
                        print(f"[State Change] Addr={fields[1]}, WC={fields[2]}, Block={fields[3]}, LT={fields[4]}")
                        print(f"  Old Hash: {fields[5]}")
                        print(f"  New Hash: {fields[6]}")
                    else:
                        print(f"[Unknown Event] {line}")
                        
    except KeyboardInterrupt:
        print("\nShutting down...")
    except Exception as e:
        print(f"Error: {e}")
    finally:
        sock.close()

if __name__ == "__main__":
    main()