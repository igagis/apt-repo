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

#include <iostream>

#include <clargs/parser.hpp>

using namespace std::string_view_literals;

namespace {

constexpr std::string_view program_name = "aptian"sv;

void print_commands_list()
{
	std::cout << "Commands:" << "\n";
	std::cout << "  init  initialize APT repository directory structure" << "\n";
	std::cout << "  add   add debian package to an APT repository" << "\n";
}

void print_help(std::string_view args_description)
{
	std::cout << program_name << " - Debian APT repository management tool" << "\n";
	std::cout << "\n";
	std::cout << "Usage:" << "\n";
	std::cout << "  " << program_name << " [options] <command> [command arguments]" << "\n";
	std::cout << "\n";
	print_commands_list();
	std::cout << "\n";
	std::cout << "Options:" << "\n";
	std::cout << args_description << std::endl;
}
} // namespace

int main(int argc, const char** argv)
{
	bool help = false;

	clargs::parser p;

	p.add( //
		"help",
		"print help message",
		[&help]() {
			help = true;
		}
	);

	auto pos_args = p.parse(argc, argv);

	if (help) {
		print_help(p.description());
		return 0;
	}

	if (pos_args.empty()) {
		std::cout << "No command given. Run with --help to see list of commands." << "\n";
		return 1;
	}

	return 0;
}
