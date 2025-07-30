/*
    IPC Publisher for TON Node Data Push
    Provides hooks for external messages, new blocks, and contract state changes
*/
#pragma once

#include "td/actor/actor.h"
#include "td/utils/buffer.h"
#include "td/utils/JsonBuilder.h"
#include "ton/ton-types.h"
#include "validator/interfaces/block.h"
#include "validator/interfaces/external-message.h"
#include <unordered_set>
#include <memory>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>

namespace ton {
namespace validator {

class IPCPublisher : public td::actor::Actor {
public:
  enum EventType {
    EXTERNAL_MESSAGE = 1,
    NEW_BLOCK = 2,
    CONTRACT_STATE_CHANGE = 3
  };

  struct IPCConfig {
    std::string socket_path = "/tmp/ton-ipc.sock";
    bool enabled = false;
    bool publish_external_messages = true;
    bool publish_new_blocks = true;
    bool publish_contract_changes = true;
    std::unordered_set<std::string> monitored_addresses;
    size_t max_queue_size = 10000;
  };

  struct ExternalMessageEvent {
    std::string hash;
    std::string destination;
    int64_t workchain_id;
    std::string address;
    size_t size;
    int64_t timestamp;
  };

  struct NewBlockEvent {
    std::string block_id;
    int64_t seqno;
    int32_t workchain_id;
    int64_t shard;
    int64_t timestamp;
    std::string prev_block_id;
  };

  struct ContractStateChangeEvent {
    std::string address;
    int32_t workchain_id;
    std::string block_id;
    int64_t lt;
    std::string old_hash;
    std::string new_hash;
    int64_t timestamp;
  };

  IPCPublisher(IPCConfig config);
  ~IPCPublisher();

  // Hook methods to be called from validator
  void publish_external_message(td::Ref<ExtMessage> message);
  void publish_new_block(BlockIdExt block_id, td::Ref<BlockData> block);
  void publish_contract_state_change(AccountIdPrefixFull account,
                                   const std::string& account_address,
                                   const std::string& old_state_hash,
                                   const std::string& new_state_hash,
                                   BlockIdExt block_id,
                                   LogicalTime lt);

  // Static singleton methods for easy integration
  static void init(IPCConfig config);
  static void shutdown();
  static IPCPublisher* instance();

private:
  void start_up() override;
  void tear_down() override;
  
  void worker_thread();
  void send_event_json(const std::string& json_str);
  bool should_monitor_address(const AccountIdPrefixFull& account) const;
  
  std::string serialize_external_message(const ExternalMessageEvent& event);
  std::string serialize_new_block(const NewBlockEvent& event);
  std::string serialize_contract_change(const ContractStateChangeEvent& event);

  IPCConfig config_;
  std::unique_ptr<std::thread> worker_thread_;
  std::atomic<bool> running_{false};
  
  // Event queue
  std::queue<std::string> event_queue_;
  std::mutex queue_mutex_;
  std::condition_variable queue_cv_;
  
  // Unix socket
  int socket_fd_ = -1;
  std::vector<int> client_fds_;
  std::mutex clients_mutex_;

  static IPCPublisher* instance_;
  static std::mutex instance_mutex_;
};

} // namespace validator
} // namespace ton