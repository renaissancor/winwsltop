// Prints disk_collector and net_collector beside direct reads of what they consume, for comparison
// with Get-Counter, WMI and Get-NetAdapter. Reads only. Tab-separated, one record per line.
//   rows       every GetIfTable2 row with the collector's filter decision, then the NVMe health log
//   raw        InOctets and OutOctets of the rows the filter keeps, once
//   nvme       the NVMe health log of disks 0 and 1, once
//   run <n>    n rounds a second apart: collector output, what the collector's own PDH counters and
//              octet map hold, raw octets read either side of each sample, NVMe log at both ends
// The collector exposes rates only. Its private members are opened below, so the raw values printed
// are the ones it read, not a second reading taken a moment later.
#include "core/platform.h"
#include "core/snapshot.h"
#include <pdh.h>
#include <winioctl.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>
#define private public
#include "core/collectors.h"
#undef private
#include "host/disk.cpp"
#include "host/network.cpp"

// UTC, 100 ns since 1601, the clock every line in every results file is aligned on.
static unsigned long long filetime_now() {
  FILETIME f;
  GetSystemTimePreciseAsFileTime(&f);
  return (static_cast<unsigned long long>(f.dwHighDateTime) << 32) | f.dwLowDateTime;
}

// The collector's filter, repeated here only to name the reason a row is dropped. run compares the
// kept set with what the collector returned, so a drift between the two shows in the output.
static const char* drop_reason(const MIB_IF_ROW2& r) {
  if (r.OperStatus != IfOperStatusUp) return "not-up";
  if (r.Type == IF_TYPE_SOFTWARE_LOOPBACK) return "loopback";
  if (r.InterfaceAndOperStatusFlags.FilterInterface) return "filter";
  return nullptr;
}

static void print_rows() {
  MIB_IF_TABLE2* t = nullptr;
  if (GetIfTable2(&t) != NO_ERROR) { printf("if-table failed\n"); return; }
  printf("if-table\tentries\t%lu\n", t->NumEntries);
  printf("if-head\tindex\tluid\talias\tdescription\ttype\toper\tadmin\tmedia\thardware\tfilter\tin\tout\ttx_bits\trx_bits\tkeep\treason\n");
  for (ULONG i = 0; i < t->NumEntries; ++i) {
    const MIB_IF_ROW2& r = t->Table[i];
    const char* why = drop_reason(r);
    printf("if\t%lu\t%llu\t%s\t%s\t%lu\t%d\t%d\t%d\t%d\t%d\t%llu\t%llu\t%llu\t%llu\t%d\t%s\n", r.InterfaceIndex, r.InterfaceLuid.Value,
           wwt::utf8(r.Alias).c_str(), wwt::utf8(r.Description).c_str(), r.Type, static_cast<int>(r.OperStatus), static_cast<int>(r.AdminStatus),
           static_cast<int>(r.MediaConnectState), r.InterfaceAndOperStatusFlags.HardwareInterface ? 1 : 0, r.InterfaceAndOperStatusFlags.FilterInterface ? 1 : 0,
           r.InOctets, r.OutOctets, r.TransmitLinkSpeed, r.ReceiveLinkSpeed, why ? 0 : 1, why ? why : "-");
  }
  FreeMibTable(t);
}

static void print_raw_octets(const char* tag, int round) {
  MIB_IF_TABLE2* t = nullptr;
  if (GetIfTable2(&t) != NO_ERROR) { printf("%s\tfailed\n", tag); return; }
  const unsigned long long ft = filetime_now();
  for (ULONG i = 0; i < t->NumEntries; ++i) {
    const MIB_IF_ROW2& r = t->Table[i];
    if (drop_reason(r)) continue;
    printf("%s\t%d\t%llu\t%llu\t%lu\t%s\t%llu\t%llu\n", tag, round, ft, r.InterfaceLuid.Value, r.InterfaceIndex, wwt::utf8(r.Alias).c_str(), r.InOctets, r.OutOctets);
  }
  FreeMibTable(t);
}

// Reads the collector's own counters after its sample: the same raw pair it just cooked, and the
// % Idle Time value it cooked from them, before the clamp. _Total is kept here to show it exists.
static std::map<std::wstring, PDH_RAW_COUNTER> raw_values(PDH_HCOUNTER c) {
  std::map<std::wstring, PDH_RAW_COUNTER> out;
  std::vector<unsigned char> buf;
  DWORD size = 0, count = 0;
  if (PdhGetRawCounterArrayW(c, &size, &count, nullptr) != PDH_MORE_DATA) return out;
  buf.resize(size);
  auto* items = reinterpret_cast<PDH_RAW_COUNTER_ITEM_W*>(buf.data());
  if (PdhGetRawCounterArrayW(c, &size, &count, items) != ERROR_SUCCESS) return out;
  for (DWORD i = 0; i < count; ++i) out[items[i].szName] = items[i].RawValue;
  return out;
}

