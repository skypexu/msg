#ifndef SIMPLE_POLICY_MESSENGER_c6b0b27ad489_H
#define SIMPLE_POLICY_MESSENGER_c6b0b27ad489_H

#include "Messenger.h"

namespace msgr {

class SimplePolicyMessenger : public Messenger
{
private:
  /// lock protecting policy
  Mutex policy_lock;
  /// the default Policy we use for Pipes
  Policy default_policy;
  /// map specifying different Policies for specific peer types
  std::map<int, Policy> policy_map; // entity_name_t::type -> Policy

public:

  SimplePolicyMessenger(MsgrContext *cct, entity_name_t name,
			std::string lname, uint64_t _nonce)
    : Messenger(cct, name, lname),
      policy_lock("SimplePolicyMessenger::policy_lock")
    {
    }

    /**
   * Get the Policy associated with a type of peer.
   * @param t The peer type to get the default policy for.
   *
   * @return A const Policy reference.
   */
  virtual Policy get_policy(int t) override {
    Mutex::Locker l(policy_lock);
    std::map<int, Policy>::iterator iter =
      policy_map.find(t);
    if (iter != policy_map.end())
      return iter->second;
    else
      return default_policy;
  }

  virtual Policy get_default_policy() override {
    Mutex::Locker l(policy_lock);
    return default_policy;
  }

  /**
   * Set a policy which is applied to all peers who do not have a type-specific
   * Policy.
   * This is an init-time function and cannot be called after calling
   * start() or bind().
   *
   * @param p The Policy to apply.
   */
  virtual void set_default_policy(Policy p) override {
    Mutex::Locker l(policy_lock);
    default_policy = p;
  }
  /**
   * Set a policy which is applied to all peers of the given type.
   * This is an init-time function and cannot be called after calling
   * start() or bind().
   *
   * @param type The peer type this policy applies to.
   * @param p The policy to apply.
   */
  virtual void set_policy(int type, Policy p) override {
    Mutex::Locker l(policy_lock);
    policy_map[type] = p;
  }

  /**
   * Set a Throttler which is applied to all Messages from the given
   * type of peer.
   * This is an init-time function and cannot be called after calling
   * start() or bind().
   *
   * @param type The peer type this Throttler will apply to.
   * @param t The Throttler to apply. SimpleMessenger does not take
   * ownership of this pointer, but you must not destroy it before
   * you destroy SimpleMessenger.
   */
  void set_policy_throttlers(int type,
			     Throttle *byte_throttle,
			     Throttle *msg_throttle) {
    Mutex::Locker l(policy_lock);
    std::map<int, Policy>::iterator iter =
      policy_map.find(type);
    if (iter != policy_map.end()) {
      iter->second.throttler_bytes = byte_throttle;
      iter->second.throttler_messages = msg_throttle;
    } else {
      default_policy.throttler_bytes = byte_throttle;
      default_policy.throttler_messages = msg_throttle;
    }
  }
}; /* SimplePolicyMessenger */

} // namespace msgr

#endif // SIMPLE_POLICY_MESSENGER_c6b0b27ad489_H
