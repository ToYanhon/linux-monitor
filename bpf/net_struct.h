#pragma once

typedef unsigned long long __u64;

struct if_counters {
  __u64 rcv_bytes;
  __u64 rcv_packets;
  __u64 snd_bytes;
  __u64 snd_packets;
};