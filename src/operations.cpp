/*
aptian - apt repository tool

Copyright (C) 2024  Ivan Gagis <igagis@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <http://www.gnu.org/licenses/>.*/

/* ================ LICENSE END ================ */

#include "operations.hpp"

#include <filesystem>
#include <map>

#include <papki/fs_file.hpp>
#include <tml/tree.hpp>
#include <utki/debug.hpp>
#include <utki/string.hpp>
#include <utki/util.hpp>

#include "configuration.hpp"
#include "packages.hpp"

using namespace std::string_literals;
using namespace std::string_view_literals;

using namespace aptian;

/*
For APT repository format info refer to: https://wiki.debian.org/DebianRepository/Format

TODO: source repository is not supported at the moment, only binary.

APT repository directory structure:

dists
	<dists>
		<comps>
			binary-<archs>
				Packages
				Packages.gz
		InRelease
		Release
		Release.gpg
pool
	<dists>
		<comps>
			<prefix>
				<package-source-name>
					<package-files>
aptian.conf
keyring.gpg
pubkey.gpg

*/

namespace {
constexpr std::string_view dists_subdir = "dists/"sv;
constexpr std::string_view pool_subdir = "pool/"sv;
constexpr std::string_view tmp_subdir = "tmp/"sv;
constexpr std::string_view lib_prefix = "lib"sv;
constexpr std::string_view binary_prefix = "binary-"sv;
constexpr std::string_view control_filename = "control"sv;
constexpr std::string_view packages_filename = "Packages"sv;
} // namespace

namespace {
bool is_aptian_repo(std::string_view dir)
{
	return papki::fs_file(utki::cat(dir, dists_subdir)).exists() &&
		papki::fs_file(utki::cat(dir, pool_subdir)).exists();
}
} // namespace

namespace {
std::string apt_pool_prefix(std::string_view package_name)
{
	ASSERT(!package_name.empty())

	constexpr auto lib_prefix_size = lib_prefix.size() + 1;
	if (package_name.starts_with(lib_prefix) && package_name.size() >= lib_prefix_size) {
		return papki::as_dir(package_name.substr(0, lib_prefix_size));
	}
	return papki::as_dir(package_name.substr(0, 1));
}
} // namespace

void aptian::init( //
	std::string_view dir,
	std::string_view gpg
)
{
	ASSERT(!dir.empty())
	ASSERT(!gpg.empty())

	papki::fs_file dir_file(papki::as_dir(dir));

	if (!dir_file.exists()) {
		std::stringstream ss;
		ss << "directory '" << dir << "' does not exist";
		throw std::invalid_argument(ss.str());
	}

	if (!dir_file.list_dir().empty()) {
		std::stringstream ss;
		ss << "directory '" << dir << "' is not empty";
		throw std::invalid_argument(ss.str());
	}

	std::cout << "initialize APT repository" << std::endl;

	std::cout << "create '" << dists_subdir << "'" << std::endl;
	papki::fs_file(utki::cat(dir_file.path(), dists_subdir)).make_dir();

	std::cout << "create '" << pool_subdir << "'" << std::endl;
	papki::fs_file(utki::cat(dir_file.path(), pool_subdir)).make_dir();

	std::cout << "create configuration file" << std::endl;
	configuration::create(dir, gpg);

	std::cout << "done" << std::endl;
}

