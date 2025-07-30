/*
    IPC Publisher Implementation
*/
#include "ipc-publisher.hpp"
#include "td/utils/format.h"
#include "td/utils/Time.h"
#include "crypto/common/util.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

namespace ton {
namespace validator {

IPCPublisher* IPCPublisher::instance_ = nullptr;
std::mutex IPCPublisher::instance_mutex_;

IPCPublisher::IPCPublisher(IPCConfig config) : config_(std::move(config)) {
}

IPCPublisher::~IPCPublisher() {
  if (running_) {
    shutdown();
  }
}

void IPCPublisher::init(IPCConfig config) {
  std::lock_guard<std::mutex> lock(instance_mutex_);
  if (!instance_ && config.enabled) {
    instance_ = new IPCPublisher(std::move(config));
    // Note: Actor registration should be done by the caller
    // td::actor::create_actor<IPCPublisher>("ipc-publisher").release();
  }
}

void IPCPublisher::shutdown() {
  std::lock_guard<std::mutex> lock(instance_mutex_);
  if (instance_) {
    instance_->running_ = false;
    instance_->queue_cv_.notify_all();
    delete instance_;
    instance_ = nullptr;
  }
}

IPCPublisher* IPCPublisher::instance() {
  std::lock_guard<std::mutex> lock(instance_mutex_);
  return instance_;
}

void IPCPublisher::start_up() {
  LOG(INFO) << "[IPC] IPCPublisher::start_up() called";
  if (!config_.enabled) {
    LOG(WARNING) << "[IPC] IPC is disabled in config";
    return;
  }
  LOG(INFO) << "[IPC] IPC is enabled, initializing socket at " << config_.socket_path;

  // Create Unix domain socket
  socket_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
  if (socket_fd_ < 0) {
    LOG(ERROR) << "Failed to create IPC socket: " << strerror(errno);
    return;
  }

  // Make socket non-blocking
  int flags = fcntl(socket_fd_, F_GETFL, 0);
  fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK);

  // Bind to socket path
  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, config_.socket_path.c_str(), sizeof(addr.sun_path) - 1);

  // Remove existing socket file
  unlink(config_.socket_path.c_str());

  if (bind(socket_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    LOG(ERROR) << "Failed to bind IPC socket: " << strerror(errno);
    close(socket_fd_);
    socket_fd_ = -1;
    return;
  }

  if (listen(socket_fd_, 10) < 0) {
    LOG(ERROR) << "Failed to listen on IPC socket: " << strerror(errno);
    close(socket_fd_);
    socket_fd_ = -1;
    return;
  }

  LOG(INFO) << "[IPC] Publisher started successfully on " << config_.socket_path;
  LOG(INFO) << "[IPC] Config: external_messages=" << config_.publish_external_messages 
            << " new_blocks=" << config_.publish_new_blocks
            << " contract_changes=" << config_.publish_contract_changes;
  
  running_ = true;
  worker_thread_ = std::make_unique<std::thread>(&IPCPublisher::worker_thread, this);
}

void IPCPublisher::tear_down() {
  running_ = false;
  queue_cv_.notify_all();
  
  if (worker_thread_ && worker_thread_->joinable()) {
    worker_thread_->join();
  }

  // Close all client connections
  {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (int fd : client_fds_) {
      close(fd);
    }
    client_fds_.clear();
  }

  if (socket_fd_ >= 0) {
    close(socket_fd_);
    unlink(config_.socket_path.c_str());
  }

  LOG(INFO) << "IPC publisher stopped";
}

void IPCPublisher::worker_thread() {
  while (running_) {
    // Accept new connections
    if (socket_fd_ >= 0) {
      struct sockaddr_un client_addr;
      socklen_t client_len = sizeof(client_addr);
      int client_fd = accept(socket_fd_, (struct sockaddr*)&client_addr, &client_len);
      
      if (client_fd >= 0) {
        // Make client socket non-blocking
        int flags = fcntl(client_fd, F_GETFL, 0);
        fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
        
        std::lock_guard<std::mutex> lock(clients_mutex_);
        client_fds_.push_back(client_fd);
        LOG(INFO) << "IPC client connected: " << client_fd;
      }
    }

    // Process event queue
    std::unique_lock<std::mutex> queue_lock(queue_mutex_);
    
    if (queue_cv_.wait_for(queue_lock, std::chrono::milliseconds(100), 
                           [this] { return !event_queue_.empty() || !running_; })) {
      
      while (!event_queue_.empty() && running_) {
        auto event = std::move(event_queue_.front());
        event_queue_.pop();
        queue_lock.unlock();
        
        send_event_json(event);
        
        queue_lock.lock();
      }
    }
  }
}

void IPCPublisher::send_event_json(const std::string& json_str) {
  std::string json_with_newline = json_str + "\n";
  
  std::lock_guard<std::mutex> lock(clients_mutex_);
  
  // Send to all connected clients
  auto it = client_fds_.begin();
  while (it != client_fds_.end()) {
    int fd = *it;
    ssize_t sent = send(fd, json_with_newline.data(), json_with_newline.size(), MSG_NOSIGNAL);
    
    if (sent < 0) {
      if (errno == EPIPE || errno == ECONNRESET) {
        LOG(INFO) << "IPC client disconnected: " << fd;
        close(fd);
        it = client_fds_.erase(it);
      } else {
        ++it;
      }
    } else {
      ++it;
    }
  }
}

void IPCPublisher::publish_external_message(td::Ref<ExtMessage> message) {
  if (!config_.enabled || !config_.publish_external_messages || message.is_null()) {
    return;
  }

  ExternalMessageEvent event;
  event.hash = message->hash().to_hex();
  event.workchain_id = message->wc();
  event.address = message->addr().to_hex();
  event.destination = message->shard().to_str();
  event.size = message->serialize().size();
  event.timestamp = static_cast<int64_t>(td::Clocks::system());

  auto json_event = serialize_external_message(event);
  
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (event_queue_.size() < config_.max_queue_size) {
      event_queue_.push(std::move(json_event));
      queue_cv_.notify_one();
    }
  }
}

