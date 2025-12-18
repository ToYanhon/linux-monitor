#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "net_struct.h"

// key: ifindex (int), value: if_counters
struct {
  __uint(type, BPF_MAP_TYPE_PERCPU_HASH);
  __uint(max_entries, 128);
  __type(key, int);
  __type(value, struct if_counters);
} if_stats SEC(".maps");

SEC("tc")
int on_egress(struct __sk_buff *skb) {
  int ifindex = skb->ifindex;
  struct if_counters *c;
  struct if_counters zero = {};

  c = bpf_map_lookup_elem(&if_stats, &ifindex);
  if (!c) {
    bpf_map_update_elem(&if_stats, &ifindex, &zero, BPF_NOEXIST);
    c = bpf_map_lookup_elem(&if_stats, &ifindex);
    if (!c)
      return 0;
  }
  __sync_fetch_and_add(&c->snd_bytes, skb->len);
  __sync_fetch_and_add(&c->snd_packets, 1);

  return 0;
}

SEC("tc")
int on_ingress(struct __sk_buff *skb) {
  int ifindex = skb->ifindex;
  struct if_counters *c;
  struct if_counters zero = {};

  c = bpf_map_lookup_elem(&if_stats, &ifindex);
  if (!c) {
    bpf_map_update_elem(&if_stats, &ifindex, &zero, BPF_NOEXIST);
    c = bpf_map_lookup_elem(&if_stats, &ifindex);
    if (!c)
      return 0;
  }

  __sync_fetch_and_add(&c->rcv_bytes, skb->len);
  __sync_fetch_and_add(&c->rcv_packets, 1);
  return 0;
}

char _license[] SEC("license") = "GPL";