namespace {
struct repo_dirs {
	std::string dist;
	std::string comp; // directory under dist_dir
	std::string pool;
	std::string tmp;
};

struct file_hashes {
	std::string md5;
	std::string sha1;
	std::string sha256;
	std::string sha512;
};

file_hashes get_file_hashes(const repo_dirs& dirs, std::string_view path)
{
	std::filesystem::create_directories(dirs.tmp);
	auto md5_path = utki::cat(dirs.tmp, "md5");
	auto sha1_path = utki::cat(dirs.tmp, "sha1");
	auto sha256_path = utki::cat(dirs.tmp, "sha256");
	auto sha512_path = utki::cat(dirs.tmp, "sha512");
	if (std::system(utki::cat("md5sum ", path, " | cut -d\" \" -f1 > ", md5_path).c_str()) != 0) {
		throw std::runtime_error( //
			utki::cat("could not calculcate md5 hash sum of ", path)
		);
	}
	if (std::system(utki::cat("sha1sum ", path, " | cut -d\" \" -f1 > ", sha1_path).c_str()) != 0) {
		throw std::runtime_error( //
			utki::cat("could not calculcate sha1 hash sum of ", path)
		);
	}
	if (std::system(utki::cat("sha256sum ", path, " | cut -d\" \" -f1 > ", sha256_path).c_str()) != 0) {
		throw std::runtime_error( //
			utki::cat("could not calculcate sha256 hash sum of ", path)
		);
	}
	if (std::system(utki::cat("sha512sum ", path, " | cut -d\" \" -f1 > ", sha512_path).c_str()) != 0) {
		throw std::runtime_error( //
			utki::cat("could not calculcate sha512 hash sum of  ", path)
		);
	}

	return file_hashes{
		.md5 = std::string(utki::trim(utki::make_string_view(papki::fs_file(md5_path).load()))),
		.sha1 = std::string(utki::trim(utki::make_string_view(papki::fs_file(sha1_path).load()))),
		.sha256 = std::string(utki::trim(utki::make_string_view(papki::fs_file(sha256_path).load()))),
		.sha512 = std::string(utki::trim(utki::make_string_view(papki::fs_file(sha512_path).load())))
	};
}

struct unadded_package {
	std::string file_path;
	package pkg;
};

std::vector<unadded_package> prepare_control_info(utki::span<const std::string> package_paths, const repo_dirs& dirs)
{
	std::vector<unadded_package> unadded_packages;

	for (const auto& pkg_path : package_paths) {
		auto filename = papki::not_dir(pkg_path);
		auto suffix = papki::suffix(filename);
		if (suffix != "deb") {
			std::cout << "unsupported package suffix: ." << suffix << std::endl;
			std::cout << "  skipping: " << filename << std::endl;
			continue;
		}

		papki::fs_file tmp_dir_file(dirs.tmp);
		if (tmp_dir_file.exists()) {
			std::filesystem::remove_all(tmp_dir_file.path());
		}
		tmp_dir_file.make_dir();

		// extract control information from deb package to tmp directory
		if (std::system(utki::cat("dpkg-deb --control ", pkg_path, " ", dirs.tmp).c_str()) != 0) {
			throw std::runtime_error(utki::cat("could not extract control information from ", filename));
		}

		package pkg( //
			utki::trim( //
				utki::make_string_view( //
					papki::fs_file( //
						utki::cat(dirs.tmp, control_filename)
					)
						.load()
				)
			)
		);

		// calculate hash sums
		{
			auto h = get_file_hashes(dirs, pkg_path);
			pkg.append_md5(h.md5);
			pkg.append_sha1(h.sha1);
			pkg.append_sha256(h.sha256);
			pkg.append_sha512(h.sha512);
		}

		pkg.append_size(papki::fs_file(pkg_path).size());
		// std::cout << "control = " << std::endl;
		std::cout << pkg.to_string();

		auto pkg_name = pkg.get_name();

		auto pkg_pool_dir = utki::cat(dirs.pool, apt_pool_prefix(pkg_name), papki::as_dir(pkg_name));

		auto pkg_pool_path = utki::cat(pkg_pool_dir, filename);

		pkg.append_filename(pkg_pool_path);

		unadded_packages.push_back({//
									.file_path = pkg_path,
									.pkg = std::move(pkg)
		});
	}

	return unadded_packages;
}
} // namespace