void IPCPublisher::publish_new_block(BlockIdExt block_id, td::Ref<BlockData> block) {
  LOG(DEBUG) << "[IPC] publish_new_block called for " << block_id.to_str();
  if (!config_.enabled || !config_.publish_new_blocks) {
    LOG(DEBUG) << "[IPC] Skipping: enabled=" << config_.enabled << " publish_new_blocks=" << config_.publish_new_blocks;
    return;
  }

  NewBlockEvent event;
  event.block_id = block_id.to_str();
  event.seqno = block_id.seqno();
  event.workchain_id = block_id.id.workchain;
  event.shard = block_id.id.shard;
  event.timestamp = static_cast<int64_t>(td::Clocks::system());
  
  // Note: BlockData doesn't provide direct access to previous block info
  // This would need to be implemented differently if needed

  auto json_event = serialize_new_block(event);
  
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (event_queue_.size() < config_.max_queue_size) {
      event_queue_.push(std::move(json_event));
      queue_cv_.notify_one();
    }
  }
}

void IPCPublisher::publish_contract_state_change(AccountIdPrefixFull account,
                                               const std::string& account_address,
                                               const std::string& old_state_hash,
                                               const std::string& new_state_hash,
                                               BlockIdExt block_id,
                                               LogicalTime lt) {
  if (!config_.enabled || !config_.publish_contract_changes) {
    return;
  }

  // Check if we should monitor this address
  if (!should_monitor_address(account)) {
    return;
  }

  ContractStateChangeEvent event;
  event.address = account_address;
  event.workchain_id = account.workchain;
  event.block_id = block_id.to_str();
  event.lt = lt;
  event.old_hash = old_state_hash;
  event.new_hash = new_state_hash;
  event.timestamp = static_cast<int64_t>(td::Clocks::system());

  auto json_event = serialize_contract_change(event);
  
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (event_queue_.size() < config_.max_queue_size) {
      event_queue_.push(std::move(json_event));
      queue_cv_.notify_one();
    }
  }
}

bool IPCPublisher::should_monitor_address(const AccountIdPrefixFull& account) const {
  if (config_.monitored_addresses.empty()) {
    return true; // Monitor all if no filter specified
  }
  
  auto addr_str = account.to_str();
  return config_.monitored_addresses.find(addr_str) != config_.monitored_addresses.end();
}

std::string IPCPublisher::serialize_external_message(const ExternalMessageEvent& event) {
  // Simple pipe-delimited format: TYPE|HASH|DEST|WC|ADDR|SIZE|TIME
  return PSTRING() << EXTERNAL_MESSAGE << "|"
                   << event.hash << "|"
                   << event.destination << "|"
                   << event.workchain_id << "|"
                   << event.address << "|"
                   << event.size << "|"
                   << event.timestamp;
}

std::string IPCPublisher::serialize_new_block(const NewBlockEvent& event) {
  // Simple pipe-delimited format: TYPE|BLOCK_ID|SEQNO|WC|SHARD|TIME|PREV
  return PSTRING() << NEW_BLOCK << "|"
                   << event.block_id << "|"
                   << event.seqno << "|"
                   << event.workchain_id << "|"
                   << event.shard << "|"
                   << event.timestamp << "|"
                   << event.prev_block_id;
}

std::string IPCPublisher::serialize_contract_change(const ContractStateChangeEvent& event) {
  // Simple pipe-delimited format: TYPE|ADDR|WC|BLOCK|LT|OLD|NEW|TIME
  return PSTRING() << CONTRACT_STATE_CHANGE << "|"
                   << event.address << "|"
                   << event.workchain_id << "|"
                   << event.block_id << "|"
                   << event.lt << "|"
                   << event.old_hash << "|"
                   << event.new_hash << "|"
                   << event.timestamp;
}

} // namespace validator
} // namespace ton