static void print_disk_internals(wwt::disk_collector& disk, int round) {
  std::map<std::wstring, double> idle;
  std::vector<unsigned char> buf;
  PDH_FMT_COUNTERVALUE_ITEM_W* items = nullptr;
  DWORD count = 0;
  if (wwt::pdh_array(disk.idle_, buf, items, count))
    for (DWORD i = 0; i < count; ++i)
      if (items[i].FmtValue.CStatus == ERROR_SUCCESS) idle[items[i].szName] = items[i].FmtValue.doubleValue;
  const auto rd = raw_values(disk.read_), wr = raw_values(disk.write_), id = raw_values(disk.idle_);
  for (const auto& [name, value] : rd) {
    const auto w = wr.find(name);
    const auto r = id.find(name);
    const auto v = idle.find(name);
    printf("r\t%d\t%s\t%lld\t%lld\t%lld\t%lld\t", round, wwt::utf8(name).c_str(), value.FirstValue, w == wr.end() ? -1LL : w->second.FirstValue,
           r == id.end() ? -1LL : r->second.FirstValue, r == id.end() ? -1LL : r->second.SecondValue);
    if (v == idle.end()) printf("-\n"); else printf("%.6f\n", v->second);
  }
}

// The drive's own Data Units Read and Written, health log page 2, a device-side count that does not
// pass through PhysicalDisk. Opens the disk with no access rights and sends a query, nothing else.
static void print_nvme(int disk) {
  char path[64];
  snprintf(path, sizeof path, "\\\\.\\PhysicalDrive%d", disk);
  HANDLE h = CreateFileA(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
  if (h == INVALID_HANDLE_VALUE) { printf("nvme\t%d\topen failed %lu\n", disk, GetLastError()); return; }
  const DWORD head = offsetof(STORAGE_PROPERTY_QUERY, AdditionalParameters) + sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA);
  std::vector<unsigned char> buf(head + 512);
  auto* query = reinterpret_cast<STORAGE_PROPERTY_QUERY*>(buf.data());
  auto* spec = reinterpret_cast<STORAGE_PROTOCOL_SPECIFIC_DATA*>(query->AdditionalParameters);
  query->PropertyId = StorageDeviceProtocolSpecificProperty;
  query->QueryType = PropertyStandardQuery;
  spec->ProtocolType = ProtocolTypeNvme;
  spec->DataType = NVMeDataTypeLogPage;
  spec->ProtocolDataRequestValue = 2;
  spec->ProtocolDataOffset = sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA);
  spec->ProtocolDataLength = 512;
  DWORD got = 0;
  if (!DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, buf.data(), static_cast<DWORD>(buf.size()), buf.data(), static_cast<DWORD>(buf.size()), &got, nullptr)) {
    printf("nvme\t%d\tquery failed %lu\n", disk, GetLastError());
    CloseHandle(h);
    return;
  }
  // The result overlays the request: the descriptor's two header DWORDs, then the protocol data.
  const auto* desc = reinterpret_cast<STORAGE_PROTOCOL_DATA_DESCRIPTOR*>(buf.data());
  const auto* log = reinterpret_cast<const unsigned char*>(&desc->ProtocolSpecificData) + desc->ProtocolSpecificData.ProtocolDataOffset;
  unsigned long long read_units = 0, write_units = 0;
  memcpy(&read_units, log + 32, 8);    // the low 8 of a 16-byte field, in units of 512000 bytes
  memcpy(&write_units, log + 48, 8);
  printf("nvme\t%d\t%llu\tdata_units_read\t%llu\tdata_units_written\t%llu\n", disk, filetime_now(), read_units, write_units);
  CloseHandle(h);
}

static void run(int rounds) {
  using namespace wwt;
  disk_collector disk;
  net_collector net;
  print_nvme(0);
  for (int r = 0; r <= rounds; ++r) {
    if (r) Sleep(1000);
    std::vector<disk_row> disks;
    double t0 = seconds_now();
    const bool dok = disk.sample(disks);
    double ms = (seconds_now() - t0) * 1000;
    printf("D\t%d\t%llu\t%d\t%.3f\n", r, filetime_now(), dok, ms);
    for (const auto& d : disks) printf("d\t%d\t%s\t%.3f\t%.3f\t%.6f\n", r, d.name.c_str(), d.read_bytes_s, d.write_bytes_s, d.busy_pct);
    print_disk_internals(disk, r);

    print_raw_octets("b", r);
    std::vector<net_row> nets;
    t0 = seconds_now();
    const bool nok = net.sample(nets);
    ms = (seconds_now() - t0) * 1000;
    printf("N\t%d\t%llu\t%d\t%.3f\n", r, filetime_now(), nok, ms);
    print_raw_octets("a", r);
    for (const auto& [id, c] : net.prev_) printf("c\t%d\t%.6f\t%llu\t%llu\t%llu\n", r, net.prev_t_, id, c.in, c.out);
    for (const auto& n : nets) printf("n\t%d\t%s\t%.3f\t%.3f\t%llu\t%d\n", r, n.name.c_str(), n.in_bytes_s, n.out_bytes_s, n.link_bits_s, n.hardware);
  }
  print_nvme(0);
}

int main(int argc, char** argv) {
  const std::string mode = argc > 1 ? argv[1] : "rows";
  if (mode == "rows") { print_rows(); print_nvme(0); print_nvme(1); }
  else if (mode == "raw") print_raw_octets("raw", 0);
  else if (mode == "nvme") { print_nvme(0); print_nvme(1); }
  else if (mode == "run") run(argc > 2 ? atoi(argv[2]) : 60);
  else return 2;
  return 0;
}