namespace {
void add_packages_to_pool(utki::span<const unadded_package> packages, const repo_dirs& dirs)
{
	// check if any of the package files are already exist in the pool
	for (const auto& p : packages) {
		if (papki::fs_file(p.pkg.fields.filename).exists()) {
			std::stringstream ss;
			ss << "package " << p.pkg.fields.filename << " already exists in the pool";
			throw std::invalid_argument(ss.str());
		}
	}

	// add packages to the pool
	for (const auto& p : packages) {
		const auto& filename = p.pkg.fields.filename;
		std::filesystem::create_directories(papki::dir(filename));

		std::cout << "add " << filename << " to the pool" << std::endl;
		std::filesystem::copy(p.file_path, filename);
	}
}
} // namespace

namespace {
class architectures
{
	std::map<std::string, std::vector<package>, std::less<>> archs;

	std::string comp_dir;

	auto& load_arch(std::string_view arch)
	{
		auto packages_path = utki::cat(this->comp_dir, binary_prefix, arch, '/', packages_filename);

		auto packages = [&]() {
			papki::fs_file file(packages_path);
			if (file.exists()) {
				return aptian::read_packages_file(file);
			}
			return decltype(archs)::value_type::second_type();
		}();

		auto res = this->archs.insert(decltype(archs)::value_type(arch, std::move(packages)));
		ASSERT(res.second)

		return res.first->second;
	}

	auto& get_arch(std::string_view arch)
	{
		auto i = this->archs.find(arch);
		if (i == this->archs.end()) {
			return this->load_arch(arch);
		}
		return i->second;
	}

public:
	architectures(std::string comp_dir) :
		comp_dir(std::move(comp_dir))
	{}

	void add(package pkg)
	{
		const auto& arch = pkg.fields.architecture;

		ASSERT(!arch.empty())

		auto& packages = this->get_arch(arch);
		packages.push_back(std::move(pkg));
	}

	void write_packages()
	{
		for (const auto& arch : this->archs) {
			auto bin_dir = utki::cat(this->comp_dir, binary_prefix, arch.first, '/');

			std::filesystem::create_directories(bin_dir);

			auto packages_path = utki::cat(bin_dir, packages_filename);

			{
				papki::fs_file packages_file(packages_path);
				papki::file::guard packages_file_guard(packages_file, papki::file::mode::create);

				// TODO: does Packages file have to be sorted by package name?
				packages_file.write(to_string(arch.second));
			}

			if (std::system(utki::cat("gzip --keep --force ", packages_path).c_str()) != 0) {
				throw std::runtime_error(utki::cat("could not gzip ", packages_path, " file"));
			}
		}
	}
};
} // namespace

namespace {
void add_to_architectures(std::vector<unadded_package> packages, const repo_dirs& dirs)
{
	architectures archs(dirs.comp);

	for (auto& p : packages) {
		archs.add(std::move(p.pkg));
	}

	archs.write_packages();
}
} // namespace

namespace {
// TODO: move to papki
std::string_view as_file(std::string_view path)
{
	if (path.empty()) {
		return path;
	}
	if (path.back() == '/') {
		return path.substr(0, path.size() - 1);
	}
	return path;
}
} // namespace

namespace {
std::vector<std::string> list_archs(const repo_dirs& dirs)
{
	std::vector<std::string> ret;
	for (const auto& f : papki::fs_file(dirs.comp).list_dir()) {
		// std::cout << "dir = " << d << std::endl;
		if (papki::is_dir(f) && f.starts_with(binary_prefix)) {
			auto arch = as_file(f).substr(binary_prefix.size());
			// std::cout << "arch = " << arch << std::endl;
			ret.emplace_back(arch);
		}
	}
	return ret;
}
} // namespace

namespace {
std::vector<std::string> list_components(const repo_dirs& dirs)
{
	std::vector<std::string> ret;
	for (const auto& f : papki::fs_file(dirs.dist).list_dir()) {
		if (papki::is_dir(f)) {
			ret.emplace_back(as_file(f));
		}
	}
	return ret;
}
} // namespace

