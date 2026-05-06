#pragma once

#include <cstddef>
#include <filesystem>
#include <mutex>
#include <string>

namespace CppServer::Services::Files {
struct Context {
	static constexpr int DEFAULT_PORT_BEGIN = 8081;
	static constexpr std::size_t DEFAULT_PORT_COUNT = 1;
	inline static constexpr const char *DEFAULT_MOUNT_PATH = "mount";

	explicit Context(int listening_port = DEFAULT_PORT_BEGIN,
						 std::string configured_mount_path = DEFAULT_MOUNT_PATH)
			: port(listening_port), mount_path(std::move(configured_mount_path)) {}

	int port;
	std::string mount_path;
	std::string resolved_mount_path;
	std::filesystem::file_time_type latest_write_time{};
	std::size_t entry_count = 0;
	bool mount_state_initialized = false;
	std::mutex mount_state_mutex;
};
} // namespace CppServer::Services::Files
