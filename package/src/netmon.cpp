// Copyright (C) 2018-2020 CERN
// License Apache2 - see LICENCE file

#include "netmon.h"

#include <dirent.h>
#include <time.h>

#include <cstring>
#include <memory>

#include "utils.h"

// Constructor; uses RAII pattern to open all monitored
// network device streams and to take initial values
// to the monitor relative differences
netmon::netmon(std::vector<std::string> netdevs)
    : interface_params{}, network_if_streams{} {
  interface_params.reserve(params.size());
  for (const auto& param : params) interface_params.push_back(param.get_name());

  if (netdevs.size() == 0) {
    monitored_netdevs = get_all_network_devs();
  } else {
    monitored_netdevs = netdevs;
  }
  open_interface_streams();

  // Ensure internal stat counters are initialised properly
  read_raw_network_stats(network_stats_start);
  read_raw_network_stats(network_stats);
}

// Get all available network devices
// This is all a bit yuk, using C style directory
// parsing. From C++17 we should use the filesystem
// library, but only when we decide it's reasonable
// to no longer support older compilers.
std::vector<std::string> const netmon::get_all_network_devs() {
  std::vector<std::string> devices{};
  DIR* d;
  struct dirent* dir;
  const char* netdir = "/sys/class/net";
  d = opendir(netdir);
  if (d) {
    while ((dir = readdir(d)) != NULL) {
      if (!(!std::strcmp(dir->d_name, ".") || !std::strcmp(dir->d_name, "..")))
        devices.push_back(dir->d_name);
    }
    closedir(d);
  } else {
    std::cerr << "Failed to open " << netdir
              << " to get list of network devices. "
              << "No network data will be available" << std::endl;
  }
  return devices;
}

// Opens an ifstream for all monitored network parameters
// Stored as unique_ptrs to ensure they are closed when
// the instance is destroyed
void netmon::open_interface_streams() {
  for (const auto& if_param : interface_params) {
    for (const auto& device : monitored_netdevs) {
      // make_unique would be better, but mandates C++14
      std::unique_ptr<std::ifstream> u_strm_ptr =
          std::unique_ptr<std::ifstream>(
              new std::ifstream(get_sys_filename(device, if_param)));
      network_if_streams[if_param][device] = std::move(u_strm_ptr);
    }
  }
}

// Read raw stat values
void netmon::read_raw_network_stats(
    std::map<std::string, unsigned long long>& stats) {
  for (const auto& if_param : interface_params) {
    unsigned long long value_read{};
    stats[if_param] = 0;
    for (const auto& device : monitored_netdevs) {
      network_if_streams[if_param][device]->seekg(0);
      *network_if_streams[if_param][device] >> value_read;
      stats[if_param] += value_read;
    }
  }
}

// Relative counter statistics for text file
std::map<std::string, unsigned long long> const netmon::get_text_stats() {
  std::map<std::string, unsigned long long> text_stats{};
  for (const auto& if_param : interface_params) {
    text_stats[if_param] =
        network_stats[if_param] - network_stats_start[if_param];
  }
  return text_stats;
}

// Also relative counters for JSON totals
std::map<std::string, unsigned long long> const netmon::get_json_total_stats() {
  return get_text_stats();
}

// For JSON averages, divide by elapsed time
std::map<std::string, double> const netmon::get_json_average_stats(
    unsigned long long elapsed_clock_ticks) {
  std::map<std::string, unsigned long long> text_stats = get_text_stats();
  std::map<std::string, double> json_average_stats{};
  for (auto& stat : text_stats) {
    json_average_stats[stat.first] =
        double(stat.second * sysconf(_SC_CLK_TCK)) / elapsed_clock_ticks;
  }
  return json_average_stats;
}

// Collect related hardware information
void const netmon::get_hardware_info(nlohmann::json& hw_json) { return; }

void const netmon::get_unit_info(nlohmann::json& unit_json) {
  prmon::fill_units(unit_json, params);
  return;
}