namespace {
// TODO: move to utki/string.hpp
std::string combine(utki::span<const std::string> strings, char delimeter)
{
	if (strings.empty()) {
		return {};
	}

	std::stringstream ss;

	ss << strings.front();

	for (const auto& s : utki::skip_front<1>(strings)) {
		ss << delimeter << s;
	}

	return ss.str();
}
} // namespace

namespace {
std::string get_cur_date(const repo_dirs& dirs)
{
	constexpr std::string_view cur_date_filename = "cur_date"sv;
	auto cur_date_path = utki::cat(dirs.tmp, cur_date_filename);

	std::filesystem::create_directories(dirs.tmp);
	if (std::system(utki::cat("date --rfc-email --utc > ", cur_date_path).c_str()) != 0) {
		throw std::runtime_error("failed to invoke 'date'");
	}
	return utki::make_string(papki::fs_file(cur_date_path).load());
}
} // namespace

namespace {
struct file_hash_info {
	std::string path; // path within dists/<component>
	size_t size;
	std::string md5sum;
	std::string sha1sum;
	std::string sha256sum;
	std::string sha512sum;
};

std::vector<file_hash_info> list_files_for_release(const repo_dirs& dirs)
{
	std::filesystem::create_directories(dirs.tmp);

	std::vector<file_hash_info> ret;

	for (const auto& comp_dir : papki::fs_file(dirs.dist).list_dir()) {
		if (!papki::is_dir(comp_dir)) {
			continue;
		}
		auto comp_path = utki::cat(dirs.dist, comp_dir);
		for (const auto& arch_dir : papki::fs_file(comp_path).list_dir()) {
			if (!papki::is_dir(arch_dir)) {
				continue;
			}
			auto arch_path = utki::cat(comp_path, arch_dir);
			for (const auto file : papki::fs_file(arch_path).list_dir()) {
				if (papki::is_dir(file)) {
					continue;
				}
				auto path = utki::cat(arch_path, file);

				ret.push_back({
					.path = utki::cat(comp_dir, arch_dir, file),
					.size = papki::fs_file(path).size()
					// TODO:
				});

				std::cout << "file = " << path << std::endl;
			}
		}
	}

	return ret;
}
} // namespace

void aptian::add(
	std::string_view dir,
	std::string_view dist,
	std::string_view comp,
	utki::span<const std::string> package_paths
)
{
	ASSERT(!dir.empty())
	ASSERT(!dist.empty())
	ASSERT(!comp.empty())
	ASSERT(!package_paths.empty())

	configuration config(dir);

	if (!is_aptian_repo(dir)) {
		std::stringstream ss;
		ss << "given --dir argument is not an aptian repository";
		throw std::invalid_argument(ss.str());
	}

	repo_dirs dirs = {
		.dist = utki::cat(dir, dists_subdir, dist),
		.comp = utki::cat(dirs.dist, comp),
		.pool = utki::cat(dir, pool_subdir, dist, comp),
		.tmp = utki::cat(dir, tmp_subdir)
	};

	// std::cout << "dirs.dist = " << dirs.dist << std::endl;
	// std::cout << "dirs.comp = " << dirs.comp << std::endl;
	// std::cout << "dirs.pool = " << dirs.pool << std::endl;

	auto unadded_packages = prepare_control_info(package_paths, dirs);

	add_packages_to_pool(unadded_packages, dirs);

	add_to_architectures(std::move(unadded_packages), dirs);

	// create Release file
	auto archs = list_archs(dirs);
	auto comps = list_components(dirs);

	std::stringstream rs;
	rs << "Origin: aptian" << '\n';
	rs << "Label: aptian" << '\n';
	rs << "Suite: " << dist << '\n';
	rs << "Codename: " << dist << '\n';
	rs << "NotAutomatic: no" << '\n';
	rs << "ButAutomaticUpgrades: no" << '\n';
	rs << "Components: " << combine(comps, ' ') << '\n';
	rs << "Architectures: " << combine(archs, ' ') << '\n';
	rs << "Date: " << get_cur_date(dirs) << '\n';

	auto files_for_signing = list_files_for_release(dirs);

	// TODO:
}
